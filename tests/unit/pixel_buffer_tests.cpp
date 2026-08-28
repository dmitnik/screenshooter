#include "capture/pixel_buffer.h"

#include <catch2/catch_test_macros.hpp>

using screenshooter::capture::CreateOpaqueBuffer;
using screenshooter::capture::Crop;
using screenshooter::platform::PixelRect;

TEST_CASE("Opaque buffer initializes alpha for every pixel") {
    const auto image = CreateOpaqueBuffer(3, 2);
    REQUIRE_FALSE(image.empty());
    REQUIRE(image.stride == 12);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            REQUIRE(std::to_integer<unsigned>(image.row(y)[(x * 4) + 3]) == 0xFFU);
        }
    }
}

TEST_CASE("Crop copies a clipped rectangular pixel region") {
    auto image = CreateOpaqueBuffer(4, 3);
    image.row(1)[4] = std::byte{0x7A};

    const auto cropped = Crop(image, PixelRect{1, 1, 3, 3});
    REQUIRE(cropped.width == 2);
    REQUIRE(cropped.height == 2);
    REQUIRE(std::to_integer<unsigned>(cropped.row(0)[0]) == 0x7AU);
}

TEST_CASE("Crop rejects a region outside the source") {
    const auto image = CreateOpaqueBuffer(4, 3);
    REQUIRE(Crop(image, PixelRect{10, 10, 12, 12}).empty());
}
