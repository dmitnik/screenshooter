#pragma once

#include <windows.h>

namespace screenshooter::performance {

class MetricTimer final {
public:
    MetricTimer() noexcept { Restart(); }
    void Restart() noexcept { ::QueryPerformanceCounter(&started_); }
    [[nodiscard]] double ElapsedMilliseconds() const noexcept;

private:
    LARGE_INTEGER started_{};
};

void LogMetric(const wchar_t* name, double milliseconds) noexcept;

}  // namespace screenshooter::performance
