#include "tray/tray_host.h"

#include <strsafe.h>

namespace screenshooter::tray {
namespace {

constexpr UINT kCaptureCommand = 2001;
constexpr UINT kExitCommand = 2002;
constexpr UINT kOpenCommand = 2003;
constexpr UINT kOpenFolderCommand = 2005;
constexpr UINT kWindowCommand = 2007;
constexpr UINT kMonitorCommand = 2008;
constexpr UINT kAllCommand = 2009;
constexpr UINT kSettingsCommand = 2010;

}  // namespace

TrayHost::~TrayHost() {
    Shutdown();
}

bool TrayHost::Initialize(const HWND owner, const UINT callback_message) {
    owner_ = owner;
    data_ = {};
    data_.cbSize = sizeof(NOTIFYICONDATAW);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_SHOWTIP | NIF_TIP;
    data_.uCallbackMessage = callback_message;
    data_.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    ::StringCchCopyW(data_.szTip, ARRAYSIZE(data_.szTip), L"Screenshooter");

    added_ = ::Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        ::Shell_NotifyIconW(NIM_SETVERSION, &data_);
    }
    return added_;
}

void TrayHost::Restore() {
    if (owner_ == nullptr) {
        return;
    }
    added_ = false;
    static_cast<void>(Initialize(owner_, data_.uCallbackMessage));
}

void TrayHost::Shutdown() {
    if (added_) {
        ::Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
}

TrayCommand TrayHost::HandleCallback(const LPARAM event_data) const {
    const UINT event = LOWORD(event_data);
    if (event == NIN_SELECT || event == NIN_KEYSELECT ||
        event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
        return ShowContextMenu();
    }
    return TrayCommand::none;
}

TrayCommand TrayHost::ShowContextMenu() const {
    POINT cursor{};
    ::GetCursorPos(&cursor);
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return TrayCommand::none;
    }

    ::AppendMenuW(menu, MF_STRING, kCaptureCommand, L"Снимок области");
    ::AppendMenuW(menu, MF_STRING, kWindowCommand, L"Окно под курсором");
    ::AppendMenuW(menu, MF_STRING, kMonitorCommand, L"Монитор под курсором");
    ::AppendMenuW(menu, MF_STRING, kAllCommand, L"Все мониторы");
    ::AppendMenuW(menu, MF_STRING, kOpenCommand, L"Открыть изображение…");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kOpenFolderCommand, L"Открыть папку сохранения");
    ::AppendMenuW(menu, MF_STRING, kSettingsCommand, L"Настройки…");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kExitCommand, L"Выход");
    ::SetForegroundWindow(owner_);
    const UINT command = ::TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON,
        cursor.x,
        cursor.y,
        owner_,
        nullptr
    );
    ::DestroyMenu(menu);

    if (command == kCaptureCommand) {
        return TrayCommand::capture_area;
    }
    if (command == kWindowCommand) {
        return TrayCommand::capture_window;
    }
    if (command == kMonitorCommand) {
        return TrayCommand::capture_monitor;
    }
    if (command == kAllCommand) {
        return TrayCommand::capture_all;
    }
    if (command == kOpenCommand) {
        return TrayCommand::open_image;
    }
    if (command == kOpenFolderCommand) {
        return TrayCommand::open_save_folder;
    }
    if (command == kSettingsCommand) {
        return TrayCommand::configure_settings;
    }
    if (command == kExitCommand) {
        return TrayCommand::exit_application;
    }
    return TrayCommand::none;
}

}  // namespace screenshooter::tray
