#pragma once

#include <windows.h>
#include <shellapi.h>

namespace screenshooter::tray {

enum class TrayCommand {
    none,
    capture_area,
    open_image,
    exit_application,
};

class TrayHost final {
public:
    ~TrayHost();

    [[nodiscard]] bool Initialize(HWND owner, UINT callback_message);
    void Restore();
    void Shutdown();
    [[nodiscard]] TrayCommand HandleCallback(LPARAM event_data) const;
    [[nodiscard]] TrayCommand ShowContextMenu() const;

private:
    HWND owner_{};
    NOTIFYICONDATAW data_{};
    bool added_{};
};

}  // namespace screenshooter::tray
