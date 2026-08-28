#include "performance/metrics.h"

#include <array>

namespace screenshooter::performance {

double MetricTimer::ElapsedMilliseconds() const noexcept {
    LARGE_INTEGER current{};
    LARGE_INTEGER frequency{};
    ::QueryPerformanceCounter(&current);
    ::QueryPerformanceFrequency(&frequency);
    if (frequency.QuadPart == 0) {
        return 0.0;
    }
    return (static_cast<double>(current.QuadPart - started_.QuadPart) * 1000.0) /
        static_cast<double>(frequency.QuadPart);
}

void LogMetric(const wchar_t* name, const double milliseconds) noexcept {
    std::array<wchar_t, 160> message{};
    ::swprintf_s(message.data(), message.size(),
        L"[Screenshooter] %s: %.2f ms\n", name, milliseconds);
    ::OutputDebugStringW(message.data());
}

}  // namespace screenshooter::performance
