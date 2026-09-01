#pragma once

#include <windows.h>

#include <string>

namespace screenshooter::exporting {

[[nodiscard]] std::wstring FormatScreenshotFileName(const SYSTEMTIME& local_time);
[[nodiscard]] std::wstring CurrentScreenshotFileName();

}  // namespace screenshooter::exporting
