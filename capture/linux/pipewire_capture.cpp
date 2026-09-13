#include "capture/capture.hpp"
#include "capture/linux/portal.hpp"
#include "common/runtime.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/param/video/format-utils.h>
#include <stdexcept>
namespace larp {
namespace {
struct PwRuntime {
    PwRuntime() {
        pw_init(nullptr, nullptr);
    }
    ~PwRuntime() {
        pw_deinit();
    }
};
struct LoopDelete {
    void operator()(pw_main_loop *p) const {
        pw_main_loop_destroy(p);
    }
};
struct ContextDelete {
    void operator()(pw_context *p) const {
        pw_context_destroy(p);
    }
};
struct CoreDelete {
    void operator()(pw_core *p) const {
        pw_core_disconnect(p);
    }
};
struct StreamDelete {
    void operator()(pw_stream *p) const {
        pw_stream_destroy(p);
    }
};
struct Listener {
    spa_hook hook{};
    bool attached = false;
    ~Listener() {
        if (attached)
            spa_hook_remove(&hook);
    }
};
class PipeWireCapture final : public ScreenCapture {
    PwRuntime runtime_;
    PortalSession portal_;
    std::unique_ptr<pw_main_loop, LoopDelete> loop_{pw_main_loop_new(nullptr)};
    std::unique_ptr<pw_context, ContextDelete> context_;
    std::unique_ptr<pw_core, CoreDelete> core_;
    std::unique_ptr<pw_stream, StreamDelete> stream_;
    Listener listener_;
    spa_video_info_raw format_{};
    std::array<char, 256> error_{};
    std::span<std::byte> target_;
    std::optional<CaptureInfo> ready_;
    CaptureStats stats_;
    unsigned buffers_ = 0;
    void fail(const char *message) noexcept {
        std::snprintf(error_.data(), error_.size(), "%s", message);
    }
    static void state(void *data, pw_stream_state previous, pw_stream_state current,
                      const char *error) {
        if (current == PW_STREAM_STATE_ERROR)
            static_cast<PipeWireCapture *>(data)->fail(error ? error : "PipeWire stream error");
        else if (current == PW_STREAM_STATE_UNCONNECTED && previous != PW_STREAM_STATE_UNCONNECTED)
            static_cast<PipeWireCapture *>(data)->fail("screen stream disconnected");
    }
    static void added(void *data, pw_buffer *buffer) {
        auto &self = *static_cast<PipeWireCapture *>(data);
        if (++self.buffers_ > 4 || buffer->buffer->n_datas != 1 ||
            buffer->buffer->datas[0].maxsize > 64U * 1024U * 1024U)
            self.fail("PipeWire exceeded the four-buffer or 64 MiB per-buffer limit");
    }
    static void removed(void *data, pw_buffer *) {
        --static_cast<PipeWireCapture *>(data)->buffers_;
    }
    static void parameter(void *data, std::uint32_t id, const spa_pod *param) {
        auto &self = *static_cast<PipeWireCapture *>(data);
        if (id != SPA_PARAM_Format || !param)
            return;
        spa_video_info_raw format{};
        if (spa_format_video_raw_parse(param, &format) < 0 || !format.size.width ||
            !format.size.height || format.size.width > 4096 || format.size.height > 2160 ||
            (format.format != SPA_VIDEO_FORMAT_BGRx && format.format != SPA_VIDEO_FORMAT_BGRA &&
             format.format != SPA_VIDEO_FORMAT_RGBx && format.format != SPA_VIDEO_FORMAT_RGBA) ||
            (format.flags & SPA_VIDEO_FLAG_MODIFIER)) {
            self.fail("unsupported screen format; requires packed 8-bit RGB, at most 4096x2160");
            return;
        }
        self.format_ = format;
        std::array<std::byte, 1024> storage{};
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage.data(), storage.size());
        const auto stride = static_cast<int>(format.size.width * 4);
        const auto bytes = static_cast<int>(format.size.height * format.size.width * 4);
        const spa_pod *params[2];
        params[0] = static_cast<spa_pod *>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
            SPA_POD_CHOICE_RANGE_Int(2, 2, 4), SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
            SPA_PARAM_BUFFERS_size, SPA_POD_Int(bytes), SPA_PARAM_BUFFERS_stride,
            SPA_POD_Int(stride), SPA_PARAM_BUFFERS_dataType,
            SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))));
        params[1] = static_cast<spa_pod *>(
            spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
                                       SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
                                       SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_region))));
        if (pw_stream_update_params(self.stream_.get(), params, 2) < 0)
            self.fail("PipeWire buffer negotiation failed");
    }
    static void process(void *data) {
        auto &self = *static_cast<PipeWireCapture *>(data);
        pw_buffer *latest = nullptr;
        for (unsigned i = 0; i < 4; ++i) {
            auto *next = pw_stream_dequeue_buffer(self.stream_.get());
            if (!next)
                break;
            if (latest) {
                pw_stream_queue_buffer(self.stream_.get(), latest);
                ++self.stats_.superseded;
            }
            latest = next;
        }
        if (!latest)
            return;
        struct ReturnBuffer {
            pw_stream *stream;
            pw_buffer *buffer;
            ~ReturnBuffer() {
                pw_stream_queue_buffer(stream, buffer);
            }
        } loan{self.stream_.get(), latest};
        if (self.target_.empty() || self.error_[0])
            return;
        const auto *buffer = latest->buffer;
        if (buffer->n_datas != 1) {
            ++self.stats_.malformed;
            return;
        }
        const auto &d = buffer->datas[0];
        if (!d.data || !d.chunk || d.chunk->stride <= 0 ||
            (d.type != SPA_DATA_MemPtr && d.type != SPA_DATA_MemFd) ||
            d.chunk->offset > d.maxsize || d.chunk->size > d.maxsize - d.chunk->offset ||
            (static_cast<std::uint32_t>(d.chunk->flags) & SPA_CHUNK_FLAG_CORRUPTED)) {
            ++self.stats_.malformed;
            return;
        }
        auto width = self.format_.size.width, height = self.format_.size.height;
        std::size_t offset = 0;
        if (auto *crop = static_cast<spa_meta_region *>(
                spa_buffer_find_meta_data(buffer, SPA_META_VideoCrop, sizeof(spa_meta_region)));
            crop && spa_meta_region_is_valid(crop)) {
            const auto &region = crop->region;
            if (region.position.x < 0 || region.position.y < 0 ||
                std::uint64_t(region.position.x) + region.size.width > width ||
                std::uint64_t(region.position.y) + region.size.height > height) {
                ++self.stats_.malformed;
                return;
            }
            offset = std::size_t(region.position.y) * static_cast<std::uint32_t>(d.chunk->stride) +
                     std::size_t(region.position.x) * 4;
            width = region.size.width;
            height = region.size.height;
        }
        if (offset > d.chunk->size) {
            ++self.stats_.malformed;
            return;
        }
        const auto bytes =
            std::span(static_cast<const std::byte *>(d.data) + d.chunk->offset + offset,
                      d.chunk->size - offset);
        const auto order = self.format_.format == SPA_VIDEO_FORMAT_BGRx ||
                                   self.format_.format == SPA_VIDEO_FORMAT_BGRA
                               ? PixelOrder::bgrx
                               : PixelOrder::rgbx;
        const auto size = preview_size({width, height});
        const auto timestamp = now_us();
        if (!copy_rgb({bytes, width, height, static_cast<std::uint32_t>(d.chunk->stride), order},
                      self.target_, size)) {
            ++self.stats_.malformed;
            return;
        }
        if (self.ready_)
            ++self.stats_.superseded;
        self.ready_ = CaptureInfo{size, timestamp};
        ++self.stats_.received;
    }

  public:
    PipeWireCapture() {
        if (!loop_)
            throw std::runtime_error("cannot create PipeWire loop");
        context_.reset(pw_context_new(pw_main_loop_get_loop(loop_.get()), nullptr, 0));
        if (!context_)
            throw std::runtime_error("cannot create PipeWire context");
        core_.reset(pw_context_connect_fd(context_.get(), portal_.open_remote(), nullptr, 0));
        if (!core_)
            throw std::runtime_error("cannot connect to portal PipeWire remote");
        auto *properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                                             "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr);
        if (!properties)
            throw std::runtime_error("cannot allocate PipeWire properties");
        const auto serial = portal_.serial();
        if (!serial.empty())
            pw_properties_set(properties, PW_KEY_TARGET_OBJECT, serial.c_str());
        stream_.reset(pw_stream_new(core_.get(), "L.A.R.P. screen capture", properties));
        if (!stream_)
            throw std::runtime_error("cannot create PipeWire stream");
        static const pw_stream_events events = [] {
            pw_stream_events result{};
            result.version = PW_VERSION_STREAM_EVENTS;
            result.state_changed = state;
            result.param_changed = parameter;
            result.add_buffer = added;
            result.remove_buffer = removed;
            result.process = process;
            return result;
        }();
        pw_stream_add_listener(stream_.get(), &listener_.hook, &events, this);
        listener_.attached = true;
        std::array<std::byte, 1024> storage{};
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage.data(), storage.size());
        const spa_rectangle preferred{1920, 1080}, minimum{1, 1}, maximum{4096, 2160};
        const spa_fraction rate{30, 1}, slow{0, 1}, fast{144, 1};
        const auto *param = static_cast<spa_pod *>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType,
            SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
            SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
            SPA_POD_CHOICE_ENUM_Id(4, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA,
                                   SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA),
            SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&preferred, &minimum, &maximum),
            SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&rate, &slow, &fast)));
        const auto flags =
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS);
        if (pw_stream_connect(stream_.get(), PW_DIRECTION_INPUT,
                              serial.empty() ? portal_.node() : PW_ID_ANY, flags, &param, 1) < 0)
            throw std::runtime_error("cannot connect screen stream");
    }
    std::optional<CaptureInfo> next(std::span<std::byte> rgb,
                                    std::chrono::milliseconds timeout) override {
        if (rgb.size() < preview_capacity || timeout.count() < 0 ||
            timeout > std::chrono::seconds(5))
            throw std::invalid_argument(
                "capture requires a full preview buffer and a timeout of 0 through 5000 ms");
        target_ = rgb;
        struct ClearTarget {
            std::span<std::byte> &value;
            ~ClearTarget() {
                value = {};
            }
        } clear_target{target_};
        ready_.reset();
        const auto until = Clock::now() + timeout;
        do {
            if (portal_.closed())
                throw std::runtime_error("screen sharing session closed");
            if (error_[0])
                throw std::runtime_error(error_.data());
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
            const int waited =
                pw_loop_iterate(pw_main_loop_get_loop(loop_.get()),
                                static_cast<int>(std::clamp<std::int64_t>(remaining, 0, 10)));
            if (waited < 0 && waited != -EINTR)
                throw std::runtime_error("PipeWire event loop failed");
            if (error_[0])
                throw std::runtime_error(error_.data());
            if (ready_)
                break;
        } while (Clock::now() < until);
        return ready_;
    }
    CaptureStats statistics() const override {
        return stats_;
    }
};
} // namespace
std::unique_ptr<ScreenCapture> open_screen_capture() {
    return std::make_unique<PipeWireCapture>();
}
} // namespace larp
