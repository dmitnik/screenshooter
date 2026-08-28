#include "platform/dpi.h"

#include <limits>

namespace screenshooter::platform {

int DipToPixels(const int dips, const std::uint32_t dpi) noexcept {
    if (dpi == 0) {
        return dips;
    }

    const auto scaled = static_cast<long long>(dips) * static_cast<long long>(dpi);
    const auto rounded = scaled >= 0
        ? (scaled + static_cast<long long>(kDefaultDpi / 2)) / kDefaultDpi
        : (scaled - static_cast<long long>(kDefaultDpi / 2)) / kDefaultDpi;

    if (rounded > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    if (rounded < std::numeric_limits<int>::min()) {
        return std::numeric_limits<int>::min();
    }
    return static_cast<int>(rounded);
}

float PixelsToDips(const int pixels, const std::uint32_t dpi) noexcept {
    if (dpi == 0) {
        return static_cast<float>(pixels);
    }
    return static_cast<float>(pixels) * static_cast<float>(kDefaultDpi) /
        static_cast<float>(dpi);
}

}  // namespace screenshooter::platform
