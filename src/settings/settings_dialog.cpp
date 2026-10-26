#include "localization/strings.h"
#include "settings/settings_dialog.h"

#include <commctrl.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

#include <filesystem>
#include <iterator>

namespace screenshooter::settings {
namespace {

constexpr wchar_t kWindowClass[] = L"Screenshooter.Settings";
constexpr int kEnabledControl = 3001;
constexpr int kAreaControl = 3002;
constexpr int kMonitorControl = 3003;
constexpr int kWindowControl = 3004;
constexpr int kFolderControl = 3005;
constexpr int kBrowseControl = 3006;
constexpr int kAutostartControl = 3007;
constexpr int kSaveControl = 3008;
constexpr int kCancelControl = 3009;
constexpr int kLanguageControl = 3010;
constexpr int kHotkeysLabel = 3020;
constexpr int kAreaLabel = 3021;
constexpr int kMonitorLabel = 3022;
constexpr int kWindowLabel = 3023;
constexpr int kFolderLabel = 3024;
constexpr int kLanguageLabel = 3025;

struct DialogState {
    Settings settings;
    std::optional<Settings> result;
    HFONT font{};
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
            ::SendMessageW(::GetParent(control), WM_GETFONT, 0, 0), TRUE);
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
    const UINT dpi = ::GetDpiForWindow(parent);
    const auto scaled = [dpi](const int value) { return ::MulDiv(value, dpi, 96); };
    HWND control = ::CreateWindowExW(0, class_name, text,
        WS_CHILD | WS_VISIBLE | style,
        scaled(x), scaled(y), scaled(width), scaled(height), parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(::GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
    SetFont(control);
    return control;
}

void UpdateHotkeyEnabledState(const HWND window) {
    const bool enabled = ::IsDlgButtonChecked(window, kEnabledControl) == BST_CHECKED;
    ::EnableWindow(::GetDlgItem(window, kAreaControl), enabled);
    ::EnableWindow(::GetDlgItem(window, kMonitorControl), enabled);
    ::EnableWindow(::GetDlgItem(window, kWindowControl), enabled);
}

void ChooseFolder(const HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)))) {
        return;
    }
    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    wchar_t current[32768]{};
    ::GetDlgItemTextW(owner, kFolderControl, current, static_cast<int>(std::size(current)));
    if (current[0] != L'\0') {
        Microsoft::WRL::ComPtr<IShellItem> folder;
        if (SUCCEEDED(::SHCreateItemFromParsingName(
                current, nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder.Get());
        }
    }
    if (dialog->Show(owner) != S_OK) {
        return;
    }
    Microsoft::WRL::ComPtr<IShellItem> result;
    PWSTR path = nullptr;
    if (SUCCEEDED(dialog->GetResult(&result)) &&
        SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
        ::SetDlgItemTextW(owner, kFolderControl, path);
        ::CoTaskMemFree(path);
    }
}

void PositionWindow(const HWND window, const HWND owner) {
    RECT window_rect{};
    ::GetWindowRect(window, &window_rect);
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
    const int width = window_rect.right - window_rect.left;
    const int height = window_rect.bottom - window_rect.top;
    ::SetWindowPos(window, nullptr,
        anchor.left + ((anchor.right - anchor.left - width) / 2),
        anchor.top + ((anchor.bottom - anchor.top - height) / 2),
        0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
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
    if (message == WM_GETFONT && state != nullptr) return reinterpret_cast<LRESULT>(state->font);
    if (message == WM_DESTROY && state != nullptr) {
        if (state->font != nullptr) ::DeleteObject(state->font);
        state->font = nullptr;
    }
    if (message == WM_CREATE && state != nullptr) {
        state->font = ::CreateFontW(-::MulDiv(10, ::GetDpiForWindow(window), 72),
            0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI");
        AddControl(window, L"STATIC", i18n::Text(i18n::Id::hotkeys), SS_LEFT, 20, 16, 520, 22, kHotkeysLabel);
        HWND enabled = AddControl(window, L"BUTTON", i18n::Text(i18n::Id::enable_hotkeys),
            BS_AUTOCHECKBOX | WS_TABSTOP, 20, 44, 520, 24, kEnabledControl);
        AddControl(window, L"STATIC", i18n::Text(i18n::Id::area_label), 0, 36, 80, 200, 22, kAreaLabel);
        AddControl(window, HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER,
            250, 76, 290, 25, kAreaControl);
        AddControl(window, L"STATIC", i18n::Text(i18n::Id::monitor_label), 0, 36, 116, 200, 22, kMonitorLabel);
        AddControl(window, HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER,
            250, 112, 290, 25, kMonitorControl);
        AddControl(window, L"STATIC", i18n::Text(i18n::Id::window_label), 0, 36, 152, 200, 22, kWindowLabel);
        AddControl(window, HOTKEY_CLASSW, L"", WS_TABSTOP | WS_BORDER,
            250, 148, 290, 25, kWindowControl);
        AddControl(window, L"STATIC", i18n::Text(i18n::Id::save_folder), SS_LEFT, 20, 194, 520, 22, kFolderLabel);
        AddControl(window, L"EDIT", state->settings.save_folder.c_str(),
            WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
            20, 222, 410, 25, kFolderControl);
        AddControl(window, L"BUTTON", i18n::Text(i18n::Id::browse), WS_TABSTOP,
            438, 220, 102, 29, kBrowseControl);
        HWND autostart = AddControl(window, L"BUTTON", i18n::Text(i18n::Id::autostart),
            BS_AUTOCHECKBOX | WS_TABSTOP, 20, 270, 520, 24, kAutostartControl);
        AddControl(window, L"STATIC", i18n::Text(i18n::Id::language),
            SS_LEFT, 20, 322, 218, 24, kLanguageLabel);
        HWND language = AddControl(window, WC_COMBOBOXW, L"",
            CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 250, 318, 290, 160, kLanguageControl);
        ::SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"English"));
        ::SendMessageW(language, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Русский"));
        ::SendMessageW(language, CB_SETCURSEL,
            state->settings.language == i18n::Language::russian ? 1 : 0, 0);

        AddControl(window, L"BUTTON", i18n::Text(i18n::Id::save), BS_DEFPUSHBUTTON | WS_TABSTOP,
            304, 382, 114, 32, kSaveControl);
        AddControl(window, L"BUTTON", i18n::Text(i18n::Id::cancel), WS_TABSTOP,
            426, 382, 114, 32, kCancelControl);

        ::SendDlgItemMessageW(window, kAreaControl, HKM_SETHOTKEY,
            ToControlHotkey(state->settings.area_hotkey), 0);
        ::SendDlgItemMessageW(window, kMonitorControl, HKM_SETHOTKEY,
            ToControlHotkey(state->settings.monitor_hotkey), 0);
        ::SendDlgItemMessageW(window, kWindowControl, HKM_SETHOTKEY,
            ToControlHotkey(state->settings.window_hotkey), 0);
        ::SendMessageW(enabled, BM_SETCHECK,
            state->settings.hotkeys_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        ::SendMessageW(autostart, BM_SETCHECK,
            state->settings.autostart_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        UpdateHotkeyEnabledState(window);
        return 0;
    }
    if (message == i18n::kLanguageChanged) {
        ::SetWindowTextW(window, i18n::Text(i18n::Id::settings_title));
        ::SetDlgItemTextW(window, kHotkeysLabel, i18n::Text(i18n::Id::hotkeys));
        ::SetDlgItemTextW(window, kAreaLabel, i18n::Text(i18n::Id::area_label));
        ::SetDlgItemTextW(window, kMonitorLabel, i18n::Text(i18n::Id::monitor_label));
        ::SetDlgItemTextW(window, kWindowLabel, i18n::Text(i18n::Id::window_label));
        ::SetDlgItemTextW(window, kFolderLabel, i18n::Text(i18n::Id::save_folder));
        ::SetDlgItemTextW(window, kLanguageLabel, i18n::Text(i18n::Id::language));
        ::SetDlgItemTextW(window, kEnabledControl, i18n::Text(i18n::Id::enable_hotkeys));
        ::SetDlgItemTextW(window, kAutostartControl, i18n::Text(i18n::Id::autostart));
        ::SetDlgItemTextW(window, kBrowseControl, i18n::Text(i18n::Id::browse));
        ::SetDlgItemTextW(window, kSaveControl, i18n::Text(i18n::Id::save));
        ::SetDlgItemTextW(window, kCancelControl, i18n::Text(i18n::Id::cancel));
        return 0;
    }
    if (message == WM_COMMAND && state != nullptr) {
        if (LOWORD(w_param) == kLanguageControl && HIWORD(w_param) == CBN_SELCHANGE) {
            state->settings.language = ::SendDlgItemMessageW(window, kLanguageControl,
                CB_GETCURSEL, 0, 0) == 1 ? i18n::Language::russian : i18n::Language::english;
            i18n::SetLanguage(state->settings.language);
            return 0;
        }
        const int command = LOWORD(w_param);
        if (command == kCancelControl || command == IDCANCEL) {
            ::DestroyWindow(window);
            return 0;
        }
        if (command == kEnabledControl) {
            UpdateHotkeyEnabledState(window);
            return 0;
        }
        if (command == kBrowseControl) {
            ChooseFolder(window);
            return 0;
        }
        if (command == kSaveControl) {
            Settings proposed = state->settings;
            proposed.hotkeys_enabled =
                ::IsDlgButtonChecked(window, kEnabledControl) == BST_CHECKED;
            proposed.autostart_enabled =
                ::IsDlgButtonChecked(window, kAutostartControl) == BST_CHECKED;
            proposed.area_hotkey = FromControlHotkey(
                ::SendDlgItemMessageW(window, kAreaControl, HKM_GETHOTKEY, 0, 0));
            proposed.monitor_hotkey = FromControlHotkey(
                ::SendDlgItemMessageW(window, kMonitorControl, HKM_GETHOTKEY, 0, 0));
            proposed.window_hotkey = FromControlHotkey(
                ::SendDlgItemMessageW(window, kWindowControl, HKM_GETHOTKEY, 0, 0));
            const int length = ::GetWindowTextLengthW(::GetDlgItem(window, kFolderControl));
            std::wstring folder(static_cast<std::size_t>(length) + 1, L'\0');
            ::GetDlgItemTextW(window, kFolderControl, folder.data(), length + 1);
            folder.resize(static_cast<std::size_t>(length));
            proposed.save_folder = std::filesystem::path(std::move(folder));

            const auto validation = ValidateSettings(proposed);
            if (validation != SettingsValidationError::none) {
                ::MessageBoxW(window,
                    validation == SettingsValidationError::missing_hotkey
                        ? i18n::Text(i18n::Id::missing_hotkeys)
                        : i18n::Text(i18n::Id::duplicate_hotkeys),
                    L"Screenshooter", MB_OK | MB_ICONWARNING);
                return 0;
            }
            state->result = std::move(proposed);
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

std::optional<Settings> ShowSettingsDialog(
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
    const UINT dpi = owner != nullptr ? ::GetDpiForWindow(owner) : 96;
    RECT bounds{0, 0, ::MulDiv(560, dpi, 96), ::MulDiv(438, dpi, 96)};
    ::AdjustWindowRectExForDpi(
        &bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    HWND window = ::CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass,
        i18n::Text(i18n::Id::settings_title), WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        owner, nullptr, instance, &state);
    if (window == nullptr) {
        return std::nullopt;
    }
    ::SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE);
    PositionWindow(window, owner);
    const bool disable_owner = owner != nullptr && ::IsWindowEnabled(owner) != FALSE;
    if (disable_owner) {
        ::EnableWindow(owner, FALSE);
    }
    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);
    ::SetFocus(::GetDlgItem(window, kEnabledControl));

    MSG message{};
    while (::IsWindow(window)) {
        const BOOL received = ::GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) {
            if (received == 0) {
                ::PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        if (message.message == WM_HOTKEY) {
            const HWND focused = ::GetFocus();
            if (focused != nullptr && ::GetParent(focused) == window &&
                (focused == ::GetDlgItem(window, kAreaControl) ||
                 focused == ::GetDlgItem(window, kMonitorControl) ||
                 focused == ::GetDlgItem(window, kWindowControl))) {
                // Registered combinations arrive as WM_HOTKEY instead of normal
                // key input. Record them here without dispatching a capture command.
                const HotkeyBinding binding{
                    LOWORD(message.lParam), HIWORD(message.lParam)};
                ::SendMessageW(focused, HKM_SETHOTKEY, ToControlHotkey(binding), 0);
                continue;
            }
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
    i18n::SetLanguage(current.language);
    return state.result;
}

}  // namespace screenshooter::settings
