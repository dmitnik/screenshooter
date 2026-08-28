#pragma once

#include <cstdint>

namespace screenshooter::platform {

inline constexpr std::uint32_t kDefaultDpi = 96;

[[nodiscard]] int DipToPixels(int dips, std::uint32_t dpi) noexcept;
[[nodiscard]] float PixelsToDips(int pixels, std::uint32_t dpi) noexcept;

}  // namespace screenshooter::platform
