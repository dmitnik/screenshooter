#include "editor/document_session.h"

#include <atomic>
#include <utility>

namespace screenshooter::editor {

DocumentSession CreateSession(capture::PixelBuffer image) {
    static std::atomic<std::uint64_t> next_id{1};
    return DocumentSession{next_id.fetch_add(1), std::move(image)};
}

}  // namespace screenshooter::editor
