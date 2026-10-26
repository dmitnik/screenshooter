#include "localization/strings.h"
#include "overlay/selection_overlay.h"

#include "app/messages.h"

#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace screenshooter::overlay {
namespace {

constexpr wchar_t kOverlayClass[] = L"Screenshooter.SelectionOverlay";

bool RegisterOverlayClass(const HINSTANCE instance) {
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &SelectionOverlay::WindowProcedure,
        .hInstance = instance,
        .hCursor = ::LoadCursorW(nullptr, IDC_CROSS),
        .hbrBackground = nullptr,
        .lpszClassName = kOverlayClass,
    };
    return ::RegisterClassExW(&window_class) != 0 ||
        ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

RECT ToClientRect(const platform::PixelRect rect, const platform::PixelRect bounds) {
    return RECT{
        rect.left - bounds.left,
        rect.top - bounds.top,
        rect.right - bounds.left,
        rect.bottom - bounds.top,
    };
}

}  // namespace

SelectionOverlay* SelectionOverlay::Create(
    const HINSTANCE instance,
    const HWND host,
    capture::CaptureFrame frame
) {
    std::unique_ptr<SelectionOverlay> overlay(
        new SelectionOverlay(instance, host, std::move(frame))
    );
    if (!overlay->CreateNativeWindow()) {
        return nullptr;
    }
    return overlay.release();
}

SelectionOverlay::SelectionOverlay(
    const HINSTANCE instance,
    const HWND host,
    capture::CaptureFrame frame
) : instance_(instance), host_(host), frame_(std::move(frame)) {}

SelectionOverlay::~SelectionOverlay() {
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
    }
    ReleaseSourceBuffer();
    ReleasePaintBuffer();
}

bool SelectionOverlay::CreateNativeWindow() {
    if (!RegisterOverlayClass(instance_)) {
        return false;
    }
    const auto& bounds = frame_.virtual_bounds;
    window_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kOverlayClass,
        i18n::Text(i18n::Id::selection_title),
        WS_POPUP,
        bounds.left,
        bounds.top,
        bounds.width(),
        bounds.height(),
        nullptr,
        nullptr,
        instance_,
        this
    );
    if (window_ == nullptr) {
        return false;
    }
    ::ShowWindow(window_, SW_SHOWNOACTIVATE);
    ::SetForegroundWindow(window_);
    ::SetFocus(window_);
    ::UpdateWindow(window_);
    return true;
}

bool SelectionOverlay::EnsureSourceBuffer(const HDC reference) {
    if (source_buffer_dc_ != nullptr && source_buffer_bitmap_ != nullptr) {
        return true;
    }

    const auto& image = frame_.pixels;
    if (image.empty()) {
        return false;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = image.width;
    bitmap_info.bmiHeader.biHeight = -image.height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    source_buffer_dc_ = ::CreateCompatibleDC(reference);
    source_buffer_bitmap_ = ::CreateDIBSection(
        reference, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (source_buffer_dc_ == nullptr || source_buffer_bitmap_ == nullptr ||
        bits == nullptr) {
        ReleaseSourceBuffer();
        return false;
    }

    source_buffer_previous_ = ::SelectObject(source_buffer_dc_, source_buffer_bitmap_);
    if (source_buffer_previous_ == nullptr || source_buffer_previous_ == HGDI_ERROR) {
        ReleaseSourceBuffer();
        return false;
    }

    constexpr std::size_t kBytesPerPixel = 4;
    const auto destination_stride = static_cast<std::size_t>(image.width) *
        kBytesPerPixel;
    auto* destination = static_cast<std::byte*>(bits);
    for (std::int32_t y = 0; y < image.height; ++y) {
        std::memcpy(destination + (static_cast<std::size_t>(y) * destination_stride),
            image.row(y), destination_stride);
    }
    return true;
}

void SelectionOverlay::ReleaseSourceBuffer() noexcept {
    if (source_buffer_dc_ != nullptr && source_buffer_previous_ != nullptr &&
        source_buffer_previous_ != HGDI_ERROR) {
        ::SelectObject(source_buffer_dc_, source_buffer_previous_);
    }
    if (source_buffer_bitmap_ != nullptr) {
        ::DeleteObject(source_buffer_bitmap_);
    }
    if (source_buffer_dc_ != nullptr) {
        ::DeleteDC(source_buffer_dc_);
    }
    source_buffer_dc_ = nullptr;
    source_buffer_bitmap_ = nullptr;
    source_buffer_previous_ = nullptr;
}

bool SelectionOverlay::EnsurePaintBuffer(
    const HDC reference,
    const int width,
    const int height
) {
    if (paint_buffer_dc_ != nullptr && paint_buffer_bitmap_ != nullptr &&
        paint_buffer_size_.cx == width && paint_buffer_size_.cy == height) {
        return true;
    }
    ReleasePaintBuffer();
    paint_buffer_dc_ = ::CreateCompatibleDC(reference);
    paint_buffer_bitmap_ = ::CreateCompatibleBitmap(
        reference, std::max(1, width), std::max(1, height));
    if (paint_buffer_dc_ == nullptr || paint_buffer_bitmap_ == nullptr) {
        ReleasePaintBuffer();
        return false;
    }
    paint_buffer_previous_ = ::SelectObject(paint_buffer_dc_, paint_buffer_bitmap_);
    if (paint_buffer_previous_ == nullptr || paint_buffer_previous_ == HGDI_ERROR) {
        ReleasePaintBuffer();
        return false;
    }
    paint_buffer_size_ = {width, height};
    return true;
}

void SelectionOverlay::ReleasePaintBuffer() noexcept {
    if (paint_buffer_dc_ != nullptr && paint_buffer_previous_ != nullptr &&
        paint_buffer_previous_ != HGDI_ERROR) {
        ::SelectObject(paint_buffer_dc_, paint_buffer_previous_);
    }
    if (paint_buffer_bitmap_ != nullptr) {
        ::DeleteObject(paint_buffer_bitmap_);
    }
    if (paint_buffer_dc_ != nullptr) {
        ::DeleteDC(paint_buffer_dc_);
    }
    paint_buffer_dc_ = nullptr;
    paint_buffer_bitmap_ = nullptr;
    paint_buffer_previous_ = nullptr;
    paint_buffer_size_ = {};
}

void SelectionOverlay::RepaintImmediately() const {
    if (window_ == nullptr) {
        return;
    }
    ::InvalidateRect(window_, nullptr, FALSE);
    ::UpdateWindow(window_);
}

void SelectionOverlay::Cancel() {
    if (notifying_) {
        return;
    }
    notifying_ = true;
    ::PostMessageW(host_, app::messages::kSelectionCancelled, 0, 0);
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
    }
}

platform::PixelPoint SelectionOverlay::CursorPosition() const noexcept {
    POINT position{};
    ::GetCursorPos(&position);
    return {
        std::clamp(static_cast<std::int32_t>(position.x),
            frame_.virtual_bounds.left, frame_.virtual_bounds.right),
        std::clamp(static_cast<std::int32_t>(position.y),
            frame_.virtual_bounds.top, frame_.virtual_bounds.bottom),
    };
}

platform::PixelRect SelectionOverlay::Selection() const noexcept {
    return platform::Intersect(
        platform::Normalize(anchor_, cursor_),
        frame_.virtual_bounds
    );
}

void SelectionOverlay::FinishSelection() {
    if (notifying_) {
        return;
    }
    const auto selected = Selection();
    if (selected.empty()) {
        Cancel();
        return;
    }
    const platform::PixelRect source_rect{
        selected.left - frame_.virtual_bounds.left,
        selected.top - frame_.virtual_bounds.top,
        selected.right - frame_.virtual_bounds.left,
        selected.bottom - frame_.virtual_bounds.top,
    };
    auto image = std::make_unique<capture::PixelBuffer>(
        capture::Crop(frame_.pixels, source_rect)
    );
    if (image->empty()) {
        Cancel();
        return;
    }
    notifying_ = true;
    capture::PixelBuffer* result = image.release();
    if (!::PostMessageW(
        host_,
        app::messages::kSelectionComplete,
        reinterpret_cast<WPARAM>(result),
        0
    )) {
        delete result;
        notifying_ = false;
        Cancel();
    }
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
    }
}

void SelectionOverlay::Paint() {
    PAINTSTRUCT paint{};
    HDC paint_dc = ::BeginPaint(window_, &paint);
    RECT client{};
    ::GetClientRect(window_, &client);
    const bool buffered = EnsurePaintBuffer(paint_dc, client.right, client.bottom);
    HDC dc = buffered ? paint_buffer_dc_ : paint_dc;
    const auto& image = frame_.pixels;
    const bool source_buffered = EnsureSourceBuffer(paint_dc);
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = image.width;
    bitmap_info.bmiHeader.biHeight = -image.height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    if (source_buffered) {
        ::BitBlt(dc, 0, 0, image.width, image.height,
            source_buffer_dc_, 0, 0, SRCCOPY);
    } else {
        ::SetDIBitsToDevice(
            dc, 0, 0, image.width, image.height, 0, 0, 0, image.height,
            image.pixels.data(), &bitmap_info, DIB_RGB_COLORS
        );
    }

    HDC dim_dc = ::CreateCompatibleDC(dc);
    HBITMAP dim_bitmap = ::CreateCompatibleBitmap(dc, 1, 1);
    HGDIOBJ old_bitmap = ::SelectObject(dim_dc, dim_bitmap);
    ::SetPixel(dim_dc, 0, 0, RGB(0, 0, 0));
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 105, 0};
    ::AlphaBlend(dc, 0, 0, client.right, client.bottom, dim_dc, 0, 0, 1, 1, blend);
    ::SelectObject(dim_dc, old_bitmap);
    ::DeleteObject(dim_bitmap);
    ::DeleteDC(dim_dc);

    if (selecting_) {
        const auto selection = Selection();
        if (!selection.empty()) {
            const RECT selected = ToClientRect(selection, frame_.virtual_bounds);
            const platform::PixelRect source_rect{
                selection.left - frame_.virtual_bounds.left,
                selection.top - frame_.virtual_bounds.top,
                selection.right - frame_.virtual_bounds.left,
                selection.bottom - frame_.virtual_bounds.top,
            };
            if (source_buffered) {
                ::BitBlt(dc, selected.left, selected.top,
                    selection.width(), selection.height(), source_buffer_dc_,
                    source_rect.left, source_rect.top, SRCCOPY);
            } else {
                const auto preview = capture::Crop(image, source_rect);
                BITMAPINFO preview_info{};
                preview_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                preview_info.bmiHeader.biWidth = preview.width;
                preview_info.bmiHeader.biHeight = -preview.height;
                preview_info.bmiHeader.biPlanes = 1;
                preview_info.bmiHeader.biBitCount = 32;
                preview_info.bmiHeader.biCompression = BI_RGB;
                ::SetDIBitsToDevice(dc, selected.left, selected.top,
                    preview.width, preview.height, 0, 0, 0, preview.height,
                    preview.pixels.data(), &preview_info, DIB_RGB_COLORS);
            }
            HPEN pen = ::CreatePen(PS_SOLID, 2, RGB(26, 115, 232));
            HGDIOBJ old_pen = ::SelectObject(dc, pen);
            HGDIOBJ old_brush = ::SelectObject(dc, ::GetStockObject(HOLLOW_BRUSH));
            ::Rectangle(dc, selected.left, selected.top, selected.right, selected.bottom);
            ::SelectObject(dc, old_brush);
            ::SelectObject(dc, old_pen);
            ::DeleteObject(pen);

            const std::wstring dimensions = std::to_wstring(selection.width()) + L" × " +
                std::to_wstring(selection.height());
            RECT label{selected.left, std::max(0L, selected.top - 28L), selected.left + 160,
                std::max(0L, selected.top - 4L)};
            ::SetBkColor(dc, RGB(26, 115, 232));
            ::SetTextColor(dc, RGB(255, 255, 255));
            ::DrawTextW(dc, dimensions.c_str(), -1, &label,
                DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
    }
    if (buffered) {
        ::BitBlt(paint_dc, 0, 0, client.right, client.bottom,
            paint_buffer_dc_, 0, 0, SRCCOPY);
    }
    ::EndPaint(window_, &paint);
}

LRESULT CALLBACK SelectionOverlay::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    SelectionOverlay* overlay = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        overlay = static_cast<SelectionOverlay*>(create->lpCreateParams);
        overlay->window_ = window;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
    } else {
        overlay = reinterpret_cast<SelectionOverlay*>(
            ::GetWindowLongPtrW(window, GWLP_USERDATA)
        );
    }
    return overlay != nullptr
        ? overlay->HandleMessage(message, w_param, l_param)
        : ::DefWindowProcW(window, message, w_param, l_param);
}

LRESULT SelectionOverlay::HandleMessage(
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    switch (message) {
    case i18n::kLanguageChanged:
        ::SetWindowTextW(window_, i18n::Text(i18n::Id::selection_title));
        return 0;
    case WM_LBUTTONDOWN:
        anchor_ = CursorPosition();
        cursor_ = anchor_;
        selecting_ = true;
        ::SetCapture(window_);
        RepaintImmediately();
        return 0;
    case WM_MOUSEMOVE:
        if (selecting_) {
            const auto position = CursorPosition();
            if (position != cursor_) {
                cursor_ = position;
                RepaintImmediately();
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (selecting_) {
            cursor_ = CursorPosition();
            selecting_ = false;
            ::ReleaseCapture();
            FinishSelection();
        }
        return 0;
    case WM_KEYDOWN:
        if (::GetKeyState(VK_ESCAPE) & 0x8000) {
            Cancel();
        }
        return 0;
    case WM_RBUTTONUP:
        Cancel();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_NCDESTROY:
        window_ = nullptr;
        return 0;
    default:
        return ::DefWindowProcW(window_, message, w_param, l_param);
    }
}

}  // namespace screenshooter::overlay
