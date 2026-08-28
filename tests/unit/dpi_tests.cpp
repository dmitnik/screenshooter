#include "platform/dpi.h"

#include <catch2/catch_test_macros.hpp>

using screenshooter::platform::DipToPixels;
using screenshooter::platform::PixelsToDips;

TEST_CASE("DIP conversion keeps values at 96 DPI") {
    REQUIRE(DipToPixels(128, 96) == 128);
    REQUIRE(PixelsToDips(128, 96) == 128.0F);
}

TEST_CASE("DIP conversion scales common monitor densities") {
    REQUIRE(DipToPixels(100, 120) == 125);
    REQUIRE(DipToPixels(100, 144) == 150);
    REQUIRE(DipToPixels(100, 192) == 200);
    REQUIRE(PixelsToDips(150, 144) == 100.0F);
}

TEST_CASE("DIP conversion handles negative coordinates") {
    REQUIRE(DipToPixels(-100, 144) == -150);
    REQUIRE(PixelsToDips(-150, 144) == -100.0F);
}

TEST_CASE("Zero DPI falls back to identity conversion") {
    REQUIRE(DipToPixels(42, 0) == 42);
    REQUIRE(PixelsToDips(42, 0) == 42.0F);
}
