#include "editor/command_history.h"

#include <utility>

namespace screenshooter::editor {

void CommandHistory::Commit(
    DocumentState before,
    DocumentState after,
    const std::optional<std::uint64_t> merge_key
) {
    if (before == after) {
        return;
    }
    redo_.clear();
    if (merge_key.has_value() && !undo_.empty() &&
        undo_.back().merge_key == merge_key) {
        undo_.back().after = std::move(after);
        return;
    }
    undo_.push_back(Entry{std::move(before), std::move(after), merge_key});
    if (undo_.size() > limit_) {
        undo_.erase(undo_.begin());
    }
}

bool CommandHistory::Undo(DocumentModel& model) {
    if (undo_.empty()) {
        return false;
    }
    Entry entry = std::move(undo_.back());
    undo_.pop_back();
    model.Restore(entry.before);
    redo_.push_back(std::move(entry));
    return true;
}

bool CommandHistory::Redo(DocumentModel& model) {
    if (redo_.empty()) {
        return false;
    }
    Entry entry = std::move(redo_.back());
    redo_.pop_back();
    model.Restore(entry.after);
    undo_.push_back(std::move(entry));
    return true;
}

}  // namespace screenshooter::editor
