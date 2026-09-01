#include "export/screenshot_file_name.h"

#include <cwchar>

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

}  // namespace screenshooter::exporting
