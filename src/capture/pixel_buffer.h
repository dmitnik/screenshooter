#pragma once

#include "platform/geometry.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace screenshooter::capture {

struct PixelBuffer {
    std::int32_t width{};
    std::int32_t height{};
    std::int32_t stride{};
    std::vector<std::byte> pixels;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] const std::byte* row(std::int32_t y) const noexcept;
    [[nodiscard]] std::byte* row(std::int32_t y) noexcept;
};

[[nodiscard]] PixelBuffer CreateOpaqueBuffer(std::int32_t width, std::int32_t height);
[[nodiscard]] PixelBuffer Crop(
    const PixelBuffer& source,
    platform::PixelRect source_rect
);

}  // namespace screenshooter::capture
