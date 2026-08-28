#include "settings/settings_dialog.h"

#include <commctrl.h>

#include <array>

namespace screenshooter::settings {
namespace {

constexpr wchar_t kWindowClass[] = L"Screenshooter.HotkeySettings";
constexpr int kEnabledControl = 3001;
constexpr int kAreaControl = 3002;
constexpr int kMonitorControl = 3003;
constexpr int kWindowControl = 3004;
constexpr int kSaveControl = 3005;
constexpr int kCancelControl = 3006;

struct DialogState {
    Settings settings;
    std::optional<Settings> result;
};

WORD ToControlHotkey(const HotkeyBinding binding) {
    BYTE flags = 0;
    if ((binding.modifiers & MOD_SHIFT) != 0) flags |= HOTKEYF_SHIFT;
    if ((binding.modifiers & MOD_CONTROL) != 0) flags |= HOTKEYF_CONTROL;
    if ((binding.modifiers & MOD_ALT) != 0) flags |= HOTKEYF_ALT;
    return MAKEWORD(static_cast<BYTE>(binding.virtual_key), flags);
}

HotkeyBinding FromControlHotkey(const LRESULT value) {
    const WORD packed = LOWORD(value);
    const BYTE flags = HIBYTE(packed);
    UINT modifiers = 0;
    if ((flags & HOTKEYF_SHIFT) != 0) modifiers |= MOD_SHIFT;
    if ((flags & HOTKEYF_CONTROL) != 0) modifiers |= MOD_CONTROL;
    if ((flags & HOTKEYF_ALT) != 0) modifiers |= MOD_ALT;
    return {modifiers, LOBYTE(packed)};
}

void SetFont(const HWND control) {
    if (control != nullptr) {
        ::SendMessageW(control, WM_SETFONT,
            reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
}

HWND AddControl(
    const HWND parent,
    const wchar_t* class_name,
    const wchar_t* text,
    const DWORD style,
    const int x,
    const int y,
    const int width,
    const int height,
    const int id
) {
    HWND control = ::CreateWindowExW(0, class_name, text,
        WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetFont(control);
    return control;
}

LRESULT CALLBACK DialogProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    auto* state = reinterpret_cast<DialogState*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<DialogState*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_CREATE && state != nullptr) {
        AddControl(window, L"STATIC", L"Снимок области:", 0, 20, 22, 155, 22, 0);
        AddControl(window, HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER,
            180, 18, 190, 25, kAreaControl);
        AddControl(window, L"STATIC", L"Монитор под курсором:", 0, 20, 62, 155, 22, 0);
        AddControl(window, HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER,
            180, 58, 190, 25, kMonitorControl);
        AddControl(window, L"STATIC", L"Окно под курсором:", 0, 20, 102, 155, 22, 0);
        AddControl(window, HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER,
            180, 98, 190, 25, kWindowControl);
        AddControl(window, L"STATIC", L"Щёлкните поле и нажмите новое сочетание клавиш.",
            0, 20, 136, 350, 22, 0);
        HWND enabled = AddControl(window, L"BUTTON", L"Включить глобальные горячие клавиши",
            BS_AUTOCHECKBOX | WS_TABSTOP, 20, 164, 350, 24, kEnabledControl);
        AddControl(window, L"BUTTON", L"Сохранить", BS_DEFPUSHBUTTON | WS_TABSTOP,
            194, 204, 84, 28, kSaveControl);
        AddControl(window, L"BUTTON", L"Отмена", WS_TABSTOP,
            286, 204, 84, 28, kCancelControl);
        ::SendDlgItemMessageW(window, kAreaControl, HKM_SETHOTKEY,
            ToControlHotkey(state->settings.area_hotkey), 0);
        ::SendDlgItemMessageW(window, kMonitorControl, HKM_SETHOTKEY,
            ToControlHotkey(state->settings.monitor_hotkey), 0);
        ::SendDlgItemMessageW(window, kWindowControl, HKM_SETHOTKEY,
            ToControlHotkey(state->settings.window_hotkey), 0);
        ::SendMessageW(enabled, BM_SETCHECK,
            state->settings.hotkeys_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        return 0;
    }
    if (message == WM_COMMAND && state != nullptr) {
        const int command = LOWORD(w_param);
        if (command == kCancelControl) {
            ::DestroyWindow(window);
            return 0;
        }
        if (command == kSaveControl) {
            Settings proposed = state->settings;
            proposed.hotkeys_enabled = ::IsDlgButtonChecked(window, kEnabledControl) == BST_CHECKED;
            proposed.area_hotkey = FromControlHotkey(
                ::SendDlgItemMessageW(window, kAreaControl, HKM_GETHOTKEY, 0, 0));
            proposed.monitor_hotkey = FromControlHotkey(
                ::SendDlgItemMessageW(window, kMonitorControl, HKM_GETHOTKEY, 0, 0));
            proposed.window_hotkey = FromControlHotkey(
                ::SendDlgItemMessageW(window, kWindowControl, HKM_GETHOTKEY, 0, 0));
            const std::array bindings{
                proposed.area_hotkey, proposed.monitor_hotkey, proposed.window_hotkey};
            if (proposed.hotkeys_enabled &&
                (bindings[0].virtual_key == 0 || bindings[1].virtual_key == 0 ||
                 bindings[2].virtual_key == 0)) {
                ::MessageBoxW(window, L"Задайте все три сочетания.",
                    L"Screenshooter", MB_OK | MB_ICONWARNING);
                return 0;
            }
            if (proposed.hotkeys_enabled &&
                (bindings[0] == bindings[1] || bindings[0] == bindings[2] ||
                 bindings[1] == bindings[2])) {
                ::MessageBoxW(window, L"Сочетания не должны повторяться.",
                    L"Screenshooter", MB_OK | MB_ICONWARNING);
                return 0;
            }
            state->result = proposed;
            ::DestroyWindow(window);
            return 0;
        }
    }
    if (message == WM_CLOSE) {
        ::DestroyWindow(window);
        return 0;
    }
    return ::DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace

std::optional<Settings> ShowHotkeySettingsDialog(
    const HINSTANCE instance,
    const HWND owner,
    const Settings& current
) {
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = DialogProcedure,
        .hInstance = instance,
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kWindowClass,
    };
    if (::RegisterClassExW(&window_class) == 0 &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return std::nullopt;
    }

    DialogState state{.settings = current};
    RECT bounds{0, 0, 400, 274};
    ::AdjustWindowRectEx(&bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const int x = (::GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (::GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    HWND window = ::CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass,
        L"Горячие клавиши Screenshooter", WS_CAPTION | WS_SYSMENU,
        x, y, width, height, owner, nullptr, instance, &state);
    if (window == nullptr) {
        return std::nullopt;
    }
    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);

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
    return state.result;
}

}  // namespace screenshooter::settings
