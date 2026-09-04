#pragma once

#include "settings/settings_store.h"

#include <windows.h>

#include <optional>

namespace screenshooter::settings {

[[nodiscard]] std::optional<Settings> ShowSettingsDialog(
    HINSTANCE instance,
    HWND owner,
    const Settings& current
);

}  // namespace screenshooter::settings
