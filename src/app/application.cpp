#include "app/application.h"

#include "app/messages.h"
#include "capture/capture_service.h"
#include "editor/document_session.h"
#include "export/share_service.h"
#include "export/image_file_service.h"

#include <commctrl.h>
#include <shobjidl_core.h>
#include <wrl/client.h>

#include <memory>
#include <utility>
#include <vector>

namespace screenshooter::app {
namespace {

constexpr wchar_t kHostWindowClass[] = L"Screenshooter.HostWindow";
constexpr wchar_t kApplicationName[] = L"Screenshooter";
constexpr int kCaptureHotkey = 1001;

}  // namespace

Application::Application(const HINSTANCE instance) noexcept
    : instance_(instance) {}

Application::~Application() {
    if (hotkey_registered_ && host_window_ != nullptr) {
        ::UnregisterHotKey(host_window_, kCaptureHotkey);
    }
    tray_.Shutdown();
}

int Application::Run(const int) {
    exporting::ShareService::CleanupOldFiles();
    INITCOMMONCONTROLSEX controls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_STANDARD_CLASSES,
    };
    ::InitCommonControlsEx(&controls);

    taskbar_created_message_ = ::RegisterWindowMessageW(L"TaskbarCreated");
    if (!RegisterHostWindowClass() || !CreateHostWindow()) {
        ::MessageBoxW(nullptr, L"Не удалось запустить приложение.",
            kApplicationName, MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!tray_.Initialize(host_window_, messages::kTrayCallback)) {
        ::MessageBoxW(nullptr, L"Не удалось создать значок в области уведомлений.",
            kApplicationName, MB_OK | MB_ICONERROR);
        ::DestroyWindow(host_window_);
        return 1;
    }
    hotkey_registered_ = ::RegisterHotKey(
        host_window_, kCaptureHotkey, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, '1'
    ) != FALSE;
    if (!hotkey_registered_) {
        ::MessageBoxW(nullptr,
            L"Сочетание Ctrl+Shift+1 занято другим приложением.\n"
            L"Снимок можно запустить через значок Screenshooter в трее.",
            kApplicationName,
            MB_OK | MB_ICONWARNING);
    }

    MSG message{};
    while (true) {
        const BOOL result = ::GetMessageW(&message, nullptr, 0, 0);
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        if (result == -1) {
            return 1;
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

bool Application::RegisterHostWindowClass() {
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = &Application::WindowProcedure,
        .hInstance = instance_,
        .lpszClassName = kHostWindowClass,
    };
    return ::RegisterClassExW(&window_class) != 0 ||
        ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool Application::CreateHostWindow() {
    host_window_ = ::CreateWindowExW(
        0,
        kHostWindowClass,
        kApplicationName,
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance_,
        this
    );
    return host_window_ != nullptr;
}

void Application::StartCapture() {
    if (exiting_ || capture_state_ != CaptureState::idle) {
        return;
    }
    capture_state_ = CaptureState::capturing;
    const HWND receiver = host_window_;
    capture_worker_ = std::jthread([receiver] {
        capture::CaptureService service;
        auto outcome = std::make_unique<capture::CaptureOutcome>(
            service.CaptureVirtualDesktop()
        );
        capture::CaptureOutcome* payload = outcome.release();
        if (!::PostMessageW(
            receiver,
            messages::kCaptureReady,
            reinterpret_cast<WPARAM>(payload),
            0
        )) {
            delete payload;
        }
    });
}

void Application::HandleCaptureReady(const WPARAM payload) {
    std::unique_ptr<capture::CaptureOutcome> outcome(
        reinterpret_cast<capture::CaptureOutcome*>(payload)
    );
    if (exiting_) {
        return;
    }
    if (outcome == nullptr || !outcome->frame.has_value()) {
        capture_state_ = CaptureState::idle;
        const wchar_t* details = outcome != nullptr && !outcome->error.empty()
            ? outcome->error.c_str()
            : L"Неизвестная ошибка захвата.";
        ::MessageBoxW(nullptr, details, kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }

    overlay_.reset(overlay::SelectionOverlay::Create(
        instance_, host_window_, std::move(*outcome->frame)
    ));
    if (overlay_ == nullptr) {
        capture_state_ = CaptureState::idle;
        ::MessageBoxW(nullptr, L"Не удалось открыть выбор области.",
            kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }
    capture_state_ = CaptureState::selecting;
}

void Application::HandleSelectionComplete(const WPARAM payload) {
    std::unique_ptr<capture::PixelBuffer> image(
        reinterpret_cast<capture::PixelBuffer*>(payload)
    );
    overlay_.reset();
    capture_state_ = CaptureState::idle;
    if (exiting_ || image == nullptr || image->empty()) {
        return;
    }

    CreateEditor(std::move(*image));
}

void Application::CreateEditor(capture::PixelBuffer image) {
    auto editor = editor::EditorWindow::Create(
        instance_, host_window_, editor::CreateSession(std::move(image))
    );
    if (editor == nullptr) {
        ::MessageBoxW(nullptr, L"Не удалось открыть снимок.",
            kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }
    const HWND window = editor->window();
    editors_.emplace(window, std::move(editor));
}

void Application::OpenImage() {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return;
    }
    const COMDLG_FILTERSPEC filters[]{
        {L"Изображения", L"*.png;*.jpg;*.jpeg;*.bmp;*.webp"},
        {L"Все файлы", L"*.*"},
    };
    dialog->SetFileTypes(ARRAYSIZE(filters), filters);
    const HRESULT shown = dialog->Show(host_window_);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return;
    }
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR path = nullptr;
    if (FAILED(shown) || FAILED(dialog->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) {
        return;
    }
    auto image = exporting::ImageFileService::Load(path);
    ::CoTaskMemFree(path);
    if (image.has_value()) {
        CreateEditor(std::move(*image));
    } else {
        ::MessageBoxW(nullptr, L"Формат изображения не поддерживается.",
            kApplicationName, MB_OK | MB_ICONERROR);
    }
}

void Application::HandleTrayCommand(const tray::TrayCommand command) {
    switch (command) {
    case tray::TrayCommand::capture_area:
        StartCapture();
        break;
    case tray::TrayCommand::open_image:
        OpenImage();
        break;
    case tray::TrayCommand::exit_application:
        ExitApplication();
        break;
    case tray::TrayCommand::none:
        break;
    }
}

void Application::ExitApplication() {
    if (exiting_) {
        return;
    }
    exiting_ = true;
    if (hotkey_registered_) {
        ::UnregisterHotKey(host_window_, kCaptureHotkey);
        hotkey_registered_ = false;
    }
    tray_.Shutdown();
    overlay_.reset();

    std::vector<HWND> windows;
    windows.reserve(editors_.size());
    for (const auto& entry : editors_) {
        windows.push_back(entry.first);
    }
    for (const HWND window : windows) {
        ::DestroyWindow(window);
    }
    editors_.clear();
    ::DestroyWindow(host_window_);
}

LRESULT CALLBACK Application::WindowProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    Application* application = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        application = static_cast<Application*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
    } else {
        application = reinterpret_cast<Application*>(
            ::GetWindowLongPtrW(window, GWLP_USERDATA)
        );
    }
    return application != nullptr
        ? application->HandleMessage(window, message, w_param, l_param)
        : ::DefWindowProcW(window, message, w_param, l_param);
}

LRESULT Application::HandleMessage(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    if (message == taskbar_created_message_) {
        tray_.Restore();
        return 0;
    }
    switch (message) {
    case messages::kTrayCallback:
        HandleTrayCommand(tray_.HandleCallback(l_param));
        return 0;
    case messages::kCaptureReady:
        HandleCaptureReady(w_param);
        return 0;
    case messages::kSelectionComplete:
        HandleSelectionComplete(w_param);
        return 0;
    case messages::kSelectionCancelled:
        overlay_.reset();
        capture_state_ = CaptureState::idle;
        return 0;
    case messages::kEditorClosed:
        editors_.erase(reinterpret_cast<HWND>(w_param));
        return 0;
    case WM_HOTKEY:
        if (w_param == kCaptureHotkey) {
            StartCapture();
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w_param != 0) {
            ExitApplication();
        }
        return 0;
    case WM_DESTROY:
        host_window_ = nullptr;
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(window, message, w_param, l_param);
    }
}

}  // namespace screenshooter::app
