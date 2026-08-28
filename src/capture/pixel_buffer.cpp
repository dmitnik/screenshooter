#include "capture/pixel_buffer.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace screenshooter::capture {
namespace {

constexpr std::int32_t kBytesPerPixel = 4;

}  // namespace

bool PixelBuffer::empty() const noexcept {
    return width <= 0 || height <= 0 || stride < width * kBytesPerPixel || pixels.empty();
}

const std::byte* PixelBuffer::row(const std::int32_t y) const noexcept {
    return pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(stride));
}

std::byte* PixelBuffer::row(const std::int32_t y) noexcept {
    return pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(stride));
}

PixelBuffer CreateOpaqueBuffer(const std::int32_t width, const std::int32_t height) {
    if (width <= 0 || height <= 0 ||
        width > std::numeric_limits<std::int32_t>::max() / kBytesPerPixel) {
        return {};
    }

    const std::int32_t stride = width * kBytesPerPixel;
    const auto byte_count = static_cast<std::size_t>(stride) *
        static_cast<std::size_t>(height);
    PixelBuffer result{width, height, stride, std::vector<std::byte>(byte_count)};

    for (std::int32_t y = 0; y < height; ++y) {
        auto* current = result.row(y);
        for (std::int32_t x = 0; x < width; ++x) {
            current[(x * kBytesPerPixel) + 3] = std::byte{0xFF};
        }
    }
    return result;
}

PixelBuffer Crop(const PixelBuffer& source, const platform::PixelRect source_rect) {
    if (source.empty()) {
        return {};
    }

    const auto clipped = platform::Intersect(
        source_rect,
        platform::PixelRect{0, 0, source.width, source.height}
    );
    if (clipped.empty()) {
        return {};
    }

    PixelBuffer result = CreateOpaqueBuffer(clipped.width(), clipped.height());
    const auto copy_size = static_cast<std::size_t>(result.width) * kBytesPerPixel;
    for (std::int32_t y = 0; y < result.height; ++y) {
        std::memcpy(
            result.row(y),
            source.row(clipped.top + y) +
                (static_cast<std::size_t>(clipped.left) * kBytesPerPixel),
            copy_size
        );
    }
    return result;
}

}  // namespace screenshooter::capture
