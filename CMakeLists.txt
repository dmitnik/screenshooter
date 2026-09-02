cmake_minimum_required(VERSION 3.28)

project(
    Screenshooter
    VERSION 0.1.22
    DESCRIPTION "Fast native screenshot editor for Windows"
    LANGUAGES CXX RC
)

include(CTest)

option(SCREENSHOOTER_BUILD_TESTS "Build automated tests" ON)
option(SCREENSHOOTER_BUILD_SPIKES "Build technical spike runner" ON)

add_library(screenshooter_core STATIC
    src/capture/capture_service.cpp
    src/capture/pixel_buffer.cpp
    src/editor/command_history.cpp
    src/editor/document_model.cpp
    src/editor/document_renderer.cpp
    src/export/clipboard_service.cpp
    src/export/image_file_service.cpp
    src/export/screenshot_file_name.cpp
    src/export/share_service.cpp
    src/platform/dpi.cpp
    src/platform/geometry.cpp
    src/performance/metrics.cpp
    src/settings/settings_store.cpp
)

target_include_directories(screenshooter_core PUBLIC src)
target_compile_features(screenshooter_core PUBLIC cxx_std_20)
target_link_libraries(screenshooter_core PUBLIC
    d3d11
    dxgi
    gdi32
    msimg32
    ole32
    shlwapi
    user32
    windowscodecs
)

add_executable(screenshooter WIN32
    src/app/application.cpp
    src/app/main.cpp
    src/editor/document_session.cpp
    src/editor/editor_window.cpp
    src/overlay/selection_overlay.cpp
    src/settings/settings_dialog.cpp
    src/tray/tray_host.cpp
    resources/app.rc
)

target_include_directories(screenshooter PRIVATE resources)

target_link_libraries(screenshooter PRIVATE
    screenshooter_core
    comctl32
    d2d1
    d3d11
    dwrite
    dwmapi
    dxgi
    gdi32
    ole32
    runtimeobject
    shell32
    shcore
    user32
    windowscodecs
)

set_target_properties(screenshooter PROPERTIES
    OUTPUT_NAME "screenshooter"
)

if(MSVC)
    target_link_options(screenshooter PRIVATE /MANIFEST:NO)
endif()

function(screenshooter_configure_target target)
    target_compile_definitions(${target} PRIVATE
        NOMINMAX
        STRICT
        UNICODE
        WIN32_LEAN_AND_MEAN
        _UNICODE
        _WIN32_WINNT=0x0A00
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /EHsc
            /MP
            /permissive-
            /utf-8
            /W4
            /WX
            /Zc:__cplusplus
        )
    endif()
endfunction()

screenshooter_configure_target(screenshooter_core)
screenshooter_configure_target(screenshooter)

if(SCREENSHOOTER_BUILD_SPIKES)
    add_executable(screenshooter_spikes
        src/spikes/blur_spike.cpp
        src/spikes/capture_spike.cpp
        src/spikes/spike_runner.cpp
        src/spikes/winrt_spike.cpp
    )

    target_link_libraries(screenshooter_spikes PRIVATE
        screenshooter_core
        d2d1
        d3d11
        dxgi
        dxguid
        ole32
        runtimeobject
        shell32
        user32
        windowscodecs
    )

    screenshooter_configure_target(screenshooter_spikes)
endif()

if(BUILD_TESTING AND SCREENSHOOTER_BUILD_TESTS)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.3
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(Catch2)

    add_executable(unit_tests
        tests/unit/pixel_buffer_tests.cpp
        tests/unit/document_model_tests.cpp
        tests/unit/settings_store_tests.cpp
        tests/unit/dpi_tests.cpp
        tests/unit/geometry_tests.cpp
        tests/unit/screenshot_file_name_tests.cpp
        tests/unit/unique_handle_tests.cpp
    )

    target_link_libraries(unit_tests PRIVATE
        screenshooter_core
        Catch2::Catch2WithMain
    )

    screenshooter_configure_target(unit_tests)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(unit_tests)

    add_executable(integration_tests
        tests/integration/image_file_tests.cpp
        tests/integration/platform_smoke_tests.cpp
    )

    target_link_libraries(integration_tests PRIVATE
        screenshooter_core
        Catch2::Catch2WithMain
        d3d11
        ole32
        windowscodecs
    )

    screenshooter_configure_target(integration_tests)
    catch_discover_tests(integration_tests)
endif()
