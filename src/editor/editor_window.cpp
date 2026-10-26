#include "editor/editor_window.h"

#include "app/messages.h"
#include "editor/document_renderer.h"
#include "export/clipboard_service.h"
#include "export/image_file_service.h"
#include "export/screenshot_file_name.h"
#include "platform/dpi.h"
#include "platform/explorer_service.h"
#include "resource.h"
#include "settings/settings_store.h"

#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace screenshooter::editor {
namespace {

constexpr wchar_t kWindowClass[] = L"Screenshooter.EditorWindow";
constexpr int kCopyButton = 3001;
constexpr int kSaveButton = 3002;
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
constexpr int kInfoButton = 3201;
constexpr int kSettingsButton = 3202;
constexpr int kRedButton = 3211;
constexpr int kGreenButton = 3212;
constexpr int kBlueButton = 3213;
constexpr int kBlackButton = 3214;
constexpr int kFontSizeCombo = 3221;
constexpr int kBoldButton = 3222;
constexpr int kItalicButton = 3223;
constexpr int kUnderlineButton = 3224;
constexpr int kFitButton = 3231;
constexpr int kActualSizeButton = 3232;
constexpr int kToolPanelWidthDip = 220;
constexpr int kCropActionWidthDip = 112;
constexpr int kCropActionHeightDip = 38;
constexpr int kCropActionGapDip = 6;
constexpr int kCropActionOffsetDip = 8;
constexpr UINT kCommitTextEdit = WM_APP + 20;

int ToolIconResourceId(const int control_id) noexcept {
    switch (control_id) {
    case kSelectButton: return IDI_TABLER_POINTER;
    case kRectangleButton: return IDI_TABLER_RECTANGLE;
    case kLineButton: return IDI_TABLER_LINE;
    case kEllipseButton: return IDI_TABLER_OVAL;
    case kArrowButton: return IDI_TABLER_ARROW_UP_RIGHT;
    case kMarkerButton: return IDI_TABLER_HIGHLIGHT;
    case kTextButton: return IDI_TABLER_LETTER_T;
    case kBlurButton: return IDI_TABLER_BLUR;
    case kCropButton: return IDI_TABLER_CROP;
    default: return 0;
    }
}

void DrawToolGlyph(const HDC dc, const int control_id, const RECT bounds) {
    const int resource_id = ToolIconResourceId(control_id);
    if (resource_id == 0) {
        return;
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const HICON icon = reinterpret_cast<HICON>(::LoadImageW(
        ::GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(resource_id),
        IMAGE_ICON,
        width,
        height,
        LR_DEFAULTCOLOR | LR_SHARED
    ));
    if (icon != nullptr) {
        ::DrawIconEx(dc, bounds.left, bounds.top, icon, width, height,
            0, nullptr, DI_NORMAL);
    }
}

int ActionIconResourceId(const int control_id) noexcept {
    switch (control_id) {
    case kCopyButton: return IDI_TABLER_COPY;
    case kSaveButton: return IDI_TABLER_DEVICE_FLOPPY;
    case kInfoButton: return IDI_TABLER_INFO_CIRCLE;
    case kSettingsButton: return IDI_TABLER_SETTINGS;
    default: return 0;
    }
}

const wchar_t* ActionButtonLabel(const int control_id) noexcept {
    switch (control_id) {
    case kCopyButton: return L"Копировать в буфер\nи закрыть";
    case kSaveButton: return L"Сохранить";
    case kApplyCropButton: return L"Применить";
    case kCancelCropButton: return L"Отмена";
    case kInfoButton: return L"";
    case kSettingsButton: return L"Настройки";
    case kFitButton: return L"Вписать";
    case kActualSizeButton: return L"100%";
    default: return L"";
    }
}

bool IsActionControlId(const int control_id) noexcept {
    return control_id == kCopyButton || control_id == kSaveButton ||
        control_id == kApplyCropButton || control_id == kCancelCropButton ||
        control_id == kInfoButton || control_id == kSettingsButton ||
        control_id == kFitButton || control_id == kActualSizeButton;
}

bool IsColorControlId(const int control_id) noexcept {
    return control_id == kRedButton || control_id == kGreenButton ||
        control_id == kBlueButton || control_id == kBlackButton;
}

std::uint32_t ColorForControlId(const int control_id) noexcept {
    switch (control_id) {
    case kGreenButton: return 0xFF2E7D32;
    case kBlueButton: return 0xFF1E88E5;
    case kBlackButton: return 0xFF202124;
    default: return 0xFFE53935;
    }
}

int TextStyleIconResourceId(const int control_id) noexcept {
    switch (control_id) {
    case kBoldButton: return IDI_TABLER_BOLD;
    case kItalicButton: return IDI_TABLER_ITALIC;
    case kUnderlineButton: return IDI_TABLER_UNDERLINE;
    default: return 0;
    }
}

bool IsTextStyleControlId(const int control_id) noexcept {
    return control_id == kBoldButton || control_id == kItalicButton ||
        control_id == kUnderlineButton;
}

bool SupportsColor(const AnnotationKind kind) noexcept {
    return kind == AnnotationKind::rectangle || kind == AnnotationKind::ellipse ||
        kind == AnnotationKind::line || kind == AnnotationKind::arrow ||
        kind == AnnotationKind::text;
}

void SetRoundedButtonRegion(
    const HWND button,
    const int width,
    const int height,
    const UINT dpi
) {
    const int radius = platform::DipToPixels(7, dpi);
    HRGN region = ::CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
    if (region != nullptr && ::SetWindowRgn(button, region, TRUE) == 0) {
        ::DeleteObject(region);
    }
}

bool RegisterEditorClass(const HINSTANCE instance) {
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
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

std::filesystem::path DefaultSaveFolder() {
    const auto configured = settings::SettingsStore().Load().save_folder;
    if (!configured.empty()) {
        return configured;
    }
    PWSTR pictures = nullptr;
    if (FAILED(::SHGetKnownFolderPath(
            FOLDERID_Pictures, static_cast<KNOWN_FOLDER_FLAG>(0), nullptr, &pictures)) ||
        pictures == nullptr) {
        return {};
    }
    const std::filesystem::path folder(pictures);
    ::CoTaskMemFree(pictures);
    return folder;
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
    if (ui_font_ != nullptr) ::DeleteObject(ui_font_);
    if (text_edit_font_ != nullptr) {
        ::DeleteObject(text_edit_font_);
    }
}

bool EditorWindow::CreateNativeWindow() {
    if (!RegisterEditorClass(instance_)) {
        return false;
    }

    const auto title = session_.screenshot_name + L" — Screenshooter";
    // Obtain the target monitor DPI before computing the physical window size.
    const auto work_area = CursorMonitorWorkArea();
    const HWND probe = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
        work_area.left, work_area.top, 1, 1, nullptr, nullptr, instance_, nullptr);
    const UINT dpi = probe != nullptr ? ::GetDpiForWindow(probe) : 96;
    if (probe != nullptr) ::DestroyWindow(probe);
    const auto dip = [dpi](int value) { return platform::DipToPixels(value, dpi); };
    const platform::PixelSize desired{
        ::MulDiv(work_area.width(), 65, 100),
        ::MulDiv(work_area.height(), 65, 100),
    };
    const platform::PixelRect placement = platform::FitCenteredRect(
        desired, {dip(480), dip(320)}, {work_area.width(), work_area.height()}, work_area);
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
    EnterCropMode();
    LayoutControls();
    ActivateEditorWindow(window_);
    ::UpdateWindow(window_);
    return true;
}

void EditorWindow::CreateControls() {
    panel_ = ::CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    ::SetWindowSubclass(panel_, &EditorWindow::PanelProcedure, 1,
        reinterpret_cast<DWORD_PTR>(this));
    footer_ = ::CreateWindowExW(WS_EX_CONTROLPARENT, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
        window_, nullptr, instance_, nullptr);
    ::SetWindowSubclass(footer_, &EditorWindow::PanelProcedure, 1,
        reinterpret_cast<DWORD_PTR>(this));
    const auto create_tool = [this](const wchar_t* label, const int command) {
        const HWND button = ::CreateWindowExW(
            0,
            L"BUTTON",
            label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0,
            0,
            0,
            0,
            panel_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)),
            instance_,
            nullptr
        );
        ::SetWindowSubclass(button, &EditorWindow::ToolButtonProcedure, 1,
            reinterpret_cast<DWORD_PTR>(this));
        ::SendMessageW(button, WM_SETFONT,
            reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return button;
    };
    rectangle_button_ = create_tool(L"Прямоугольник", kRectangleButton);
    ellipse_button_ = create_tool(L"Эллипс", kEllipseButton);
    line_button_ = create_tool(L"Линия", kLineButton);
    arrow_button_ = create_tool(L"Стрелка", kArrowButton);
    marker_button_ = create_tool(L"Маркер", kMarkerButton);
    text_button_ = create_tool(L"Текст", kTextButton);
    blur_button_ = create_tool(L"Размыть", kBlurButton);
    crop_button_ = create_tool(L"Обрезать", kCropButton);
    select_button_ = create_tool(L"Выбор", kSelectButton);
    const auto create_owner_button = [this](const wchar_t* name, const int command) {
        return ::CreateWindowExW(
            0, L"BUTTON", name, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, panel_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)), instance_, nullptr);
    };
    info_button_ = create_owner_button(L"Информация", kInfoButton);
    settings_button_ = create_owner_button(L"Настройки", kSettingsButton);
    fit_button_ = create_owner_button(L"Вписать", kFitButton);
    actual_size_button_ = create_owner_button(L"100%", kActualSizeButton);
    zoom_label_ = ::CreateWindowExW(0, L"STATIC", L"Масштаб: 100%",
        WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, panel_, nullptr, instance_, nullptr);
    red_button_ = create_owner_button(L"Красный", kRedButton);
    green_button_ = create_owner_button(L"Зелёный", kGreenButton);
    blue_button_ = create_owner_button(L"Синий", kBlueButton);
    black_button_ = create_owner_button(L"Чёрный", kBlackButton);
    bold_button_ = create_owner_button(L"Полужирный", kBoldButton);
    italic_button_ = create_owner_button(L"Курсив", kItalicButton);
    underline_button_ = create_owner_button(L"Подчёркнутый", kUnderlineButton);
    font_size_combo_ = ::CreateWindowExW(
        0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            CBS_DROPDOWNLIST | CBS_HASSTRINGS | CBS_OWNERDRAWFIXED,
        0, 0, 0, 0, panel_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontSizeCombo)), instance_, nullptr);
    for (const wchar_t* size : {L"18 px", L"24 px", L"32 px", L"40 px", L"48 px"}) {
        ::SendMessageW(font_size_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(size));
    }
    ::SendMessageW(font_size_combo_, CB_SETCURSEL, 1, 0);
    ::SetWindowSubclass(font_size_combo_, &EditorWindow::ToolButtonProcedure, 1,
        reinterpret_cast<DWORD_PTR>(this));
    apply_crop_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Применить", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApplyCropButton)), instance_, nullptr);
    cancel_crop_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Отмена", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelCropButton)), instance_, nullptr);
    copy_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Копировать в буфер обмена и закрыть", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, footer_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyButton)),
        instance_, nullptr);
    save_button_ = ::CreateWindowExW(
        0, L"BUTTON", L"Сохранить", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, footer_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSaveButton)),
        instance_, nullptr);
    const HWND action_buttons[]{
        apply_crop_button_, cancel_crop_button_, copy_button_, save_button_,
        info_button_, settings_button_, fit_button_, actual_size_button_, red_button_, green_button_, blue_button_,
        black_button_,
        bold_button_, italic_button_, underline_button_,
    };
    for (const HWND button : action_buttons) {
        ::SetWindowSubclass(button, &EditorWindow::ToolButtonProcedure, 1,
            reinterpret_cast<DWORD_PTR>(this));
        ::SendMessageW(button, WM_SETFONT,
            reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    ::SendMessageW(font_size_combo_, WM_SETFONT,
        reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    const HWND tooltip = ::CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        window_, nullptr, instance_, nullptr);
    if (tooltip != nullptr) {
        const auto add_tooltip = [tooltip, this](const HWND control, wchar_t* text) {
            TOOLINFOW info{
                .cbSize = sizeof(TOOLINFOW),
                .uFlags = TTF_IDISHWND | TTF_SUBCLASS,
                .hwnd = window_,
                .uId = reinterpret_cast<UINT_PTR>(control),
                .lpszText = text,
            };
            ::SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        };
        add_tooltip(copy_button_, const_cast<wchar_t*>(L"Копировать снимок в буфер обмена и закрыть редактор"));
        add_tooltip(info_button_, const_cast<wchar_t*>(L"Информация"));
        add_tooltip(settings_button_, const_cast<wchar_t*>(L"Настройки"));
        add_tooltip(bold_button_, const_cast<wchar_t*>(L"Полужирный"));
        add_tooltip(italic_button_, const_cast<wchar_t*>(L"Курсив"));
        add_tooltip(underline_button_, const_cast<wchar_t*>(L"Подчёркнутый"));
        add_tooltip(actual_size_button_, const_cast<wchar_t*>(L"Исходный размер: один пиксель изображения на пиксель экрана"));
        add_tooltip(fit_button_, const_cast<wchar_t*>(L"Вписать снимок в окно (Ctrl+0)"));
    }
    ::EnableWindow(save_button_, TRUE);
    ::DragAcceptFiles(window_, TRUE);
    // Child Z order also defines native Tab order.
    for (HWND control : {settings_button_, info_button_, rectangle_button_, ellipse_button_,
            line_button_, arrow_button_, marker_button_, text_button_, blur_button_,
            crop_button_, select_button_, red_button_, green_button_, blue_button_,
            black_button_, font_size_combo_, bold_button_, italic_button_, underline_button_,
            actual_size_button_, fit_button_, copy_button_, save_button_}) {
        ::SetWindowPos(control, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    UpdateUiFont();
    UpdatePropertyControls();
}

void EditorWindow::UpdateUiFont() {
    const UINT dpi = ::GetDpiForWindow(window_);
    HFONT font = ::CreateFontW(-::MulDiv(11, static_cast<int>(dpi), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (font == nullptr) return;
    ::EnumChildWindows(window_, [](HWND child, LPARAM value) -> BOOL {
        ::SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(value), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
    if (text_edit_ != nullptr && text_edit_font_ != nullptr) {
        ::SendMessageW(text_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(text_edit_font_), TRUE);
    }
    if (ui_font_ != nullptr) ::DeleteObject(ui_font_);
    ui_font_ = font;
}

void EditorWindow::ScrollPanel(const int position) {
    panel_scroll_ = std::clamp(position, 0, panel_scroll_max_);
    LayoutControls();
    ::InvalidateRect(panel_, nullptr, TRUE);
}

void EditorWindow::LayoutControls() {
    if (panel_ == nullptr || laying_out_) return;
    laying_out_ = true;
    RECT client{};
    ::GetClientRect(window_, &client);
    const UINT dpi = ::GetDpiForWindow(window_);
    const auto dip = [dpi](int value) { return platform::DipToPixels(value, dpi); };
    const int panel_width = dip(kToolPanelWidthDip);
    const int footer_height = dip(114);
    const int panel_height = std::max(0, static_cast<int>(client.bottom) - footer_height);
    ::MoveWindow(panel_, client.right - panel_width, 0, panel_width, panel_height, TRUE);
    ::MoveWindow(footer_, client.right - panel_width, panel_height,
        panel_width, footer_height, TRUE);
    const bool colors = (::GetWindowLongPtrW(red_button_, GWL_STYLE) & WS_VISIBLE) != 0;
    const bool text = (::GetWindowLongPtrW(font_size_combo_, GWL_STYLE) & WS_VISIBLE) != 0;
    const int inset = dip(8), gap = dip(4), height = dip(38);
    const int content_height = dip(24) + inset * 4 + height * 11 + gap * 8 +
        (colors ? height + gap : 0) + (text ? height + gap : 0);
    panel_scroll_max_ = std::max(0, content_height - panel_height);
    panel_scroll_ = std::clamp(panel_scroll_, 0, panel_scroll_max_);
    SCROLLINFO scroll{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS,
        0, content_height - 1, static_cast<UINT>(panel_height), panel_scroll_, 0};
    ::SetScrollInfo(panel_, SB_VERT, &scroll, TRUE);
    RECT panel_client{};
    ::GetClientRect(panel_, &panel_client);
    const int width = panel_client.right - inset * 2;
    int y = inset - panel_scroll_;
    const auto place = [&](HWND control, int x, int top, int w, int h) {
        ::MoveWindow(control, x, top, w, h, TRUE);
    };
    const int settings_width = std::min(dip(132), width - height - gap);
    place(settings_button_, inset, y, settings_width, height);
    place(info_button_, inset + settings_width + gap, y, height, height);
    y += height + inset;
    for (HWND control : {rectangle_button_, ellipse_button_, line_button_, arrow_button_,
            marker_button_, text_button_, blur_button_, crop_button_, select_button_}) {
        place(control, inset, y, width, height);
        y += height + gap;
    }
    y += inset - gap;
    if (colors) {
        const HWND buttons[]{red_button_, green_button_, blue_button_, black_button_};
        for (int i = 0; i < 4; ++i) {
            place(buttons[i], inset + i * width / 4, y, width / 4, height);
        }
        y += height + gap;
    }
    if (text) {
        const int combo_width = dip(76);
        place(font_size_combo_, inset, y, combo_width, dip(180));
        // Native drop-down height follows its selection field, not MoveWindow's
        // height (which sizes the list). Preserve the theme's border metrics.
        RECT combo_bounds{};
        ::GetWindowRect(font_size_combo_, &combo_bounds);
        const int selection_height = static_cast<int>(::SendMessageW(
            font_size_combo_, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0));
        if (selection_height != CB_ERR) {
            const int item_height = std::max(1, selection_height + height -
                static_cast<int>(combo_bounds.bottom - combo_bounds.top));
            ::SendMessageW(font_size_combo_, CB_SETITEMHEIGHT, 0, item_height);
            ::SendMessageW(font_size_combo_, CB_SETITEMHEIGHT,
                static_cast<WPARAM>(-1), item_height);
        }
        const int style_width = (width - combo_width - 3 * gap) / 3;
        int x = inset + combo_width + gap;
        for (HWND control : {bold_button_, italic_button_, underline_button_}) {
            place(control, x, y, style_width, height);
            x += style_width + gap;
        }
        y += height + gap;
    }
    place(zoom_label_, inset, y, width, dip(24));
    y += dip(24);
    const int half = (width - gap) / 2;
    place(actual_size_button_, inset, y, half, height);
    place(fit_button_, inset + half + gap, y, width - half - gap, height);
    const int action_width = panel_width - inset * 2;
    const int copy_height = dip(54);
    place(copy_button_, inset, inset, action_width, copy_height);
    place(save_button_, inset, inset + copy_height + dip(6), action_width, height);
    SetRoundedButtonRegion(copy_button_, action_width, copy_height, dpi);
    SetRoundedButtonRegion(save_button_, action_width, height, dpi);
    LayoutCropActions();
    laying_out_ = false;
}

LRESULT CALLBACK EditorWindow::PanelProcedure(HWND window, UINT message, WPARAM w_param,
    LPARAM l_param, UINT_PTR, DWORD_PTR reference_data) {
    auto* editor = reinterpret_cast<EditorWindow*>(reference_data);
    if (message == WM_COMMAND || message == WM_DRAWITEM || message == WM_MEASUREITEM) {
        return ::SendMessageW(editor->window_, message, w_param, l_param);
    }
    if (message == WM_CTLCOLORSTATIC) {
        const HDC dc = reinterpret_cast<HDC>(w_param);
        ::SetTextColor(dc, RGB(39, 47, 58));
        ::SetBkColor(dc, RGB(248, 249, 251));
        ::SetDCBrushColor(dc, RGB(248, 249, 251));
        return reinterpret_cast<LRESULT>(::GetStockObject(DC_BRUSH));
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(window, &paint);
        ::SetDCBrushColor(dc, RGB(248, 249, 251));
        ::FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
        ::EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_MOUSEWHEEL) {
        editor->ScrollPanel(editor->panel_scroll_ -
            ::MulDiv(GET_WHEEL_DELTA_WPARAM(w_param),
                platform::DipToPixels(84, ::GetDpiForWindow(window)), WHEEL_DELTA));
        return 0;
    }
    if (message == WM_VSCROLL) {
        SCROLLINFO info{sizeof(SCROLLINFO), SIF_ALL};
        ::GetScrollInfo(window, SB_VERT, &info);
        int position = info.nPos;
        const int step = platform::DipToPixels(42, ::GetDpiForWindow(window));
        switch (LOWORD(w_param)) {
        case SB_LINEUP: position -= step; break;
        case SB_LINEDOWN: position += step; break;
        case SB_PAGEUP: position -= info.nPage; break;
        case SB_PAGEDOWN: position += info.nPage; break;
        case SB_THUMBTRACK: position = info.nTrackPos; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = editor->panel_scroll_max_; break;
        default: return 0;
        }
        editor->ScrollPanel(position);
        return 0;
    }
    return ::DefSubclassProc(window, message, w_param, l_param);
}

std::optional<EditorWindow::Tool> EditorWindow::ToolForControlId(
    const int control_id
) const noexcept {
    switch (control_id) {
    case kSelectButton: return Tool::select;
    case kRectangleButton: return Tool::rectangle;
    case kLineButton: return Tool::line;
    case kEllipseButton: return Tool::ellipse;
    case kArrowButton: return Tool::arrow;
    case kMarkerButton: return Tool::marker;
    case kTextButton: return Tool::text;
    case kBlurButton: return Tool::blur;
    case kCropButton: return Tool::crop;
    default: return std::nullopt;
    }
}

void EditorWindow::InvalidateToolButtons() const {
    const HWND buttons[]{
        rectangle_button_, ellipse_button_, line_button_, arrow_button_,
        marker_button_, text_button_, blur_button_, crop_button_, select_button_,
    };
    for (const HWND button : buttons) {
        ::InvalidateRect(button, nullptr, FALSE);
    }
}

void EditorWindow::DrawToolButton(const DRAWITEMSTRUCT& item) const {
    const auto item_tool = ToolForControlId(static_cast<int>(item.CtlID));
    const bool active = item_tool.has_value() && *item_tool == tool_;
    const bool hovered = item.hwndItem == hovered_button_;
    COLORREF background = RGB(248, 249, 251);
    if ((item.itemState & ODS_SELECTED) != 0) {
        background = RGB(199, 222, 248);
    } else if (active) {
        background = RGB(213, 232, 253);
    } else if (hovered) {
        background = RGB(232, 242, 253);
    }
    HBRUSH fill = ::CreateSolidBrush(background);
    ::FillRect(item.hDC, &item.rcItem, fill);
    ::DeleteObject(fill);

    if (active) {
        RECT accent = item.rcItem;
        accent.right = accent.left + platform::DipToPixels(
            4, ::GetDpiForWindow(window_));
        HBRUSH accent_brush = ::CreateSolidBrush(RGB(38, 117, 204));
        ::FillRect(item.hDC, &accent, accent_brush);
        ::DeleteObject(accent_brush);
    }

    const int icon_left = item.rcItem.left + platform::DipToPixels(
        10, ::GetDpiForWindow(window_));
    const int icon_size = platform::DipToPixels(24, ::GetDpiForWindow(window_));
    RECT icon{
        icon_left,
        item.rcItem.top + ((item.rcItem.bottom - item.rcItem.top - icon_size) / 2),
        icon_left + icon_size,
        item.rcItem.top + ((item.rcItem.bottom - item.rcItem.top + icon_size) / 2),
    };
    DrawToolGlyph(item.hDC, static_cast<int>(item.CtlID), icon);

    wchar_t label[64]{};
    ::GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    RECT text = item.rcItem;
    text.left = icon.right + platform::DipToPixels(8, ::GetDpiForWindow(window_));
    text.right -= platform::DipToPixels(8, ::GetDpiForWindow(window_));
    ::SetBkMode(item.hDC, TRANSPARENT);
    ::SetTextColor(item.hDC, RGB(39, 47, 58));
    HGDIOBJ old_font = ::SelectObject(item.hDC,
        reinterpret_cast<HFONT>(::SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
    ::DrawTextW(item.hDC, label, -1, &text,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    ::SelectObject(item.hDC, old_font);

    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        ::InflateRect(&focus, -3, -3);
        ::DrawFocusRect(item.hDC, &focus);
    }
}

void EditorWindow::DrawActionButton(const DRAWITEMSTRUCT& item) const {
    const UINT dpi = ::GetDpiForWindow(window_);
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool hovered = item.hwndItem == hovered_button_;
    COLORREF background = RGB(248, 249, 251);
    if (pressed) {
        background = RGB(213, 225, 238);
    } else if (hovered && !disabled) {
        background = RGB(232, 242, 253);
    }
    const COLORREF border = disabled ? RGB(215, 219, 224) : RGB(190, 198, 207);
    HBRUSH brush = ::CreateSolidBrush(background);
    HPEN pen = ::CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = ::SelectObject(item.hDC, brush);
    HGDIOBJ old_pen = ::SelectObject(item.hDC, pen);
    const int radius = platform::DipToPixels(7, dpi);
    ::RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
        item.rcItem.right, item.rcItem.bottom, radius, radius);
    ::SelectObject(item.hDC, old_pen);
    ::SelectObject(item.hDC, old_brush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);

    RECT content = item.rcItem;
    if (pressed) {
        ::OffsetRect(&content, 0, platform::DipToPixels(1, dpi));
    }
    const int resource_id = ActionIconResourceId(static_cast<int>(item.CtlID));
    const wchar_t* label = ActionButtonLabel(static_cast<int>(item.CtlID));
    if (resource_id != 0) {
        const int icon_size = platform::DipToPixels(24, dpi);
        const bool icon_only = label[0] == L'\0';
        RECT icon{
            icon_only
                ? content.left + ((content.right - content.left - icon_size) / 2)
                : content.left + platform::DipToPixels(10, dpi),
            content.top + ((content.bottom - content.top - icon_size) / 2),
            0,
            content.top + ((content.bottom - content.top + icon_size) / 2),
        };
        icon.right = icon.left + icon_size;
        const HICON glyph = reinterpret_cast<HICON>(::LoadImageW(
            instance_, MAKEINTRESOURCEW(resource_id), IMAGE_ICON,
            icon_size, icon_size, LR_DEFAULTCOLOR | LR_SHARED));
        if (glyph != nullptr) {
            ::DrawIconEx(item.hDC, icon.left, icon.top, glyph,
                icon_size, icon_size, 0, nullptr, DI_NORMAL);
        }
        if (!icon_only) {
            content.left = icon.right + platform::DipToPixels(6, dpi);
        }
    }
    ::SetBkMode(item.hDC, TRANSPARENT);
    ::SetTextColor(item.hDC, disabled ? RGB(142, 148, 156) : RGB(39, 47, 58));
    HGDIOBJ old_font = ::SelectObject(item.hDC,
        reinterpret_cast<HFONT>(::SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
    if (item.CtlID == kCopyButton) {
        content.right -= platform::DipToPixels(8, dpi);
        RECT measured{0, 0, content.right - content.left, 0};
        ::DrawTextW(item.hDC, label, -1, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        content.top += std::max(0L, (content.bottom - content.top - measured.bottom) / 2);
        ::DrawTextW(item.hDC, label, -1, &content, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    } else {
        ::DrawTextW(item.hDC, label, -1, &content,
            (resource_id == 0 ? DT_CENTER : DT_LEFT) |
            DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    ::SelectObject(item.hDC, old_font);
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        ::InflateRect(&focus, -4, -4);
        ::DrawFocusRect(item.hDC, &focus);
    }
}

void EditorWindow::DrawColorButton(const DRAWITEMSTRUCT& item) const {
    const UINT dpi = ::GetDpiForWindow(window_);
    const auto selected_id = session_.model.state().selected;
    const Annotation* selected = selected_id.has_value()
        ? session_.model.Find(*selected_id)
        : nullptr;
    const std::uint32_t displayed_color =
        selected != nullptr && SupportsColor(selected->kind)
            ? selected->color
            : current_color_;
    const std::uint32_t color = ColorForControlId(static_cast<int>(item.CtlID));
    COLORREF fill_color = RGB(
        (color >> 16U) & 0xFFU,
        (color >> 8U) & 0xFFU,
        color & 0xFFU);
    if ((item.itemState & ODS_SELECTED) != 0) {
        fill_color = RGB(
            (GetRValue(fill_color) * 88) / 100,
            (GetGValue(fill_color) * 88) / 100,
            (GetBValue(fill_color) * 88) / 100);
    }

    const int width = item.rcItem.right - item.rcItem.left;
    const int height = item.rcItem.bottom - item.rcItem.top;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(
        item.hDC, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC buffer = ::CreateCompatibleDC(item.hDC);
    if (bitmap != nullptr && buffer != nullptr && bits != nullptr) {
        struct Pixel {
            std::uint8_t blue;
            std::uint8_t green;
            std::uint8_t red;
            std::uint8_t alpha;
        };
        auto* pixels = static_cast<Pixel*>(bits);
        const COLORREF background = RGB(248, 249, 251);
        const COLORREF outline = item.hwndItem == hovered_button_
            ? RGB(55, 75, 98)
            : RGB(76, 84, 94);
        const bool active = displayed_color == color;
        const COLORREF selection_ring = RGB(231, 239, 248);
        const double scale = dpi / 96.0;
        const double center_x = width / 2.0;
        const double center_y = height / 2.0;
        const double outer_radius = (active ? 12.5 : 11.0) * scale;
        const double ring_radius = 10.75 * scale;
        const double fill_radius = 9.5 * scale;
        const auto coverage = [](const double radius, const double distance) {
            return std::clamp(radius + 0.5 - distance, 0.0, 1.0);
        };
        const auto blend = [](const COLORREF base, const COLORREF overlay,
                              const double amount) {
            const auto channel = [amount](const BYTE base_value, const BYTE overlay_value) {
                return static_cast<BYTE>(std::lround(
                    base_value + ((overlay_value - base_value) * amount)));
            };
            return RGB(
                channel(GetRValue(base), GetRValue(overlay)),
                channel(GetGValue(base), GetGValue(overlay)),
                channel(GetBValue(base), GetBValue(overlay)));
        };
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double dx = (x + 0.5) - center_x;
                const double dy = (y + 0.5) - center_y;
                const double distance = std::sqrt((dx * dx) + (dy * dy));
                COLORREF pixel = blend(
                    background, outline, coverage(outer_radius, distance));
                if (active) {
                    pixel = blend(
                        pixel, selection_ring, coverage(ring_radius, distance));
                }
                pixel = blend(pixel, fill_color, coverage(fill_radius, distance));
                Pixel& target = pixels[(y * width) + x];
                target.blue = GetBValue(pixel);
                target.green = GetGValue(pixel);
                target.red = GetRValue(pixel);
                target.alpha = 0xFF;
            }
        }
        HGDIOBJ previous = ::SelectObject(buffer, bitmap);
        ::BitBlt(item.hDC, item.rcItem.left, item.rcItem.top,
            width, height, buffer, 0, 0, SRCCOPY);
        ::SelectObject(buffer, previous);
    } else {
        HBRUSH background = ::CreateSolidBrush(RGB(248, 249, 251));
        ::FillRect(item.hDC, &item.rcItem, background);
        ::DeleteObject(background);
    }
    if (buffer != nullptr) {
        ::DeleteDC(buffer);
    }
    if (bitmap != nullptr) {
        ::DeleteObject(bitmap);
    }
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        ::InflateRect(&focus, -2, -2);
        ::DrawFocusRect(item.hDC, &focus);
    }
}

void EditorWindow::DrawTextStyleButton(const DRAWITEMSTRUCT& item) const {
    const auto selected_id = session_.model.state().selected;
    const Annotation* selected = selected_id.has_value()
        ? session_.model.Find(*selected_id)
        : nullptr;
    const bool bold = selected != nullptr && selected->kind == AnnotationKind::text
        ? selected->font_bold : current_font_bold_;
    const bool italic = selected != nullptr && selected->kind == AnnotationKind::text
        ? selected->font_italic : current_font_italic_;
    const bool underline = selected != nullptr && selected->kind == AnnotationKind::text
        ? selected->font_underline : current_font_underline_;
    const bool active = item.CtlID == kBoldButton ? bold
        : item.CtlID == kItalicButton ? italic : underline;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hovered = item.hwndItem == hovered_button_;
    const COLORREF background = pressed ? RGB(199, 222, 248)
        : active ? RGB(213, 232, 253)
        : hovered ? RGB(232, 242, 253)
        : RGB(248, 249, 251);
    HBRUSH brush = ::CreateSolidBrush(background);
    HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(190, 198, 207));
    HGDIOBJ old_brush = ::SelectObject(item.hDC, brush);
    HGDIOBJ old_pen = ::SelectObject(item.hDC, pen);
    const UINT dpi = ::GetDpiForWindow(window_);
    const int radius = platform::DipToPixels(6, dpi);
    ::RoundRect(item.hDC, item.rcItem.left, item.rcItem.top,
        item.rcItem.right, item.rcItem.bottom, radius, radius);
    ::SelectObject(item.hDC, old_pen);
    ::SelectObject(item.hDC, old_brush);
    ::DeleteObject(pen);
    ::DeleteObject(brush);
    const int icon_size = platform::DipToPixels(18, dpi);
    const HICON icon = reinterpret_cast<HICON>(::LoadImageW(
        instance_, MAKEINTRESOURCEW(TextStyleIconResourceId(static_cast<int>(item.CtlID))),
        IMAGE_ICON, icon_size, icon_size, LR_DEFAULTCOLOR | LR_SHARED));
    if (icon != nullptr) {
        ::DrawIconEx(item.hDC,
            item.rcItem.left + ((item.rcItem.right - item.rcItem.left - icon_size) / 2),
            item.rcItem.top + ((item.rcItem.bottom - item.rcItem.top - icon_size) / 2),
            icon, icon_size, icon_size, 0, nullptr, DI_NORMAL);
    }
    if ((item.itemState & ODS_FOCUS) != 0) {
        RECT focus = item.rcItem;
        ::InflateRect(&focus, -3, -3);
        ::DrawFocusRect(item.hDC, &focus);
    }
}

void EditorWindow::DrawFontSizeCombo(const DRAWITEMSTRUCT& item) const {
    const bool highlighted = (item.itemState & ODS_SELECTED) != 0 &&
        (item.itemState & ODS_COMBOBOXEDIT) == 0;
    ::FillRect(item.hDC, &item.rcItem,
        ::GetSysColorBrush(highlighted ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    const LRESULT index = item.itemID == static_cast<UINT>(-1)
        ? ::SendMessageW(item.hwndItem, CB_GETCURSEL, 0, 0)
        : static_cast<LRESULT>(item.itemID);
    if (index != CB_ERR) {
        wchar_t label[32]{};
        const LRESULT length = ::SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, index, 0);
        if (length >= 0 && length < static_cast<LRESULT>(std::size(label))) {
            ::SendMessageW(item.hwndItem, CB_GETLBTEXT, index,
                reinterpret_cast<LPARAM>(label));
            HGDIOBJ previous = ::SelectObject(item.hDC,
                reinterpret_cast<HFONT>(::SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
            ::SetBkMode(item.hDC, TRANSPARENT);
            ::SetTextColor(item.hDC, ::GetSysColor((item.itemState & ODS_DISABLED) != 0
                ? COLOR_GRAYTEXT : highlighted ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            RECT text = item.rcItem;
            ::DrawTextW(item.hDC, label, -1, &text,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(item.hDC, previous);
        }
    }
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0) {
        ::DrawFocusRect(item.hDC, &item.rcItem);
    }
}

void EditorWindow::UpdatePropertyControls() {
    const auto selected_id = session_.model.state().selected;
    const Annotation* selected = selected_id.has_value()
        ? session_.model.Find(*selected_id)
        : nullptr;
    const bool tool_supports_color = tool_ == Tool::rectangle || tool_ == Tool::ellipse ||
        tool_ == Tool::line || tool_ == Tool::arrow || tool_ == Tool::text;
    const bool color_visible = selected != nullptr
        ? SupportsColor(selected->kind)
        : tool_supports_color;
    const bool text_visible = selected != nullptr
        ? selected->kind == AnnotationKind::text
        : tool_ == Tool::text;
    for (const HWND button : {red_button_, green_button_, blue_button_, black_button_}) {
        ::ShowWindow(button, color_visible ? SW_SHOW : SW_HIDE);
        ::InvalidateRect(button, nullptr, FALSE);
    }
    ::ShowWindow(font_size_combo_, text_visible ? SW_SHOW : SW_HIDE);
    for (const HWND button : {bold_button_, italic_button_, underline_button_}) {
        ::ShowWindow(button, text_visible ? SW_SHOW : SW_HIDE);
        ::InvalidateRect(button, nullptr, FALSE);
    }
    LayoutControls();
    if (text_visible) {
        const int size = selected != nullptr ? selected->font_size : current_font_size_;
        constexpr int sizes[]{18, 24, 32, 40, 48};
        int index = 1;
        for (int candidate = 0; candidate < static_cast<int>(std::size(sizes)); ++candidate) {
            if (sizes[candidate] == size) {
                index = candidate;
                break;
            }
        }
        ::SendMessageW(font_size_combo_, CB_SETCURSEL, index, 0);
    }
}

void EditorWindow::ChooseColor(const std::uint32_t color) {
    current_color_ = color;
    const auto selected_id = session_.model.state().selected;
    Annotation* selected = selected_id.has_value() ? session_.model.Find(*selected_id) : nullptr;
    if (selected != nullptr && SupportsColor(selected->kind) && selected->color != color) {
        const auto before = session_.model.Snapshot();
        selected->color = color;
        history_.Commit(before, session_.model.Snapshot());
        InvalidateCanvas();
    }
    UpdatePropertyControls();
}

void EditorWindow::ChooseFontSize() {
    constexpr int sizes[]{18, 24, 32, 40, 48};
    const LRESULT selection = ::SendMessageW(font_size_combo_, CB_GETCURSEL, 0, 0);
    if (selection < 0 || selection >= static_cast<LRESULT>(std::size(sizes))) {
        return;
    }
    const int size = sizes[selection];
    current_font_size_ = size;
    const auto selected_id = session_.model.state().selected;
    Annotation* selected = selected_id.has_value() ? session_.model.Find(*selected_id) : nullptr;
    if (selected != nullptr && selected->kind == AnnotationKind::text &&
        selected->font_size != size) {
        const auto before = session_.model.Snapshot();
        selected->font_size = size;
        history_.Commit(before, session_.model.Snapshot());
        InvalidateCanvas();
    }
    UpdatePropertyControls();
}

void EditorWindow::ToggleTextStyle(const int control_id) {
    bool* current = control_id == kBoldButton ? &current_font_bold_
        : control_id == kItalicButton ? &current_font_italic_
        : &current_font_underline_;
    *current = !*current;
    const auto selected_id = session_.model.state().selected;
    Annotation* selected = selected_id.has_value() ? session_.model.Find(*selected_id) : nullptr;
    if (selected != nullptr && selected->kind == AnnotationKind::text) {
        bool* value = control_id == kBoldButton ? &selected->font_bold
            : control_id == kItalicButton ? &selected->font_italic
            : &selected->font_underline;
        const auto before = session_.model.Snapshot();
        *value = !*value;
        *current = *value;
        history_.Commit(before, session_.model.Snapshot());
        InvalidateCanvas();
    }
    UpdatePropertyControls();
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
    const int panel = platform::DipToPixels(kToolPanelWidthDip, ::GetDpiForWindow(window_));
    RECT canvas = client;
    canvas.right -= panel;

    HBRUSH canvas_brush = ::CreateSolidBrush(RGB(205, 216, 230));
    ::FillRect(dc, &canvas, canvas_brush);
    ::DeleteObject(canvas_brush);
    RECT panel_rect{canvas.right, client.top, client.right, canvas.bottom};
    HBRUSH panel_brush = ::CreateSolidBrush(RGB(248, 249, 251));
    ::FillRect(dc, &panel_rect, panel_brush);
    ::DeleteObject(panel_brush);
    HPEN separator = ::CreatePen(PS_SOLID, 1, RGB(220, 225, 231));
    HGDIOBJ old_separator = ::SelectObject(dc, separator);
    ::MoveToEx(dc, canvas.right, canvas.top, nullptr);
    ::LineTo(dc, canvas.right, canvas.bottom);
    ::SelectObject(dc, old_separator);
    ::DeleteObject(separator);

    const auto rendered = DocumentRenderer::Render(session_.model, false);
    if (rendered.empty()) {
        finish_paint();
        return;
    }

    if (fit_) {
        const float available_width = static_cast<float>(
            std::max(1L, (canvas.right - canvas.left) - 32));
        const float available_height = static_cast<float>(
            std::max(1L, (canvas.bottom - canvas.top) - 32));
        scale_ = std::clamp(
            std::min(
                available_width / static_cast<float>(rendered.width),
                available_height / static_cast<float>(rendered.height)
            ),
            0.1F,
            allow_fit_upscale_ ? 8.0F : 1.0F
        );
        offset_x_ = canvas.left +
            ((static_cast<float>(canvas.right - canvas.left) -
                (rendered.width * scale_)) / 2.0F);
        offset_y_ = canvas.top +
            ((static_cast<float>(canvas.bottom - canvas.top) -
                (rendered.height * scale_)) / 2.0F);
    }
    const auto zoom_text = L"Масштаб: " + std::to_wstring(static_cast<int>(std::lround(scale_ * 100))) + L"%";
    wchar_t previous_zoom[64]{};
    ::GetWindowTextW(zoom_label_, previous_zoom, 64);
    if (zoom_text != previous_zoom) ::SetWindowTextW(zoom_label_, zoom_text.c_str());
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = rendered.width;
    bitmap_info.bmiHeader.biHeight = -rendered.height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    const int saved_canvas_dc = ::SaveDC(dc);
    ::IntersectClipRect(dc, canvas.left, canvas.top, canvas.right, canvas.bottom);
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
    ::IntersectClipRect(dc,
        static_cast<int>(std::lround(offset_x_)),
        static_cast<int>(std::lround(offset_y_)),
        static_cast<int>(std::lround(offset_x_)) +
            static_cast<int>(std::lround(rendered.width * scale_)),
        static_cast<int>(std::lround(offset_y_)) +
            static_cast<int>(std::lround(rendered.height * scale_)));
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
    if (saved_canvas_dc != 0) {
        ::RestoreDC(dc, saved_canvas_dc);
    }
    finish_paint();
    // Moving children can repaint them immediately. Finish the canvas blit first,
    // while its paint DC still clips the original child positions.
    LayoutCropActions();
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
    canvas.right -= platform::DipToPixels(kToolPanelWidthDip, dpi);
    ::InvalidateRect(window_, &canvas, FALSE);
}

platform::PixelRect EditorWindow::VisibleDocumentBounds() const noexcept {
    return {0, 0, session_.model.source().width, session_.model.source().height};
}

void EditorWindow::ShowCropActions(const bool visible) const {
    ::ShowWindow(apply_crop_button_, visible ? SW_SHOW : SW_HIDE);
    ::ShowWindow(cancel_crop_button_, visible ? SW_SHOW : SW_HIDE);
    if (visible) {
        const HWND buttons[]{apply_crop_button_, cancel_crop_button_};
        for (const HWND button : buttons) {
            ::SetWindowPos(button, HWND_TOP, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ::RedrawWindow(button, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
    }
    ::EnableWindow(copy_button_, TRUE);
    ::EnableWindow(save_button_, TRUE);
}

void EditorWindow::LayoutCropActions() const {
    if (!crop_selection_ready_ || window_ == nullptr) {
        return;
    }

    RECT client{};
    ::GetClientRect(window_, &client);
    const UINT dpi = ::GetDpiForWindow(window_);
    const int panel = platform::DipToPixels(kToolPanelWidthDip, dpi);
    const int button_width = platform::DipToPixels(kCropActionWidthDip, dpi);
    const int button_height = platform::DipToPixels(kCropActionHeightDip, dpi);
    const int button_gap = platform::DipToPixels(kCropActionGapDip, dpi);
    const int corner_gap = platform::DipToPixels(kCropActionOffsetDip, dpi);
    const auto crop = session_.model.state().crop;
    const platform::PixelRect crop_client{
        static_cast<std::int32_t>(std::lround(offset_x_ + (crop.left * scale_))),
        static_cast<std::int32_t>(std::lround(offset_y_ + (crop.top * scale_))),
        static_cast<std::int32_t>(std::lround(offset_x_ + (crop.right * scale_))),
        static_cast<std::int32_t>(std::lround(offset_y_ + (crop.bottom * scale_))),
    };
    const auto actions = platform::PlaceCropActions(
        crop_client,
        crop_release_on_right_,
        crop_release_on_bottom_,
        platform::PixelSize{button_width * 2 + button_gap, button_height},
        platform::PixelRect{0, 0, client.right - panel, client.bottom},
        corner_gap
    );
    if (actions.empty()) {
        return;
    }
    const auto place_button = [&](const HWND button, const int left) {
        RECT previous{};
        ::GetWindowRect(button, &previous);
        ::MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&previous), 2);
        const RECT next{left, actions.top, left + button_width, actions.top + button_height};
        if (::EqualRect(&previous, &next)) {
            return false;
        }
        const bool resized = previous.right - previous.left != button_width ||
            previous.bottom - previous.top != button_height;
        // Do not reuse old pixels: these controls move over a changing canvas.
        ::SetWindowPos(button, nullptr, left, actions.top, button_width, button_height,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        if (resized) {
            SetRoundedButtonRegion(button, button_width, button_height, dpi);
        }
        return true;
    };
    const bool apply_moved = place_button(apply_crop_button_, actions.left);
    const bool cancel_moved = place_button(cancel_crop_button_, actions.left + button_width + button_gap);
    if (apply_moved || cancel_moved) {
        // Both positions must be final before repainting either sibling.
        for (const HWND button : {apply_crop_button_, cancel_crop_button_}) {
            ::RedrawWindow(button, nullptr, nullptr,
                RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        }
    }
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
    crop_selection_ready_ = false;
    ShowCropActions(false);
    InvalidateToolButtons();
    UpdatePropertyControls();
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
    const auto applied = session_.model.Snapshot();
    history_.Commit(crop_mode_before_, applied);
    crop_mode_before_ = applied;
    fit_ = true;
    crop_selection_ready_ = false;
    ::SetFocus(window_);
    ShowCropActions(false);
    InvalidateToolButtons();
    UpdatePropertyControls();
    InvalidateCanvas();
}

void EditorWindow::CancelCrop() {
    if (tool_ != Tool::crop) {
        return;
    }
    session_.model.Restore(crop_mode_before_);
    tool_ = Tool::select;
    fit_ = true;
    crop_selection_ready_ = false;
    ShowCropActions(false);
    InvalidateToolButtons();
    UpdatePropertyControls();
    InvalidateCanvas();
}

void EditorWindow::ClearCropSelection() {
    if (tool_ != Tool::crop || !crop_selection_ready_) {
        return;
    }
    const auto& source = session_.model.source();
    session_.model.SetCrop({0, 0, source.width, source.height});
    crop_selection_ready_ = false;
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
    LayoutCropActions();
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
    const auto default_folder = DefaultSaveFolder();
    if (!default_folder.empty()) {
        Microsoft::WRL::ComPtr<IShellItem> folder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(
                default_folder.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
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
    const std::filesystem::path saved_path(path);
    const bool saved = exporting::ImageFileService::Save(saved_path, rendered, format);
    ::CoTaskMemFree(path);
    if (saved) {
        session_.model.MarkClean();
        static_cast<void>(platform::RevealFileInExplorer(saved_path));
    } else {
        ::MessageBoxW(window_, L"Не удалось сохранить изображение.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
    }
}

bool EditorWindow::SaveToDefaultFolder() {
    const auto folder = DefaultSaveFolder();
    if (folder.empty()) {
        ::MessageBoxW(window_,
            L"Не удалось определить папку сохранения по умолчанию.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const auto path = error
        ? std::filesystem::path{}
        : exporting::NextAvailableScreenshotPath(folder, session_.screenshot_name);
    if (path.empty() || !exporting::ImageFileService::Save(
            path, DocumentRenderer::Render(session_.model),
            exporting::ImageFormat::png)) {
        ::MessageBoxW(window_,
            L"Не удалось сохранить изображение в папку по умолчанию.",
            L"Screenshooter", MB_OK | MB_ICONERROR);
        return false;
    }
    session_.model.MarkClean();
    static_cast<void>(platform::RevealFileInExplorer(path));
    return true;
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
    UpdatePropertyControls();
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
    const int panel = platform::DipToPixels(kToolPanelWidthDip, dpi);
    const int bottom = client.bottom;
    if (client_point.x < client.left || client_point.x >= client.right - panel ||
        client_point.y < client.top || client_point.y >= bottom) {
        return false;
    }
    return true;
}

void EditorWindow::BeginDocumentAction(const POINT client_point) {
    if (!IsCanvasPoint(client_point)) {
        return;
    }
    ::SetFocus(window_);
    last_client_point_ = client_point;
    last_document_point_ = DocumentPoint(client_point);
    action_anchor_ = last_document_point_;
    action_before_ = session_.model.Snapshot();
    if (tool_ != Tool::crop) {
        const bool had_selection = session_.model.state().selected.has_value();
        const bool inside_image = platform::Contains(
            VisibleDocumentBounds(), last_document_point_);
        if (had_selection && inside_image) {
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
        const auto hit = inside_image
            ? session_.model.HitTest(last_document_point_) : std::nullopt;
        if (hit.has_value()) {
            session_.model.Select(hit);
            active_annotation_ = *hit;
            dragging_ = true;
            ::SetCapture(window_);
            UpdatePropertyControls();
            InvalidateCanvas();
            return;
        }
        if (had_selection && inside_image) {
            session_.model.Select(std::nullopt);
            UpdatePropertyControls();
            InvalidateCanvas();
            return;
        }
        if (tool_ == Tool::select) {
            session_.model.Select(std::nullopt);
            UpdatePropertyControls();
            InvalidateCanvas();
            return;
        }
        session_.model.Select(std::nullopt);
    }
    if (tool_ == Tool::text) {
        BeginTextEdit(client_point);
        return;
    }
    if (tool_ == Tool::crop) {
        const auto& source = session_.model.source();
        session_.model.SetCrop({0, 0, source.width, source.height});
        crop_selection_ready_ = false;
        ShowCropActions(false);
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
        .color = current_color_,
    };
    if (kind == AnnotationKind::marker) {
        annotation.color = 0x66FFEB3B;
        annotation.thickness = 18;
        annotation.points.push_back(last_document_point_);
        marker_straight_drafting_ = false;
    }
    active_annotation_ = session_.model.Add(annotation);
    UpdatePropertyControls();
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
    const auto current = DocumentPoint(client_point);
    last_client_point_ = client_point;
    if (crop_drafting_) {
        const auto crop = platform::Intersect(
            platform::Normalize(action_anchor_, current), VisibleDocumentBounds());
        session_.model.SetCrop(crop.empty() ? VisibleDocumentBounds() : crop);
    } else if (resizing_) {
        if (Annotation* annotation = session_.model.Find(active_annotation_)) {
            static_cast<void>(ResizeAnnotationEndpoint(
                *annotation,
                resize_handle_,
                current,
                {},
                constrain_geometry
            ));
        }
    } else if (dragging_) {
        const platform::PixelPoint delta{
            current.x - last_document_point_.x,
            current.y - last_document_point_.y,
        };
        static_cast<void>(session_.model.Move(active_annotation_, delta));
    } else if (Annotation* annotation = session_.model.Find(active_annotation_)) {
        if (annotation->kind == AnnotationKind::marker) {
            marker_straight_drafting_ =
                marker_straight_drafting_ || constrain_geometry;
            UpdateMarkerStroke(
                *annotation, current, marker_straight_drafting_);
        } else {
            annotation->end = current;
        }
        if (constrain_geometry && annotation->kind != AnnotationKind::marker) {
            annotation->end = ConstrainShapeEnd(
                annotation->kind,
                annotation->start,
                annotation->end,
                {}
            );
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
            platform::Contains(VisibleDocumentBounds(), annotation->end) &&
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
    marker_straight_drafting_ = false;
    resizing_ = false;
    resize_handle_ = ResizeHandle::none;
    const bool completed_crop = crop_drafting_;
    if (completed_crop) {
        const auto crop = platform::Intersect(
            platform::Normalize(action_anchor_, last_document_point_),
            VisibleDocumentBounds());
        crop_release_on_right_ = last_document_point_.x >= action_anchor_.x;
        crop_release_on_bottom_ = last_document_point_.y >= action_anchor_.y;
        crop_selection_ready_ = !crop.empty();
    }
    crop_drafting_ = false;
    if (!completed_crop) {
        history_.Commit(action_before_, session_.model.Snapshot());
    }
    if (completed_crop && crop_selection_ready_) {
        LayoutCropActions();
        ShowCropActions(true);
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
    UpdatePropertyControls();
    InvalidateCanvas();
}

void EditorWindow::Undo() {
    if (history_.Undo(session_.model)) {
        UpdatePropertyControls();
        InvalidateCanvas();
    }
}

void EditorWindow::Redo() {
    if (history_.Redo(session_.model)) {
        UpdatePropertyControls();
        InvalidateCanvas();
    }
}

void EditorWindow::BeginTextEdit(
    const POINT client_point,
    const std::optional<AnnotationId> existing
) {
    CommitTextEdit();
    action_before_ = session_.model.Snapshot();
    editing_text_id_ = existing;
    const Annotation* annotation = existing.has_value()
        ? session_.model.Find(*existing)
        : nullptr;
    POINT edit_origin = client_point;
    int document_width = 240;
    int document_height = 90;
    const wchar_t* initial_text = L"";
    if (annotation != nullptr && annotation->kind == AnnotationKind::text) {
        session_.model.Select(annotation->id);
        current_color_ = annotation->color;
        current_font_size_ = annotation->font_size;
        current_font_bold_ = annotation->font_bold;
        current_font_italic_ = annotation->font_italic;
        current_font_underline_ = annotation->font_underline;
        const auto bounds = platform::Normalize(annotation->start, annotation->end);
        const auto visible = VisibleDocumentBounds();
        edit_origin = {
            static_cast<LONG>(std::lround(
                offset_x_ + ((bounds.left - visible.left) * scale_))),
            static_cast<LONG>(std::lround(
                offset_y_ + ((bounds.top - visible.top) * scale_))),
        };
        document_width = bounds.width();
        document_height = bounds.height();
        initial_text = annotation->text.c_str();
    } else {
        editing_text_id_.reset();
        text_origin_ = DocumentPoint(client_point);
    }
    const int width = static_cast<int>(std::lround(document_width * scale_));
    const int height = static_cast<int>(std::lround(document_height * scale_));
    RECT client{};
    ::GetClientRect(window_, &client);
    const auto text_clip = platform::Intersect(
        {static_cast<int>(std::lround(offset_x_)),
         static_cast<int>(std::lround(offset_y_)),
         static_cast<int>(std::lround(offset_x_ + session_.model.source().width * scale_)),
         static_cast<int>(std::lround(offset_y_ + session_.model.source().height * scale_))},
        {0, 0, client.right - platform::DipToPixels(
            kToolPanelWidthDip, ::GetDpiForWindow(window_)), client.bottom});
    if (platform::Intersect(text_clip,
            {edit_origin.x, edit_origin.y,
             edit_origin.x + std::max(width, existing.has_value() ? 80 : 160),
             edit_origin.y + std::max(height, existing.has_value() ? 40 : 60)}).empty()) {
        editing_text_id_.reset();
        return;
    }
    text_edit_ = ::CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        initial_text,
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
        edit_origin.x,
        edit_origin.y,
        std::max(width, existing.has_value() ? 80 : 160),
        std::max(height, existing.has_value() ? 40 : 60),
        window_,
        nullptr,
        instance_,
        nullptr
    );
    if (text_edit_ == nullptr) {
        editing_text_id_.reset();
        return;
    }
    HRGN text_region = ::CreateRectRgn(
        text_clip.left - edit_origin.x, text_clip.top - edit_origin.y,
        text_clip.right - edit_origin.x, text_clip.bottom - edit_origin.y);
    if (text_region != nullptr && ::SetWindowRgn(text_edit_, text_region, TRUE) == 0) {
        ::DeleteObject(text_region);
    }
    const int edit_font_size = std::max(
        8, static_cast<int>(std::lround(current_font_size_ * scale_)));
    text_edit_font_ = ::CreateFontW(-edit_font_size, 0, 0, 0,
        current_font_bold_ ? FW_BOLD : FW_NORMAL,
        current_font_italic_, current_font_underline_, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (text_edit_font_ != nullptr) {
        ::SendMessageW(text_edit_, WM_SETFONT,
            reinterpret_cast<WPARAM>(text_edit_font_), TRUE);
    }
    ::SetWindowSubclass(
        text_edit_,
        &EditorWindow::TextEditProcedure,
        1,
        reinterpret_cast<DWORD_PTR>(this)
    );
    ::SendMessageW(text_edit_, EM_SETSEL,
        static_cast<WPARAM>(::GetWindowTextLengthW(text_edit_)),
        static_cast<LPARAM>(::GetWindowTextLengthW(text_edit_)));
    ::SetFocus(text_edit_);
    UpdatePropertyControls();
    InvalidateCanvas();
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
    if (text_edit_font_ != nullptr) {
        ::DeleteObject(text_edit_font_);
        text_edit_font_ = nullptr;
    }
    const auto edited_id = editing_text_id_;
    editing_text_id_.reset();
    if (edited_id.has_value()) {
        Annotation* annotation = session_.model.Find(*edited_id);
        if (annotation != nullptr) {
            if (text.empty()) {
                static_cast<void>(session_.model.Remove(*edited_id));
            } else {
                annotation->text = std::move(text);
                annotation->color = current_color_;
                annotation->font_size = current_font_size_;
                annotation->font_bold = current_font_bold_;
                annotation->font_italic = current_font_italic_;
                annotation->font_underline = current_font_underline_;
            }
            history_.Commit(action_before_, session_.model.Snapshot());
        }
        UpdatePropertyControls();
        InvalidateCanvas();
        return;
    }
    if (text.empty()) {
        return;
    }
    Annotation annotation{
        .kind = AnnotationKind::text,
        .start = text_origin_,
        .end = {text_origin_.x + 240, text_origin_.y + 90},
        .color = current_color_,
        .font_size = current_font_size_,
        .font_bold = current_font_bold_,
        .font_italic = current_font_italic_,
        .font_underline = current_font_underline_,
        .text = std::move(text),
    };
    static_cast<void>(session_.model.Add(std::move(annotation)));
    history_.Commit(action_before_, session_.model.Snapshot());
    UpdatePropertyControls();
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
        UpdatePropertyControls();
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
    UpdatePropertyControls();
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

LRESULT CALLBACK EditorWindow::ToolButtonProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param,
    const UINT_PTR,
    const DWORD_PTR reference_data
) {
    auto* editor = reinterpret_cast<EditorWindow*>(reference_data);
    if (message == WM_KEYDOWN && w_param == VK_TAB) {
        const HWND next = ::GetNextDlgTabItem(editor->window_, window,
            (::GetKeyState(VK_SHIFT) & 0x8000) != 0);
        if (next != nullptr) ::SetFocus(next);
        return 0;
    }
    if (message == WM_KEYDOWN && w_param == VK_RETURN && window != editor->font_size_combo_) {
        ::SendMessageW(window, BM_CLICK, 0, 0);
        return 0;
    }
    const bool dismiss_editor = w_param == VK_ESCAPE &&
        (window != editor->font_size_combo_ ||
            ::SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0) == FALSE);
    if (message == WM_KEYDOWN && (dismiss_editor ||
            (::GetKeyState(VK_CONTROL) & 0x8000) != 0)) {
        return ::SendMessageW(editor->window_, message, w_param, l_param);
    }
    if (message == WM_SETFOCUS && ::GetParent(window) == editor->panel_) {
        RECT bounds{}, viewport{};
        ::GetWindowRect(window, &bounds);
        ::MapWindowPoints(nullptr, editor->panel_, reinterpret_cast<POINT*>(&bounds), 2);
        ::GetClientRect(editor->panel_, &viewport);
        if (bounds.top < 0) editor->ScrollPanel(editor->panel_scroll_ + bounds.top);
        else if (bounds.bottom > viewport.bottom)
            editor->ScrollPanel(editor->panel_scroll_ + bounds.bottom - viewport.bottom);
    }
    if (message == WM_MOUSEMOVE) {
        if (editor->hovered_button_ != window) {
            const HWND previous = editor->hovered_button_;
            editor->hovered_button_ = window;
            if (previous != nullptr) {
                ::InvalidateRect(previous, nullptr, FALSE);
            }
            ::InvalidateRect(window, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tracking{
            .cbSize = sizeof(TRACKMOUSEEVENT),
            .dwFlags = TME_LEAVE,
            .hwndTrack = window,
        };
        ::TrackMouseEvent(&tracking);
    } else if (message == WM_MOUSELEAVE) {
        if (editor->hovered_button_ == window) {
            editor->hovered_button_ = nullptr;
            ::InvalidateRect(window, nullptr, FALSE);
        }
    } else if (message == WM_SETCURSOR && window != editor->font_size_combo_) {
        ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
    } else if (message == WM_NCDESTROY && editor->hovered_button_ == window) {
        editor->hovered_button_ = nullptr;
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
    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
        if (item != nullptr && item->CtlID == kFontSizeCombo) {
            item->itemHeight = platform::DipToPixels(26, ::GetDpiForWindow(window_));
            return TRUE;
        }
        return ::DefWindowProcW(window_, message, w_param, l_param);
    }
    case WM_DRAWITEM:
        if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
            item != nullptr && item->CtlID == kFontSizeCombo) {
            DrawFontSizeCombo(*item);
            return TRUE;
        }
        if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
            item != nullptr && ToolForControlId(static_cast<int>(item->CtlID)).has_value()) {
            DrawToolButton(*item);
            return TRUE;
        }
        if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
            item != nullptr && IsActionControlId(static_cast<int>(item->CtlID))) {
            DrawActionButton(*item);
            return TRUE;
        }
        if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
            item != nullptr && IsColorControlId(static_cast<int>(item->CtlID))) {
            DrawColorButton(*item);
            return TRUE;
        }
        if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
            item != nullptr && IsTextStyleControlId(static_cast<int>(item->CtlID))) {
            DrawTextStyleButton(*item);
            return TRUE;
        }
        return ::DefWindowProcW(window_, message, w_param, l_param);
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(l_param) == text_edit_) {
            const HDC dc = reinterpret_cast<HDC>(w_param);
            ::SetTextColor(dc, RGB(
                (current_color_ >> 16U) & 0xFFU,
                (current_color_ >> 8U) & 0xFFU,
                current_color_ & 0xFFU));
            ::SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(::GetStockObject(WHITE_BRUSH));
        }
        return ::DefWindowProcW(window_, message, w_param, l_param);
    case WM_COMMAND: {
        const auto choose_tool = [this](const Tool next) {
            if (tool_ == Tool::crop && next != Tool::crop) {
                CancelCrop();
            }
            tool_ = next;
            InvalidateToolButtons();
            UpdatePropertyControls();
        };
        if (reinterpret_cast<HWND>(l_param) == text_edit_ &&
            HIWORD(w_param) == EN_KILLFOCUS) {
            ::PostMessageW(window_, kCommitTextEdit, 0, 0);
        } else if (LOWORD(w_param) == kCopyButton) {
            CopyAndClose();
        } else if (LOWORD(w_param) == kFitButton) {
            fit_ = true;
            allow_fit_upscale_ = true;
            InvalidateCanvas();
        } else if (LOWORD(w_param) == kActualSizeButton) {
            RECT client{};
            ::GetClientRect(window_, &client);
            const POINT center{(client.right - platform::DipToPixels(kToolPanelWidthDip,
                ::GetDpiForWindow(window_))) / 2, client.bottom / 2};
            ZoomAt(center, 1.0F / scale_);
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
        } else if (LOWORD(w_param) == kInfoButton) {
            ::SendMessageW(host_, app::messages::kShowInfo,
                reinterpret_cast<WPARAM>(window_), 0);
        } else if (LOWORD(w_param) == kSettingsButton) {
            ::SendMessageW(host_, app::messages::kShowSettings,
                reinterpret_cast<WPARAM>(window_), 0);
        } else if (LOWORD(w_param) == kRedButton) {
            ChooseColor(ColorForControlId(kRedButton));
        } else if (LOWORD(w_param) == kGreenButton) {
            ChooseColor(ColorForControlId(kGreenButton));
        } else if (LOWORD(w_param) == kBlueButton) {
            ChooseColor(ColorForControlId(kBlueButton));
        } else if (LOWORD(w_param) == kBlackButton) {
            ChooseColor(ColorForControlId(kBlackButton));
        } else if (LOWORD(w_param) == kFontSizeCombo && HIWORD(w_param) == CBN_SELCHANGE) {
            ChooseFontSize();
        } else if (IsTextStyleControlId(LOWORD(w_param))) {
            ToggleTextStyle(LOWORD(w_param));
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
        UpdateUiFont();
        const auto* rect = reinterpret_cast<const RECT*>(l_param);
        ::SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
        LayoutControls();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ::ScreenToClient(window_, &point);
        RECT client{};
        ::GetClientRect(window_, &client);
        if (point.x >= client.right - platform::DipToPixels(kToolPanelWidthDip,
                ::GetDpiForWindow(window_))) {
            return ::SendMessageW(panel_, message, w_param, l_param);
        }
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
    case WM_LBUTTONDBLCLK: {
        const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        if (tool_ != Tool::crop && IsCanvasPoint(point) &&
            platform::Contains(VisibleDocumentBounds(), DocumentPoint(point))) {
            const auto hit = session_.model.HitTest(DocumentPoint(point));
            const Annotation* annotation = hit.has_value()
                ? session_.model.Find(*hit)
                : nullptr;
            if (annotation != nullptr && annotation->kind == AnnotationKind::text) {
                BeginTextEdit(point, annotation->id);
            }
        }
        return 0;
    }
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
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(l_param);
        const UINT dpi = ::GetDpiForWindow(window_);
        limits->ptMinTrackSize = {platform::DipToPixels(480, dpi), platform::DipToPixels(320, dpi)};
        return 0;
    }
    case WM_KEYDOWN:
        if (w_param == VK_TAB) {
            const HWND next = ::GetNextDlgTabItem(window_, nullptr,
                (::GetKeyState(VK_SHIFT) & 0x8000) != 0);
            if (next != nullptr) ::SetFocus(next);
            return 0;
        }
        if (w_param == VK_SHIFT && resizing_) {
            UpdateDocumentAction(last_client_point_, true);
            return 0;
        }
        if (w_param == VK_ESCAPE) {
            if (tool_ == Tool::crop && crop_selection_ready_) {
                ClearCropSelection();
            } else if (session_.model.state().selected.has_value()) {
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
                allow_fit_upscale_ = true;
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
    case WM_KEYUP:
        if (w_param == VK_SHIFT && resizing_) {
            UpdateDocumentAction(last_client_point_, false);
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
        if (!closing_after_export_ && session_.model.dirty()) {
            const int choice = ::MessageBoxW(
                window_,
                L"Сохранить изменения перед закрытием?",
                L"Screenshooter",
                MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1
            );
            if (choice == IDYES && !SaveToDefaultFolder()) {
                return 0;
            }
            if (choice != IDYES && choice != IDNO) {
                return 0;
            }
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
