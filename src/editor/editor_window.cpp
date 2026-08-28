#include "editor/editor_window.h"

#include "app/messages.h"
#include "editor/document_renderer.h"
#include "export/clipboard_service.h"
#include "platform/dpi.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace screenshooter::editor {
namespace {

constexpr wchar_t kWindowClass[] = L"Screenshooter.EditorWindow";
constexpr int kCopyButton = 3001;
constexpr int kSaveButton = 3002;
constexpr int kShareButton = 3003;
constexpr int kSelectButton = 3101;
constexpr int kRectangleButton = 3102;
constexpr int kLineButton = 3103;
constexpr int kBarHeightDip = 54;
constexpr int kToolbarHeightDip = 48;

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
}

bool EditorWindow::CreateNativeWindow() {
    if (!RegisterEditorClass(instance_)) {
        return false;
    }

    const auto title = L"Снимок " + std::to_wstring(session_.id) + L" — Screenshooter";
    const int width = std::clamp(session_.model.source().width + 80, 680, 1280);
    const int height = std::clamp(session_.model.source().height + 188, 520, 900);
    window_ = ::CreateWindowExW(
        0,
        kWindowClass,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        nullptr,
        nullptr,
        instance_,
        this
    );
    if (window_ == nullptr) {
        return false;
    }
    CreateControls();
    LayoutControls();
    ::ShowWindow(window_, SW_SHOWNORMAL);
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
    ::EnableWindow(save_button_, FALSE);
    ::EnableWindow(share_button_, FALSE);
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

    const int tool_height = platform::DipToPixels(32, dpi);
    const int tool_top = (toolbar - tool_height) / 2;
    int tool_left = gap;
    ::MoveWindow(select_button_, tool_left, tool_top,
        platform::DipToPixels(88, dpi), tool_height, TRUE);
    tool_left += platform::DipToPixels(96, dpi);
    ::MoveWindow(rectangle_button_, tool_left, tool_top,
        platform::DipToPixels(150, dpi), tool_height, TRUE);
    tool_left += platform::DipToPixels(158, dpi);
    ::MoveWindow(line_button_, tool_left, tool_top,
        platform::DipToPixels(88, dpi), tool_height, TRUE);
}

void EditorWindow::Paint() {
    PAINTSTRUCT paint{};
    HDC dc = ::BeginPaint(window_, &paint);
    RECT client{};
    ::GetClientRect(window_, &client);
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

    const auto rendered = DocumentRenderer::Render(session_.model);
    if (rendered.empty()) {
        ::EndPaint(window_, &paint);
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
    if (session_.model.state().selected.has_value()) {
        const Annotation* selected = session_.model.Find(*session_.model.state().selected);
        if (selected != nullptr) {
            const auto bounds = platform::Normalize(selected->start, selected->end);
            RECT selection{
                static_cast<LONG>(std::lround(offset_x_ + (bounds.left * scale_))),
                static_cast<LONG>(std::lround(offset_y_ + (bounds.top * scale_))),
                static_cast<LONG>(std::lround(offset_x_ + (bounds.right * scale_))),
                static_cast<LONG>(std::lround(offset_y_ + (bounds.bottom * scale_))),
            };
            HPEN pen = ::CreatePen(PS_DOT, 1, RGB(26, 115, 232));
            HGDIOBJ old_pen = ::SelectObject(dc, pen);
            HGDIOBJ old_brush = ::SelectObject(dc, ::GetStockObject(HOLLOW_BRUSH));
            ::Rectangle(dc, selection.left - 4, selection.top - 4,
                selection.right + 4, selection.bottom + 4);
            ::SelectObject(dc, old_brush);
            ::SelectObject(dc, old_pen);
            ::DeleteObject(pen);
        }
    }
    ::EndPaint(window_, &paint);
}

void EditorWindow::ZoomAt(const POINT client_point, const float factor) {
    const float document_x = (client_point.x - offset_x_) / scale_;
    const float document_y = (client_point.y - offset_y_) / scale_;
    scale_ = std::clamp(scale_ * factor, 0.1F, 8.0F);
    offset_x_ = client_point.x - (document_x * scale_);
    offset_y_ = client_point.y - (document_y * scale_);
    fit_ = false;
    ::InvalidateRect(window_, nullptr, FALSE);
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

platform::PixelPoint EditorWindow::DocumentPoint(const POINT client_point) const noexcept {
    return {
        static_cast<std::int32_t>(std::lround((client_point.x - offset_x_) / scale_)),
        static_cast<std::int32_t>(std::lround((client_point.y - offset_y_) / scale_)),
    };
}

bool EditorWindow::IsCanvasPoint(const POINT client_point) const noexcept {
    RECT client{};
    ::GetClientRect(window_, &client);
    const UINT dpi = ::GetDpiForWindow(window_);
    const int top = platform::DipToPixels(kToolbarHeightDip, dpi);
    const int bottom = client.bottom - platform::DipToPixels(kBarHeightDip, dpi);
    return client_point.y >= top && client_point.y < bottom;
}

void EditorWindow::BeginDocumentAction(const POINT client_point) {
    if (!IsCanvasPoint(client_point)) {
        return;
    }
    last_document_point_ = DocumentPoint(client_point);
    action_before_ = session_.model.Snapshot();
    if (tool_ == Tool::select) {
        const auto hit = session_.model.HitTest(last_document_point_);
        session_.model.Select(hit);
        if (hit.has_value()) {
            active_annotation_ = *hit;
            dragging_ = true;
            ::SetCapture(window_);
        }
        ::InvalidateRect(window_, nullptr, FALSE);
        return;
    }

    Annotation annotation{
        .kind = tool_ == Tool::rectangle
            ? AnnotationKind::rectangle
            : AnnotationKind::line,
        .start = last_document_point_,
        .end = last_document_point_,
    };
    active_annotation_ = session_.model.Add(annotation);
    creating_ = true;
    ::SetCapture(window_);
    ::InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::UpdateDocumentAction(const POINT client_point) {
    if (!dragging_ && !creating_) {
        return;
    }
    const auto current = DocumentPoint(client_point);
    if (dragging_) {
        const platform::PixelPoint delta{
            current.x - last_document_point_.x,
            current.y - last_document_point_.y,
        };
        static_cast<void>(session_.model.Move(active_annotation_, delta));
    } else if (Annotation* annotation = session_.model.Find(active_annotation_)) {
        annotation->end = current;
    }
    last_document_point_ = current;
    ::InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::EndDocumentAction(const POINT client_point) {
    if (!dragging_ && !creating_) {
        return;
    }
    UpdateDocumentAction(client_point);
    ::ReleaseCapture();
    dragging_ = false;
    creating_ = false;
    history_.Commit(action_before_, session_.model.Snapshot());
    ::InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::DeleteSelection() {
    if (!session_.model.state().selected.has_value()) {
        return;
    }
    const auto before = session_.model.Snapshot();
    static_cast<void>(session_.model.Remove(*session_.model.state().selected));
    history_.Commit(before, session_.model.Snapshot());
    ::InvalidateRect(window_, nullptr, FALSE);
}

void EditorWindow::Undo() {
    if (history_.Undo(session_.model)) {
        ::InvalidateRect(window_, nullptr, FALSE);
    }
}

void EditorWindow::Redo() {
    if (history_.Redo(session_.model)) {
        ::InvalidateRect(window_, nullptr, FALSE);
    }
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
    case WM_COMMAND:
        if (LOWORD(w_param) == kCopyButton) {
            CopyAndClose();
        } else if (LOWORD(w_param) == kSelectButton) {
            tool_ = Tool::select;
        } else if (LOWORD(w_param) == kRectangleButton) {
            tool_ = Tool::rectangle;
        } else if (LOWORD(w_param) == kLineButton) {
            tool_ = Tool::line;
        }
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
    case WM_LBUTTONDOWN:
        BeginDocumentAction(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        return 0;
    case WM_LBUTTONUP:
        EndDocumentAction(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        return 0;
    case WM_MBUTTONDOWN:
        panning_ = true;
        last_pan_ = POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        fit_ = false;
        ::SetCapture(window_);
        return 0;
    case WM_MOUSEMOVE:
        UpdateDocumentAction(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
        if (panning_) {
            const POINT current{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            offset_x_ += static_cast<float>(current.x - last_pan_.x);
            offset_y_ += static_cast<float>(current.y - last_pan_.y);
            last_pan_ = current;
            ::InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_MBUTTONUP:
        panning_ = false;
        ::ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (w_param == 'C') {
                CopyAndClose();
                return 0;
            }
            if (w_param == '0') {
                fit_ = true;
                ::InvalidateRect(window_, nullptr, FALSE);
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
            ::InvalidateRect(window_, nullptr, FALSE);
            return 0;
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_CLOSE:
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
