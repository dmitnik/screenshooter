#include "settings/settings_store.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <string>

namespace {

struct RegistryCleanup {
    std::wstring path;
    ~RegistryCleanup() { ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
};

std::wstring TestPath() {
    return L"Software\\ScreenshooterTests\\" +
        std::to_wstring(::GetCurrentProcessId());
}

}  // namespace

TEST_CASE("HKCU settings preserve the default save folder") {
    const std::wstring path = TestPath();
    RegistryCleanup cleanup{path};
    screenshooter::settings::SettingsStore store(path);
    screenshooter::settings::Settings settings{
        .hotkeys_enabled = false,
        .autostart_enabled = true,
        .save_folder = L"D:\\Screenshots",
    };

    REQUIRE(store.Save(settings));
    const auto loaded = store.Load();
    REQUIRE_FALSE(loaded.hotkeys_enabled);
    REQUIRE(loaded.autostart_enabled);
    REQUIRE(loaded.save_folder == std::filesystem::path(L"D:\\Screenshots"));
}

TEST_CASE("Unknown registry schema safely restores defaults") {
    const std::wstring path = TestPath();
    RegistryCleanup cleanup{path};
    HKEY key = nullptr;
    DWORD disposition = 0;
    REQUIRE(::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, &key, &disposition) == ERROR_SUCCESS);
    static_cast<void>(disposition);
    const DWORD schema = 999;
    REQUIRE(::RegSetValueExW(key, L"SchemaVersion", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&schema), sizeof(schema)) == ERROR_SUCCESS);
    ::RegCloseKey(key);

    const auto loaded = screenshooter::settings::SettingsStore(path).Load();
    REQUIRE(loaded.hotkeys_enabled);
    REQUIRE_FALSE(loaded.autostart_enabled);
    REQUIRE(loaded.save_folder.empty());
}
