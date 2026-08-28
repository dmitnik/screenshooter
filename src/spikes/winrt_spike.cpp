#include "spikes/spikes.h"

#include <windows.h>
#include <shobjidl_core.h>
#include <windows.graphics.capture.interop.h>

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/base.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

namespace screenshooter::spikes {
namespace {

class ScratchWindow final {
public:
    ScratchWindow() {
        window_ = ::CreateWindowExW(
            WS_EX_TOOLWINDOW,
            L"STATIC",
            L"Screenshooter interop spike",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            480,
            320,
            nullptr,
            nullptr,
            ::GetModuleHandleW(nullptr),
            nullptr
        );
    }

    ~ScratchWindow() {
        if (window_ != nullptr) {
            ::DestroyWindow(window_);
        }
    }

    ScratchWindow(const ScratchWindow&) = delete;
    ScratchWindow& operator=(const ScratchWindow&) = delete;

    [[nodiscard]] HWND get() const noexcept { return window_; }

private:
    HWND window_{};
};

void PrintWinrtFailure(const wchar_t* operation, const winrt::hresult_error& error) {
    std::wcerr << operation << L" failed: 0x" << std::hex
               << static_cast<unsigned long>(error.code().value) << std::dec
               << L" (" << error.message().c_str() << L")\n";
}

}  // namespace

bool RunWindowCaptureInteropSpike() {
    try {
        using namespace winrt::Windows::Graphics::Capture;

        if (!GraphicsCaptureSession::IsSupported()) {
            std::wcerr << L"Windows.Graphics.Capture is not supported.\n";
            return false;
        }

        ScratchWindow window;
        if (window.get() == nullptr) {
            std::wcerr << L"CreateWindowExW failed: " << ::GetLastError() << L'\n';
            return false;
        }

        ::ShowWindow(window.get(), SW_SHOWNOACTIVATE);
        ::UpdateWindow(window.get());

        const auto interop = winrt::get_activation_factory<
            GraphicsCaptureItem,
            IGraphicsCaptureItemInterop
        >();

        GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(interop->CreateForWindow(
            window.get(),
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item)
        ));

        const auto size = item.Size();
        std::wcout << L"GraphicsCaptureItem created for HWND (" << size.Width
                   << L" x " << size.Height << L").\n";
        return size.Width > 0 && size.Height > 0;
    } catch (const winrt::hresult_error& error) {
        PrintWinrtFailure(L"Window capture interop", error);
        return false;
    }
}

bool RunShareInteropSpike(const bool show_ui) {
    try {
        using namespace winrt::Windows::ApplicationModel::DataTransfer;

        ScratchWindow window;
        if (window.get() == nullptr) {
            std::wcerr << L"CreateWindowExW failed: " << ::GetLastError() << L'\n';
            return false;
        }

        const auto interop = winrt::get_activation_factory<
            DataTransferManager,
            IDataTransferManagerInterop
        >();

        DataTransferManager manager{nullptr};
        winrt::check_hresult(interop->GetForWindow(
            window.get(),
            winrt::guid_of<DataTransferManager>(),
            winrt::put_abi(manager)
        ));

        const auto data_requested_token = manager.DataRequested(
            [](const DataTransferManager&, const DataRequestedEventArgs& event) {
                DataPackage package = event.Request().Data();
                package.Properties().Title(L"Screenshooter interop spike");
                package.SetText(L"Windows Share interop is available.");
            }
        );

        if (show_ui) {
            ::ShowWindow(window.get(), SW_SHOWNOACTIVATE);
            winrt::check_hresult(interop->ShowShareUIForWindow(window.get()));

            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline) {
                MSG message{};
                while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    ::TranslateMessage(&message);
                    ::DispatchMessageW(&message);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        manager.DataRequested(data_requested_token);
        std::wcout << L"IDataTransferManagerInterop is available"
                   << (show_ui ? L"; ShowShareUIForWindow returned successfully.\n"
                               : L".\n");
        return true;
    } catch (const winrt::hresult_error& error) {
        PrintWinrtFailure(L"Share interop", error);
        return false;
    }
}

}  // namespace screenshooter::spikes
