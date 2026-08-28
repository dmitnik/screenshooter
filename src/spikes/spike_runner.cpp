#include "platform/com_apartment.h"
#include "spikes/spikes.h"

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void PrintUsage() {
    std::wcout
        << L"Usage: screenshooter_spikes [options]\n"
        << L"  --all                 Run all non-interactive spikes\n"
        << L"  --desktop             Test DXGI Desktop Duplication\n"
        << L"  --window              Test CreateForWindow interop\n"
        << L"  --blur <path>         Render and verify a blurred PNG\n"
        << L"  --share               Test DataTransferManager HWND interop\n"
        << L"  --share-ui            Also show the Windows Share UI\n";
}

}  // namespace

int wmain(const int argument_count, wchar_t** arguments) {
    const screenshooter::platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    if (FAILED(apartment.result())) {
        std::wcerr << L"COM initialization failed: 0x" << std::hex
                   << static_cast<unsigned long>(apartment.result()) << L'\n';
        return 1;
    }

    bool run_desktop = false;
    bool run_window = false;
    bool run_share = false;
    bool show_share_ui = false;
    std::filesystem::path blur_output;

    for (int index = 1; index < argument_count; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument == L"--all") {
            run_desktop = true;
            run_window = true;
            run_share = true;
            blur_output = std::filesystem::current_path() / L"blur-spike.png";
        } else if (argument == L"--desktop") {
            run_desktop = true;
        } else if (argument == L"--window") {
            run_window = true;
        } else if (argument == L"--share") {
            run_share = true;
        } else if (argument == L"--share-ui") {
            run_share = true;
            show_share_ui = true;
        } else if (argument == L"--blur" && index + 1 < argument_count) {
            blur_output = arguments[++index];
        } else {
            PrintUsage();
            return 2;
        }
    }

    if (!run_desktop && !run_window && !run_share && blur_output.empty()) {
        PrintUsage();
        return 2;
    }

    std::vector<bool> results;
    if (run_desktop) {
        results.push_back(screenshooter::spikes::RunDesktopDuplicationSpike());
    }
    if (run_window) {
        results.push_back(screenshooter::spikes::RunWindowCaptureInteropSpike());
    }
    if (!blur_output.empty()) {
        results.push_back(screenshooter::spikes::RunGaussianBlurSpike(blur_output));
    }
    if (run_share) {
        results.push_back(screenshooter::spikes::RunShareInteropSpike(show_share_ui));
    }

    for (const bool result : results) {
        if (!result) {
            return 1;
        }
    }
    return 0;
}
