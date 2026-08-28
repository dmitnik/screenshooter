#pragma once

#include "editor/editor_window.h"
#include "overlay/selection_overlay.h"
#include "tray/tray_host.h"

#include <windows.h>

#include <memory>
#include <thread>
#include <unordered_map>

namespace screenshooter::app {

class Application final {
public:
    explicit Application(HINSTANCE instance) noexcept;
    ~Application();

    [[nodiscard]] int Run(int show_command);

private:
    enum class CaptureState { idle, capturing, selecting };

    [[nodiscard]] bool RegisterHostWindowClass();
    [[nodiscard]] bool CreateHostWindow();
    void StartCapture();
    void HandleCaptureReady(WPARAM payload);
    void HandleSelectionComplete(WPARAM payload);
    void HandleTrayCommand(tray::TrayCommand command);
    void ExitApplication();

    static LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_{};
    HWND host_window_{};
    UINT taskbar_created_message_{};
    tray::TrayHost tray_;
    CaptureState capture_state_{CaptureState::idle};
    std::jthread capture_worker_;
    std::unique_ptr<overlay::SelectionOverlay> overlay_;
    std::unordered_map<HWND, std::unique_ptr<editor::EditorWindow>> editors_;
    bool hotkey_registered_{};
    bool exiting_{};
};

}  // namespace screenshooter::app
