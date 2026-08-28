#pragma once

#include "capture/pixel_buffer.h"

#include <filesystem>
#include <optional>

namespace screenshooter::exporting {

enum class ImageFormat { png, jpeg };

class ImageFileService final {
public:
    [[nodiscard]] static bool Save(
        const std::filesystem::path& path,
        const capture::PixelBuffer& image,
        ImageFormat format,
        float jpeg_quality = 0.92F
    );
    [[nodiscard]] static std::optional<capture::PixelBuffer> Load(
        const std::filesystem::path& path
    );
};

}  // namespace screenshooter::exporting
