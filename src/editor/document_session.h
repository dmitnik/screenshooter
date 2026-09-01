#pragma once

#include "editor/document_model.h"

#include <cstdint>
#include <string>

namespace screenshooter::editor {

struct DocumentSession {
    std::uint64_t id{};
    std::wstring screenshot_name;
    DocumentModel model;
};

[[nodiscard]] DocumentSession CreateSession(capture::PixelBuffer image);

}  // namespace screenshooter::editor
