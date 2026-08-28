#pragma once

#include <windows.h>

namespace screenshooter::app::messages {

inline constexpr UINT kTrayCallback = WM_APP + 1;
inline constexpr UINT kCaptureReady = WM_APP + 2;
inline constexpr UINT kSelectionComplete = WM_APP + 3;
inline constexpr UINT kSelectionCancelled = WM_APP + 4;
inline constexpr UINT kEditorClosed = WM_APP + 5;

}  // namespace screenshooter::app::messages
