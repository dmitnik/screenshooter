#pragma once

#include "editor/document_model.h"

namespace screenshooter::editor {

class DocumentRenderer final {
public:
    [[nodiscard]] static capture::PixelBuffer Render(
        const DocumentModel& model,
        bool apply_crop = true
    );
};

}  // namespace screenshooter::editor
