#include "platform/geometry.h"

#include <catch2/catch_test_macros.hpp>

using screenshooter::platform::Contains;
using screenshooter::platform::Intersect;
using screenshooter::platform::Normalize;
using screenshooter::platform::PixelPoint;
using screenshooter::platform::PixelRect;

TEST_CASE("Selection coordinates normalize across negative monitor origins") {
    const PixelRect result = Normalize(PixelPoint{200, 100}, PixelPoint{-300, -50});
    REQUIRE(result == PixelRect{-300, -50, 200, 100});
    REQUIRE(result.width() == 500);
    REQUIRE(result.height() == 150);
}

TEST_CASE("Rectangle intersection clips to the shared area") {
    const PixelRect result = Intersect(
        PixelRect{-1920, 0, 0, 1080},
        PixelRect{-200, 100, 300, 600}
    );
    REQUIRE(result == PixelRect{-200, 100, 0, 600});
}

TEST_CASE("Disjoint rectangle intersection is empty") {
    REQUIRE(Intersect(PixelRect{0, 0, 10, 10}, PixelRect{20, 20, 30, 30}).empty());
}

TEST_CASE("Rectangle containment uses exclusive right and bottom edges") {
    const PixelRect rect{-10, -10, 10, 10};
    REQUIRE(Contains(rect, PixelPoint{-10, -10}));
    REQUIRE(Contains(rect, PixelPoint{9, 9}));
    REQUIRE_FALSE(Contains(rect, PixelPoint{10, 9}));
    REQUIRE_FALSE(Contains(rect, PixelPoint{9, 10}));
}
