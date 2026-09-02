#include "editor/editor_window.h"

#include "app/messages.h"
#include "editor/document_renderer.h"
#include "export/clipboard_service.h"
#include "export/image_file_service.h"
#include "platform/dpi.h"
#include "settings/settings_store.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace screenshooter::editor {
namespace {

constexpr wchar_t kWindowClass[] = L"Screenshooter.EditorWindow";
constexpr int kCopyButton = 3001;
constexpr int kSaveButton = 3002;
constexpr int kShareButton = 3003;
constexpr int kSelectButton = 3101;
constexpr int kRectangleButton = 3102;
constexpr int kLineButton = 3103;
constexpr int kEllipseButton = 3104;
constexpr int kArrowButton = 3106;
constexpr int kMarkerButton = 3108;
constexpr int kTextButton = 3109;
constexpr int kBlurButton = 3110;
constexpr int kCropButton = 3111;
constexpr int kApplyCropButton = 3112;
constexpr int kCancelCropButton = 3113;
constexpr int kBarHeightDip = 54;
constexpr int kToolbarHeightDip = 48;
constexpr UINT kCommitTextEdit = WM_APP + 20;

bool RegisterEditorClass(const HINSTANCE instance) {
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &EditorWindow::WindowProcedure,
        .hInstance = instance,
        .hIcon = ::LoadIconW(nullptr, IDI_APPLICATION),
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = nullptr,
        .lpszClassName = kWindowClass,
        .hIconSm = ::LoadIconW(nullptr, IDI_APPLICATION),
    };
    return ::RegisterClassExW(&window_class) != 0 ||
        ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

platform::PixelRect CursorMonitorWorkArea() noexcept {
    POINT cursor{};
    if (!::GetCursorPos(&cursor)) {
        cursor = {};
    }
    const HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{.cbSize = sizeof(MONITORINFO)};
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &info)) {
        return {info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom};
    }

    RECT work_area{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        return {work_area.left, work_area.top, work_area.right, work_area.bottom};
    }
    return {0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
}

void ActivateEditorWindow(const HWND window) noexcept {
    ::ShowWindow(window, SW_SHOWNORMAL);
    ::BringWindowToTop(window);
    ::SetForegroundWindow(window);

    const HWND foreground = ::GetForegroundWindow();
    const DWORD current_thread = ::GetCurrentThreadId();
    DWORD foreground_thread = 0;
    if (foreground != nullptr && foreground != window) {
        foreground_thread = ::GetWindowThreadProcessId(foreground, nullptr);
    }
    const bool attached = foreground_thread != 0 &&
        foreground_thread != current_thread &&
        ::AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE;
    if (attached) {
        ::BringWindowToTop(window);
        ::SetForegroundWindow(window);
    }
    ::SetActiveWindow(window);
    ::SetFocus(window);
    if (attached) {
        ::AttachThreadInput(current_thread, foreground_thread, FALSE);
    }

    if (::GetForegroundWindow() != window) {
        FLASHWINFO flash{
            .cbSize = sizeof(FLASHWINFO),
            .hwnd = window,
            .dwFlags = FLASHW_TRAY,
            .uCount = 3,
        };
        ::FlashWindowEx(&flash);
    }
}

void FillTranslucentRegions(
    const HDC target,
    const RECT* regions,
    const std::size_t count
) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = -1;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC source = ::CreateCompatibleDC(target);
    HBITMAP bitmap = ::CreateDIBSection(target, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (source == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) ::DeleteObject(bitmap);
        if (source != nullptr) ::DeleteDC(source);
        return;
    }
    constexpr BYTE kAlpha = 58;
    constexpr BYTE kGray = 88;
    constexpr BYTE kPremultiplied = static_cast<BYTE>((kGray * kAlpha) / 255);
    *static_cast<std::uint32_t*>(bits) =
        (static_cast<std::uint32_t>(kAlpha) << 24U) |
        (static_cast<std::uint32_t>(kPremultiplied) << 16U) |
        (static_cast<std::uint32_t>(kPremultiplied) << 8U) |
        kPremultiplied;
    HGDIOBJ previous = ::SelectObject(source, bitmap);
    const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    for (std::size_t index = 0; index < count; ++index) {
        const RECT& area = regions[index];
        if (area.right > area.left && area.bottom > area.top) {
            ::AlphaBlend(target, area.left, area.top,
                area.right - area.left, area.bottom - area.top,
                source, 0, 0, 1, 1, blend);
        }
    }
    ::SelectObject(source, previous);
    ::DeleteObject(bitmap);
    ::DeleteDC(source);
}

}  // namespace

std::unique_ptr<EditorWindow> EditorWindow::Create(
    const HINSTANCE instance,
    const HWND host,
    DocumentSession session
) {
    auto editor = std::unique_ptr<EditorWindow>(
        new EditorWindow(instance, host, std::move(session))
    );
    return editor->CreateNativeWindow() ? std::move(editor) : nullptr;
}

EditorWindow::EditorWindow(
    const HINSTANCE instance,
    const HWND host,
    DocumentSession session
) : instance_(instance), host_(host), session_(std::move(session)) {}

EditorWindow::~EditorWindow() {
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
    }
    ReleasePaintBuffer();
}

bool EditorWindow::CreateNativeWindow() {
    if (!RegisterEditorClass(instance_)) {
        return false;
    }

    const auto title = session_.screenshot_name + L" — Screenshooter";
    const platform::PixelSize desired{
        std::min(session_.model.source().width, 1360) + 80,
        std::min(session_.model.source().height, 712) + 188,
    };
    const platform::PixelRect placement = platform::FitCenteredRect(
        desired,
        {1080, 520},
        {1440, 900},
        CursorMonitorWorkArea()
    );
    window_ = ::CreateWindowExW(
        0,
        kWindowClass,
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        placement.left,
        placement.top,
        placement.width(),
        placement.height(),
        nullptr,
        nullptr,
        instance_,
        this
    );
    if (window_ == nullptr) {
        return false;
    }
    ::SetWindowDisplayAffinity(window_, WDA_EXCLUDEFROMCAPTURE);
    CreateControls();
    LayoutControls();
    ActivateEditorWindow(window_);
    ::UpdateWindow(window_);
    return true;
}

void EditorWindow::CreateControls() {
    select_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Выбор", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSelectButton)),
        instance_, nullptr);
    rectangle_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Прямоугольник", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRectangleButton)), instance_, nullptr);
    line_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Линия", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLineButton)),
        instance_, nullptr);
    ellipse_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Эллипс", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEllipseButton)),
        instance_, nullptr);
    arrow_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Стрелка", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kArrowButton)),
        instance_, nullptr);
    marker_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Маркер", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kMarkerButton)),
        instance_, nullptr);
    text_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Текст", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTextButton)),
        instance_, nullptr);
    blur_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Размыть", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBlurButton)),
        instance_, nullptr);
    crop_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Обрезать", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCropButton)),
        instance_, nullptr);
    apply_crop_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Применить", WS_CHILD | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyCropButton)), instance_, nullptr);
    cancel_crop_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Отмена", WS_CHILD | BS_PUSHBUTTON,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelCropButton)), instance_, nullptr);
    copy_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Копировать", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyButton)),
        instance_, nullptr);
    save_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Сохранить", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveButton)),
        instance_, nullptr);
    share_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Поделиться", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShareButton)),
        instance_, nullptr);
    ::EnableWindow(save_button_, TRUE);
    ::EnableWindow(share_button_, TRUE);
    ::DragAcceptFiles(window_, TRUE);
}

void EditorWindow::LayoutControls() {
    if (window_ == nullptr) {
        return;
    }
    RECT client{};
    ::GetClientRect(window_, &client);
    const UINT dpi = ::GetDpiForWindow(window_);
    const int bar = platform::DipToPixels(kBarHeightDip, dpi);
    const int toolbar = platform::DipToPixels(kToolbarHeightDip, dpi);
    const int width = platform::DipToPixels(116, dpi);
    const int gap = platform::DipToPixels(8, dpi);
    const int height = platform::DipToPixels(34, dpi);
    const int top = client.bottom - bar + ((bar - height) / 2);
    int left = client.right - gap - width;
    ::MoveWindow(share_button_, left, top, width, height, TRUE);
    left -= width + gap;
    ::MoveWindow(save_button_, left, top, width, height, TRUE);
    left -= width + gap;
    ::MoveWindow(copy_button_, left, top, width, height, TRUE);

    const int crop_action_width = platform::DipToPixels(108, dpi);
    int crop_action_left = gap;
    ::MoveWindow(apply_crop_button_, crop_action_left, top,
        crop_action_width, height, TRUE);
    crop_action_left += crop_action_width + gap;
    ::MoveWindow(cancel_crop_button_, crop_action_left, top,
        crop_action_width, height, TRUE);

    const int tool_height = platform::DipToPixels(32, dpi);
    const int tool_top = (toolbar - tool_height) / 2;
    int tool_left = gap;
    const auto place_tool = [&](const HWND button, const int width_dip) {
        const int button_width = platform::DipToPixels(width_dip, dpi);
        ::MoveWindow(button, tool_left, tool_top, button_width, tool_height, TRUE);
        tool_left += button_width + gap;
    };
    place_tool(select_button_, 72);
    place_tool(rectangle_button_, 126);
    place_tool(line_button_, 72);
    place_tool(ellipse_button_, 78);
    place_tool(arrow_button_, 86);
    place_tool(marker_button_, 82);
    place_tool(text_button_, 72);
    place_tool(blur_button_, 84);
    place_tool(crop_button_, 92);
}

bool EditorWindow::EnsurePaintBuffer(
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

void EditorWindow::ReleasePaintBuffer() noexcept {
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

void EditorWindow::Paint() {
    PAINTSTRUCT paint{};
    HDC paint_dc = ::BeginPaint(window_, &paint);
    RECT client{};
    ::GetClientRect(window_, &client);
    const bool buffered = EnsurePaintBuffer(paint_dc, client.right, client.bottom);
    HDC dc = buffered ? paint_buffer_dc_ : paint_dc;
    const auto finish_paint = [&] {
        if (buffered) {
            ::BitBlt(paint_dc, 0, 0, client.right, client.bottom,
                paint_buffer_dc_, 0, 0, SRCCOPY);
        }
        ::EndPaint(window_, &paint);
    };
    const int bar = platform::DipToPixels(kBarHeightDip, ::GetDpiForWindow(window_));
    const int toolbar = platform::DipToPixels(kToolbarHeightDip, ::GetDpiForWindow(window_));
    RECT canvas = client;
    canvas.top += toolbar;
    canvas.bottom -= bar;

    HBRUSH canvas_brush = ::CreateSolidBrush(RGB(205, 216, 230));
    ::FillRect(dc, &canvas, canvas_brush);
    ::DeleteObject(canvas_brush);
    RECT bar_rect{client.left, canvas.bottom, client.right, client.bottom};
    HBRUSH bar_brush = ::CreateSolidBrush(RGB(248, 249, 251));
    ::FillRect(dc, &bar_rect, bar_brush);
    ::DeleteObject(bar_brush);
    RECT toolbar_rect{client.left, client.top, client.right, canvas.top};
    HBRUSH toolbar_brush = ::CreateSolidBrush(RGB(248, 249, 251));
    ::FillRect(dc, &toolbar_rect, toolbar_brush);
    ::DeleteObject(toolbar_brush);

    const auto rendered = DocumentRenderer::Render(session_.model, false);
    if (rendered.empty()) {
        finish_paint();
        return;
    }

    if (fit_) {
        const float available_width = static_cast<float>(std::max(1L, canvas.right - 32));
        const float available_height = static_cast<float>(std::max(1L, canvas.bottom - 32));
        scale_ = std::clamp(
            std::min(
                available_width / static_cast<float>(rendered.width),
                available_height / static_cast<float>(rendered.height)
            ),
            0.1F,
            1.0F
        );
        offset_x_ = (static_cast<float>(canvas.right) - (rendered.width * scale_)) / 2.0F;
        offset_y_ = canvas.top +
            ((static_cast<float>(canvas.bottom - canvas.top) -
                (rendered.height * scale_)) / 2.0F);
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = rendered.width;
    bitmap_info.bmiHeader.biHeight = -rendered.height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    ::SetStretchBltMode(dc, HALFTONE);
    ::StretchDIBits(
        dc,
        static_cast<int>(std::lround(offset_x_)),
        static_cast<int>(std::lround(offset_y_)),
        static_cast<int>(std::lround(rendered.width * scale_)),
        static_cast<int>(std::lround(rendered.height * scale_)),
        0,
        0,
        rendered.width,
        rendered.height,
        rendered.pixels.data(),
        &bitmap_info,
        DIB_RGB_COLORS,
        SRCCOPY
    );
    const auto visible_bounds = VisibleDocumentBounds();
    if (session_.model.state().selected.has_value()) {
        const Annotation* selected = session_.model.Find(*session_.model.state().selected);
        if (selected != nullptr) {
            const auto bounds = platform::Normalize(selected->start, selected->end);
            RECT selection{
                static_cast<LONG>(std::lround(
                    offset_x_ + ((bounds.left - visible_bounds.left) * scale_))),
                static_cast<LONG>(std::lround(
                    offset_y_ + ((bounds.top - visible_bounds.top) * scale_))),
                static_cast<LONG>(std::lround(
                    offset_x_ + ((bounds.right - visible_bounds.left) * scale_))),
                static_cast<LONG>(std::lround(
                    offset_y_ + ((bounds.bottom - visible_bounds.top) * scale_))),
            };
            HPEN pen = ::CreatePen(PS_DOT, 1, RGB(26, 115, 232));
            HGDIOBJ old_pen = ::SelectObject(dc, pen);
            HGDIOBJ old_brush = ::SelectObject(dc, ::GetStockObject(HOLLOW_BRUSH));
            ::Rectangle(dc, selection.left - 4, selection.top - 4,
                selection.right + 4, selection.bottom + 4);
            ::SelectObject(dc, old_brush);
            ::SelectObject(dc, old_pen);
            ::DeleteObject(pen);

            const auto to_client = [this, visible_bounds](
                                       const platform::PixelPoint point) {
                return POINT{
                    static_cast<LONG>(std::lround(
                        offset_x_ + ((point.x - visible_bounds.left) * scale_))),
                    static_cast<LONG>(std::lround(
                        offset_y_ + ((point.y - visible_bounds.top) * scale_))),
                };
            };
            const int handle_radius = platform::DipToPixels(
                5, ::GetDpiForWindow(window_));
            HBRUSH handle_fill = ::CreateSolidBrush(RGB(255, 255, 255));
            HBRUSH handle_border = ::CreateSolidBrush(RGB(26, 115, 232));
            const auto draw_handle = [&](const platform::PixelPoint point) {
                const POINT client_point = to_client(point);
                RECT handle{
                    client_point.x - handle_radius,
                    client_point.y - handle_radius,
                    client_point.x + handle_radius + 1,
                    client_point.y + handle_radius + 1,
                };
                ::FillRect(dc, &handle, handle_fill);
                ::FrameRect(dc, &handle, handle_border);
            };
            draw_handle(selected->start);
            if (selected->end != selected->start) {
                draw_handle(selected->end);
            }
            ::DeleteObject(handle_border);
            ::DeleteObject(handle_fill);
        }
    }
    const platform::PixelRect full{
        0, 0, session_.model.source().width, session_.model.source().height
    };
    if (session_.model.state().crop != full) {
        const auto crop = session_.model.state().crop;
        RECT crop_rect{
            static_cast<LONG>(std::lround(offset_x_ + (crop.left * scale_))),
            static_cast<LONG>(std::lround(offset_y_ + (crop.top * scale_))),
            static_cast<LONG>(std::lround(offset_x_ + (crop.right * scale_))),
            static_cast<LONG>(std::lround(offset_y_ + (crop.bottom * scale_))),
        };
        const RECT image_rect{
            static_cast<LONG>(std::lround(offset_x_)),
            static_cast<LONG>(std::lround(offset_y_)),
            static_cast<LONG>(std::lround(offset_x_ + (rendered.width * scale_))),
            static_cast<LONG>(std::lround(offset_y_ + (rendered.height * scale_))),
        };
        const RECT outside[]{
            {image_rect.left, image_rect.top, image_rect.right, crop_rect.top},
            {image_rect.left, crop_rect.bottom, image_rect.right, image_rect.bottom},
            {image_rect.left, crop_rect.top, crop_rect.left, crop_rect.bottom},
            {crop_rect.right, crop_rect.top, image_rect.right, crop_rect.bottom},
        };
        FillTranslucentRegions(dc, outside, std::size(outside));
        HPEN crop_pen = ::CreatePen(PS_SOLID, 2, RGB(26, 115, 232));
        HGDIOBJ old_pen = ::SelectObject(dc, crop_pen);
        HGDIOBJ old_brush = ::SelectObject(dc, ::GetStockObject(HOLLOW_BRUSH));
        ::Rectangle(dc, crop_rect.left, crop_rect.top, crop_rect.right, crop_rect.bottom);
        ::SelectObject(dc, old_brush);
        ::SelectObject(dc, old_pen);
        ::DeleteObject(crop_pen);
    }
    finish_paint();
    if (!first_paint_logged_) {
        first_paint_logged_ = true;
        performance::LogMetric(
            L"editor first paint", first_paint_timer_.ElapsedMilliseconds());
    }
}

void EditorWindow::InvalidateCanvas() const {
    if (window_ == nullptr) {
        return;
    }
    RECT canvas{};
    ::GetClientRect(window_, &canvas);
    const UINT dpi = ::GetDpiForWindow(window_);
    canvas.top += platform::DipToPixels(kToolbarHeightDip, dpi);
    canvas.bottom -= platform::DipToPixels(kBarHeightDip, dpi);
    ::InvalidateRect(window_, &canvas, FALSE);
}

platform::PixelRect EditorWindow::VisibleDocumentBounds() const noexcept {
    return {0, 0, session_.model.source().width, session_.model.source().height};
}

void EditorWindow::ShowCropActions(const bool visible) const {
    ::ShowWindow(apply_crop_button_, visible ? SW_SHOW : SW_HIDE);
    ::ShowWindow(cancel_crop_button_, visible ? SW_SHOW : SW_HIDE);
    ::EnableWindow(copy_button_, TRUE);
    ::EnableWindow(save_button_, visible ? FALSE : TRUE);
    ::EnableWindow(share_button_, visible ? FALSE : TRUE);
}

void EditorWindow::EnterCropMode() {
    if (tool_ == Tool::crop) {
        return;
    }
    CommitTextEdit();
    crop_mode_before_ = session_.model.Snapshot();
    session_.model.Select(std::nullopt);
    tool_ = Tool::crop;
    fit_ = true;
    ShowCropActions(true);
    InvalidateCanvas();
}

void EditorWindow::ApplyCrop() {
    if (tool_ != Tool::crop) {
        return;
    }
    if (!session_.model.ApplyCrop()) {
        ::MessageBoxW(window_, L"Не удалось применить обрезку.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
        return;
    }
    history_.Commit(crop_mode_before_, session_.model.Snapshot());
    tool_ = Tool::select;
    fit_ = true;
    ShowCropActions(false);
    InvalidateCanvas();
}

void EditorWindow::CancelCrop() {
    if (tool_ != Tool::crop) {
        return;
    }
    session_.model.Restore(crop_mode_before_);
    tool_ = Tool::select;
    fit_ = true;
    ShowCropActions(false);
    InvalidateCanvas();
}

void EditorWindow::ZoomAt(const POINT client_point, const float factor) {
    const float document_x = (client_point.x - offset_x_) / scale_;
    const float document_y = (client_point.y - offset_y_) / scale_;
    scale_ = std::clamp(scale_ * factor, 0.1F, 8.0F);
    offset_x_ = client_point.x - (document_x * scale_);
    offset_y_ = client_point.y - (document_y * scale_);
    fit_ = false;
    InvalidateCanvas();
}

void EditorWindow::CopyAndClose() {
    const auto rendered = DocumentRenderer::Render(session_.model);
    if (exporting::ClipboardService::CopyImage(window_, rendered)) {
        closing_after_export_ = true;
        ::DestroyWindow(window_);
    } else {
        ::MessageBoxW(
            window_,
            L"Не удалось скопировать изображение.",
            L"Screenshooter",
            MB_OK | MB_ICONERROR
        );
    }
}

void EditorWindow::SaveAs() {
    const auto rendered = DocumentRenderer::Render(session_.model);
    Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
    if (FAILED(::CoCreateInstance(
            CLSID_FileSaveDialog,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)))) {
        ::MessageBoxW(window_, L"Не удалось открыть диалог сохранения.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
        return;
    }
    const COMDLG_FILTERSPEC filters[]{
        {L"PNG изображение", L"*.png"},
        {L"JPEG изображение", L"*.jpg;*.jpeg"},
    };
    dialog->SetFileTypes(ARRAYSIZE(filters), filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetDefaultExtension(L"png");
    const auto configured_folder = settings::SettingsStore().Load().save_folder;
    if (!configured_folder.empty()) {
        Microsoft::WRL::ComPtr<IShellItem> folder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(
                configured_folder.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder.Get());
        }
    }
    dialog->SetFileName(session_.screenshot_name.c_str());
    const HRESULT shown = dialog->Show(window_);
    ::SetFocus(window_);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return;
    }
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    UINT file_type = 1;
    if (FAILED(shown) || FAILED(dialog->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) {
        ::MessageBoxW(window_, L"Не удалось выбрать путь сохранения.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
        return;
    }
    dialog->GetFileTypeIndex(&file_type);
    const auto format = file_type == 2
        ? exporting::ImageFormat::jpeg
        : exporting::ImageFormat::png;
    const bool saved = exporting::ImageFileService::Save(path, rendered, format);
    ::CoTaskMemFree(path);
    if (saved) {
        session_.model.MarkClean();
        ::MessageBoxW(window_, L"Изображение сохранено.",
            L"Screenshooter", MB_OK | MB_ICONINFORMATION);
    } else {
        ::MessageBoxW(window_, L"Не удалось сохранить изображение.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
    }
}

void EditorWindow::Share() {
    const auto rendered = DocumentRenderer::Render(session_.model);
    if (!share_service_.Show(window_, rendered)) {
        ::MessageBoxW(window_,
            L"Не удалось открыть системную панель «Поделиться».",
            L"Screenshooter", MB_OK | MB_ICONERROR);
    }
}

void EditorWindow::InsertImageFile(const wchar_t* path) {
    auto image = exporting::ImageFileService::Load(path);
    if (!image.has_value()) {
        ::MessageBoxW(window_, L"Не удалось открыть изображение.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
        return;
    }
    const auto before = session_.model.Snapshot();
    auto shared_image = std::make_shared<capture::PixelBuffer>(std::move(*image));
    const auto visible = VisibleDocumentBounds();
    const int maximum_width = std::max(1, visible.width() - 40);
    const int maximum_height = std::max(1, visible.height() - 40);
    const double scale = std::min({
        1.0,
        maximum_width / static_cast<double>(shared_image->width),
        maximum_height / static_cast<double>(shared_image->height),
    });
    Annotation annotation{
        .kind = AnnotationKind::image,
        .start = {visible.left + 20, visible.top + 20},
        .end = {
            visible.left + 20 +
                static_cast<int>(std::lround(shared_image->width * scale)),
            visible.top + 20 +
                static_cast<int>(std::lround(shared_image->height * scale)),
        },
        .image = std::move(shared_image),
    };
    static_cast<void>(session_.model.Add(std::move(annotation)));
    history_.Commit(before, session_.model.Snapshot());
    InvalidateCanvas();
}

platform::PixelPoint EditorWindow::DocumentPoint(const POINT client_point) const noexcept {
    const auto visible = VisibleDocumentBounds();
    return {
        visible.left + static_cast<std::int32_t>(
            std::lround((client_point.x - offset_x_) / scale_)),
        visible.top + static_cast<std::int32_t>(
            std::lround((client_point.y - offset_y_) / scale_)),
    };
}

bool EditorWindow::IsCanvasPoint(const POINT client_point) const noexcept {
    RECT client{};
    ::GetClientRect(window_, &client);
    const UINT dpi = ::GetDpiForWindow(window_);
    const int top = platform::DipToPixels(kToolbarHeightDip, dpi);
    const int bottom = client.bottom - platform::DipToPixels(kBarHeightDip, dpi);
    if (client_point.y < top || client_point.y >= bottom) {
        return false;
    }
    const auto document = DocumentPoint(client_point);
    return platform::Contains(VisibleDocumentBounds(), document);
}

void EditorWindow::BeginDocumentAction(const POINT client_point) {
    if (!IsCanvasPoint(client_point)) {
        return;
    }
    ::SetFocus(window_);
    last_document_point_ = DocumentPoint(client_point);
    action_anchor_ = last_document_point_;
    action_before_ = session_.model.Snapshot();
    if (tool_ != Tool::crop) {
        if (session_.model.state().selected.has_value()) {
            Annotation* selected = session_.model.Find(*session_.model.state().selected);
            const int handle_tolerance = std::max(
                4,
                static_cast<int>(std::ceil(
                    platform::DipToPixels(8, ::GetDpiForWindow(window_)) /
                    std::max(scale_, 0.1F)
                ))
            );
            resize_handle_ = selected != nullptr
                ? HitResizeHandle(*selected, last_document_point_, handle_tolerance)
                : ResizeHandle::none;
            if (selected != nullptr && resize_handle_ != ResizeHandle::none) {
                active_annotation_ = selected->id;
                resizing_ = true;
                ::SetCapture(window_);
                InvalidateCanvas();
                return;
            }
        }
        const auto hit = session_.model.HitTest(last_document_point_);
        if (hit.has_value()) {
            session_.model.Select(hit);
            active_annotation_ = *hit;
            dragging_ = true;
            ::SetCapture(window_);
            InvalidateCanvas();
            return;
        }
        if (tool_ == Tool::select) {
            session_.model.Select(std::nullopt);
            InvalidateCanvas();
            return;
        }
    }
    if (tool_ == Tool::text) {
        BeginTextEdit(client_point);
        return;
    }
    if (tool_ == Tool::crop) {
        crop_drafting_ = true;
        ::SetCapture(window_);
        return;
    }

    AnnotationKind kind = AnnotationKind::line;
    switch (tool_) {
    case Tool::rectangle:
        kind = AnnotationKind::rectangle;
        break;
    case Tool::ellipse:
        kind = AnnotationKind::ellipse;
        break;
    case Tool::arrow:
        kind = AnnotationKind::arrow;
        break;
    case Tool::marker:
        kind = AnnotationKind::marker;
        break;
    case Tool::text:
        kind = AnnotationKind::text;
        break;
    case Tool::blur:
        kind = AnnotationKind::blur;
        break;
    case Tool::crop:
        kind = AnnotationKind::rectangle;
        break;
    case Tool::line:
    case Tool::select:
        kind = AnnotationKind::line;
        break;
    }
    Annotation annotation{
        .kind = kind,
        .start = last_document_point_,
        .end = last_document_point_,
    };
    if (kind == AnnotationKind::marker) {
        annotation.color = 0x66FFEB3B;
        annotation.thickness = 18;
        annotation.points.push_back(last_document_point_);
    }
    active_annotation_ = session_.model.Add(annotation);
    creating_ = true;
    ::SetCapture(window_);
    InvalidateCanvas();
}

void EditorWindow::UpdateDocumentAction(
    const POINT client_point,
    const bool constrain_geometry
) {
    if (!dragging_ && !creating_ && !resizing_ && !crop_drafting_) {
        return;
    }
    auto current = DocumentPoint(client_point);
    const auto& source = session_.model.source();
    if (crop_drafting_) {
        current.x = std::clamp(current.x, 0, source.width);
        current.y = std::clamp(current.y, 0, source.height);
    } else {
        const auto visible = VisibleDocumentBounds();
        current.x = std::clamp(current.x, visible.left, visible.right - 1);
        current.y = std::clamp(current.y, visible.top, visible.bottom - 1);
    }
    if (crop_drafting_) {
        session_.model.SetCrop(platform::Normalize(action_anchor_, current));
    } else if (resizing_) {
        if (Annotation* annotation = session_.model.Find(active_annotation_)) {
            static_cast<void>(ResizeAnnotationEndpoint(
                *annotation, resize_handle_, current));
        }
    } else if (dragging_) {
        const platform::PixelPoint delta{
            current.x - last_document_point_.x,
            current.y - last_document_point_.y,
        };
        static_cast<void>(session_.model.Move(active_annotation_, delta));
    } else if (Annotation* annotation = session_.model.Find(active_annotation_)) {
        annotation->end = current;
        if (constrain_geometry) {
            annotation->end = ConstrainShapeEnd(
                annotation->kind,
                annotation->start,
                annotation->end,
                VisibleDocumentBounds()
            );
        }
        if (annotation->kind == AnnotationKind::marker &&
            (annotation->points.empty() || annotation->points.back() != current)) {
            annotation->points.push_back(current);
        }
    }
    last_document_point_ = current;
    InvalidateCanvas();
}

void EditorWindow::EndDocumentAction(
    const POINT client_point,
    const bool constrain_geometry
) {
    if (!dragging_ && !creating_ && !resizing_ && !crop_drafting_) {
        return;
    }
    UpdateDocumentAction(client_point, constrain_geometry);
    if (creating_) {
        if (Annotation* annotation = session_.model.Find(active_annotation_);
            annotation != nullptr && annotation->kind == AnnotationKind::arrow &&
            std::abs(annotation->end.x - annotation->start.x) <= 8 &&
            std::abs(annotation->end.y - annotation->start.y) <= 8) {
            const platform::PixelPoint tip = annotation->end;
            annotation->start = DefaultArrowStart(
                tip, VisibleDocumentBounds());
            annotation->end = tip;
        }
    }
    ::ReleaseCapture();
    dragging_ = false;
    creating_ = false;
    resizing_ = false;
    resize_handle_ = ResizeHandle::none;
    const bool completed_crop = crop_drafting_;
    crop_drafting_ = false;
    if (!completed_crop) {
        history_.Commit(action_before_, session_.model.Snapshot());
    }
    InvalidateCanvas();
}

void EditorWindow::DeleteSelection() {
    if (!session_.model.state().selected.has_value()) {
        return;
    }
    const auto before = session_.model.Snapshot();
    static_cast<void>(session_.model.Remove(*session_.model.state().selected));
    history_.Commit(before, session_.model.Snapshot());
    InvalidateCanvas();
}

void EditorWindow::Undo() {
    if (history_.Undo(session_.model)) {
        InvalidateCanvas();
    }
}

void EditorWindow::Redo() {
    if (history_.Redo(session_.model)) {
        InvalidateCanvas();
    }
}

void EditorWindow::BeginTextEdit(const POINT client_point) {
    CommitTextEdit();
    action_before_ = session_.model.Snapshot();
    text_origin_ = DocumentPoint(client_point);
    const int width = static_cast<int>(std::lround(240.0F * scale_));
    const int height = static_cast<int>(std::lround(90.0F * scale_));
    text_edit_ = ::CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
        client_point.x,
        client_point.y,
        std::max(width, 160),
        std::max(height, 60),
        window_,
        nullptr,
        instance_,
        nullptr
    );
    if (text_edit_ == nullptr) {
        return;
    }
    ::SetWindowSubclass(
        text_edit_,
        &EditorWindow::TextEditProcedure,
        1,
        reinterpret_cast<DWORD_PTR>(this)
    );
    ::SetFocus(text_edit_);
}

void EditorWindow::CommitTextEdit() {
    if (text_edit_ == nullptr) {
        return;
    }
    const int length = ::GetWindowTextLengthW(text_edit_);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    ::GetWindowTextW(text_edit_, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    const HWND edit = text_edit_;
    text_edit_ = nullptr;
    ::RemoveWindowSubclass(edit, &EditorWindow::TextEditProcedure, 1);
    ::DestroyWindow(edit);
    if (text.empty()) {
        return;
    }
    Annotation annotation{
        .kind = AnnotationKind::text,
        .start = text_origin_,
        .end = {text_origin_.x + 240, text_origin_.y + 90},
        .text = std::move(text),
    };
    static_cast<void>(session_.model.Add(std::move(annotation)));
    history_.Commit(action_before_, session_.model.Snapshot());
    InvalidateCanvas();
}

void EditorWindow::CopySelectedObject() {
    if (!session_.model.state().selected.has_value()) {
        return;
    }
    const Annotation* selected = session_.model.Find(*session_.model.state().selected);
    if (selected != nullptr) {
        object_clipboard_ = *selected;
    }
}

void EditorWindow::PasteObject() {
    if (!object_clipboard_.has_value()) {
        auto image = exporting::ClipboardService::ReadImage(window_);
        if (!image.has_value()) {
            return;
        }
        const auto before = session_.model.Snapshot();
        auto shared_image = std::make_shared<capture::PixelBuffer>(std::move(*image));
        const auto visible = VisibleDocumentBounds();
        Annotation annotation{
            .kind = AnnotationKind::image,
            .start = {visible.left + 20, visible.top + 20},
            .end = {
                visible.left + 20 + shared_image->width,
                visible.top + 20 + shared_image->height},
            .image = std::move(shared_image),
        };
        static_cast<void>(session_.model.Add(std::move(annotation)));
        history_.Commit(before, session_.model.Snapshot());
        InvalidateCanvas();
        return;
    }
    const auto before = session_.model.Snapshot();
    Annotation copy = *object_clipboard_;
    copy.start.x += 12;
    copy.start.y += 12;
    copy.end.x += 12;
    copy.end.y += 12;
    for (auto& point : copy.points) {
        point.x += 12;
        point.y += 12;
    }
    static_cast<void>(session_.model.Add(std::move(copy)));
    history_.Commit(before, session_.model.Snapshot());
    InvalidateCanvas();
}

LRESULT CALLBACK EditorWindow::TextEditProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param,
    const UINT_PTR,
    const DWORD_PTR reference_data
) {
    auto* editor = reinterpret_cast<EditorWindow*>(reference_data);
    if (message == WM_KEYDOWN) {
        if (w_param == VK_ESCAPE) {
            ::PostMessageW(editor->window_, WM_CLOSE, 0, 0);
            return 0;
        }
        if (w_param == VK_RETURN && (::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            ::PostMessageW(editor->window_, kCommitTextEdit, 0, 0);
            return 0;
        }
    }
    return ::DefSubclassProc(window, message, w_param, l_param);
}

LRESULT CALLBACK EditorWindow::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    EditorWindow* editor = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        editor = static_cast<EditorWindow*>(create->lpCreateParams);
        editor->window_ = window;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    } else {
        editor = reinterpret_cast<EditorWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return editor != nullptr
        ? editor->HandleMessage(message, w_param, l_param)
        : ::DefWindowProcW(window, message, w_param, l_param);
}

LRESULT EditorWindow::HandleMessage(
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    switch (message) {
    case WM_COMMAND: {
        const auto choose_tool = [this](const Tool next) {
            if (tool_ == Tool::crop && next != Tool::crop) {
                CancelCrop();
            }
            tool_ = next;
        };
        if (reinterpret_cast<HWND>(l_param) == text_edit_ &&
            HIWORD(w_param) == EN_KILLFOCUS) {
            ::PostMessageW(window_, kCommitTextEdit, 0, 0);
        } else if (LOWORD(w_param) == kCopyButton) {
            CopyAndClose();
        } else if (LOWORD(w_param) == kSelectButton) {
            choose_tool(Tool::select);
        } else if (LOWORD(w_param) == kRectangleButton) {
            choose_tool(Tool::rectangle);
        } else if (LOWORD(w_param) == kLineButton) {
            choose_tool(Tool::line);
        } else if (LOWORD(w_param) == kEllipseButton) {
            choose_tool(Tool::ellipse);
        } else if (LOWORD(w_param) == kArrowButton) {
            choose_tool(Tool::arrow);
        } else if (LOWORD(w_param) == kMarkerButton) {
            choose_tool(Tool::marker);
        } else if (LOWORD(w_param) == kTextButton) {
            choose_tool(Tool::text);
        } else if (LOWORD(w_param) == kBlurButton) {
            choose_tool(Tool::blur);
        } else if (LOWORD(w_param) == kCropButton) {
            EnterCropMode();
        } else if (LOWORD(w_param) == kApplyCropButton) {
            ApplyCrop();
        } else if (LOWORD(w_param) == kCancelCropButton) {
            CancelCrop();
        } else if (LOWORD(w_param) == kSaveButton) {
            SaveAs();
        } else if (LOWORD(w_param) == kShareButton) {
            Share();
        }
        return 0;
    }
    case kCommitTextEdit:
        CommitTextEdit();
        ::SetFocus(window_);
        return 0;
    case WM_SIZE:
        LayoutControls();
        ::InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<const RECT*>(l_param);
        ::SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
        LayoutControls();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ::ScreenToClient(window_, &point);
        ZoomAt(point, GET_WHEEL_DELTA_WPARAM(w_param) > 0 ? 1.1F : (1.0F / 1.1F));
        return 0;
    }
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(w_param);
        const UINT length = ::DragQueryFileW(drop, 0, nullptr, 0);
        std::vector<wchar_t> path(static_cast<std::size_t>(length) + 1);
        if (length > 0 && ::DragQueryFileW(drop, 0, path.data(),
                static_cast<UINT>(path.size())) > 0) {
            InsertImageFile(path.data());
        }
        ::DragFinish(drop);
        return 0;
    }
    case WM_LBUTTONDOWN:
        BeginDocumentAction(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        return 0;
    case WM_LBUTTONUP:
        EndDocumentAction(
            POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)},
            (w_param & MK_SHIFT) != 0
        );
        return 0;
    case WM_MBUTTONDOWN:
        panning_ = true;
        last_pan_ = POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        fit_ = false;
        ::SetCapture(window_);
        return 0;
    case WM_MOUSEMOVE:
        UpdateDocumentAction(
            POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)},
            (w_param & MK_SHIFT) != 0
        );
        if (panning_) {
            const POINT current{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            offset_x_ += static_cast<float>(current.x - last_pan_.x);
            offset_y_ += static_cast<float>(current.y - last_pan_.y);
            last_pan_ = current;
            InvalidateCanvas();
        }
        return 0;
    case WM_MBUTTONUP:
        panning_ = false;
        ::ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        if (w_param == VK_ESCAPE) {
            if (session_.model.state().selected.has_value()) {
                DeleteSelection();
            } else {
                ::SendMessageW(window_, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (w_param == 'C') {
                if (session_.model.state().selected.has_value()) {
                    CopySelectedObject();
                } else {
                    CopyAndClose();
                }
                return 0;
            }
            if (w_param == 'X' && session_.model.state().selected.has_value()) {
                CopySelectedObject();
                DeleteSelection();
                return 0;
            }
            if (w_param == 'V') {
                PasteObject();
                return 0;
            }
            if (w_param == '0') {
                fit_ = true;
                InvalidateCanvas();
                return 0;
            }
            if (w_param == 'Z') {
                Undo();
                return 0;
            }
            if (w_param == 'Y') {
                Redo();
                return 0;
            }
            RECT client{};
            ::GetClientRect(window_, &client);
            if (w_param == VK_OEM_PLUS || w_param == VK_ADD) {
                ZoomAt(POINT{client.right / 2, client.bottom / 2}, 1.1F);
                return 0;
            }
            if (w_param == VK_OEM_MINUS || w_param == VK_SUBTRACT) {
                ZoomAt(POINT{client.right / 2, client.bottom / 2}, 1.0F / 1.1F);
                return 0;
            }
        }
        if (w_param == VK_DELETE || w_param == VK_BACK) {
            DeleteSelection();
            return 0;
        }
        if (session_.model.state().selected.has_value() &&
            (w_param == VK_LEFT || w_param == VK_RIGHT ||
             w_param == VK_UP || w_param == VK_DOWN)) {
            const int distance = (::GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 10 : 1;
            platform::PixelPoint delta{};
            if (w_param == VK_LEFT) {
                delta.x = -distance;
            } else if (w_param == VK_RIGHT) {
                delta.x = distance;
            } else if (w_param == VK_UP) {
                delta.y = -distance;
            } else {
                delta.y = distance;
            }
            const auto before = session_.model.Snapshot();
            static_cast<void>(session_.model.Move(
                *session_.model.state().selected, delta));
            history_.Commit(before, session_.model.Snapshot());
            InvalidateCanvas();
            return 0;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_CLOSE:
        CommitTextEdit();
        if (!closing_after_export_ && session_.model.dirty() &&
            ::MessageBoxW(
                window_,
                L"В снимке есть несохранённые изменения. Закрыть окно?",
                L"Screenshooter",
                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
            ) != IDYES) {
            return 0;
        }
        ::DestroyWindow(window_);
        return 0;
    case WM_NCDESTROY: {
        const HWND closed = window_;
        window_ = nullptr;
        ::SetWindowLongPtrW(closed, GWLP_USERDATA, 0);
        ::PostMessageW(
            host_,
            app::messages::kEditorClosed,
            reinterpret_cast<WPARAM>(closed),
            0
        );
        return ::DefWindowProcW(closed, message, w_param, l_param);
    }
    default:
        return ::DefWindowProcW(window_, message, w_param, l_param);
    }
}

}  // namespace screenshooter::editor
