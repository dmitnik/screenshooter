#include "capture/capture_service.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace screenshooter::capture {
namespace {

using Microsoft::WRL::ComPtr;
constexpr std::int32_t kBytesPerPixel = 4;

platform::PixelRect GetVirtualBounds() noexcept {
    const auto left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const auto top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {
        left,
        top,
        left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
        top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN),
    };
}

void CopyRotatedOutput(
    const D3D11_MAPPED_SUBRESOURCE& mapped,
    const D3D11_TEXTURE2D_DESC& source,
    const DXGI_OUTPUT_DESC& output,
    const platform::PixelRect virtual_bounds,
    PixelBuffer& destination
) {
    const auto output_width = output.DesktopCoordinates.right -
        output.DesktopCoordinates.left;
    const auto output_height = output.DesktopCoordinates.bottom -
        output.DesktopCoordinates.top;

    for (std::int32_t y = 0; y < output_height; ++y) {
        for (std::int32_t x = 0; x < output_width; ++x) {
            std::int32_t source_x = x;
            std::int32_t source_y = y;
            switch (output.Rotation) {
            case DXGI_MODE_ROTATION_ROTATE90:
                source_x = y;
                source_y = static_cast<std::int32_t>(source.Height) - 1 - x;
                break;
            case DXGI_MODE_ROTATION_ROTATE180:
                source_x = static_cast<std::int32_t>(source.Width) - 1 - x;
                source_y = static_cast<std::int32_t>(source.Height) - 1 - y;
                break;
            case DXGI_MODE_ROTATION_ROTATE270:
                source_x = static_cast<std::int32_t>(source.Width) - 1 - y;
                source_y = x;
                break;
            default:
                break;
            }

            if (source_x < 0 || source_y < 0 ||
                source_x >= static_cast<std::int32_t>(source.Width) ||
                source_y >= static_cast<std::int32_t>(source.Height)) {
                continue;
            }

            const auto destination_x = output.DesktopCoordinates.left - virtual_bounds.left + x;
            const auto destination_y = output.DesktopCoordinates.top - virtual_bounds.top + y;
            auto* target = destination.row(destination_y) +
                (static_cast<std::size_t>(destination_x) * kBytesPerPixel);
            const auto* current_source = static_cast<const std::byte*>(mapped.pData) +
                (static_cast<std::size_t>(source_y) * mapped.RowPitch) +
                (static_cast<std::size_t>(source_x) * kBytesPerPixel);
            std::memcpy(target, current_source, 3);
            target[3] = std::byte{0xFF};
        }
    }
}

}  // namespace

CaptureOutcome CaptureService::CaptureVirtualDesktop() const {
    if (auto frame = CaptureWithDesktopDuplication()) {
        return {std::move(frame), {}};
    }
    if (auto frame = CaptureWithGdi()) {
        return {std::move(frame), {}};
    }
    return {std::nullopt, L"Не удалось получить изображение рабочего стола."};
}

std::optional<CaptureFrame> CaptureService::CaptureWithDesktopDuplication() const {
    const auto virtual_bounds = GetVirtualBounds();
    if (virtual_bounds.empty()) {
        return std::nullopt;
    }

    PixelBuffer composite = CreateOpaqueBuffer(
        virtual_bounds.width(),
        virtual_bounds.height()
    );
    if (composite.empty()) {
        return std::nullopt;
    }

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return std::nullopt;
    }

    unsigned int expected_outputs = 0;
    unsigned int captured_outputs = 0;

    for (unsigned int adapter_index = 0;; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT adapter_result = factory->EnumAdapters1(adapter_index, &adapter);
        if (adapter_result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(adapter_result)) {
            return std::nullopt;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        if (FAILED(::D3D11CreateDevice(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &device,
                nullptr,
                &context))) {
            continue;
        }

        for (unsigned int output_index = 0;; ++output_index) {
            ComPtr<IDXGIOutput> output;
            const HRESULT output_result = adapter->EnumOutputs(output_index, &output);
            if (output_result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(output_result)) {
                return std::nullopt;
            }

            DXGI_OUTPUT_DESC output_description{};
            output->GetDesc(&output_description);
            if (!output_description.AttachedToDesktop) {
                continue;
            }
            ++expected_outputs;

            ComPtr<IDXGIOutput1> output1;
            ComPtr<IDXGIOutputDuplication> duplication;
            if (FAILED(output.As(&output1)) ||
                FAILED(output1->DuplicateOutput(device.Get(), &duplication))) {
                return std::nullopt;
            }

            DXGI_OUTDUPL_FRAME_INFO frame_info{};
            ComPtr<IDXGIResource> desktop_resource;
            if (FAILED(duplication->AcquireNextFrame(
                    200,
                    &frame_info,
                    &desktop_resource))) {
                return std::nullopt;
            }

            ComPtr<ID3D11Texture2D> texture;
            HRESULT result = desktop_resource.As(&texture);
            D3D11_TEXTURE2D_DESC texture_description{};
            if (SUCCEEDED(result)) {
                texture->GetDesc(&texture_description);
                auto staging_description = texture_description;
                staging_description.Usage = D3D11_USAGE_STAGING;
                staging_description.BindFlags = 0;
                staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                staging_description.MiscFlags = 0;

                ComPtr<ID3D11Texture2D> staging;
                result = device->CreateTexture2D(
                    &staging_description,
                    nullptr,
                    &staging
                );
                if (SUCCEEDED(result)) {
                    context->CopyResource(staging.Get(), texture.Get());
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    result = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                    if (SUCCEEDED(result)) {
                        CopyRotatedOutput(
                            mapped,
                            texture_description,
                            output_description,
                            virtual_bounds,
                            composite
                        );
                        context->Unmap(staging.Get(), 0);
                        ++captured_outputs;
                    }
                }
            }
            duplication->ReleaseFrame();
        }
    }

    if (expected_outputs == 0 || captured_outputs != expected_outputs) {
        return std::nullopt;
    }
    return CaptureFrame{std::move(composite), virtual_bounds, CaptureBackend::desktop_duplication};
}

std::optional<CaptureFrame> CaptureService::CaptureWithGdi() const {
    const auto bounds = GetVirtualBounds();
    if (bounds.empty()) {
        return std::nullopt;
    }

    HDC screen = ::GetDC(nullptr);
    if (screen == nullptr) {
        return std::nullopt;
    }
    HDC memory = ::CreateCompatibleDC(screen);
    if (memory == nullptr) {
        ::ReleaseDC(nullptr, screen);
        return std::nullopt;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = bounds.width();
    bitmap_info.bmiHeader.biHeight = -bounds.height();
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(
        screen,
        &bitmap_info,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0
    );
    if (bitmap == nullptr || bits == nullptr) {
        ::DeleteDC(memory);
        ::ReleaseDC(nullptr, screen);
        return std::nullopt;
    }

    HGDIOBJ previous = ::SelectObject(memory, bitmap);
    const BOOL copied = ::BitBlt(
        memory,
        0,
        0,
        bounds.width(),
        bounds.height(),
        screen,
        bounds.left,
        bounds.top,
        SRCCOPY | CAPTUREBLT
    );

    PixelBuffer pixels;
    if (copied) {
        pixels = CreateOpaqueBuffer(bounds.width(), bounds.height());
        for (std::int32_t y = 0; y < pixels.height; ++y) {
            std::memcpy(
                pixels.row(y),
                static_cast<const std::byte*>(bits) +
                    (static_cast<std::size_t>(y) * pixels.stride),
                static_cast<std::size_t>(pixels.stride)
            );
            for (std::int32_t x = 0; x < pixels.width; ++x) {
                pixels.row(y)[(x * kBytesPerPixel) + 3] = std::byte{0xFF};
            }
        }
    }

    ::SelectObject(memory, previous);
    ::DeleteObject(bitmap);
    ::DeleteDC(memory);
    ::ReleaseDC(nullptr, screen);

    if (pixels.empty()) {
        return std::nullopt;
    }
    return CaptureFrame{std::move(pixels), bounds, CaptureBackend::gdi};
}

}  // namespace screenshooter::capture
