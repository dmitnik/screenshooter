# Screenshooter

Screenshooter is a native, local-only screenshot capture and editing application
for Windows 10 version 1903 and newer. The approved requirements are documented
in `PLAN_WINDOWS_SCREENSHOT_APP.md`; engineering delivery is tracked in
`IMPLEMENTATION_PLAN_WINDOWS_SCREENSHOT_APP.md`.

## Current status

Implementation is in progress. The repository currently contains the foundational
Win32 application, platform primitives, automated tests, and technical spikes
required before the first usable capture flow.

## Prerequisites

- Windows 10 version 1903 or newer, x64;
- Visual Studio Build Tools 2022 with the C++ workload and Windows SDK;
- CMake 3.28 or newer;
- Git, used by CMake to fetch the pinned test-only Catch2 dependency.

The application has no runtime network dependency. Catch2 is fetched only while
configuring test builds and is not linked into `screenshooter.exe`.

## Build

Initialize an x64 Visual Studio developer environment, then run:

```text
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Release artifacts are built with the `release` presets.
