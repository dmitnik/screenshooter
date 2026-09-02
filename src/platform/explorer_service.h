#pragma once

#include <filesystem>

namespace screenshooter::platform {

[[nodiscard]] bool RevealFileInExplorer(const std::filesystem::path& file_path);

}  // namespace screenshooter::platform
