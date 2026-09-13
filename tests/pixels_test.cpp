#include "capture/pixels.hpp"
#include <array>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
int main() {
    using namespace larp;
    std::array<std::byte, 24> source{};
    source[0] = std::byte{1};
    source[1] = std::byte{2};
    source[2] = std::byte{3};
    source[4] = std::byte{4};
    source[5] = std::byte{5};
    source[6] = std::byte{6};
    source[12] = std::byte{7};
    source[13] = std::byte{8};
    source[14] = std::byte{9};
    source[16] = std::byte{10};
    source[17] = std::byte{11};
    source[18] = std::byte{12};
    std::array<std::byte, 12> target{};
    CHECK(copy_rgb({source, 2, 2, 12, PixelOrder::bgrx}, target, {2, 2}));
    CHECK(target[0] == std::byte{3} && target[2] == std::byte{1});
    CHECK(target[6] == std::byte{9} && target[11] == std::byte{10});
    CHECK(copy_rgb({source, 2, 2, 12, PixelOrder::rgbx}, target, {1, 1}));
    CHECK(target[0] == std::byte{1} && target[2] == std::byte{3});
    CHECK(!copy_rgb({std::span(source).first(19), 2, 2, 12, PixelOrder::rgbx}, target, {2, 2}));
    CHECK(!copy_rgb({source, 2, 2, 7, PixelOrder::rgbx}, target, {2, 2}));
    CHECK(!copy_rgb({source, 0, 2, 12, PixelOrder::rgbx}, target, {2, 2}));
    CHECK(!copy_rgb({source, 2, 2, 12, PixelOrder::rgbx}, std::span(target).first(11), {2, 2}));
    CHECK(!copy_rgb({source, 2, 2, 12, PixelOrder::rgbx}, target, {3, 2}));
    CHECK((preview_size({1920, 1080}) == Dimensions{320, 180}));
    CHECK((preview_size({1080, 1920}) == Dimensions{101, 180}));
    CHECK((preview_size({2, 2}) == Dimensions{2, 2}));
}
