#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace screenshooter::exporting {

[[nodiscard]] std::wstring FormatScreenshotFileName(const SYSTEMTIME& local_time);
[[nodiscard]] std::wstring CurrentScreenshotFileName();
[[nodiscard]] std::filesystem::path NextAvailableScreenshotPath(
    const std::filesystem::path& directory,
    const std::wstring& screenshot_name
);

}  // namespace screenshooter::exporting
