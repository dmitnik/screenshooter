#include "export/image_file_service.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

namespace screenshooter::exporting {
namespace {

using Microsoft::WRL::ComPtr;

ComPtr<IWICImagingFactory> CreateFactory() {
    ComPtr<IWICImagingFactory> factory;
    ::CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)
    );
    return factory;
}

}  // namespace

bool ImageFileService::Save(
    const std::filesystem::path& path,
    const capture::PixelBuffer& image,
    const ImageFormat format,
    const float jpeg_quality
) {
    if (image.empty()) {
        return false;
    }
    auto factory = CreateFactory();
    if (factory == nullptr) {
        return false;
    }
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) {
        return false;
    }
    ComPtr<IWICBitmapEncoder> encoder;
    const GUID container = format == ImageFormat::png
        ? GUID_ContainerFormatPng
        : GUID_ContainerFormatJpeg;
    if (FAILED(factory->CreateEncoder(container, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return false;
    }
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(encoder->CreateNewFrame(&frame, &properties))) {
        return false;
    }
    if (format == ImageFormat::jpeg && properties != nullptr) {
        PROPBAG2 option{};
        option.pstrName = const_cast<wchar_t*>(L"ImageQuality");
        VARIANT value{};
        ::VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = std::clamp(jpeg_quality, 0.0F, 1.0F);
        properties->Write(1, &option, &value);
        ::VariantClear(&value);
    }
    if (FAILED(frame->Initialize(properties.Get())) ||
        FAILED(frame->SetSize(image.width, image.height))) {
        return false;
    }
    WICPixelFormatGUID pixel_format = format == ImageFormat::png
        ? GUID_WICPixelFormat32bppBGRA
        : GUID_WICPixelFormat24bppBGR;
    if (FAILED(frame->SetPixelFormat(&pixel_format))) {
        return false;
    }
    if (format == ImageFormat::png && pixel_format == GUID_WICPixelFormat32bppBGRA) {
        if (FAILED(frame->WritePixels(
            image.height, image.stride, static_cast<UINT>(image.pixels.size()),
            reinterpret_cast<BYTE*>(const_cast<std::byte*>(image.pixels.data()))))) {
            return false;
        }
    } else {
        ComPtr<IWICBitmap> bitmap;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateBitmapFromMemory(
                image.width,
                image.height,
                GUID_WICPixelFormat32bppBGRA,
                image.stride,
                static_cast<UINT>(image.pixels.size()),
                reinterpret_cast<BYTE*>(const_cast<std::byte*>(image.pixels.data())),
                &bitmap)) ||
            FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat24bppBGR,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
            FAILED(frame->WriteSource(converter.Get(), nullptr))) {
            return false;
        }
    }
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

std::optional<capture::PixelBuffer> ImageFileService::Load(
    const std::filesystem::path& path
) {
    auto factory = CreateFactory();
    if (factory == nullptr) {
        return std::nullopt;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return std::nullopt;
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > static_cast<UINT>(INT_MAX) || height > static_cast<UINT>(INT_MAX)) {
        return std::nullopt;
    }
    auto image = capture::CreateOpaqueBuffer(
        static_cast<std::int32_t>(width),
        static_cast<std::int32_t>(height)
    );
    if (image.empty() || FAILED(converter->CopyPixels(
        nullptr, image.stride, static_cast<UINT>(image.pixels.size()),
        reinterpret_cast<BYTE*>(image.pixels.data())))) {
        return std::nullopt;
    }
    return image;
}

}  // namespace screenshooter::exporting
