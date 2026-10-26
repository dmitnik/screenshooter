#pragma once

#include <windows.h>
#include "localization/strings.h"

#include <filesystem>
#include <string>
#include <utility>

namespace screenshooter::settings {

struct HotkeyBinding {
    UINT modifiers{};
    UINT virtual_key{};

    friend bool operator==(const HotkeyBinding&, const HotkeyBinding&) = default;
};

struct Settings {
    i18n::Language language{i18n::Language::english};
    bool hotkeys_enabled{true};
    bool autostart_enabled{false};
    std::filesystem::path save_folder;
    HotkeyBinding area_hotkey{MOD_CONTROL | MOD_SHIFT, '1'};
    HotkeyBinding monitor_hotkey{MOD_CONTROL | MOD_SHIFT, '3'};
    HotkeyBinding window_hotkey{MOD_CONTROL | MOD_SHIFT, '4'};
};

enum class SettingsValidationError {
    none,
    missing_hotkey,
    duplicate_hotkey,
};

[[nodiscard]] SettingsValidationError ValidateSettings(const Settings& settings) noexcept;

class SettingsStore final {
public:
    explicit SettingsStore(std::wstring settings_path = L"Software\\Screenshooter")
        : settings_path_(std::move(settings_path)) {}

    [[nodiscard]] Settings Load() const;
    [[nodiscard]] bool Save(const Settings& settings) const;
    [[nodiscard]] bool SetAutostart(bool enabled) const;

private:
    std::wstring settings_path_;
};

}  // namespace screenshooter::settings
