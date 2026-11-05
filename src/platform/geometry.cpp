#include "platform/geometry.h"

#include <algorithm>
#include <cstdlib>

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

CropResizeHandle HitCropResizeHandle(
    const PixelRect crop, const PixelPoint point, const std::int32_t tolerance
) noexcept {
    if (crop.empty() || tolerance < 0 ||
        point.x < crop.left - tolerance || point.x > crop.right + tolerance ||
        point.y < crop.top - tolerance || point.y > crop.bottom + tolerance) {
        return CropResizeHandle::none;
    }
    // Choose the nearer edge when handles overlap on a small selection.
    const bool left = std::abs(point.x - crop.left) <= std::abs(point.x - crop.right);
    const bool top = std::abs(point.y - crop.top) <= std::abs(point.y - crop.bottom);
    const bool horizontal = std::abs(point.x - (left ? crop.left : crop.right)) <= tolerance;
    const bool vertical = std::abs(point.y - (top ? crop.top : crop.bottom)) <= tolerance;
    if (horizontal && vertical) {
        if (top) return left ? CropResizeHandle::top_left : CropResizeHandle::top_right;
        return left ? CropResizeHandle::bottom_left : CropResizeHandle::bottom_right;
    }
    if (horizontal) return left ? CropResizeHandle::left : CropResizeHandle::right;
    if (vertical) return top ? CropResizeHandle::top : CropResizeHandle::bottom;
    return CropResizeHandle::none;
}

PixelRect ResizeCrop(
    PixelRect crop, const CropResizeHandle handle, const PixelPoint delta,
    const PixelRect bounds
) noexcept {
    crop = Intersect(crop, bounds);
    if (crop.empty()) return crop;
    if (handle == CropResizeHandle::left || handle == CropResizeHandle::top_left ||
        handle == CropResizeHandle::bottom_left) {
        crop.left = std::clamp(crop.left + delta.x, bounds.left, crop.right - 1);
    }
    if (handle == CropResizeHandle::right || handle == CropResizeHandle::top_right ||
        handle == CropResizeHandle::bottom_right) {
        crop.right = std::clamp(crop.right + delta.x, crop.left + 1, bounds.right);
    }
    if (handle == CropResizeHandle::top || handle == CropResizeHandle::top_left ||
        handle == CropResizeHandle::top_right) {
        crop.top = std::clamp(crop.top + delta.y, bounds.top, crop.bottom - 1);
    }
    if (handle == CropResizeHandle::bottom || handle == CropResizeHandle::bottom_left ||
        handle == CropResizeHandle::bottom_right) {
        crop.bottom = std::clamp(crop.bottom + delta.y, crop.top + 1, bounds.bottom);
    }
    return crop;
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

PixelRect PlaceCropActions(
    const PixelRect crop,
    const bool release_on_right,
    const bool release_on_bottom,
    const PixelSize action_size,
    const PixelRect canvas,
    const std::int32_t gap
) noexcept {
    if (canvas.empty() || action_size.width <= 0 || action_size.height <= 0) {
        return {};
    }

    const std::int32_t width = std::min(action_size.width, canvas.width());
    const std::int32_t height = std::min(action_size.height, canvas.height());
    const std::int32_t preferred_left = release_on_right
        ? crop.right + gap
        : crop.left - gap - width;
    const std::int32_t alternate_left = release_on_right
        ? crop.left - gap - width
        : crop.right + gap;
    const auto fits_horizontally = [canvas, width](const std::int32_t left) {
        return left >= canvas.left && left + width <= canvas.right;
    };

    std::int32_t left = preferred_left;
    if (!fits_horizontally(left) && fits_horizontally(alternate_left)) {
        left = alternate_left;
    }
    left = std::clamp(left, canvas.left, canvas.right - width);

    std::int32_t top = release_on_bottom
        ? crop.bottom - height
        : crop.top;
    top = std::clamp(top, canvas.top, canvas.bottom - height);
    return {left, top, left + width, top + height};
}

}  // namespace screenshooter::platform
