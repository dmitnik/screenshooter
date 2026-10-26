#include "localization/strings.h"
#include "app/info_dialog.h"

#include <commctrl.h>
#include <shellapi.h>

namespace screenshooter::app {
namespace {

constexpr wchar_t kWindowClass[] = L"Screenshooter.Information";
constexpr int kCloseControl = 4001;
constexpr int kRepositoryControl = 4003;
constexpr wchar_t kRepositoryUrl[] = L"https://github.com/dmitnik/screenshooter";
constexpr wchar_t kRepositoryLink[] =
    L"<a href=\"https://github.com/dmitnik/screenshooter\">github.com/dmitnik/screenshooter</a>";

LRESULT CALLBACK InfoProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    if (message == i18n::kLanguageChanged) {
        ::SetWindowTextW(window, i18n::Text(i18n::Id::info_title));
        ::SetDlgItemTextW(window, 4002, i18n::Text(i18n::Id::instructions));
        ::SetDlgItemTextW(window, kCloseControl, i18n::Text(i18n::Id::close));
        return 0;
    }
    if (message == WM_CREATE) {
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            ::GetWindowLongPtrW(window, GWLP_HINSTANCE));
        const UINT dpi = ::GetDpiForWindow(window);
        const auto scaled = [dpi](const int value) { return ::MulDiv(value, dpi, 96); };
        HWND text = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", i18n::Text(i18n::Id::instructions),
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
                ES_AUTOVSCROLL | ES_READONLY,
            scaled(16), scaled(16), scaled(528), scaled(384),
            window, reinterpret_cast<HMENU>(4002), instance, nullptr);
        HWND repository = ::CreateWindowExW(0, WC_LINK, kRepositoryLink,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            scaled(16), scaled(414), scaled(528), scaled(24), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRepositoryControl)), instance, nullptr);
        HWND close = ::CreateWindowExW(0, L"BUTTON", i18n::Text(i18n::Id::close),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            scaled(444), scaled(450), scaled(100), scaled(30), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseControl)), instance, nullptr);
        const WPARAM font = reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT));
        ::SendMessageW(text, WM_SETFONT, font, TRUE);
        ::SendMessageW(close, WM_SETFONT, font, TRUE);
        ::SendMessageW(repository, WM_SETFONT, font, TRUE);
        return 0;
    }
    if (message == WM_NOTIFY && l_param != 0) {
        const auto* notification = reinterpret_cast<const NMHDR*>(l_param);
        if (notification->idFrom == kRepositoryControl &&
            (notification->code == NM_CLICK || notification->code == NM_RETURN)) {
            const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(
                window, L"open", kRepositoryUrl, nullptr, nullptr, SW_SHOWNORMAL));
            if (result <= 32) {
                ::MessageBoxW(window, i18n::Text(i18n::Id::repository_open_error),
                    L"Screenshooter", MB_OK | MB_ICONERROR);
            }
            return 0;
        }
    }
    if (message == WM_COMMAND &&
        (LOWORD(w_param) == kCloseControl || LOWORD(w_param) == IDCANCEL)) {
        ::DestroyWindow(window);
        return 0;
    }
    if (message == WM_CLOSE) {
        ::DestroyWindow(window);
        return 0;
    }
    return ::DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace

void ShowInfoDialog(const HINSTANCE instance, const HWND owner) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LINK_CLASS};
    if (!::InitCommonControlsEx(&controls)) return;
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = InfoProcedure,
        .hInstance = instance,
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kWindowClass,
    };
    if (::RegisterClassExW(&window_class) == 0 &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return;
    }
    const UINT dpi = owner != nullptr ? ::GetDpiForWindow(owner) : 96;
    RECT bounds{0, 0, ::MulDiv(560, dpi, 96), ::MulDiv(496, dpi, 96)};
    ::AdjustWindowRectExForDpi(
        &bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    RECT anchor{};
    if (owner == nullptr || !::IsWindowVisible(owner) ||
        !::GetWindowRect(owner, &anchor) || ::IsRectEmpty(&anchor)) {
        POINT cursor{};
        ::GetCursorPos(&cursor);
        MONITORINFO info{.cbSize = sizeof(MONITORINFO)};
        const HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        anchor = monitor != nullptr && ::GetMonitorInfoW(monitor, &info)
            ? info.rcWork
            : RECT{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    }
    HWND window = ::CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass,
        i18n::Text(i18n::Id::info_title), WS_CAPTION | WS_SYSMENU,
        anchor.left + ((anchor.right - anchor.left - width) / 2),
        anchor.top + ((anchor.bottom - anchor.top - height) / 2),
        width, height, owner, nullptr, instance, nullptr);
    if (window == nullptr) {
        return;
    }
    ::SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE);
    const bool disable_owner = owner != nullptr && ::IsWindowEnabled(owner) != FALSE;
    if (disable_owner) {
        ::EnableWindow(owner, FALSE);
    }
    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);
    ::SetFocus(::GetDlgItem(window, kRepositoryControl));
    MSG message{};
    while (::IsWindow(window)) {
        const BOOL received = ::GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) {
            if (received == 0) {
                ::PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        if (!::IsDialogMessageW(window, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
    if (disable_owner && ::IsWindow(owner)) {
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
    }
}

}  // namespace screenshooter::app
