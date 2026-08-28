#include "export/clipboard_service.h"

#include <objidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstring>
#include <optional>

namespace screenshooter::exporting {
namespace {

using Microsoft::WRL::ComPtr;

HGLOBAL CreateDibV5(const capture::PixelBuffer& image) {
    const auto pixel_bytes = static_cast<std::size_t>(image.stride) * image.height;
    const auto allocation_size = sizeof(BITMAPV5HEADER) + pixel_bytes;
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, allocation_size);
    if (memory == nullptr) {
        return nullptr;
    }

    auto* bytes = static_cast<std::byte*>(::GlobalLock(memory));
    if (bytes == nullptr) {
        ::GlobalFree(memory);
        return nullptr;
    }

    auto* header = reinterpret_cast<BITMAPV5HEADER*>(bytes);
    *header = {};
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(pixel_bytes);
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    header->bV5Intent = LCS_GM_IMAGES;

    std::memcpy(bytes + sizeof(BITMAPV5HEADER), image.pixels.data(), pixel_bytes);
    ::GlobalUnlock(memory);
    return memory;
}

HGLOBAL CreatePng(const capture::PixelBuffer& image) {
    ComPtr<IStream> stream;
    if (FAILED(::CreateStreamOnHGlobal(nullptr, FALSE, &stream))) {
        return nullptr;
    }

    const auto fail = [&stream]() -> HGLOBAL {
        HGLOBAL memory = nullptr;
        if (SUCCEEDED(::GetHGlobalFromStream(stream.Get(), &memory)) && memory != nullptr) {
            ::GlobalFree(memory);
        }
        return nullptr;
    };

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(::CoCreateInstance(
            CLSID_WICImagingFactory2,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) {
        return fail();
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
        return fail();
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (FAILED(encoder->CreateNewFrame(&frame, &properties)) ||
        FAILED(frame->Initialize(properties.Get())) ||
        FAILED(frame->SetSize(image.width, image.height))) {
        return fail();
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) ||
        format != GUID_WICPixelFormat32bppBGRA ||
        FAILED(frame->WritePixels(
            image.height,
            image.stride,
            static_cast<UINT>(image.pixels.size()),
            reinterpret_cast<BYTE*>(const_cast<std::byte*>(image.pixels.data()))
        )) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return fail();
    }

    HGLOBAL memory = nullptr;
    return SUCCEEDED(::GetHGlobalFromStream(stream.Get(), &memory)) ? memory : fail();
}

}  // namespace

bool ClipboardService::CopyImage(
    const HWND owner,
    const capture::PixelBuffer& image
) {
    if (image.empty()) {
        return false;
    }

    HGLOBAL dib = CreateDibV5(image);
    HGLOBAL png = CreatePng(image);
    if (dib == nullptr || png == nullptr) {
        if (dib != nullptr) {
            ::GlobalFree(dib);
        }
        if (png != nullptr) {
            ::GlobalFree(png);
        }
        return false;
    }

    if (!::OpenClipboard(owner)) {
        ::GlobalFree(dib);
        ::GlobalFree(png);
        return false;
    }

    bool dib_transferred = false;
    bool png_transferred = false;
    if (::EmptyClipboard()) {
        dib_transferred = ::SetClipboardData(CF_DIBV5, dib) != nullptr;
        const UINT png_format = ::RegisterClipboardFormatW(L"PNG");
        png_transferred = png_format != 0 &&
            ::SetClipboardData(png_format, png) != nullptr;
    }
    ::CloseClipboard();

    if (!dib_transferred) {
        ::GlobalFree(dib);
    }
    if (!png_transferred) {
        ::GlobalFree(png);
    }
    return dib_transferred && png_transferred;
}

std::optional<capture::PixelBuffer> ClipboardService::ReadImage(const HWND owner) {
    if (!::OpenClipboard(owner)) {
        return std::nullopt;
    }
    HANDLE handle = ::GetClipboardData(CF_DIBV5);
    if (handle == nullptr) {
        ::CloseClipboard();
        return std::nullopt;
    }
    const auto* header = static_cast<const BITMAPV5HEADER*>(::GlobalLock(handle));
    if (header == nullptr || header->bV5Size < sizeof(BITMAPV5HEADER) ||
        header->bV5BitCount != 32 || header->bV5Width <= 0 || header->bV5Height == 0) {
        if (header != nullptr) {
            ::GlobalUnlock(handle);
        }
        ::CloseClipboard();
        return std::nullopt;
    }

    const auto width = static_cast<std::int32_t>(header->bV5Width);
    const auto height = static_cast<std::int32_t>(std::abs(header->bV5Height));
    capture::PixelBuffer image = capture::CreateOpaqueBuffer(width, height);
    const auto required = static_cast<std::size_t>(header->bV5Size) + image.pixels.size();
    if (image.empty() || ::GlobalSize(handle) < required) {
        ::GlobalUnlock(handle);
        ::CloseClipboard();
        return std::nullopt;
    }
    const auto* pixels = reinterpret_cast<const std::byte*>(header) + header->bV5Size;
    for (std::int32_t y = 0; y < height; ++y) {
        const std::int32_t source_y = header->bV5Height < 0 ? y : height - 1 - y;
        std::memcpy(
            image.row(y),
            pixels + (static_cast<std::size_t>(source_y) * image.stride),
            static_cast<std::size_t>(image.stride)
        );
    }
    ::GlobalUnlock(handle);
    ::CloseClipboard();
    return image;
}

}  // namespace screenshooter::exporting
