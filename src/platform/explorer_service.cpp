#include "platform/explorer_service.h"

#include <windows.h>
#include <ole2.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlobj_core.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include <filesystem>
#include <string>
#include <vector>

namespace screenshooter::platform {
namespace {

using Microsoft::WRL::ComPtr;

std::filesystem::path NormalizePath(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

bool SamePath(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) {
    if (left.empty() || right.empty()) {
        return false;
    }
    const auto normalized_left = NormalizePath(left).native();
    const auto normalized_right = NormalizePath(right).native();
    return ::CompareStringOrdinal(
        normalized_left.c_str(), -1,
        normalized_right.c_str(), -1,
        TRUE
    ) == CSTR_EQUAL;
}

std::filesystem::path ExplorerLocation(const ComPtr<IWebBrowser2>& browser) {
    BSTR location_url = nullptr;
    if (FAILED(browser->get_LocationURL(&location_url)) || location_url == nullptr) {
        return {};
    }

    std::vector<wchar_t> decoded(32'768, L'\0');
    DWORD decoded_size = static_cast<DWORD>(decoded.size());
    const HRESULT decoded_result = ::PathCreateFromUrlW(
        location_url,
        decoded.data(),
        &decoded_size,
        0
    );
    ::SysFreeString(location_url);
    if (FAILED(decoded_result)) {
        return {};
    }
    return std::filesystem::path(decoded.data());
}

bool SelectFile(
    const ComPtr<IWebBrowser2>& browser,
    const std::filesystem::path& file_path
) {
    ComPtr<IDispatch> document;
    if (FAILED(browser->get_Document(&document)) || document == nullptr) {
        return false;
    }

    ComPtr<IShellFolderViewDual> folder_view;
    if (FAILED(document.As(&folder_view))) {
        return false;
    }

    ComPtr<Folder> folder;
    if (FAILED(folder_view->get_Folder(&folder)) || folder == nullptr) {
        return false;
    }

    const auto file_name = file_path.filename().native();
    BSTR name = ::SysAllocString(file_name.c_str());
    if (name == nullptr) {
        return false;
    }
    ComPtr<FolderItem> item;
    const HRESULT parsed = folder->ParseName(name, &item);
    ::SysFreeString(name);
    if (FAILED(parsed) || item == nullptr) {
        return false;
    }

    VARIANT item_variant{};
    item_variant.vt = VT_DISPATCH;
    item_variant.pdispVal = item.Get();
    return SUCCEEDED(folder_view->SelectItem(
        &item_variant,
        SVSI_DESELECTOTHERS | SVSI_SELECT | SVSI_FOCUSED | SVSI_ENSUREVISIBLE
    ));
}

bool ActivateExplorer(const ComPtr<IWebBrowser2>& browser) {
    SHANDLE_PTR browser_handle = 0;
    if (FAILED(browser->get_HWND(&browser_handle)) || browser_handle == 0) {
        return false;
    }
    const auto window = reinterpret_cast<HWND>(browser_handle);
    if (::IsIconic(window)) {
        ::ShowWindow(window, SW_RESTORE);
    }

    const HWND foreground = ::GetForegroundWindow();
    const DWORD current_thread = ::GetCurrentThreadId();
    const DWORD foreground_thread = foreground == nullptr
        ? 0
        : ::GetWindowThreadProcessId(foreground, nullptr);
    const DWORD explorer_thread = ::GetWindowThreadProcessId(window, nullptr);
    const bool attached_foreground = foreground_thread != 0 &&
        foreground_thread != current_thread &&
        ::AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE;
    const bool attached_explorer = explorer_thread != 0 &&
        explorer_thread != current_thread &&
        explorer_thread != foreground_thread &&
        ::AttachThreadInput(current_thread, explorer_thread, TRUE) != FALSE;

    ::BringWindowToTop(window);
    ::SetActiveWindow(window);
    ::SetFocus(window);
    static_cast<void>(::SetForegroundWindow(window));
    const bool activated = ::GetForegroundWindow() == window;

    if (attached_explorer) {
        ::AttachThreadInput(current_thread, explorer_thread, FALSE);
    }
    if (attached_foreground) {
        ::AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
    return activated;
}

bool RevealInExistingExplorer(const std::filesystem::path& file_path) {
    ComPtr<IShellWindows> shell_windows;
    if (FAILED(::CoCreateInstance(
            CLSID_ShellWindows,
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&shell_windows)))) {
        return false;
    }

    long count = 0;
    if (FAILED(shell_windows->get_Count(&count))) {
        return false;
    }
    const auto folder_path = file_path.parent_path();
    for (long index = 0; index < count; ++index) {
        VARIANT item_index{};
        item_index.vt = VT_I4;
        item_index.lVal = index;
        ComPtr<IDispatch> dispatch;
        if (FAILED(shell_windows->Item(item_index, &dispatch)) || dispatch == nullptr) {
            continue;
        }
        ComPtr<IWebBrowser2> browser;
        if (FAILED(dispatch.As(&browser)) ||
            !SamePath(ExplorerLocation(browser), folder_path)) {
            continue;
        }
        if (SelectFile(browser, file_path)) {
            static_cast<void>(ActivateExplorer(browser));
            return true;
        }
    }
    return false;
}

bool OpenExplorerAndSelectFile(const std::filesystem::path& file_path) {
    PIDLIST_ABSOLUTE item = nullptr;
    if (FAILED(::SHParseDisplayName(
            file_path.c_str(),
            nullptr,
            &item,
            0,
            nullptr)) || item == nullptr) {
        return false;
    }
    const HRESULT shown = ::SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ::CoTaskMemFree(item);
    return SUCCEEDED(shown);
}

}  // namespace

bool RevealFileInExplorer(const std::filesystem::path& file_path) {
    if (file_path.empty()) {
        return false;
    }
    const auto normalized = NormalizePath(file_path);
    ::SHChangeNotify(
        SHCNE_CREATE | SHCNE_UPDATEITEM,
        SHCNF_PATHW | SHCNF_FLUSH,
        normalized.c_str(),
        nullptr
    );
    if (RevealInExistingExplorer(normalized)) {
        return true;
    }
    return OpenExplorerAndSelectFile(normalized);
}

}  // namespace screenshooter::platform
