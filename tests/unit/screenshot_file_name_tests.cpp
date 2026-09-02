#include "export/screenshot_file_name.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("Screenshot file name contains a Windows-safe local date and time") {
    SYSTEMTIME local_time{};
    local_time.wYear = 2026;
    local_time.wMonth = 9;
    local_time.wDay = 1;
    local_time.wHour = 7;
    local_time.wMinute = 5;
    local_time.wSecond = 9;

    REQUIRE(
        screenshooter::exporting::FormatScreenshotFileName(local_time) ==
        L"2026-09-01_07-05-09"
    );
}

TEST_CASE("Screenshot path keeps existing files and adds a numeric suffix") {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"screenshooter-file-name-test-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    REQUIRE(std::filesystem::create_directories(directory));

    const std::wstring name = L"2026-09-02_17-00-00";
    std::ofstream(directory / (name + L".png")).put('\0');
    std::ofstream(directory / (name + L" (2).png")).put('\0');

    REQUIRE(
        screenshooter::exporting::NextAvailableScreenshotPath(directory, name) ==
        directory / (name + L" (3).png")
    );

    std::filesystem::remove_all(directory, error);
}
