# Increment 0 technical spike record

Date: 2026-08-28

Environment:

- Windows x64 interactive desktop session;
- MSVC 19.44.35228;
- Windows SDK 10.0.26100.0;
- CMake 4.4.3;
- one active 1920 x 1080 display.

## Automated checks

Both Debug and Release configurations were configured and built with the Visual
Studio 2022 x64 generator. CTest passed 13 of 13 discovered tests in each
configuration.

The Release GUI executable started, created a main window, accepted
`WM_CLOSE`, and exited with code 0. Its embedded manifest was extracted with
`mt.exe`; it contains `PerMonitorV2` and `asInvoker`.

## Technical spikes

The Release spike runner completed the following checks with exit code 0:

1. DXGI Desktop Duplication captured `\\.\DISPLAY1` at 1920 x 1080. Result:
   1 of 1 enumerated outputs captured.
2. `IGraphicsCaptureItemInterop::CreateForWindow` created a
   `GraphicsCaptureItem` for a visible test HWND with content size 466 x 313.
3. Direct2D `CLSID_D2D1GaussianBlur` rendered into an offscreen D3D11 WARP
   texture. WIC encoded the 128 x 128 BGRA surface to PNG and decoded it back with
   the expected dimensions.
4. `IDataTransferManagerInterop::GetForWindow` returned a manager for a Win32
   HWND. The separate interactive check called `ShowShareUIForWindow` and returned
   success.

## Confirmed fallbacks and limitations

- Desktop Duplication remains a per-output API; multi-output timing must be
  coordinated by the capture layer and cannot be made strictly atomic by DXGI.
- `CreateForWindow` rejects hidden/non-capturable windows, so window capture must
  validate the selected HWND and keep the frozen-desktop crop fallback.
- Share UI availability does not guarantee that a compatible receiving app is
  installed; production UI still needs an empty-target/error path.
- WARP is suitable for deterministic offscreen render tests. The production path
  should prefer hardware and recreate device-dependent resources after device
  loss.
