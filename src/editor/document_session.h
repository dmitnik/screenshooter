#pragma once

#include "editor/document_model.h"

#include <cstdint>

namespace screenshooter::editor {

struct DocumentSession {
    std::uint64_t id{};
    DocumentModel model;
};

[[nodiscard]] DocumentSession CreateSession(capture::PixelBuffer image);

}  // namespace screenshooter::editor
