#include "settings/settings_store.h"

#include <array>
#include <string>

namespace screenshooter::settings {
namespace {

constexpr wchar_t kRunPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr DWORD kSchemaVersion = 2;
constexpr UINT kAllowedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;

DWORD ReadDword(const HKEY key, const wchar_t* name, const DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    DWORD type = 0;
    return ::RegQueryValueExW(key, name, nullptr, &type,
        reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS && type == REG_DWORD
        ? value
        : fallback;
}

std::filesystem::path ReadString(const HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD size = 0;
    if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
        type != REG_SZ || size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (::RegQueryValueExW(key, name, nullptr, &type,
            reinterpret_cast<BYTE*>(value.data()), &size) != ERROR_SUCCESS) {
        return {};
    }
    value.resize(std::char_traits<wchar_t>::length(value.c_str()));
    return value;
}

bool WriteDword(const HKEY key, const wchar_t* name, const DWORD value) {
    return ::RegSetValueExW(key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

HotkeyBinding ReadHotkey(
    const HKEY key,
    const wchar_t* name,
    const HotkeyBinding fallback
) {
    const DWORD packed = ReadDword(
        key, name, (fallback.modifiers << 16U) | fallback.virtual_key);
    const HotkeyBinding binding{
        .modifiers = (packed >> 16U) & kAllowedModifiers,
        .virtual_key = packed & 0xFFFFU,
    };
    return binding.virtual_key != 0 ? binding : fallback;
}

DWORD PackHotkey(const HotkeyBinding binding) {
    return ((binding.modifiers & kAllowedModifiers) << 16U) |
        (binding.virtual_key & 0xFFFFU);
}

}  // namespace

Settings SettingsStore::Load() const {
    Settings settings;
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, settings_path_.c_str(), 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return settings;
    }
    const DWORD schema = ReadDword(key, L"SchemaVersion", 1);
    if (schema == 1 || schema == kSchemaVersion) {
        settings.hotkeys_enabled = ReadDword(key, L"HotkeysEnabled", 1) != 0;
        settings.autostart_enabled = ReadDword(key, L"AutostartEnabled", 0) != 0;
        settings.save_folder = ReadString(key, L"SaveFolder");
        if (schema == kSchemaVersion) {
            settings.area_hotkey = ReadHotkey(key, L"AreaHotkey", settings.area_hotkey);
            settings.monitor_hotkey = ReadHotkey(
                key, L"MonitorHotkey", settings.monitor_hotkey);
            settings.window_hotkey = ReadHotkey(key, L"WindowHotkey", settings.window_hotkey);
        }
    }
    ::RegCloseKey(key);
    return settings;
}

bool SettingsStore::Save(const Settings& settings) const {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, settings_path_.c_str(), 0, nullptr, 0,
            KEY_WRITE, nullptr, &key, &disposition) != ERROR_SUCCESS) {
        return false;
    }
    static_cast<void>(disposition);
    const std::wstring folder = settings.save_folder.wstring();
    const bool saved = WriteDword(key, L"SchemaVersion", kSchemaVersion) &&
        WriteDword(key, L"HotkeysEnabled", settings.hotkeys_enabled ? 1U : 0U) &&
        WriteDword(key, L"AutostartEnabled", settings.autostart_enabled ? 1U : 0U) &&
        WriteDword(key, L"AreaHotkey", PackHotkey(settings.area_hotkey)) &&
        WriteDword(key, L"MonitorHotkey", PackHotkey(settings.monitor_hotkey)) &&
        WriteDword(key, L"WindowHotkey", PackHotkey(settings.window_hotkey)) &&
        ::RegSetValueExW(key, L"SaveFolder", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(folder.c_str()),
            static_cast<DWORD>((folder.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    ::RegCloseKey(key);
    return saved;
}

bool SettingsStore::SetAutostart(const bool enabled) const {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunPath, 0, KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    LONG result = ERROR_SUCCESS;
    if (enabled) {
        std::array<wchar_t, 32768> module_path{};
        const DWORD length = ::GetModuleFileNameW(
            nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
        if (length == 0 || length >= module_path.size()) {
            ::RegCloseKey(key);
            return false;
        }
        const std::wstring command = L"\"" + std::wstring(module_path.data(), length) + L"\"";
        result = ::RegSetValueExW(key, L"Screenshooter", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        result = ::RegDeleteValueW(key, L"Screenshooter");
        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }
    ::RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

}  // namespace screenshooter::settings
