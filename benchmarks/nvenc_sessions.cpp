#include "common/runtime.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}
#include <iostream>
#include <memory>
struct ContextDelete {
    void operator()(AVCodecContext *ctx) const {
        avcodec_free_context(&ctx);
    }
};
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::invalid_argument("Usage: larp-nvenc-sessions COUNT");
        const auto count = larp::number(argv[1], 1, 1000);
        const auto *codec = avcodec_find_encoder_by_name("h264_nvenc");
        if (!codec)
            throw std::runtime_error("NVENC is unavailable");
        for (unsigned i = 0; i < count; ++i) {
            std::unique_ptr<AVCodecContext, ContextDelete> ctx(avcodec_alloc_context3(codec));
            if (!ctx)
                throw std::bad_alloc();
            ctx->width = 320;
            ctx->height = 180;
            ctx->time_base = AVRational{1, 30};
            ctx->pix_fmt = AV_PIX_FMT_YUV420P;
            if (av_opt_set(ctx->priv_data, "preset", "p1", 0) < 0 ||
                avcodec_open2(ctx.get(), codec, nullptr) < 0)
                throw std::runtime_error("NVENC initialization failed");
        }
        std::cerr << "Opened and closed NVENC sessions: " << count << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
