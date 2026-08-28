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

}  // namespace screenshooter::platform
