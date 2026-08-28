#include "editor/document_model.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace screenshooter::editor {
namespace {

double DistanceToSegment(
    const platform::PixelPoint point,
    const platform::PixelPoint start,
    const platform::PixelPoint end
) {
    const double dx = static_cast<double>(end.x - start.x);
    const double dy = static_cast<double>(end.y - start.y);
    if (dx == 0.0 && dy == 0.0) {
        return std::hypot(point.x - start.x, point.y - start.y);
    }
    const double projection = std::clamp(
        ((point.x - start.x) * dx + (point.y - start.y) * dy) /
            ((dx * dx) + (dy * dy)),
        0.0,
        1.0
    );
    return std::hypot(
        point.x - (start.x + (projection * dx)),
        point.y - (start.y + (projection * dy))
    );
}

bool Hits(const Annotation& annotation, const platform::PixelPoint point) {
    constexpr std::int32_t kTolerance = 7;
    if (annotation.kind == AnnotationKind::line) {
        return DistanceToSegment(point, annotation.start, annotation.end) <=
            std::max(kTolerance, annotation.thickness + 3);
    }
    const auto bounds = platform::Normalize(annotation.start, annotation.end);
    const platform::PixelRect outer{
        bounds.left - kTolerance,
        bounds.top - kTolerance,
        bounds.right + kTolerance + 1,
        bounds.bottom + kTolerance + 1,
    };
    const platform::PixelRect inner{
        bounds.left + kTolerance,
        bounds.top + kTolerance,
        bounds.right - kTolerance,
        bounds.bottom - kTolerance,
    };
    return platform::Contains(outer, point) &&
        (inner.empty() || !platform::Contains(inner, point));
}

}  // namespace

DocumentModel::DocumentModel(capture::PixelBuffer source)
    : source_(std::move(source)) {
    state_.crop = {0, 0, source_.width, source_.height};
    clean_state_ = state_;
}

void DocumentModel::Restore(DocumentState state) {
    state_ = std::move(state);
    for (const auto& annotation : state_.annotations) {
        next_id_ = std::max(next_id_, annotation.id + 1);
    }
}

AnnotationId DocumentModel::Add(Annotation annotation) {
    annotation.id = next_id_++;
    state_.annotations.push_back(annotation);
    state_.selected = annotation.id;
    return annotation.id;
}

bool DocumentModel::Remove(const AnnotationId id) {
    const auto position = std::find_if(
        state_.annotations.begin(),
        state_.annotations.end(),
        [id](const Annotation& annotation) { return annotation.id == id; }
    );
    if (position == state_.annotations.end()) {
        return false;
    }
    state_.annotations.erase(position);
    if (state_.selected == id) {
        state_.selected.reset();
    }
    return true;
}

bool DocumentModel::Move(const AnnotationId id, const platform::PixelPoint delta) {
    Annotation* annotation = Find(id);
    if (annotation == nullptr) {
        return false;
    }
    annotation->start.x += delta.x;
    annotation->start.y += delta.y;
    annotation->end.x += delta.x;
    annotation->end.y += delta.y;
    return true;
}

Annotation* DocumentModel::Find(const AnnotationId id) noexcept {
    const auto position = std::find_if(
        state_.annotations.begin(),
        state_.annotations.end(),
        [id](const Annotation& annotation) { return annotation.id == id; }
    );
    return position == state_.annotations.end() ? nullptr : &*position;
}

const Annotation* DocumentModel::Find(const AnnotationId id) const noexcept {
    const auto position = std::find_if(
        state_.annotations.begin(),
        state_.annotations.end(),
        [id](const Annotation& annotation) { return annotation.id == id; }
    );
    return position == state_.annotations.end() ? nullptr : &*position;
}

std::optional<AnnotationId> DocumentModel::HitTest(
    const platform::PixelPoint point
) const {
    for (auto position = state_.annotations.rbegin();
         position != state_.annotations.rend(); ++position) {
        if (Hits(*position, point)) {
            return position->id;
        }
    }
    return std::nullopt;
}

void DocumentModel::Select(const std::optional<AnnotationId> id) noexcept {
    state_.selected = id;
}

bool DocumentModel::dirty() const noexcept {
    return state_.annotations != clean_state_.annotations ||
        state_.crop != clean_state_.crop;
}

}  // namespace screenshooter::editor
