#include "export/screenshot_file_name.h"

#include <cwchar>
#include <system_error>

namespace screenshooter::exporting {

std::wstring FormatScreenshotFileName(const SYSTEMTIME& local_time) {
    wchar_t name[32]{};
    const int written = ::swprintf_s(
        name,
        L"%04u-%02u-%02u_%02u-%02u-%02u",
        local_time.wYear,
        local_time.wMonth,
        local_time.wDay,
        local_time.wHour,
        local_time.wMinute,
        local_time.wSecond
    );
    return written > 0 ? std::wstring(name) : std::wstring{};
}

std::wstring CurrentScreenshotFileName() {
    SYSTEMTIME local_time{};
    ::GetLocalTime(&local_time);
    return FormatScreenshotFileName(local_time);
}

std::filesystem::path NextAvailableScreenshotPath(
    const std::filesystem::path& directory,
    const std::wstring& screenshot_name
) {
    if (directory.empty() || screenshot_name.empty()) {
        return {};
    }
    for (unsigned int index = 1; index <= 9999; ++index) {
        const std::wstring suffix = index == 1
            ? std::wstring{}
            : L" (" + std::to_wstring(index) + L")";
        const auto candidate = directory / (screenshot_name + suffix + L".png");
        std::error_code error;
        const bool exists = std::filesystem::exists(candidate, error);
        if (error) {
            return {};
        }
        if (!exists) {
            return candidate;
        }
    }
    return {};
}

}  // namespace screenshooter::exporting
