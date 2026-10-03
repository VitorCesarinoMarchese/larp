#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
namespace larp {
enum class CodecBackend { software, nvidia };
CodecBackend parse_codec_backend(std::string_view name);
inline constexpr std::size_t h264_capacity = 256 * 1024;
struct DecodedSize {
    std::uint32_t width, height;
};
class H264Encoder {
  public:
    H264Encoder(std::uint32_t width, std::uint32_t height, unsigned fps,
                CodecBackend backend = CodecBackend::software, std::uint32_t bitrate = 0);
    ~H264Encoder();
    void set_bitrate(std::uint32_t bitrate);
    std::string_view name() const;
    std::size_t encode(std::span<const std::byte> rgb, std::span<std::byte> output);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class H264Decoder {
  public:
    explicit H264Decoder(CodecBackend backend = CodecBackend::software);
    ~H264Decoder();
    std::string_view name() const;
    std::optional<DecodedSize> decode(std::span<const std::byte> encoded, std::span<std::byte> rgb);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace larp
