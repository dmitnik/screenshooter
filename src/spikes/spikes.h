#pragma once

#include <filesystem>

namespace screenshooter::spikes {

[[nodiscard]] bool RunDesktopDuplicationSpike();
[[nodiscard]] bool RunWindowCaptureInteropSpike();
[[nodiscard]] bool RunShareInteropSpike(bool show_ui);
[[nodiscard]] bool RunGaussianBlurSpike(const std::filesystem::path& output_path);

}  // namespace screenshooter::spikes
