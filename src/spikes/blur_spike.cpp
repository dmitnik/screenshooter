#include "spikes/spikes.h"

#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

namespace screenshooter::spikes {
namespace {

using Microsoft::WRL::ComPtr;

constexpr unsigned int kImageSize = 128;

void PrintFailure(const wchar_t* operation, const HRESULT result) {
    std::wcerr << operation << L" failed: 0x" << std::hex
               << static_cast<unsigned long>(result) << std::dec << L'\n';
}

[[nodiscard]] HRESULT EncodePng(
    const std::filesystem::path& path,
    const unsigned int width,
    const unsigned int height,
    const unsigned int stride,
    const std::byte* pixels,
    const unsigned int byte_count
) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = ::CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(result)) {
        return result;
    }

    ComPtr<IWICStream> stream;
    result = factory->CreateStream(&stream);
    if (FAILED(result)) {
        return result;
    }

    result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(result)) {
        return result;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(result)) {
        return result;
    }

    result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(result)) {
        return result;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    result = encoder->CreateNewFrame(&frame, &properties);
    if (FAILED(result)) {
        return result;
    }

    result = frame->Initialize(properties.Get());
    if (FAILED(result)) {
        return result;
    }
    result = frame->SetSize(width, height);
    if (FAILED(result)) {
        return result;
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    result = frame->SetPixelFormat(&format);
    if (FAILED(result) || format != GUID_WICPixelFormat32bppBGRA) {
        return FAILED(result) ? result : WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }

    result = frame->WritePixels(
        height,
        stride,
        byte_count,
        reinterpret_cast<BYTE*>(const_cast<std::byte*>(pixels))
    );
    if (FAILED(result)) {
        return result;
    }
    result = frame->Commit();
    if (FAILED(result)) {
        return result;
    }
    return encoder->Commit();
}

[[nodiscard]] bool VerifyPng(const std::filesystem::path& path) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = ::CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    if (FAILED(result)) {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &decoder
    );
    if (FAILED(result)) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) {
        return false;
    }

    unsigned int width = 0;
    unsigned int height = 0;
    result = frame->GetSize(&width, &height);
    return SUCCEEDED(result) && width == kImageSize && height == kImageSize;
}

}  // namespace

bool RunGaussianBlurSpike(const std::filesystem::path& output_path) {
    constexpr unsigned int device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    ComPtr<ID3D11Device> d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    D3D_FEATURE_LEVEL feature_level{};

    HRESULT result = ::D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        device_flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &d3d_device,
        &feature_level,
        &d3d_context
    );
    if (FAILED(result)) {
        PrintFailure(L"D3D11CreateDevice(WARP)", result);
        return false;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    result = d3d_device.As(&dxgi_device);
    if (FAILED(result)) {
        PrintFailure(L"ID3D11Device::QueryInterface(IDXGIDevice)", result);
        return false;
    }

    ComPtr<ID2D1Factory1> d2d_factory;
    result = ::D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        IID_PPV_ARGS(&d2d_factory)
    );
    if (FAILED(result)) {
        PrintFailure(L"D2D1CreateFactory", result);
        return false;
    }

    ComPtr<ID2D1Device> d2d_device;
    result = d2d_factory->CreateDevice(dxgi_device.Get(), &d2d_device);
    if (FAILED(result)) {
        PrintFailure(L"ID2D1Factory1::CreateDevice", result);
        return false;
    }

    ComPtr<ID2D1DeviceContext> d2d_context;
    result = d2d_device->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &d2d_context
    );
    if (FAILED(result)) {
        PrintFailure(L"ID2D1Device::CreateDeviceContext", result);
        return false;
    }

    D3D11_TEXTURE2D_DESC texture_description{
        .Width = kImageSize,
        .Height = kImageSize,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    };

    ComPtr<ID3D11Texture2D> target_texture;
    result = d3d_device->CreateTexture2D(&texture_description, nullptr, &target_texture);
    if (FAILED(result)) {
        PrintFailure(L"ID3D11Device::CreateTexture2D(target)", result);
        return false;
    }

    ComPtr<IDXGISurface> target_surface;
    result = target_texture.As(&target_surface);
    if (FAILED(result)) {
        PrintFailure(L"ID3D11Texture2D::QueryInterface(IDXGISurface)", result);
        return false;
    }

    const D2D1_BITMAP_PROPERTIES1 target_properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED
        )
    );

    ComPtr<ID2D1Bitmap1> target_bitmap;
    result = d2d_context->CreateBitmapFromDxgiSurface(
        target_surface.Get(),
        &target_properties,
        &target_bitmap
    );
    if (FAILED(result)) {
        PrintFailure(L"ID2D1DeviceContext::CreateBitmapFromDxgiSurface", result);
        return false;
    }

    std::array<std::uint32_t, kImageSize * kImageSize> source_pixels{};
    for (unsigned int y = 0; y < kImageSize; ++y) {
        for (unsigned int x = 0; x < kImageSize; ++x) {
            const bool alternate = ((x / 8) + (y / 8)) % 2 != 0;
            source_pixels[(y * kImageSize) + x] = alternate
                ? 0xFFE84A5FU
                : 0xFF2F80EDU;
        }
    }

    const D2D1_BITMAP_PROPERTIES1 source_properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED
        )
    );

    ComPtr<ID2D1Bitmap1> source_bitmap;
    result = d2d_context->CreateBitmap(
        D2D1::SizeU(kImageSize, kImageSize),
        source_pixels.data(),
        kImageSize * sizeof(std::uint32_t),
        &source_properties,
        &source_bitmap
    );
    if (FAILED(result)) {
        PrintFailure(L"ID2D1DeviceContext::CreateBitmap(source)", result);
        return false;
    }

    ComPtr<ID2D1Effect> blur;
    result = d2d_context->CreateEffect(CLSID_D2D1GaussianBlur, &blur);
    if (FAILED(result)) {
        PrintFailure(L"ID2D1DeviceContext::CreateEffect(GaussianBlur)", result);
        return false;
    }
    blur->SetInput(0, source_bitmap.Get());
    result = blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 5.0F);
    if (FAILED(result)) {
        PrintFailure(L"ID2D1Effect::SetValue", result);
        return false;
    }

    d2d_context->SetTarget(target_bitmap.Get());
    d2d_context->BeginDraw();
    d2d_context->Clear(D2D1::ColorF(D2D1::ColorF::White));
    d2d_context->DrawImage(blur.Get());
    result = d2d_context->EndDraw();
    if (FAILED(result)) {
        PrintFailure(L"ID2D1DeviceContext::EndDraw", result);
        return false;
    }

    texture_description.Usage = D3D11_USAGE_STAGING;
    texture_description.BindFlags = 0;
    texture_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging_texture;
    result = d3d_device->CreateTexture2D(
        &texture_description,
        nullptr,
        &staging_texture
    );
    if (FAILED(result)) {
        PrintFailure(L"ID3D11Device::CreateTexture2D(staging)", result);
        return false;
    }

    d3d_context->CopyResource(staging_texture.Get(), target_texture.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = d3d_context->Map(
        staging_texture.Get(),
        0,
        D3D11_MAP_READ,
        0,
        &mapped
    );
    if (FAILED(result)) {
        PrintFailure(L"ID3D11DeviceContext::Map", result);
        return false;
    }

    const auto byte_count = mapped.RowPitch * kImageSize;
    std::vector<std::byte> pixel_copy(byte_count);
    std::memcpy(pixel_copy.data(), mapped.pData, byte_count);
    d3d_context->Unmap(staging_texture.Get(), 0);

    result = EncodePng(
        output_path,
        kImageSize,
        kImageSize,
        mapped.RowPitch,
        pixel_copy.data(),
        byte_count
    );
    if (FAILED(result)) {
        PrintFailure(L"WIC PNG encode", result);
        return false;
    }

    if (!VerifyPng(output_path)) {
        std::wcerr << L"WIC PNG verification failed.\n";
        return false;
    }

    std::wcout << L"Gaussian Blur rendered and verified: " << output_path.c_str()
               << L'\n';
    return true;
}

}  // namespace screenshooter::spikes
