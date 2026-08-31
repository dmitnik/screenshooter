#include "platform/geometry.h"

#include <algorithm>

namespace screenshooter::platform {

PixelRect Normalize(const PixelPoint first, const PixelPoint second) noexcept {
    return {
        .left = std::min(first.x, second.x),
        .top = std::min(first.y, second.y),
        .right = std::max(first.x, second.x),
        .bottom = std::max(first.y, second.y),
    };
}

PixelRect Intersect(const PixelRect first, const PixelRect second) noexcept {
    PixelRect result{
        .left = std::max(first.left, second.left),
        .top = std::max(first.top, second.top),
        .right = std::min(first.right, second.right),
        .bottom = std::min(first.bottom, second.bottom),
    };

    if (result.empty()) {
        return {};
    }
    return result;
}

bool Contains(const PixelRect rect, const PixelPoint point) noexcept {
    return point.x >= rect.left && point.x < rect.right &&
        point.y >= rect.top && point.y < rect.bottom;
}

PixelRect FitCenteredRect(
    const PixelSize desired,
    const PixelSize minimum,
    const PixelSize maximum,
    const PixelRect bounds
) noexcept {
    if (bounds.empty()) {
        return {};
    }

    const std::int32_t maximum_width = std::clamp(
        maximum.width, 1, bounds.width());
    const std::int32_t maximum_height = std::clamp(
        maximum.height, 1, bounds.height());
    const std::int32_t minimum_width = std::clamp(
        minimum.width, 1, maximum_width);
    const std::int32_t minimum_height = std::clamp(
        minimum.height, 1, maximum_height);
    const std::int32_t width = std::clamp(
        desired.width, minimum_width, maximum_width);
    const std::int32_t height = std::clamp(
        desired.height, minimum_height, maximum_height);
    const std::int32_t left = bounds.left + ((bounds.width() - width) / 2);
    const std::int32_t top = bounds.top + ((bounds.height() - height) / 2);
    return {left, top, left + width, top + height};
}

}  // namespace screenshooter::platform
