#include "spikes/spikes.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <iomanip>
#include <iostream>
#include <string>

namespace screenshooter::spikes {
namespace {

using Microsoft::WRL::ComPtr;

void PrintFailure(const wchar_t* operation, const HRESULT result) {
    std::wcerr << operation << L" failed: 0x" << std::hex
               << static_cast<unsigned long>(result) << std::dec << L'\n';
}

}  // namespace

bool RunDesktopDuplicationSpike() {
    ComPtr<IDXGIFactory1> factory;
    HRESULT result = ::CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        PrintFailure(L"CreateDXGIFactory1", result);
        return false;
    }

    unsigned int output_count = 0;
    unsigned int captured_count = 0;

    for (unsigned int adapter_index = 0;; ++adapter_index) {
        ComPtr<IDXGIAdapter1> adapter;
        result = factory->EnumAdapters1(adapter_index, &adapter);
        if (result == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(result)) {
            PrintFailure(L"IDXGIFactory1::EnumAdapters1", result);
            return false;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL feature_level{};
        result = ::D3D11CreateDevice(
            adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device,
            &feature_level,
            &context
        );
        if (FAILED(result)) {
            PrintFailure(L"D3D11CreateDevice(adapter)", result);
            continue;
        }

        for (unsigned int output_index = 0;; ++output_index) {
            ComPtr<IDXGIOutput> output;
            result = adapter->EnumOutputs(output_index, &output);
            if (result == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(result)) {
                PrintFailure(L"IDXGIAdapter1::EnumOutputs", result);
                break;
            }

            ++output_count;
            DXGI_OUTPUT_DESC output_description{};
            output->GetDesc(&output_description);

            ComPtr<IDXGIOutput1> output1;
            result = output.As(&output1);
            if (FAILED(result)) {
                PrintFailure(L"IDXGIOutput::QueryInterface(IDXGIOutput1)", result);
                continue;
            }

            ComPtr<IDXGIOutputDuplication> duplication;
            result = output1->DuplicateOutput(device.Get(), &duplication);
            if (FAILED(result)) {
                std::wcerr << L"DuplicateOutput skipped " << output_description.DeviceName
                           << L": 0x" << std::hex << static_cast<unsigned long>(result)
                           << std::dec << L'\n';
                continue;
            }

            DXGI_OUTDUPL_FRAME_INFO frame_info{};
            ComPtr<IDXGIResource> desktop_resource;
            result = duplication->AcquireNextFrame(1500, &frame_info, &desktop_resource);
            if (FAILED(result)) {
                std::wcerr << L"AcquireNextFrame skipped " << output_description.DeviceName
                           << L": 0x" << std::hex << static_cast<unsigned long>(result)
                           << std::dec << L'\n';
                continue;
            }

            ComPtr<ID3D11Texture2D> texture;
            result = desktop_resource.As(&texture);
            if (SUCCEEDED(result)) {
                D3D11_TEXTURE2D_DESC description{};
                texture->GetDesc(&description);
                std::wcout << L"Captured " << output_description.DeviceName << L" ("
                           << description.Width << L" x " << description.Height << L")\n";
                ++captured_count;
            } else {
                PrintFailure(L"IDXGIResource::QueryInterface(ID3D11Texture2D)", result);
            }

            duplication->ReleaseFrame();
        }
    }

    std::wcout << L"Desktop Duplication: " << captured_count << L"/" << output_count
               << L" outputs captured.\n";
    return output_count > 0 && captured_count > 0;
}

}  // namespace screenshooter::spikes
