#include "platform/geometry.h"

#include <catch2/catch_test_macros.hpp>

using screenshooter::platform::Contains;
using screenshooter::platform::FitCenteredRect;
using screenshooter::platform::Intersect;
using screenshooter::platform::Normalize;
using screenshooter::platform::PlaceCropActions;
using screenshooter::platform::PixelPoint;
using screenshooter::platform::PixelRect;
using screenshooter::platform::PixelSize;

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

TEST_CASE("Editor window is centered and limited to the monitor work area") {
    REQUIRE(FitCenteredRect(
        PixelSize{1440, 900}, PixelSize{1080, 520}, PixelSize{1440, 900},
        PixelRect{0, 0, 1920, 1040}
    ) == PixelRect{240, 70, 1680, 970});

    REQUIRE(FitCenteredRect(
        PixelSize{1080, 520}, PixelSize{1080, 520}, PixelSize{1440, 900},
        PixelRect{-1920, 0, 0, 1040}
    ) == PixelRect{-1500, 260, -420, 780});
}

TEST_CASE("Editor window shrinks when the work area is below its preferred minimum") {
    REQUIRE(FitCenteredRect(
        PixelSize{1080, 520}, PixelSize{1080, 520}, PixelSize{1440, 900},
        PixelRect{100, 50, 900, 500}
    ) == PixelRect{100, 50, 900, 500});
}

TEST_CASE("Crop actions follow the bottom-right release corner") {
    REQUIRE(PlaceCropActions(
        PixelRect{100, 80, 400, 300}, true, true,
        PixelSize{198, 34}, PixelRect{0, 0, 900, 600}, 8
    ) == PixelRect{408, 266, 606, 300});
}

TEST_CASE("Crop actions follow the top-left release corner") {
    REQUIRE(PlaceCropActions(
        PixelRect{300, 140, 700, 500}, false, false,
        PixelSize{198, 34}, PixelRect{0, 0, 900, 600}, 8
    ) == PixelRect{94, 140, 292, 174});
}

TEST_CASE("Crop actions use the opposite side at the canvas edge") {
    REQUIRE(PlaceCropActions(
        PixelRect{600, 10, 890, 250}, true, false,
        PixelSize{198, 34}, PixelRect{0, 0, 900, 600}, 8
    ) == PixelRect{394, 10, 592, 44});
}
