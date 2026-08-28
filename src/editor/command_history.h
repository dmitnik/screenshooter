#pragma once

#include "editor/document_model.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace screenshooter::editor {

class CommandHistory final {
public:
    explicit CommandHistory(std::size_t limit = 100) : limit_(limit) {}

    void Commit(
        DocumentState before,
        DocumentState after,
        std::optional<std::uint64_t> merge_key = std::nullopt
    );
    [[nodiscard]] bool Undo(DocumentModel& model);
    [[nodiscard]] bool Redo(DocumentModel& model);
    [[nodiscard]] std::size_t undo_size() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redo_size() const noexcept { return redo_.size(); }

private:
    struct Entry {
        DocumentState before;
        DocumentState after;
        std::optional<std::uint64_t> merge_key;
    };

    std::size_t limit_;
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
};

}  // namespace screenshooter::editor
