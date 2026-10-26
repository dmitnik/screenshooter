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

// Per-editor, full-resolution content cache. Selection, crop preview and viewport
// are painted separately. Compare values because Find() permits in-place edits.
class DocumentRenderCache final {
public:
    [[nodiscard]] const capture::PixelBuffer& Render(const DocumentModel& model);

private:
    std::shared_ptr<const capture::PixelBuffer> source_;
    std::vector<Annotation> annotations_;
    capture::PixelBuffer rendered_;
};

}  // namespace screenshooter::editor
