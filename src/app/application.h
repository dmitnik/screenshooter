#pragma once

#include "editor/editor_window.h"
#include "overlay/selection_overlay.h"
#include "tray/tray_host.h"
#include "settings/settings_store.h"
#include "performance/metrics.h"

#include <windows.h>

#include <memory>
#include <thread>
#include <unordered_map>
#include <optional>
#include <vector>

namespace screenshooter::app {

class Application final {
public:
    explicit Application(HINSTANCE instance) noexcept;
    ~Application();

    [[nodiscard]] int Run(int show_command);

private:
    enum class CaptureState { idle, capturing, selecting };
    enum class CaptureRequest { area, window, monitor, all };

    [[nodiscard]] bool RegisterHostWindowClass();
    [[nodiscard]] bool CreateHostWindow();
    void StartCapture(CaptureRequest request = CaptureRequest::area);
    void HandleCaptureReady(WPARAM payload);
    void HandleSelectionComplete(WPARAM payload);
    void HandleTrayCommand(tray::TrayCommand command);
    void OpenImage();
    void ChooseSaveFolder();
    void OpenSaveFolder();
    void ConfigureHotkeys();
    void ToggleAutostart();
    [[nodiscard]] bool RegisterConfiguredHotkeys(const settings::Settings& settings);
    void UnregisterHotkeys();
    void CreateEditor(capture::PixelBuffer image);
    void ExitApplication();

    static LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_{};
    HWND host_window_{};
    UINT taskbar_created_message_{};
    tray::TrayHost tray_;
    settings::SettingsStore settings_store_;
    settings::Settings settings_;
    CaptureState capture_state_{CaptureState::idle};
    CaptureRequest capture_request_{CaptureRequest::area};
    std::optional<platform::PixelRect> requested_bounds_;
    std::jthread capture_worker_;
    performance::MetricTimer capture_timer_;
    std::unique_ptr<overlay::SelectionOverlay> overlay_;
    std::unordered_map<HWND, std::unique_ptr<editor::EditorWindow>> editors_;
    std::vector<int> hotkey_ids_;
    bool exiting_{};
};

}  // namespace screenshooter::app
