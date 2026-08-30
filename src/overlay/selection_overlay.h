#pragma once

#include "capture/capture_service.h"
#include "platform/geometry.h"

#include <windows.h>

namespace screenshooter::overlay {

class SelectionOverlay final {
public:
    [[nodiscard]] static SelectionOverlay* Create(
        HINSTANCE instance,
        HWND host,
        capture::CaptureFrame frame
    );

    ~SelectionOverlay();
    SelectionOverlay(const SelectionOverlay&) = delete;
    SelectionOverlay& operator=(const SelectionOverlay&) = delete;

    void Cancel();
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM, LPARAM);

private:
    SelectionOverlay(HINSTANCE instance, HWND host, capture::CaptureFrame frame);

    [[nodiscard]] bool CreateNativeWindow();
    [[nodiscard]] bool EnsurePaintBuffer(HDC reference, int width, int height);
    void ReleasePaintBuffer() noexcept;
    void RepaintImmediately() const;
    void Paint();
    void FinishSelection();
    [[nodiscard]] platform::PixelPoint CursorPosition() const noexcept;
    [[nodiscard]] platform::PixelRect Selection() const noexcept;
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
    HINSTANCE instance_{};
    HWND host_{};
    HWND window_{};
    HDC paint_buffer_dc_{};
    HBITMAP paint_buffer_bitmap_{};
    HGDIOBJ paint_buffer_previous_{};
    SIZE paint_buffer_size_{};
    capture::CaptureFrame frame_;
    platform::PixelPoint anchor_{};
    platform::PixelPoint cursor_{};
    bool selecting_{};
    bool notifying_{};
};

}  // namespace screenshooter::overlay
