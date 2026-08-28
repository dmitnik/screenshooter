#pragma once

#include "capture/pixel_buffer.h"

#include <windows.h>

namespace screenshooter::exporting {

class ClipboardService final {
public:
    [[nodiscard]] static bool CopyImage(
        HWND owner,
        const capture::PixelBuffer& image
    );
};

}  // namespace screenshooter::exporting
