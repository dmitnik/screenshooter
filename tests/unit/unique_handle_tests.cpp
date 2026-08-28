#include "platform/unique_handle.h"

#include <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <utility>

using screenshooter::platform::UniqueHandle;

TEST_CASE("UniqueHandle closes an owned kernel handle") {
    HANDLE raw = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    REQUIRE(raw != nullptr);

    {
        UniqueHandle handle(raw);
        REQUIRE(handle.get() == raw);
        REQUIRE(static_cast<bool>(handle));
    }

    REQUIRE_FALSE(::SetEvent(raw));
    REQUIRE(::GetLastError() == ERROR_INVALID_HANDLE);
}

TEST_CASE("UniqueHandle transfers ownership by move") {
    HANDLE raw = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    REQUIRE(raw != nullptr);

    UniqueHandle first(raw);
    UniqueHandle second(std::move(first));

    REQUIRE_FALSE(static_cast<bool>(first));
    REQUIRE(second.get() == raw);
}

TEST_CASE("UniqueHandle release returns ownership to the caller") {
    UniqueHandle handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    REQUIRE(static_cast<bool>(handle));

    HANDLE raw = handle.release();
    REQUIRE_FALSE(static_cast<bool>(handle));
    REQUIRE(::CloseHandle(raw));
}
