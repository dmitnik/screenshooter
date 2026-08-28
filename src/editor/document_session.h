#pragma once

#include "capture/pixel_buffer.h"

#include <cstdint>

namespace screenshooter::editor {

struct DocumentSession {
    std::uint64_t id{};
    capture::PixelBuffer image;
};

[[nodiscard]] DocumentSession CreateSession(capture::PixelBuffer image);

}  // namespace screenshooter::editor
