#pragma once
#include "media/raw_frame.hpp"
#include <memory>
namespace larp {
class VideoWindow {
  public:
    VideoWindow();
    ~VideoWindow();
    VideoWindow(const VideoWindow &) = delete;
    VideoWindow &operator=(const VideoWindow &) = delete;
    bool poll();
    void present(RawFrameView frame);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace larp
