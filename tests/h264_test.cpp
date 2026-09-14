#include "codec/h264.hpp"
#include "media/raw_frame.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    H264Encoder encoder(320, 180, 30);
    H264Decoder decoder;
    std::vector<std::byte> rgb(320 * 180 * 3, std::byte{90});
    std::vector<std::byte> encoded(h264_capacity), output(rgb.size());
    std::uint64_t bytes = 0;
    for (int i = 0; i < 1000; ++i) {
        const auto size = encoder.encode(rgb, encoded);
        CHECK(size > 20 && size <= h264_capacity);
        bytes += size;
        // Skip access units deliberately. Every later frame must decode independently.
        if (i % 3 == 1)
            continue;
        auto decoded = decoder.decode(std::span(encoded).first(size), output);
        CHECK(decoded && decoded->width == 320 && decoded->height == 180);
        for (auto pixel : output)
            CHECK(std::abs(int(std::to_integer<unsigned>(pixel)) - 90) <= 4);
        encoded[size - 1] ^= std::byte{1};
        CHECK(!decoder.decode(std::span(encoded).first(size), output));
    }
    CHECK(!decoder.decode({}, output));
    auto size = encoder.encode(rgb, encoded);
    H264Decoder late_joiner;
    CHECK(late_joiner.decode(std::span(encoded).first(size), output));
    auto dependent = encoded;
    std::size_t end = 20;
    for (std::size_t i = 20; i + 4 < size;) {
        std::size_t prefix = 0;
        if (encoded[i] == std::byte{} && encoded[i + 1] == std::byte{}) {
            if (encoded[i + 2] == std::byte{1})
                prefix = 3;
            else if (encoded[i + 2] == std::byte{} && encoded[i + 3] == std::byte{1})
                prefix = 4;
        }
        if (!prefix) {
            ++i;
            continue;
        }
        const auto type = std::to_integer<unsigned>(encoded[i + prefix]) & 31;
        if (type == 5) {
            end = i;
            break;
        }
        i += prefix + 1;
    }
    CHECK(end > 20);
    std::copy(encoded.begin() + static_cast<std::ptrdiff_t>(end),
              encoded.begin() + static_cast<std::ptrdiff_t>(size), dependent.begin() + 20);
    const auto dependent_size = size - end + 20;
    raw_detail::put(dependent, 16,
                    raw_detail::checksum(std::span(dependent).first(dependent_size)));
    CHECK(!decoder.decode(std::span(dependent).first(dependent_size), output));
    // A valid envelope must not turn malformed compressed data into a usable frame.
    std::fill(encoded.begin() + 20, encoded.begin() + static_cast<std::ptrdiff_t>(size),
              std::byte{});
    raw_detail::put(encoded, 16, raw_detail::checksum(std::span(encoded).first(size)));
    CHECK(!decoder.decode(std::span(encoded).first(size), output));
    size = encoder.encode(rgb, encoded);
    CHECK(decoder.decode(std::span(encoded).first(size), output));
    CHECK(!decoder.decode(std::span(encoded).first(size), std::span(output).first(3)));
    raw_detail::put(encoded, 8, 4096);
    raw_detail::put(encoded, 16, raw_detail::checksum(std::span(encoded).first(size)));
    CHECK(!decoder.decode(std::span(encoded).first(size), output));
    bool rejected = false;
    try {
        H264Encoder invalid(4096, 2160, 30);
    } catch (const std::exception &) {
        rejected = true;
    }
    CHECK(rejected);
    rejected = false;
    try {
        encoder.encode(std::span(rgb).first(3), encoded);
    } catch (const std::exception &) {
        rejected = true;
    }
    CHECK(rejected);
    H264Encoder odd(319, 179, 5);
    size = odd.encode(std::span(rgb).first(319 * 179 * 3), encoded);
    auto decoded = decoder.decode(std::span(encoded).first(size), output);
    CHECK(decoded && decoded->width == 318 && decoded->height == 178);
    std::cout << "Encoded 1000 frames, bytes: " << bytes << '\n';
}
