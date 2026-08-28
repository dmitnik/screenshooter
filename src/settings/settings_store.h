#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <utility>

namespace screenshooter::settings {

struct Settings {
    bool hotkeys_enabled{true};
    bool autostart_enabled{false};
    std::filesystem::path save_folder;
};

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
