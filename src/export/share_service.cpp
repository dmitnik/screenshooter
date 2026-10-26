#include "localization/strings.h"
#include "export/share_service.h"

#include "export/image_file_service.h"

#include <shcore.h>
#include <shobjidl_core.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>

#include <chrono>
#include <filesystem>
#include <system_error>

namespace screenshooter::exporting {
namespace {

std::filesystem::path ShareDirectory() {
    return std::filesystem::temp_directory_path() / L"Screenshooter" / L"Share";
}

std::filesystem::path NewSharePath() {
    return ShareDirectory() /
        (L"share-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
         std::to_wstring(::GetTickCount64()) + L".png");
}

}  // namespace

struct ShareService::Impl {
    winrt::Windows::ApplicationModel::DataTransfer::DataTransferManager manager{nullptr};
    winrt::event_token token{};
    winrt::Windows::Storage::Streams::RandomAccessStreamReference reference{nullptr};
    std::filesystem::path path;

    void Reset() {
        if (manager != nullptr && token.value != 0) {
            manager.DataRequested(token);
        }
        token = {};
        manager = nullptr;
        reference = nullptr;
    }

    ~Impl() {
        Reset();
    }
};

ShareService::ShareService() : impl_(std::make_unique<Impl>()) {}
ShareService::~ShareService() = default;

bool ShareService::Show(const HWND owner, const capture::PixelBuffer& image) {
    try {
        using namespace winrt::Windows::ApplicationModel::DataTransfer;
        using namespace winrt::Windows::Storage::Streams;

        impl_->Reset();
        std::error_code error;
        std::filesystem::create_directories(ShareDirectory(), error);
        if (error) {
            return false;
        }
        impl_->path = NewSharePath();
        if (!ImageFileService::Save(impl_->path, image, ImageFormat::png)) {
            return false;
        }

        Microsoft::WRL::ComPtr<IStream> file_stream;
        if (FAILED(::SHCreateStreamOnFileEx(
                impl_->path.c_str(),
                STGM_READ | STGM_SHARE_DENY_WRITE,
                FILE_ATTRIBUTE_NORMAL,
                FALSE,
                nullptr,
                &file_stream))) {
            return false;
        }
        IRandomAccessStream random_stream{nullptr};
        winrt::check_hresult(::CreateRandomAccessStreamOverStream(
            file_stream.Get(),
            BSOS_DEFAULT,
            winrt::guid_of<IRandomAccessStream>(),
            winrt::put_abi(random_stream)
        ));
        impl_->reference = RandomAccessStreamReference::CreateFromStream(random_stream);

        const auto interop = winrt::get_activation_factory<
            DataTransferManager,
            IDataTransferManagerInterop
        >();
        winrt::check_hresult(interop->GetForWindow(
            owner,
            winrt::guid_of<DataTransferManager>(),
            winrt::put_abi(impl_->manager)
        ));
        const auto reference = impl_->reference;
        impl_->token = impl_->manager.DataRequested(
            [reference](const DataTransferManager&, const DataRequestedEventArgs& event) {
                auto package = event.Request().Data();
                package.Properties().Title(i18n::Text(i18n::Id::share_title));
                package.SetBitmap(reference);
            }
        );
        winrt::check_hresult(interop->ShowShareUIForWindow(owner));
        return true;
    } catch (const winrt::hresult_error&) {
        impl_->Reset();
        return false;
    }
}

void ShareService::CleanupOldFiles() {
    std::error_code error;
    const auto directory = ShareDirectory();
    if (!std::filesystem::exists(directory, error) || error) {
        return;
    }
    const auto cutoff = std::filesystem::file_time_type::clock::now() -
        std::chrono::hours(24);
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error) && !error &&
            entry.path().extension() == L".png" &&
            entry.last_write_time(error) < cutoff && !error) {
            std::filesystem::remove(entry.path(), error);
            error.clear();
        }
    }
}

}  // namespace screenshooter::exporting
