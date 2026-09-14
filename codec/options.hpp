#pragma once
#include "codec/h264.hpp"
namespace larp {
inline CodecBackend take_codec_option(int &argc, char **argv) {
    if (argc >= 3 && std::string_view(argv[argc - 2]) == "--codec") {
        const auto backend = parse_codec_backend(argv[argc - 1]);
        argc -= 2;
        return backend;
    }
    return CodecBackend::software;
}
} // namespace larp
