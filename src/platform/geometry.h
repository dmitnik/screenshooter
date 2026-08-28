#pragma once

#include <cstdint>

namespace screenshooter::platform {

struct PixelPoint {
    std::int32_t x{};
    std::int32_t y{};

    friend bool operator==(const PixelPoint&, const PixelPoint&) = default;
};

struct PixelSize {
    std::int32_t width{};
    std::int32_t height{};

    friend bool operator==(const PixelSize&, const PixelSize&) = default;
};

struct PixelRect {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};

    [[nodiscard]] std::int32_t width() const noexcept { return right - left; }
    [[nodiscard]] std::int32_t height() const noexcept { return bottom - top; }
    [[nodiscard]] bool empty() const noexcept { return width() <= 0 || height() <= 0; }

    friend bool operator==(const PixelRect&, const PixelRect&) = default;
};

[[nodiscard]] PixelRect Normalize(PixelPoint first, PixelPoint second) noexcept;
[[nodiscard]] PixelRect Intersect(PixelRect first, PixelRect second) noexcept;
[[nodiscard]] bool Contains(PixelRect rect, PixelPoint point) noexcept;

}  // namespace screenshooter::platform
