#include "platform/com_apartment.h"

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <catch2/catch_test_macros.hpp>

using Microsoft::WRL::ComPtr;

TEST_CASE("WIC imaging factory is available") {
    const screenshooter::platform::ComApartment apartment(COINIT_MULTITHREADED);
    REQUIRE(SUCCEEDED(apartment.result()));

    ComPtr<IWICImagingFactory> factory;
    const HRESULT result = ::CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );

    REQUIRE(SUCCEEDED(result));
    REQUIRE(factory != nullptr);
}

TEST_CASE("Direct3D WARP device supports BGRA surfaces") {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL feature_level{};

    const HRESULT result = ::D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &feature_level,
        &context
    );

    REQUIRE(SUCCEEDED(result));
    REQUIRE(device != nullptr);
    REQUIRE(context != nullptr);
    REQUIRE(feature_level >= D3D_FEATURE_LEVEL_10_0);
}
