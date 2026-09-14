#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
namespace larp {
inline constexpr std::size_t h264_capacity = 256 * 1024;
struct DecodedSize {
    std::uint32_t width, height;
};
class H264Encoder {
  public:
    H264Encoder(std::uint32_t width, std::uint32_t height, unsigned fps);
    ~H264Encoder();
    std::size_t encode(std::span<const std::byte> rgb, std::span<std::byte> output);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class H264Decoder {
  public:
    H264Decoder();
    ~H264Decoder();
    std::optional<DecodedSize> decode(std::span<const std::byte> encoded, std::span<std::byte> rgb);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace larp
