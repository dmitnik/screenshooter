#include "export/screenshot_file_name.h"

#include <catch2/catch_test_macros.hpp>

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
