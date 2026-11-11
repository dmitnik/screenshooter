// Explicit desktop regression test; intentionally excluded from unattended CTest.
#include "editor/editor_window.h"
#include "platform/com_apartment.h"

#include <commctrl.h>
#include <windowsx.h>

#include <iostream>
#include <cmath>
#include <vector>

using namespace screenshooter;
namespace {
int failures{};

void Check(bool value, const char* message) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void Pump() {
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message != WM_QUIT) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
}

HWND Child(HWND parent, int id) {
    struct Search { int id; HWND found; } search{id, nullptr};
    ::EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        if (::GetDlgCtrlID(child) == search.id) {
            search.found = child;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

void Click(HWND editor, int id) {
    HWND button = Child(editor, id);
    ::SetFocus(button);
    Pump(); // A real user can pause between focus transfer and button activation.
    ::SendMessageW(button, BM_CLICK, 0, 0);
    Pump();
}

void Drag(HWND editor, POINT start, POINT end) {
    ::SendMessageW(editor, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x, start.y));
    ::SendMessageW(editor, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end.x, end.y));
    ::SendMessageW(editor, WM_LBUTTONUP, 0, MAKELPARAM(end.x, end.y));
    Pump();
}

std::vector<COLORREF> Region(HWND window, RECT region) {
    ::SetWindowDisplayAffinity(window, WDA_NONE); // Only this test's own window.
    ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RECT bounds{};
    ::GetWindowRect(window, &bounds);
    POINT origin{};
    ::ClientToScreen(window, &origin);
    HDC dc = ::GetDC(window), memory = ::CreateCompatibleDC(dc);
    HBITMAP bitmap = ::CreateCompatibleBitmap(dc, bounds.right - bounds.left, bounds.bottom - bounds.top);
    auto previous = ::SelectObject(memory, bitmap);
    Check(::PrintWindow(window, memory, 2) != FALSE, "editor rendered for comparison");
    std::vector<COLORREF> pixels;
    for (int y = region.top; y < region.bottom; ++y) {
        for (int x = region.left; x < region.right; ++x) {
            pixels.push_back(::GetPixel(memory, x + origin.x - bounds.left, y + origin.y - bounds.top));
        }
    }
    ::SelectObject(memory, previous);
    ::DeleteObject(bitmap);
    ::DeleteDC(memory);
    ::ReleaseDC(window, dc);
    return pixels;
}

auto CreateEditor() {
    auto editor = editor::EditorWindow::Create(::GetModuleHandleW(nullptr), nullptr,
        editor::CreateSession(capture::CreateOpaqueBuffer(640, 480)));
    if (editor) {
        ::SetWindowPos(editor->window(), nullptr, 0, 0, 1100, 850, SWP_NOMOVE | SWP_NOZORDER);
        Pump();
        // Set a deterministic viewport even if the zoom button is scrolled out of view.
        ::SendMessageW(editor->window(), WM_COMMAND, 3232, 0);
        Pump();
    }
    return editor;
}

POINT CanvasCenter(HWND window) {
    RECT client{};
    ::GetClientRect(window, &client);
    return {(client.right - ::MulDiv(220, static_cast<int>(::GetDpiForWindow(window)), 96)) / 2,
        client.bottom / 2};
}

void CheckRepeatedDrawing() {
    for (int tool : {3102, 3104, 3103, 3106, 3108}) {
        auto editor = CreateEditor();
        Check(editor != nullptr, "figure editor created");
        if (!editor) return;
        HWND window = editor->window();
        const auto center = CanvasCenter(window);
        Click(window, tool);
        Drag(window, {center.x - 220, center.y - 100}, {center.x - 140, center.y - 40});
        RECT second{center.x + 30, center.y - 110, center.x + 140, center.y - 20};
        const auto before = Region(window, second);
        Drag(window, {center.x + 40, center.y - 100}, {center.x + 120, center.y - 40});
        // A free click with Select removes handles, so only actual drawn content can pass.
        Click(window, 3101);
        Drag(window, {center.x, center.y + 150}, {center.x, center.y + 150});
        Check(before != Region(window, second), "second gesture draws a figure without a clearing click");
        ::DestroyWindow(window);
        Pump();
    }
}

void CheckSelection(HWND edit) {
    DWORD start{}, end{};
    ::SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    Check(start == 1 && end == 4, "text selection survives property changes");
}

void CheckMarkerHeight() {
    auto editor = CreateEditor();
    Check(editor != nullptr, "marker height editor created");
    if (!editor) return;
    const HWND window = editor->window();
    const auto center = CanvasCenter(window);
    Click(window, 3108);
    ::SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON | MK_SHIFT,
        MAKELPARAM(center.x - 140, center.y));
    ::SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON | MK_SHIFT,
        MAKELPARAM(center.x + 140, center.y + 30));
    ::SendMessageW(window, WM_LBUTTONUP, MK_SHIFT,
        MAKELPARAM(center.x + 140, center.y + 30));
    Pump();
    const RECT band{center.x - 70, center.y - 50, center.x + 70, center.y + 50};
    const auto initial = Region(window, band);
    Drag(window, {center.x + 3, center.y - 14}, {center.x + 3, center.y - 14});
    Check(initial == Region(window, band), "grip click without movement preserves marker thickness");
    Drag(window, {center.x, center.y - 17}, {center.x, center.y - 37});
    const auto taller = Region(window, band);
    Check(initial != taller, "top grip increases straight marker height");
    BYTE original_keys[256]{};
    ::GetKeyboardState(original_keys);
    BYTE keys[256]{};
    keys[VK_CONTROL] = 0x80;
    ::SetKeyboardState(keys);
    ::SendMessageW(window, WM_KEYDOWN, 'Z', 0);
    Check(initial == Region(window, band), "undo restores original marker height");
    ::SendMessageW(window, WM_KEYDOWN, 'Y', 0);
    Check(taller == Region(window, band), "redo restores increased marker height");
    ::SetKeyboardState(original_keys);
    ::SetFocus(window);
    ::SendMessageW(window, WM_KEYDOWN, VK_ADD, 0);
    Check(taller != Region(window, band), "plus increases selected marker height");
    ::SendMessageW(window, WM_KEYDOWN, VK_SUBTRACT, 0);
    Check(taller == Region(window, band), "minus restores previous marker height");
    keys[VK_CONTROL] = 0;
    keys[VK_SHIFT] = 0x80;
    ::SetKeyboardState(keys);
    ::SendMessageW(window, WM_KEYDOWN, VK_ADD, 0);
    ::SetKeyboardState(original_keys);
    for (int step = 0; step < 10; ++step) {
        ::SendMessageW(window, WM_KEYDOWN, VK_SUBTRACT, 0);
    }
    Check(taller == Region(window, band), "Shift plus increases marker height by ten pixels");
    Drag(window, {center.x, center.y + 37}, {center.x, center.y + 47});
    Check(taller != Region(window, band), "bottom grip increases marker height");
    ::DestroyWindow(window);
    Pump();
}

void CheckTextProperties() {
    auto editor = CreateEditor();
    Check(editor != nullptr, "text editor created");
    if (!editor) return;
    HWND window = editor->window();
    const auto center = CanvasCenter(window);
    const POINT origin{center.x - 200, center.y - 150};
    Click(window, 3109);
    Drag(window, origin, origin);
    HWND edit = ::FindWindowExW(window, nullptr, L"EDIT", nullptr);
    Check(edit != nullptr, "text field opened");
    if (edit == nullptr) { ::DestroyWindow(window); return; }
    LOGFONTW initial_font{};
    ::GetObjectW(reinterpret_cast<HFONT>(::SendMessageW(edit, WM_GETFONT, 0, 0)),
        sizeof(initial_font), &initial_font);
    ::SetWindowTextW(edit, L"Sample text");
    ::SendMessageW(edit, EM_SETSEL, 1, 4);
    Click(window, 3213);
    Check(::IsWindow(edit) && ::GetFocus() == edit, "color keeps field open and returns focus");
    HDC dc = ::GetDC(edit);
    ::SendMessageW(window, WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(edit));
    Check(::GetTextColor(dc) == RGB(30, 136, 229), "new color is visible in text field");
    ::ReleaseDC(edit, dc);
    CheckSelection(edit);
    for (int style : {3222, 3223, 3224}) Click(window, style);
    Check(::IsWindow(edit) && ::GetFocus() == edit, "text styles keep field open");
    CheckSelection(edit);
    HWND combo = Child(window, 3221);
    ::SetFocus(combo);
    Pump();
    ::SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
    Pump();
    Check(::IsWindow(edit), "size dropdown keeps text editing session");
    ::SendMessageW(combo, CB_SETCURSEL, 3, 0);
    ::SendMessageW(window, WM_COMMAND, MAKEWPARAM(3221, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
    LOGFONTW font{};
    ::GetObjectW(reinterpret_cast<HFONT>(::SendMessageW(edit, WM_GETFONT, 0, 0)), sizeof(font), &font);
    const auto live_height = font.lfHeight;
    Check(std::abs(live_height - std::lround(initial_font.lfHeight * 40.0 / 24.0)) <= 1 &&
        font.lfWeight == FW_BOLD && font.lfItalic && font.lfUnderline,
        "size and all styles update live for whole text object");
    Check(::SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != FALSE,
        "live preview does not close the size list");
    ::SendMessageW(combo, WM_KEYDOWN, VK_RETURN, 0);
    Pump();
    Check(::IsWindow(edit) && ::GetFocus() == edit, "closing size list returns to text");
    CheckSelection(edit);
    Click(window, 3106);
    Check(::FindWindowExW(window, nullptr, L"EDIT", nullptr) == nullptr, "tool change commits text");
    ::SendMessageW(window, WM_LBUTTONDBLCLK, 0, MAKELPARAM(origin.x + 10, origin.y + 10));
    Pump();
    edit = ::FindWindowExW(window, nullptr, L"EDIT", nullptr);
    Check(edit != nullptr, "existing text reopens");
    if (edit != nullptr) {
        Check(::IsWindowVisible(Child(window, 3222)) != FALSE,
            "text properties stay visible when reopening from another tool");
        ::GetObjectW(reinterpret_cast<HFONT>(::SendMessageW(edit, WM_GETFONT, 0, 0)), sizeof(font), &font);
        Check(font.lfHeight == live_height && font.lfWeight == FW_BOLD && font.lfItalic && font.lfUnderline,
            "committed text retains formatting");
        Click(window, 3222);
        ::GetObjectW(reinterpret_cast<HFONT>(::SendMessageW(edit, WM_GETFONT, 0, 0)), sizeof(font), &font);
        Check(::IsWindow(edit) && font.lfWeight == FW_NORMAL, "existing text style updates live too");
        Drag(window, {center.x + 200, center.y + 100}, {center.x + 200, center.y + 100});
        Check(::FindWindowExW(window, nullptr, L"EDIT", nullptr) == nullptr, "canvas click commits text");
    }
    ::DestroyWindow(window);
    Pump();
}
}

int main() {
    USEROBJECTFLAGS station{};
    DWORD needed{};
    if (!::GetUserObjectInformationW(::GetProcessWindowStation(), UOI_FLAGS, &station, sizeof(station), &needed) ||
        (station.dwFlags & WSF_VISIBLE) == 0) {
        std::cerr << "Interactive desktop required\n";
        return 2;
    }
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    ::InitCommonControlsEx(&controls);
    CheckRepeatedDrawing();
    CheckMarkerHeight();
    CheckTextProperties();
    std::cout << "Editor interaction desktop smoke: " << (failures == 0 ? "PASS" : "FAIL")
        << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
