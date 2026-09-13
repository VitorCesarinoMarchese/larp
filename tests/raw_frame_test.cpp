#include "media/raw_frame.hpp"
#include <array>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    std::array<std::byte, 32> frame{};
    for (std::size_t i = 20; i < frame.size(); ++i)
        frame[i] = std::byte(i);
    CHECK(write_raw_header(frame, 2, 2));
    CHECK(frame[0] == std::byte{'L'} && frame[3] == std::byte{'B'});
    auto parsed = read_raw_frame(frame);
    CHECK(parsed && parsed->width == 2 && parsed->height == 2 && parsed->pixels.size() == 12);
    frame[20] ^= std::byte{1};
    CHECK(!read_raw_frame(frame));
    frame[20] ^= std::byte{1};
    frame[11] = std::byte{1};
    CHECK(!read_raw_frame(frame));
    CHECK(write_raw_header(frame, 2, 2));
    CHECK(!read_raw_frame(std::span(frame).first(31)));
    CHECK(!write_raw_header(frame, 0, 2));
    CHECK(!write_raw_header(frame, 100000, 100000));
    CHECK(!read_raw_frame(std::span(frame).first(3)));
    CHECK(!write_raw_header(std::span(frame).first(2), 1, 1));
}
