#pragma once

#include "capture/pixel_buffer.h"
#include "platform/geometry.h"

#include <optional>
#include <string>

namespace screenshooter::capture {

enum class CaptureBackend {
    desktop_duplication,
    gdi,
};

struct CaptureFrame {
    PixelBuffer pixels;
    platform::PixelRect virtual_bounds;
    CaptureBackend backend{CaptureBackend::desktop_duplication};
};

struct CaptureOutcome {
    std::optional<CaptureFrame> frame;
    std::wstring error;
};

class CaptureService final {
public:
    [[nodiscard]] CaptureOutcome CaptureVirtualDesktop() const;

private:
    [[nodiscard]] std::optional<CaptureFrame> CaptureWithDesktopDuplication() const;
    [[nodiscard]] std::optional<CaptureFrame> CaptureWithGdi() const;
};

}  // namespace screenshooter::capture
