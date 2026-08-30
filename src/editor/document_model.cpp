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
    if (annotation.kind == AnnotationKind::line ||
        annotation.kind == AnnotationKind::arrow) {
        return DistanceToSegment(point, annotation.start, annotation.end) <=
            std::max(kTolerance, annotation.thickness + 3);
    }
    if (annotation.kind == AnnotationKind::curved_arrow && !annotation.points.empty()) {
        platform::PixelPoint previous = annotation.start;
        for (int step = 1; step <= 24; ++step) {
            const double t = step / 24.0;
            const double inverse = 1.0 - t;
            const platform::PixelPoint current{
                static_cast<std::int32_t>(std::lround(
                    (inverse * inverse * annotation.start.x) +
                    (2.0 * inverse * t * annotation.points.front().x) +
                    (t * t * annotation.end.x))),
                static_cast<std::int32_t>(std::lround(
                    (inverse * inverse * annotation.start.y) +
                    (2.0 * inverse * t * annotation.points.front().y) +
                    (t * t * annotation.end.y))),
            };
            if (DistanceToSegment(point, previous, current) <=
                std::max(kTolerance, annotation.thickness + 3)) {
                return true;
            }
            previous = current;
        }
        return false;
    }
    if (annotation.kind == AnnotationKind::marker) {
        for (std::size_t index = 1; index < annotation.points.size(); ++index) {
            if (DistanceToSegment(point, annotation.points[index - 1],
                    annotation.points[index]) <=
                std::max(kTolerance, annotation.thickness + 3)) {
                return true;
            }
        }
        return false;
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
    if (annotation.kind == AnnotationKind::text ||
        annotation.kind == AnnotationKind::image) {
        return platform::Contains(outer, point);
    }
    return platform::Contains(outer, point) &&
        (inner.empty() || !platform::Contains(inner, point));
}

}  // namespace

DocumentModel::DocumentModel(capture::PixelBuffer source)
{
    state_.source = std::make_shared<const capture::PixelBuffer>(std::move(source));
    state_.crop = {0, 0, state_.source->width, state_.source->height};
    clean_state_ = state_;
}

void DocumentModel::Restore(DocumentState state) {
    if (state.source == nullptr) {
        return;
    }
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
    for (auto& point : annotation->points) {
        point.x += delta.x;
        point.y += delta.y;
    }
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

void DocumentModel::SetCrop(const platform::PixelRect crop) noexcept {
    const auto& image = source();
    const platform::PixelRect source_bounds{0, 0, image.width, image.height};
    const auto clipped = platform::Intersect(crop, source_bounds);
    if (!clipped.empty()) {
        state_.crop = clipped;
    }
}

bool DocumentModel::ApplyCrop() {
    const auto& image = source();
    const platform::PixelRect full{0, 0, image.width, image.height};
    const auto applied = platform::Intersect(state_.crop, full);
    if (applied.empty()) {
        return false;
    }
    if (applied == full) {
        state_.crop = full;
        state_.selected.reset();
        return true;
    }
    auto cropped = capture::Crop(image, applied);
    if (cropped.empty()) {
        return false;
    }
    const platform::PixelPoint shift{-applied.left, -applied.top};
    for (auto& annotation : state_.annotations) {
        annotation.start.x += shift.x;
        annotation.start.y += shift.y;
        annotation.end.x += shift.x;
        annotation.end.y += shift.y;
        for (auto& point : annotation.points) {
            point.x += shift.x;
            point.y += shift.y;
        }
    }
    state_.source = std::make_shared<const capture::PixelBuffer>(std::move(cropped));
    state_.crop = {0, 0, state_.source->width, state_.source->height};
    state_.selected.reset();
    return true;
}

bool DocumentModel::dirty() const noexcept {
    return state_.source != clean_state_.source ||
        state_.annotations != clean_state_.annotations ||
        state_.crop != clean_state_.crop;
}

}  // namespace screenshooter::editor
