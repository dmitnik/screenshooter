// Explicit desktop smoke test; intentionally not registered with unattended CTest.
#include "editor/editor_window.h"
#include "settings/settings_dialog.h"
#include "localization/strings.h"
#include "platform/com_apartment.h"
#include <commctrl.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

using namespace screenshooter;
namespace {
int failures = 0;
HWND editor_window = nullptr;
bool save_dialog = false;
void Check(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::wstring Caption(HWND window) {
    std::wstring value(static_cast<std::size_t>(::GetWindowTextLengthW(window)) + 1, L'\0');
    ::GetWindowTextW(window, value.data(), static_cast<int>(value.size()));
    value.resize(value.size() - 1);
    return value;
}
HWND Child(HWND parent, int id) {
    struct Search { int id; HWND result; } search{id, nullptr};
    ::EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        if (::GetDlgCtrlID(child) == search.id) { search.result = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
void Measure(HWND control, int padding, const wchar_t* override_text = nullptr, bool multiline = false) {
    RECT bounds{};
    ::GetClientRect(control, &bounds);
    const UINT dpi = ::GetDpiForWindow(control);
    const int available = bounds.right - ::MulDiv(padding, dpi, 96);
    HDC dc = ::GetDC(control);
    auto old = ::SelectObject(dc, reinterpret_cast<HFONT>(::SendMessageW(control, WM_GETFONT, 0, 0)));
    const auto caption = Caption(control);
    RECT measured{0, 0, available, 0};
    ::DrawTextW(dc, override_text != nullptr ? override_text : caption.c_str(), -1, &measured,
        DT_CALCRECT | DT_NOPREFIX | (multiline ? DT_WORDBREAK : DT_SINGLELINE));
    if (measured.right > available || measured.bottom > bounds.bottom) {
        std::wcerr << L"Clipped: " << caption << L" available=" << available << L"x" << bounds.bottom
            << L" measured=" << measured.right << L"x" << measured.bottom << L'\n';
        Check(false, "caption fits its actual control");
    }
    ::SelectObject(dc, old);
    ::ReleaseDC(control, dc);
}
void Snapshot(HWND window, const std::wstring& name) {
    ::SetWindowDisplayAffinity(window, WDA_NONE); // Only this test process's windows.
    RECT bounds{};
    ::GetWindowRect(window, &bounds);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    HDC dc = ::GetDC(window), memory = ::CreateCompatibleDC(dc);
    HBITMAP bitmap = ::CreateCompatibleBitmap(dc, width, height);
    auto old = ::SelectObject(memory, bitmap);
    ::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    Check(::PrintWindow(window, memory, 2) != FALSE, "window snapshot rendered");
    ::SelectObject(memory, old);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    std::vector<char> pixels(static_cast<std::size_t>(width) * height * 4);
    Check(::GetDIBits(dc, bitmap, 0, height, pixels.data(), &info, DIB_RGB_COLORS) != 0, "snapshot pixels read");
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42; header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
    std::filesystem::create_directories(L"out/localization-check");
    std::ofstream file(std::filesystem::path(L"out/localization-check") / (name + L".bmp"), std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
    file.write(pixels.data(), static_cast<std::streamsize>(pixels.size()));
    Check(file.good(), "snapshot saved");
    ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(window, dc);
}

void CheckEditor() {
    Check(Caption(Child(editor_window, 3102)) == i18n::Text(i18n::Id::rectangle), "open editor translated");
    for (int id : {3101, 3102, 3103, 3104, 3106, 3108, 3109, 3110, 3111}) Measure(Child(editor_window, id), 50);
    Measure(Child(editor_window, 3202), 40);
    Measure(Child(editor_window, 3002), 48);
    Measure(Child(editor_window, 3001), 48, i18n::Text(i18n::Id::copy), true);
    Measure(Child(editor_window, 3231), 8);
}
void CALLBACK ExerciseDialog(HWND, UINT, UINT_PTR timer, DWORD) {
    ::KillTimer(nullptr, timer);
    HWND dialog = ::FindWindowW(L"Screenshooter.Settings", nullptr);
    Check(dialog != nullptr, "settings dialog opened");
    if (dialog == nullptr) return;
    const auto title = Caption(editor_window);
    ::SendDlgItemMessageW(dialog, 3005, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"D:\\Test folder"));
    ::SendDlgItemMessageW(dialog, 3002, HKM_SETHOTKEY, MAKEWORD('K', HOTKEYF_ALT), 0);
    for (int language : {1, 0, 1}) {
        ::SendDlgItemMessageW(dialog, 3010, CB_SETCURSEL, language, 0);
        ::SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(3010, CBN_SELCHANGE),
            reinterpret_cast<LPARAM>(::GetDlgItem(dialog, 3010)));
        Check(i18n::CurrentLanguage() == (language == 1 ? i18n::Language::russian : i18n::Language::english), "language switches immediately");
        Check(Caption(dialog) == i18n::Text(i18n::Id::settings_title), "settings title translated");
        Check(Caption(::GetDlgItem(dialog, 3008)) == i18n::Text(i18n::Id::save), "save caption translated");
        for (int id : {3020, 3021, 3022, 3023, 3024, 3025}) Measure(::GetDlgItem(dialog, id), 0);
        for (int id : {3006, 3008, 3009}) Measure(::GetDlgItem(dialog, id), 16);
        for (int id : {3001, 3007}) Measure(::GetDlgItem(dialog, id), 24);
        CheckEditor();
        Snapshot(dialog, language == 1 ? L"settings-ru" : L"settings-en");
        Snapshot(editor_window, language == 1 ? L"editor-ru" : L"editor-en");
        Check(Caption(editor_window) == title, "screenshot identity unchanged");
        Check(Caption(::GetDlgItem(dialog, 3005)) == L"D:\\Test folder", "pending folder preserved");
        Check(::SendDlgItemMessageW(dialog, 3002, HKM_GETHOTKEY, 0, 0) == MAKEWORD('K', HOTKEYF_ALT), "pending hotkey preserved");
    }
    Check(::GetNextDlgTabItem(dialog, ::GetDlgItem(dialog, 3010), FALSE) == ::GetDlgItem(dialog, 3008), "Tab moves from language to Save");
    ::SendMessageW(dialog, WM_COMMAND, save_dialog ? 3008 : 3009, 0);
}
void CheckScaledLabels() {
    for (int dpi : {96, 120, 144, 192}) {
        HDC dc = ::GetDC(nullptr);
        HFONT font = ::CreateFontW(-::MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
            FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        auto old = ::SelectObject(dc, font);
        for (auto language : {i18n::Language::english, i18n::Language::russian}) {
            RECT text{0, 0, ::MulDiv(137, dpi, 96), 0};
            ::DrawTextW(dc, i18n::Text(i18n::Id::rectangle, language), -1, &text, DT_CALCRECT | DT_SINGLELINE);
            Check(text.right <= ::MulDiv(137, dpi, 96), "longest tool caption fits at 100/125/150/200 percent");
            RECT copy{0, 0, ::MulDiv(156, dpi, 96), 0};
            ::DrawTextW(dc, i18n::Text(i18n::Id::copy, language), -1, &copy, DT_CALCRECT | DT_WORDBREAK);
            Check(copy.bottom <= ::MulDiv(54, dpi, 96), "copy caption fits at 100/125/150/200 percent");
        }
        ::SelectObject(dc, old); ::DeleteObject(font); ::ReleaseDC(nullptr, dc);
    }
}
}
int main() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
    ::InitCommonControlsEx(&controls);
    auto editor = editor::EditorWindow::Create(::GetModuleHandleW(nullptr), nullptr,
        editor::CreateSession(capture::CreateOpaqueBuffer(640, 480)));
    if (!editor) return 2;
    editor_window = editor->window();
    settings::Settings settings;
    CheckEditor();
    CheckScaledLabels();
    for (bool save : {false, true}) {
        save_dialog = save;
        ::SetTimer(nullptr, 0, 100, ExerciseDialog);
        const auto result = settings::ShowSettingsDialog(::GetModuleHandleW(nullptr), editor_window, settings);
        Check(result.has_value() == save, "Save and Cancel semantics");
        Check(i18n::CurrentLanguage() == i18n::Language::english, "preview restored before application commit");
        if (result) {
            Check(result->language == i18n::Language::russian, "saved result contains language");
            i18n::SetLanguage(result->language);
            CheckEditor();
        }
    }
    // Exercise the existing compact layout with both languages.
    ::SetWindowPos(editor_window, nullptr, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER);
    for (auto language : {i18n::Language::english, i18n::Language::russian}) {
        i18n::SetLanguage(language); CheckEditor();
    }
    ::DestroyWindow(editor_window);
    std::cout << "Localization desktop smoke: " << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}
