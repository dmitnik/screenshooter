#include "app/info_dialog.h"

namespace screenshooter::app {
namespace {

constexpr wchar_t kWindowClass[] = L"Screenshooter.Information";
constexpr int kCloseControl = 4001;
constexpr wchar_t kInstructions[] =
    L"БЫСТРЫЙ СТАРТ\r\n"
    L"Откройте меню значка Screenshooter в трее, выберите режим захвата, "
    L"отредактируйте снимок и нажмите «Копировать» или «Сохранить».\r\n\r\n"
    L"РЕЖИМЫ ЗАХВАТА\r\n"
    L"Доступны снимок области, окна под курсором, монитора под курсором и всех мониторов. "
    L"Те же действия можно запускать настроенными глобальными горячими клавишами.\r\n\r\n"
    L"ИНСТРУМЕНТЫ РЕДАКТОРА\r\n"
    L"Прямоугольник, эллипс, линия, стрелка, маркер, текст, размытие, обрезка и выбор. "
    L"Для фигур и текста доступны красный, зелёный и синий цвета. Для текста также "
    L"можно выбрать размер, полужирное, курсивное и подчёркнутое начертание.\r\n\r\n"
    L"SHIFT\r\n"
    L"Shift превращает прямоугольник в квадрат и эллипс в круг, фиксирует линию и стрелку "
    L"с шагом 45°, выпрямляет маркер и сохраняет пропорции вставленного изображения. "
    L"Shift можно нажать или отпустить прямо во время изменения размера.\r\n\r\n"
    L"ВЫДЕЛЕНИЕ И ИСТОРИЯ\r\n"
    L"Выделенный объект можно перемещать, изменять за управляющие точки и удалять клавишами "
    L"Delete, Backspace или Esc. Ctrl+Z отменяет действие, Ctrl+Y повторяет его.\r\n\r\n"
    L"БУФЕР ОБМЕНА И ФАЙЛЫ\r\n"
    L"Ctrl+C копирует выделенный объект, а без выделения копирует готовый снимок и закрывает окно. "
    L"Ctrl+X вырезает объект, Ctrl+V вставляет объект или изображение из системного буфера. "
    L"Изображение также можно перетащить в редактор.\r\n\r\n"
    L"СОХРАНЕНИЕ\r\n"
    L"«Сохранить» открывает системный диалог PNG/JPEG. Папка по умолчанию, горячие клавиши "
    L"и автозапуск настраиваются кнопкой с шестерёнкой или пунктом «Настройки…» в трее.";

LRESULT CALLBACK InfoProcedure(
    const HWND window,
    const UINT message,
    const WPARAM w_param,
    const LPARAM l_param
) {
    if (message == WM_CREATE) {
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            ::GetWindowLongPtrW(window, GWLP_HINSTANCE));
        const UINT dpi = ::GetDpiForWindow(window);
        const auto scaled = [dpi](const int value) { return ::MulDiv(value, dpi, 96); };
        HWND text = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kInstructions,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
                ES_AUTOVSCROLL | ES_READONLY,
            scaled(16), scaled(16), scaled(528), scaled(384),
            window, nullptr, instance, nullptr);
        HWND close = ::CreateWindowExW(0, L"BUTTON", L"Закрыть",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            scaled(444), scaled(414), scaled(100), scaled(30), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseControl)), instance, nullptr);
        const WPARAM font = reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT));
        ::SendMessageW(text, WM_SETFONT, font, TRUE);
        ::SendMessageW(close, WM_SETFONT, font, TRUE);
        return 0;
    }
    if (message == WM_COMMAND &&
        (LOWORD(w_param) == kCloseControl || LOWORD(w_param) == IDCANCEL)) {
        ::DestroyWindow(window);
        return 0;
    }
    if (message == WM_CLOSE) {
        ::DestroyWindow(window);
        return 0;
    }
    return ::DefWindowProcW(window, message, w_param, l_param);
}

}  // namespace

void ShowInfoDialog(const HINSTANCE instance, const HWND owner) {
    WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .lpfnWndProc = InfoProcedure,
        .hInstance = instance,
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kWindowClass,
    };
    if (::RegisterClassExW(&window_class) == 0 &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return;
    }
    const UINT dpi = owner != nullptr ? ::GetDpiForWindow(owner) : 96;
    RECT bounds{0, 0, ::MulDiv(560, dpi, 96), ::MulDiv(460, dpi, 96)};
    ::AdjustWindowRectExForDpi(
        &bounds, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    RECT anchor{};
    if (owner == nullptr || !::IsWindowVisible(owner) ||
        !::GetWindowRect(owner, &anchor) || ::IsRectEmpty(&anchor)) {
        POINT cursor{};
        ::GetCursorPos(&cursor);
        MONITORINFO info{.cbSize = sizeof(MONITORINFO)};
        const HMONITOR monitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        anchor = monitor != nullptr && ::GetMonitorInfoW(monitor, &info)
            ? info.rcWork
            : RECT{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    }
    HWND window = ::CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass,
        L"Информация о Screenshooter", WS_CAPTION | WS_SYSMENU,
        anchor.left + ((anchor.right - anchor.left - width) / 2),
        anchor.top + ((anchor.bottom - anchor.top - height) / 2),
        width, height, owner, nullptr, instance, nullptr);
    if (window == nullptr) {
        return;
    }
    ::SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE);
    const bool disable_owner = owner != nullptr && ::IsWindowEnabled(owner) != FALSE;
    if (disable_owner) {
        ::EnableWindow(owner, FALSE);
    }
    ::ShowWindow(window, SW_SHOW);
    ::SetForegroundWindow(window);
    MSG message{};
    while (::IsWindow(window)) {
        const BOOL received = ::GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) {
            if (received == 0) {
                ::PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        if (!::IsDialogMessageW(window, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
    if (disable_owner && ::IsWindow(owner)) {
        ::EnableWindow(owner, TRUE);
        ::SetForegroundWindow(owner);
    }
}

}  // namespace screenshooter::app
