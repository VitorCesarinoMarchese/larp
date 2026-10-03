#include "codec/h264.hpp"
#include "common/bitrate.hpp"
#include "media/raw_frame.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace larp {
CodecBackend parse_codec_backend(std::string_view name) {
    if (name == "software")
        return CodecBackend::software;
    if (name == "nvidia")
        return CodecBackend::nvidia;
    throw std::invalid_argument("codec must be software or nvidia");
}
namespace {
struct ContextDelete {
    void operator()(AVCodecContext *p) const {
        avcodec_free_context(&p);
    }
};
struct FrameDelete {
    void operator()(AVFrame *p) const {
        av_frame_free(&p);
    }
};
struct PacketDelete {
    void operator()(AVPacket *p) const {
        av_packet_free(&p);
    }
};
struct ScaleDelete {
    void operator()(SwsContext *p) const {
        sws_freeContext(p);
    }
};
using Context = std::unique_ptr<AVCodecContext, ContextDelete>;
using Frame = std::unique_ptr<AVFrame, FrameDelete>;
using Packet = std::unique_ptr<AVPacket, PacketDelete>;
using Scale = std::unique_ptr<SwsContext, ScaleDelete>;
void check(int result) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(std::string("FFmpeg: ") + message);
    }
}
bool dimensions(std::uint32_t w, std::uint32_t h) {
    return w >= 2 && h >= 2 && w <= 320 && h <= 180;
}
bool independent_access_unit(std::span<const std::byte> bytes) {
    auto prefix = [&](std::size_t i) -> std::size_t {
        if (i + 3 > bytes.size() || bytes[i] != std::byte{} || bytes[i + 1] != std::byte{})
            return 0;
        if (bytes[i + 2] == std::byte{1})
            return 3;
        if (i + 4 <= bytes.size() && bytes[i + 2] == std::byte{} && bytes[i + 3] == std::byte{1})
            return 4;
        return 0;
    };
    bool sps = false, pps = false, idr = false;
    std::size_t offset = 0;
    unsigned units = 0;
    while (offset < bytes.size()) {
        const auto start = prefix(offset);
        if (!start || offset + start >= bytes.size() || ++units > 8)
            return false;
        const auto header = std::to_integer<unsigned>(bytes[offset + start]);
        if (header & 0x80)
            return false;
        switch (header & 31) {
        case 7:
            if (sps || pps || idr)
                return false;
            sps = true;
            break;
        case 8:
            if (!sps || pps || idr)
                return false;
            pps = true;
            break;
        case 5:
            if (!pps || idr)
                return false;
            idr = true;
            break;
        case 6:
        case 9:
            if (idr)
                return false;
            break;
        default:
            return false;
        }
        offset += start + 1;
        while (offset < bytes.size() && !prefix(offset))
            ++offset;
    }
    return sps && pps && idr;
}
Context context(const AVCodec *codec) {
    if (!codec)
        throw std::runtime_error("required H.264 codec is unavailable in this FFmpeg build");
    Context result(avcodec_alloc_context3(codec));
    if (!result)
        throw std::bad_alloc();
    result->thread_count = 1;
    result->max_pixels = 320 * 192;
    return result;
}
Frame frame() {
    Frame result(av_frame_alloc());
    if (!result)
        throw std::bad_alloc();
    return result;
}
Packet packet() {
    Packet result(av_packet_alloc());
    if (!result)
        throw std::bad_alloc();
    return result;
}
} // namespace
struct H264Encoder::Impl {
    Context ctx;
    Frame input = frame();
    Packet output = packet();
    Scale scale;
    std::uint32_t width, height;
    CodecBackend backend;
    std::int64_t pts = 0;
    Impl(std::uint32_t w, std::uint32_t h, unsigned fps, CodecBackend selected,
         std::uint32_t bitrate)
        : width(w), height(h), backend(selected) {
        if (!dimensions(w, h) || fps < 1 || fps > 30)
            throw std::invalid_argument("H.264 requires 2..320 x 2..180 RGB pixels and 1..30 FPS");
        if (bitrate &&
            (backend != CodecBackend::software || bitrate < min_bitrate || bitrate > max_bitrate))
            throw std::invalid_argument(
                "adaptive bitrate requires software encoding at 128..8000 kbps");
        const auto *codec = avcodec_find_encoder_by_name(
            backend == CodecBackend::nvidia ? "h264_nvenc" : "libx264");
        ctx = context(codec);
        ctx->width = static_cast<int>(w & ~1U);
        ctx->height = static_cast<int>(h & ~1U);
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx->time_base = AVRational{1, static_cast<int>(fps)};
        ctx->framerate = AVRational{static_cast<int>(fps), 1};
        ctx->gop_size = 1;
        ctx->max_b_frames = 0;
        ctx->color_range = AVCOL_RANGE_MPEG;
        ctx->colorspace = AVCOL_SPC_BT470BG;
        if (backend == CodecBackend::nvidia) {
            check(av_opt_set(ctx->priv_data, "preset", "p1", 0));
            check(av_opt_set(ctx->priv_data, "tune", "ull", 0));
            check(av_opt_set(ctx->priv_data, "profile", "baseline", 0));
            check(av_opt_set(ctx->priv_data, "rc", "constqp", 0));
            check(av_opt_set_int(ctx->priv_data, "qp", 23, 0));
            check(av_opt_set_int(ctx->priv_data, "surfaces", 2, 0));
            check(av_opt_set_int(ctx->priv_data, "delay", 0, 0));
            check(av_opt_set_int(ctx->priv_data, "zerolatency", 1, 0));
            check(av_opt_set_int(ctx->priv_data, "rc-lookahead", 0, 0));
        } else {
            check(av_opt_set(ctx->priv_data, "preset", "ultrafast", 0));
            check(av_opt_set(ctx->priv_data, "tune", "zerolatency", 0));
            if (bitrate) {
                ctx->bit_rate = bitrate;
                ctx->rc_max_rate = bitrate;
                ctx->rc_buffer_size = static_cast<int>(bitrate / fps);
            } else
                check(av_opt_set(ctx->priv_data, "crf", "23", 0));
            check(av_opt_set(ctx->priv_data, "x264-params",
                             "repeat-headers=1:annexb=1:keyint=1:threads=1", 0));
        }
        check(avcodec_open2(ctx.get(), codec, nullptr));
        input->format = ctx->pix_fmt;
        input->width = ctx->width;
        input->height = ctx->height;
        check(av_frame_get_buffer(input.get(), 32));
        scale.reset(sws_getContext(static_cast<int>(w), static_cast<int>(h), AV_PIX_FMT_RGB24,
                                   ctx->width, ctx->height, ctx->pix_fmt, SWS_FAST_BILINEAR,
                                   nullptr, nullptr, nullptr));
        if (!scale)
            throw std::runtime_error("cannot create RGB conversion");
    }
};
H264Encoder::H264Encoder(std::uint32_t w, std::uint32_t h, unsigned fps, CodecBackend backend,
                         std::uint32_t bitrate)
    : impl_(std::make_unique<Impl>(w, h, fps, backend, bitrate)) {}
H264Encoder::~H264Encoder() = default;
void H264Encoder::set_bitrate(std::uint32_t bitrate) {
    auto &s = *impl_;
    if (s.backend != CodecBackend::software || !s.ctx->rc_max_rate || bitrate < min_bitrate ||
        bitrate > max_bitrate)
        throw std::invalid_argument("bitrate changes require an adaptive software encoder");
    s.ctx->bit_rate = bitrate;
    s.ctx->rc_max_rate = bitrate;
    s.ctx->rc_buffer_size = static_cast<int>(bitrate / static_cast<unsigned>(s.ctx->framerate.num));
}
std::string_view H264Encoder::name() const {
    return impl_->ctx->codec->name;
}
std::size_t H264Encoder::encode(std::span<const std::byte> rgb, std::span<std::byte> output) {
    auto &s = *impl_;
    if (rgb.size() != std::size_t(s.width) * s.height * 3 || output.size() < h264_capacity)
        throw std::invalid_argument("invalid H.264 encode buffer size");
    check(av_frame_make_writable(s.input.get()));
    const std::uint8_t *source[4] = {reinterpret_cast<const std::uint8_t *>(rgb.data())};
    const int strides[4] = {static_cast<int>(s.width * 3)};
    check(sws_scale(s.scale.get(), source, strides, 0, static_cast<int>(s.height), s.input->data,
                    s.input->linesize));
    s.input->pts = s.pts++;
    check(avcodec_send_frame(s.ctx.get(), s.input.get()));
    av_packet_unref(s.output.get());
    check(avcodec_receive_packet(s.ctx.get(), s.output.get()));
    const auto size = static_cast<std::size_t>(s.output->size);
    if (!(s.output->flags & AV_PKT_FLAG_KEY) || size == 0 || size > h264_capacity - 20)
        throw std::runtime_error("H.264 encoder violated independent frame size contract");
    if (!independent_access_unit({reinterpret_cast<const std::byte *>(s.output->data), size}))
        throw std::runtime_error("H.264 encoder did not emit an independent Annex B access unit");
    std::memcpy(output.data() + 20, s.output->data, size);
    const auto result = output.first(size + 20);
    raw_detail::put(result, 0, 0x4c483236);
    raw_detail::put(result, 4, 0x00010001);
    raw_detail::put(result, 8, static_cast<std::uint32_t>(s.ctx->width));
    raw_detail::put(result, 12, static_cast<std::uint32_t>(s.ctx->height));
    raw_detail::put(result, 16, raw_detail::checksum(result));
    av_packet_unref(s.output.get());
    if (avcodec_receive_packet(s.ctx.get(), s.output.get()) != AVERROR(EAGAIN))
        throw std::runtime_error("H.264 encoder buffered unexpected output");
    return result.size();
}
struct H264Decoder::Impl {
    Context ctx;
    Frame output = frame();
    Frame downloaded = frame();
    Packet input = packet();
    Scale scale;
    int width = 0, height = 0;
    CodecBackend backend;
    bool hardware_failed = false;
    bool needs_reset = false;
    static AVPixelFormat hardware_format(AVCodecContext *ctx,
                                         const AVPixelFormat *formats) noexcept {
        auto &self = *static_cast<Impl *>(ctx->opaque);
        self.hardware_failed = false;
        bool cuda = false;
        for (const auto *p = formats; *p != AV_PIX_FMT_NONE; ++p)
            if (*p == AV_PIX_FMT_CUDA)
                cuda = true;
        if (!cuda || ctx->coded_width <= 0 || ctx->coded_width > 320 || ctx->coded_height <= 0 ||
            ctx->coded_height > 192 || ctx->sw_pix_fmt != AV_PIX_FMT_YUV420P)
            return AV_PIX_FMT_NONE;
        AVBufferRef *frames = nullptr;
        if (avcodec_get_hw_frames_parameters(ctx, ctx->hw_device_ctx, AV_PIX_FMT_CUDA, &frames) <
            0) {
            self.hardware_failed = true;
            return AV_PIX_FMT_NONE;
        }
        auto *pool = reinterpret_cast<AVHWFramesContext *>(frames->data);
        if (pool->initial_pool_size < 0 || pool->initial_pool_size > 8 || pool->width > 320 ||
            pool->height > 192 || pool->sw_format != AV_PIX_FMT_NV12) {
            av_buffer_unref(&frames);
            return AV_PIX_FMT_NONE;
        }
        pool->initial_pool_size = 8;
        if (av_hwframe_ctx_init(frames) < 0) {
            av_buffer_unref(&frames);
            self.hardware_failed = true;
            return AV_PIX_FMT_NONE;
        }
        ctx->hw_frames_ctx = frames;
        self.hardware_failed = false;
        return AV_PIX_FMT_CUDA;
    }
    void open_context() {
        const auto *codec = avcodec_find_decoder_by_name("h264");
        auto old = std::move(ctx);
        ctx = context(codec);
        ctx->err_recognition = AV_EF_EXPLODE | AV_EF_CAREFUL;
        if (backend == CodecBackend::nvidia) {
            bool supported = false;
            for (int i = 0; const auto *config = avcodec_get_hw_config(codec, i); ++i)
                if (config->device_type == AV_HWDEVICE_TYPE_CUDA &&
                    config->pix_fmt == AV_PIX_FMT_CUDA &&
                    (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                    supported = true;
            if (!supported)
                throw std::runtime_error("NVDEC is unavailable in this FFmpeg build");
            if (old && old->hw_device_ctx) {
                ctx->hw_device_ctx = av_buffer_ref(old->hw_device_ctx);
                if (!ctx->hw_device_ctx)
                    throw std::bad_alloc();
            } else {
                check(av_hwdevice_ctx_create(&ctx->hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr,
                                             nullptr, 0));
            }
            ctx->opaque = this;
            ctx->get_format = hardware_format;
            ctx->extra_hw_frames = 0;
        }
        check(avcodec_open2(ctx.get(), codec, nullptr));
        hardware_failed = false;
        needs_reset = false;
    }
    explicit Impl(CodecBackend selected) : backend(selected) {
        open_context();
        check(av_new_packet(input.get(), static_cast<int>(h264_capacity)));
    }
};
H264Decoder::H264Decoder(CodecBackend backend) : impl_(std::make_unique<Impl>(backend)) {}
H264Decoder::~H264Decoder() = default;
std::string_view H264Decoder::name() const {
    return impl_->backend == CodecBackend::nvidia ? "h264+nvdec" : "h264";
}
std::optional<DecodedSize> H264Decoder::decode(std::span<const std::byte> encoded,
                                               std::span<std::byte> rgb) {
    if (encoded.size() <= 20 || encoded.size() > h264_capacity ||
        raw_detail::get(encoded, 0) != 0x4c483236 || raw_detail::get(encoded, 4) != 0x00010001 ||
        raw_detail::checksum(encoded) != raw_detail::get(encoded, 16) ||
        !independent_access_unit(encoded.subspan(20)))
        return {};
    const auto w = raw_detail::get(encoded, 8), h = raw_detail::get(encoded, 12);
    if (!dimensions(w, h) || (w & 1) || (h & 1) || rgb.size() < std::size_t(w) * h * 3)
        return {};
    auto &s = *impl_;
    av_frame_unref(s.output.get());
    if (s.needs_reset)
        s.open_context();
    avcodec_flush_buffers(s.ctx.get());
    s.input->size = static_cast<int>(encoded.size() - 20);
    std::memcpy(s.input->data, encoded.data() + 20, encoded.size() - 20);
    std::memset(s.input->data + s.input->size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    s.needs_reset = true;
    const auto sent = avcodec_send_packet(s.ctx.get(), s.input.get());
    const auto received = sent < 0 ? sent : avcodec_receive_frame(s.ctx.get(), s.output.get());
    if (s.hardware_failed)
        throw std::runtime_error("NVDEC device or frame-pool initialization failed");
    if (received < 0)
        return {};
    const auto &f = *s.output;
    if (f.width != static_cast<int>(w) || f.height != static_cast<int>(h) ||
        f.format != (s.backend == CodecBackend::nvidia ? AV_PIX_FMT_CUDA : AV_PIX_FMT_YUV420P) ||
        !(f.flags & AV_FRAME_FLAG_KEY) || f.decode_error_flags || (f.flags & AV_FRAME_FLAG_CORRUPT))
        return {};
    const auto format = s.backend == CodecBackend::nvidia ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
    if (s.width != f.width || s.height != f.height) {
        s.scale.reset(sws_getContext(f.width, f.height, format, f.width, f.height, AV_PIX_FMT_RGB24,
                                     SWS_FAST_BILINEAR, nullptr, nullptr, nullptr));
        if (!s.scale)
            throw std::runtime_error("cannot create decoded RGB conversion");
        s.width = f.width;
        s.height = f.height;
        if (s.backend == CodecBackend::nvidia) {
            av_frame_unref(s.downloaded.get());
            s.downloaded->format = AV_PIX_FMT_NV12;
            s.downloaded->width = f.width;
            s.downloaded->height = f.height;
            check(av_frame_get_buffer(s.downloaded.get(), 32));
        }
    }
    const AVFrame *pixels = &f;
    if (s.backend == CodecBackend::nvidia) {
        check(av_hwframe_transfer_data(s.downloaded.get(), s.output.get(), 0));
        pixels = s.downloaded.get();
    }
    std::uint8_t *dest[4] = {reinterpret_cast<std::uint8_t *>(rgb.data())};
    const int strides[4] = {static_cast<int>(w * 3)};
    check(sws_scale(s.scale.get(), pixels->data, pixels->linesize, 0, f.height, dest, strides));
    av_frame_unref(s.output.get());
    if (avcodec_receive_frame(s.ctx.get(), s.output.get()) != AVERROR(EAGAIN))
        return {};
    s.needs_reset = false;
    return DecodedSize{w, h};
}
} // namespace larp
