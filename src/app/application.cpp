#include "app/application.h"

#include "platform/dpi.h"
#include "resource.h"

#include <commctrl.h>

#include <array>

namespace screenshooter::app {
namespace {

constexpr COLORREF kBackgroundColor = RGB(245, 247, 250);
constexpr COLORREF kTextColor = RGB(34, 40, 49);

}  // namespace

Application::Application(const HINSTANCE instance) noexcept
    : instance_(instance) {}

int Application::Run(const int show_command) {
    INITCOMMONCONTROLSEX controls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_STANDARD_CLASSES,
    };
    ::InitCommonControlsEx(&controls);

    title_ = LoadResourceString(IDS_APP_TITLE);
    window_class_ = LoadResourceString(IDS_WINDOW_CLASS);

    if (title_.empty() || window_class_.empty() ||
        !RegisterMainWindowClass() || !CreateMainWindow(show_command)) {
        return 1;
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

bool Application::RegisterMainWindowClass() {
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = &Application::WindowProcedure,
        .hInstance = instance_,
        .hIcon = ::LoadIconW(nullptr, IDI_APPLICATION),
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = nullptr,
        .lpszClassName = window_class_.c_str(),
        .hIconSm = ::LoadIconW(nullptr, IDI_APPLICATION),
    };

    return ::RegisterClassExW(&window_class) != 0;
}

bool Application::CreateMainWindow(const int show_command) {
    constexpr int kInitialWidthDip = 720;
    constexpr int kInitialHeightDip = 460;
    const UINT system_dpi = ::GetDpiForSystem();

    main_window_ = ::CreateWindowExW(
        0,
        window_class_.c_str(),
        title_.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        platform::DipToPixels(kInitialWidthDip, system_dpi),
        platform::DipToPixels(kInitialHeightDip, system_dpi),
        nullptr,
        nullptr,
        instance_,
        this
    );

    if (main_window_ == nullptr) {
        return false;
    }

    ::ShowWindow(main_window_, show_command);
    ::UpdateWindow(main_window_);
    return true;
}

std::wstring Application::LoadResourceString(const UINT id) const {
    std::array<wchar_t, 256> buffer{};
    const int length = ::LoadStringW(
        instance_,
        id,
        buffer.data(),
        static_cast<int>(buffer.size())
    );
    return length > 0 ? std::wstring(buffer.data(), static_cast<std::size_t>(length)) : L"";
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

    if (application != nullptr) {
        return application->HandleMessage(window, message, w_param, l_param);
    }
    return ::DefWindowProcW(window, message, w_param, l_param);
}

LRESULT Application::HandleMessage(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    switch (message) {
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(l_param);
        ::SetWindowPos(
            window,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOACTIVATE | SWP_NOZORDER
        );
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC device_context = ::BeginPaint(window, &paint);

        RECT client{};
        ::GetClientRect(window, &client);
        HBRUSH background = ::CreateSolidBrush(kBackgroundColor);
        ::FillRect(device_context, &client, background);
        ::DeleteObject(background);

        ::SetBkMode(device_context, TRANSPARENT);
        ::SetTextColor(device_context, kTextColor);
        const wchar_t* text = L"Screenshooter — технический каркас готов";
        ::DrawTextW(
            device_context,
            text,
            -1,
            &client,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER
        );

        ::EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(window, message, w_param, l_param);
    }
}

}  // namespace screenshooter::app
