#pragma once
#include "codec/h264.hpp"
#include "common/bitrate.hpp"
#include "common/runtime.hpp"
namespace larp {
inline CodecBackend take_codec_option(int &argc, char **argv) {
    if (argc >= 3 && std::string_view(argv[argc - 2]) == "--codec") {
        const auto backend = parse_codec_backend(argv[argc - 1]);
        argc -= 2;
        return backend;
    }
    return CodecBackend::software;
}
struct EncoderOptions {
    CodecBackend backend = CodecBackend::software;
    std::uint32_t bitrate = 0;
};
inline EncoderOptions encoder_options(int argc, char **argv) {
    EncoderOptions result;
    bool codec_seen = false;
    for (int i = 6; i < argc; i += 2) {
        if (i + 1 == argc)
            throw std::invalid_argument("encoder option needs a value");
        const std::string_view option = argv[i];
        if (option == "--codec" && !codec_seen) {
            result.backend = parse_codec_backend(argv[i + 1]);
            codec_seen = true;
        } else if (option == "--adaptive" && !result.bitrate)
            result.bitrate = number(argv[i + 1], min_bitrate / 1000, max_bitrate / 1000) * 1000;
        else
            throw std::invalid_argument("unknown or duplicate encoder option");
    }
    if (result.bitrate && result.backend != CodecBackend::software)
        throw std::invalid_argument("adaptive bitrate currently requires the software codec");
    return result;
}
} // namespace larp
