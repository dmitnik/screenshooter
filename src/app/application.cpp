#include "localization/strings.h"
#include "app/application.h"

#include "app/info_dialog.h"

#include "app/messages.h"
#include "capture/capture_service.h"
#include "editor/document_session.h"
#include "export/share_service.h"
#include "export/image_file_service.h"
#include "settings/settings_dialog.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shobjidl_core.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <memory>
#include <utility>
#include <vector>

namespace screenshooter::app {
namespace {

constexpr wchar_t kHostWindowClass[] = L"Screenshooter.HostWindow";
constexpr wchar_t kApplicationName[] = L"Screenshooter";
constexpr int kCaptureHotkey = 1001;
constexpr int kMonitorHotkey = 1003;
constexpr int kWindowHotkey = 1004;

}  // namespace

Application::Application(const HINSTANCE instance) noexcept
    : instance_(instance) {}

Application::~Application() {
    UnregisterHotkeys();
    tray_.Shutdown();
}

int Application::Run(const int) {
    exporting::ShareService::CleanupOldFiles();
    settings_ = settings_store_.Load();
    i18n::SetLanguage(settings_.language);
    INITCOMMONCONTROLSEX controls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS,
    };
    ::InitCommonControlsEx(&controls);

    taskbar_created_message_ = ::RegisterWindowMessageW(L"TaskbarCreated");
    if (!RegisterHostWindowClass() || !CreateHostWindow()) {
        ::MessageBoxW(nullptr, i18n::Text(i18n::Id::startup_error),
            kApplicationName, MB_OK | MB_ICONERROR);
        return 1;
    }
    if (!tray_.Initialize(host_window_, messages::kTrayCallback)) {
        ::MessageBoxW(nullptr, i18n::Text(i18n::Id::tray_error),
            kApplicationName, MB_OK | MB_ICONERROR);
        ::DestroyWindow(host_window_);
        return 1;
    }
    if (!RegisterConfiguredHotkeys(settings_)) {
        ::MessageBoxW(nullptr,
            i18n::Text(i18n::Id::hotkeys_busy),
            kApplicationName,
            MB_OK | MB_ICONWARNING);
    }

    int argument_count = 0;
    PWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(), &argument_count);
    if (arguments != nullptr) {
        if (argument_count > 1) {
            auto image = exporting::ImageFileService::Load(arguments[1]);
            if (image.has_value()) {
                CreateEditor(std::move(*image));
            }
        }
        ::LocalFree(arguments);
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

bool Application::RegisterConfiguredHotkeys(const settings::Settings& settings) {
    if (!settings.hotkeys_enabled) {
        return true;
    }
    const struct HotkeyDefinition {
        int id;
        settings::HotkeyBinding binding;
    } definitions[]{
        {kCaptureHotkey, settings.area_hotkey},
        {kMonitorHotkey, settings.monitor_hotkey},
        {kWindowHotkey, settings.window_hotkey},
    };
    for (const auto& definition : definitions) {
        if (!::RegisterHotKey(host_window_, definition.id,
                definition.binding.modifiers | MOD_NOREPEAT,
                definition.binding.virtual_key)) {
            UnregisterHotkeys();
            return false;
        }
        hotkey_ids_.push_back(definition.id);
    }
    return true;
}

void Application::UnregisterHotkeys() {
    if (host_window_ != nullptr) {
        for (const int id : hotkey_ids_) {
            ::UnregisterHotKey(host_window_, id);
        }
    }
    hotkey_ids_.clear();
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

void Application::StartCapture(const CaptureRequest request) {
    if (exiting_ || capture_state_ != CaptureState::idle) {
        return;
    }
    capture_request_ = request;
    requested_bounds_.reset();
    if (request == CaptureRequest::monitor) {
        POINT cursor{};
        ::GetCursorPos(&cursor);
        MONITORINFO monitor{.cbSize = sizeof(MONITORINFO)};
        if (::GetMonitorInfoW(::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST),
                &monitor)) {
            requested_bounds_ = platform::PixelRect{
                monitor.rcMonitor.left, monitor.rcMonitor.top,
                monitor.rcMonitor.right, monitor.rcMonitor.bottom};
        }
    } else if (request == CaptureRequest::window) {
        POINT cursor{};
        ::GetCursorPos(&cursor);
        HWND target = ::GetAncestor(::WindowFromPoint(cursor), GA_ROOT);
        DWORD process_id = 0;
        ::GetWindowThreadProcessId(target, &process_id);
        if (target == nullptr || process_id == ::GetCurrentProcessId() ||
            !::IsWindowVisible(target) || ::IsIconic(target)) {
            ::MessageBoxW(nullptr, i18n::Text(i18n::Id::no_window),
                kApplicationName, MB_OK | MB_ICONINFORMATION);
            return;
        }
        RECT bounds{};
        if (FAILED(::DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS,
                &bounds, sizeof(bounds)))) {
            ::GetWindowRect(target, &bounds);
        }
        requested_bounds_ = platform::PixelRect{
            bounds.left, bounds.top, bounds.right, bounds.bottom};
    }
    capture_state_ = CaptureState::capturing;
    capture_timer_.Restart();
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
    performance::LogMetric(L"capture ready", capture_timer_.ElapsedMilliseconds());
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
            : i18n::Text(i18n::Id::capture_unknown);
        ::MessageBoxW(nullptr, details, kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }

    if (capture_request_ != CaptureRequest::area) {
        auto frame = std::move(*outcome->frame);
        capture::PixelBuffer image;
        if (capture_request_ == CaptureRequest::all || !requested_bounds_.has_value()) {
            image = std::move(frame.pixels);
        } else {
            const auto selected = platform::Intersect(*requested_bounds_, frame.virtual_bounds);
            image = capture::Crop(frame.pixels, {
                selected.left - frame.virtual_bounds.left,
                selected.top - frame.virtual_bounds.top,
                selected.right - frame.virtual_bounds.left,
                selected.bottom - frame.virtual_bounds.top,
            });
        }
        capture_state_ = CaptureState::idle;
        if (!image.empty()) {
            CreateEditor(std::move(image));
        }
        return;
    }
    overlay_.reset(overlay::SelectionOverlay::Create(
        instance_, host_window_, std::move(*outcome->frame)
    ));
    if (overlay_ == nullptr) {
        capture_state_ = CaptureState::idle;
        ::MessageBoxW(nullptr, i18n::Text(i18n::Id::selection_error),
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
        ::MessageBoxW(nullptr, i18n::Text(i18n::Id::editor_error),
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
        {i18n::Text(i18n::Id::images), L"*.png;*.jpg;*.jpeg;*.bmp;*.webp"},
        {i18n::Text(i18n::Id::all_files), L"*.*"},
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
        ::MessageBoxW(nullptr, i18n::Text(i18n::Id::unsupported_image),
            kApplicationName, MB_OK | MB_ICONERROR);
    }
}

void Application::OpenSaveFolder() {
    if (settings_.save_folder.empty()) {
        ConfigureSettings(host_window_);
    }
    if (!settings_.save_folder.empty()) {
        const auto result = reinterpret_cast<INT_PTR>(::ShellExecuteW(
            nullptr, L"open", settings_.save_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) {
            ::MessageBoxW(nullptr, i18n::Text(i18n::Id::folder_error),
                kApplicationName, MB_OK | MB_ICONERROR);
        }
    }
}

void Application::ConfigureSettings(const HWND owner) {
    const auto proposed = settings::ShowSettingsDialog(
        instance_, owner != nullptr ? owner : host_window_, settings_);
    if (!proposed.has_value()) {
        return;
    }
    const settings::Settings previous = settings_;
    UnregisterHotkeys();
    if (!RegisterConfiguredHotkeys(*proposed)) {
        static_cast<void>(RegisterConfiguredHotkeys(previous));
        ::MessageBoxW(owner,
            i18n::Text(i18n::Id::hotkey_busy),
            kApplicationName, MB_OK | MB_ICONWARNING);
        return;
    }
    const bool autostart_changed =
        proposed->autostart_enabled != previous.autostart_enabled;
    if (autostart_changed &&
        !settings_store_.SetAutostart(proposed->autostart_enabled)) {
        static_cast<void>(settings_store_.SetAutostart(previous.autostart_enabled));
        UnregisterHotkeys();
        static_cast<void>(RegisterConfiguredHotkeys(previous));
        ::MessageBoxW(owner, i18n::Text(i18n::Id::autostart_error),
            kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }
    if (!settings_store_.Save(*proposed)) {
        UnregisterHotkeys();
        static_cast<void>(RegisterConfiguredHotkeys(previous));
        if (autostart_changed) {
            static_cast<void>(settings_store_.SetAutostart(previous.autostart_enabled));
        }
        static_cast<void>(settings_store_.Save(previous));
        ::MessageBoxW(owner, i18n::Text(i18n::Id::settings_error),
            kApplicationName, MB_OK | MB_ICONERROR);
        return;
    }
    settings_ = *proposed;
    i18n::SetLanguage(settings_.language);
}

void Application::HandleTrayCommand(const tray::TrayCommand command) {
    switch (command) {
    case tray::TrayCommand::capture_area:
        StartCapture(CaptureRequest::area);
        break;
    case tray::TrayCommand::capture_window:
        StartCapture(CaptureRequest::window);
        break;
    case tray::TrayCommand::capture_monitor:
        StartCapture(CaptureRequest::monitor);
        break;
    case tray::TrayCommand::capture_all:
        StartCapture(CaptureRequest::all);
        break;
    case tray::TrayCommand::open_image:
        OpenImage();
        break;
    case tray::TrayCommand::open_save_folder:
        OpenSaveFolder();
        break;
    case tray::TrayCommand::configure_settings:
        ConfigureSettings(host_window_);
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
    UnregisterHotkeys();
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
    case messages::kShowInfo:
        ShowInfoDialog(instance_, reinterpret_cast<HWND>(w_param));
        return 0;
    case messages::kShowSettings:
        ConfigureSettings(reinterpret_cast<HWND>(w_param));
        return 0;
    case WM_HOTKEY:
        if (w_param == kCaptureHotkey) {
            StartCapture(CaptureRequest::area);
        } else if (w_param == kMonitorHotkey) {
            StartCapture(CaptureRequest::monitor);
        } else if (w_param == kWindowHotkey) {
            StartCapture(CaptureRequest::window);
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_DISPLAYCHANGE:
        if (overlay_ != nullptr) {
            overlay_->Cancel();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (w_param == PBT_APMSUSPEND && overlay_ != nullptr) {
            overlay_->Cancel();
        }
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
