#pragma once

#include "capture/pixel_buffer.h"
#include "platform/geometry.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace screenshooter::editor {

using AnnotationId = std::uint64_t;

enum class AnnotationKind {
    rectangle,
    line,
    ellipse,
    arrow,
    marker,
    text,
    image,
    blur,
};

[[nodiscard]] platform::PixelPoint DefaultArrowStart(
    platform::PixelPoint end,
    platform::PixelRect bounds
) noexcept;

struct Annotation {
    AnnotationId id{};
    AnnotationKind kind{AnnotationKind::rectangle};
    platform::PixelPoint start{};
    platform::PixelPoint end{};
    std::uint32_t color{0xFFE53935};
    std::int32_t thickness{4};
    std::vector<platform::PixelPoint> points;
    std::wstring text;
    std::shared_ptr<const capture::PixelBuffer> image;

    friend bool operator==(const Annotation&, const Annotation&) = default;
};

struct DocumentState {
    std::shared_ptr<const capture::PixelBuffer> source;
    std::vector<Annotation> annotations;
    platform::PixelRect crop{};
    std::optional<AnnotationId> selected;

    friend bool operator==(const DocumentState&, const DocumentState&) = default;
};

class DocumentModel final {
public:
    explicit DocumentModel(capture::PixelBuffer source);

    [[nodiscard]] const capture::PixelBuffer& source() const noexcept {
        return *state_.source;
    }
    [[nodiscard]] const DocumentState& state() const noexcept { return state_; }
    [[nodiscard]] DocumentState Snapshot() const { return state_; }
    void Restore(DocumentState state);

    [[nodiscard]] AnnotationId Add(Annotation annotation);
    [[nodiscard]] bool Remove(AnnotationId id);
    [[nodiscard]] bool Move(AnnotationId id, platform::PixelPoint delta);
    [[nodiscard]] Annotation* Find(AnnotationId id) noexcept;
    [[nodiscard]] const Annotation* Find(AnnotationId id) const noexcept;
    [[nodiscard]] std::optional<AnnotationId> HitTest(platform::PixelPoint point) const;
    void Select(std::optional<AnnotationId> id) noexcept;
    void SetCrop(platform::PixelRect crop) noexcept;
    [[nodiscard]] bool ApplyCrop();

    [[nodiscard]] bool dirty() const noexcept;
    void MarkClean() { clean_state_ = state_; }

private:
    DocumentState state_;
    DocumentState clean_state_;
    AnnotationId next_id_{1};
};

}  // namespace screenshooter::editor
