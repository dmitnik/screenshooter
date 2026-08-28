#pragma once

#include <windows.h>

#include <string>

namespace screenshooter::app {

class Application final {
public:
    explicit Application(HINSTANCE instance) noexcept;

    [[nodiscard]] int Run(int show_command);

private:
    [[nodiscard]] bool RegisterMainWindowClass();
    [[nodiscard]] bool CreateMainWindow(int show_command);
    [[nodiscard]] std::wstring LoadResourceString(UINT id) const;

    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param
    );

    LRESULT HandleMessage(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_{};
    HWND main_window_{};
    std::wstring title_;
    std::wstring window_class_;
};

}  // namespace screenshooter::app
