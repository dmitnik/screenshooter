#include "export/image_file_service.h"
#include "platform/com_apartment.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <filesystem>

namespace {

std::filesystem::path TemporaryImagePath(const wchar_t* extension) {
    return std::filesystem::temp_directory_path() /
        (L"screenshooter-test-" + std::to_wstring(::GetCurrentProcessId()) + extension);
}

}  // namespace

TEST_CASE("WIC PNG round trip preserves dimensions and pixels") {
    screenshooter::platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(apartment.result()));
    auto image = screenshooter::capture::CreateOpaqueBuffer(13, 9);
    image.row(4)[(7 * 4) + 2] = std::byte{0xA5};
    const auto path = TemporaryImagePath(L".png");

    REQUIRE(screenshooter::exporting::ImageFileService::Save(
        path, image, screenshooter::exporting::ImageFormat::png));
    const auto decoded = screenshooter::exporting::ImageFileService::Load(path);
    std::filesystem::remove(path);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->width == 13);
    REQUIRE(decoded->height == 9);
    REQUIRE(std::to_integer<unsigned>(decoded->row(4)[(7 * 4) + 2]) == 0xA5U);
}

TEST_CASE("WIC JPEG output is readable and keeps dimensions") {
    screenshooter::platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(SUCCEEDED(apartment.result()));
    const auto image = screenshooter::capture::CreateOpaqueBuffer(17, 11);
    const auto path = TemporaryImagePath(L".jpg");

    REQUIRE(screenshooter::exporting::ImageFileService::Save(
        path, image, screenshooter::exporting::ImageFormat::jpeg, 0.85F));
    const auto decoded = screenshooter::exporting::ImageFileService::Load(path);
    std::filesystem::remove(path);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->width == 17);
    REQUIRE(decoded->height == 11);
}
