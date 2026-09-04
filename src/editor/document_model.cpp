#include "editor/document_model.h"

#include <algorithm>
#include <array>
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
        annotation.kind == AnnotationKind::image ||
        annotation.kind == AnnotationKind::blur) {
        return platform::Contains(outer, point);
    }
    return platform::Contains(outer, point) &&
        (inner.empty() || !platform::Contains(inner, point));
}

}  // namespace

platform::PixelPoint DefaultArrowStart(
    const platform::PixelPoint end,
    const platform::PixelRect bounds
) noexcept {
    if (bounds.empty()) {
        return end;
    }

    struct Direction {
        platform::PixelPoint delta;
        std::int32_t available;
    };
    const std::array directions{
        Direction{{-1, 0}, end.x - bounds.left},
        Direction{{1, 0}, bounds.right - 1 - end.x},
        Direction{{0, -1}, end.y - bounds.top},
        Direction{{0, 1}, bounds.bottom - 1 - end.y},
    };
    constexpr std::int32_t kDefaultLength = 20;
    auto chosen = std::find_if(
        directions.begin(), directions.end(),
        [](const Direction& direction) {
            return direction.available >= kDefaultLength;
        });
    if (chosen == directions.end()) {
        chosen = std::max_element(
            directions.begin(), directions.end(),
            [](const Direction& left, const Direction& right) {
                return left.available < right.available;
            });
    }
    const std::int32_t length = std::clamp(chosen->available, 0, kDefaultLength);
    return {
        end.x + (chosen->delta.x * length),
        end.y + (chosen->delta.y * length),
    };
}

platform::PixelPoint ConstrainShapeEnd(
    const AnnotationKind kind,
    const platform::PixelPoint start,
    const platform::PixelPoint end,
    const platform::PixelRect bounds
) noexcept {
    const bool square = kind == AnnotationKind::rectangle ||
        kind == AnnotationKind::ellipse;
    const bool angled = kind == AnnotationKind::line ||
        kind == AnnotationKind::arrow;
    if ((!square && !angled) || bounds.empty() || start == end) {
        return end;
    }

    const std::int32_t dx = end.x - start.x;
    const std::int32_t dy = end.y - start.y;
    const std::int32_t absolute_x = std::abs(dx);
    const std::int32_t absolute_y = std::abs(dy);
    constexpr double kTan22Point5Degrees = 0.41421356237309503;

    if (angled &&
        absolute_y <= static_cast<double>(absolute_x) * kTan22Point5Degrees) {
        return {end.x, start.y};
    }
    if (angled &&
        absolute_x <= static_cast<double>(absolute_y) * kTan22Point5Degrees) {
        return {start.x, end.y};
    }

    const auto available = [](const std::int32_t coordinate,
                              const std::int32_t direction,
                              const std::int32_t lower,
                              const std::int32_t upper) {
        return direction < 0 ? coordinate - lower : upper - 1 - coordinate;
    };
    const auto direction = [](const std::int32_t delta,
                              const std::int32_t coordinate,
                              const std::int32_t lower,
                              const std::int32_t upper) {
        if (delta != 0) {
            return delta < 0 ? -1 : 1;
        }
        return coordinate - lower > upper - 1 - coordinate ? -1 : 1;
    };
    const std::int32_t direction_x = direction(
        dx, start.x, bounds.left, bounds.right);
    const std::int32_t direction_y = direction(
        dy, start.y, bounds.top, bounds.bottom);
    const std::int32_t maximum_side = std::max(0, std::min(
        available(start.x, direction_x, bounds.left, bounds.right),
        available(start.y, direction_y, bounds.top, bounds.bottom)
    ));
    const std::int32_t side = std::min(
        std::max(absolute_x, absolute_y), maximum_side);
    return {
        start.x + (direction_x * side),
        start.y + (direction_y * side),
    };
}

ResizeHandle HitResizeHandle(
    const Annotation& annotation,
    const platform::PixelPoint point,
    const std::int32_t tolerance
) noexcept {
    const auto distance = [point](const platform::PixelPoint handle) {
        return std::max(
            std::abs(point.x - handle.x),
            std::abs(point.y - handle.y)
        );
    };
    const std::int32_t start_distance = distance(annotation.start);
    const std::int32_t end_distance = distance(annotation.end);
    if (start_distance <= tolerance && start_distance <= end_distance) {
        return ResizeHandle::start;
    }
    if (end_distance <= tolerance) {
        return ResizeHandle::end;
    }
    return ResizeHandle::none;
}

bool ResizeAnnotationEndpoint(
    Annotation& annotation,
    const ResizeHandle handle,
    platform::PixelPoint point,
    const platform::PixelRect bounds,
    const bool constrain
) noexcept {
    if (handle == ResizeHandle::none) {
        return false;
    }
    if (constrain && annotation.kind == AnnotationKind::image &&
        annotation.image != nullptr && annotation.image->width > 0 &&
        annotation.image->height > 0) {
        const platform::PixelPoint anchor = handle == ResizeHandle::start
            ? annotation.end
            : annotation.start;
        const platform::PixelPoint previous = handle == ResizeHandle::start
            ? annotation.start
            : annotation.end;
        const auto direction = [](const std::int32_t delta, const std::int32_t fallback) {
            if (delta != 0) {
                return delta < 0 ? -1 : 1;
            }
            return fallback < 0 ? -1 : 1;
        };
        const std::int32_t direction_x = direction(point.x - anchor.x, previous.x - anchor.x);
        const std::int32_t direction_y = direction(point.y - anchor.y, previous.y - anchor.y);
        const auto available = [](const std::int32_t coordinate, const std::int32_t sign,
                                  const std::int32_t lower, const std::int32_t upper) {
            return sign < 0 ? coordinate - lower : upper - 1 - coordinate;
        };
        const double desired_scale = std::max(
            static_cast<double>(std::abs(point.x - anchor.x)) / annotation.image->width,
            static_cast<double>(std::abs(point.y - anchor.y)) / annotation.image->height);
        const double maximum_scale = std::min(
            static_cast<double>(available(anchor.x, direction_x, bounds.left, bounds.right)) /
                annotation.image->width,
            static_cast<double>(available(anchor.y, direction_y, bounds.top, bounds.bottom)) /
                annotation.image->height);
        const double scale = std::max(0.0, std::min(desired_scale, maximum_scale));
        point = {
            anchor.x + direction_x * static_cast<std::int32_t>(
                std::lround(annotation.image->width * scale)),
            anchor.y + direction_y * static_cast<std::int32_t>(
                std::lround(annotation.image->height * scale)),
        };
    } else if (constrain) {
        const platform::PixelPoint anchor = handle == ResizeHandle::start
            ? annotation.end
            : annotation.start;
        point = annotation.kind == AnnotationKind::marker
            ? platform::PixelPoint{point.x, anchor.y}
            : ConstrainShapeEnd(annotation.kind, anchor, point, bounds);
    }
    platform::PixelPoint& endpoint = handle == ResizeHandle::start
        ? annotation.start
        : annotation.end;
    endpoint = point;
    if (annotation.kind == AnnotationKind::marker && !annotation.points.empty()) {
        if (constrain) {
            annotation.points.clear();
            annotation.points.push_back(annotation.start);
            if (annotation.end != annotation.start) {
                annotation.points.push_back(annotation.end);
            }
            return true;
        }
        platform::PixelPoint& marker_endpoint = handle == ResizeHandle::start
            ? annotation.points.front()
            : annotation.points.back();
        marker_endpoint = point;
    }
    return true;
}

void UpdateMarkerStroke(
    Annotation& annotation,
    const platform::PixelPoint end,
    const bool straight
) {
    annotation.end = straight
        ? platform::PixelPoint{end.x, annotation.start.y}
        : end;
    if (straight) {
        annotation.points.clear();
        annotation.points.push_back(annotation.start);
        if (annotation.end != annotation.start) {
            annotation.points.push_back(annotation.end);
        }
        return;
    }
    if (annotation.points.empty()) {
        annotation.points.push_back(annotation.start);
    }
    if (annotation.points.back() != annotation.end) {
        annotation.points.push_back(annotation.end);
    }
}

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
