#pragma once

#include <windows.h>
#include <objbase.h>

namespace screenshooter::platform {

class ComApartment final {
public:
    explicit ComApartment(const DWORD flags) noexcept
        : result_(::CoInitializeEx(nullptr, flags)) {}

    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            ::CoUninitialize();
        }
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    ComApartment(ComApartment&&) = delete;
    ComApartment& operator=(ComApartment&&) = delete;

    [[nodiscard]] HRESULT result() const noexcept { return result_; }

private:
    HRESULT result_;
};

}  // namespace screenshooter::platform
