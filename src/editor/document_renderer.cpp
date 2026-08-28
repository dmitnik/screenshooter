#include "editor/document_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace screenshooter::editor {
namespace {

void SetPixel(
    capture::PixelBuffer& image,
    const std::int32_t x,
    const std::int32_t y,
    const std::uint32_t color
) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) {
        return;
    }
    std::byte* pixel = image.row(y) + (static_cast<std::size_t>(x) * 4);
    pixel[0] = static_cast<std::byte>(color & 0xFFU);
    pixel[1] = static_cast<std::byte>((color >> 8U) & 0xFFU);
    pixel[2] = static_cast<std::byte>((color >> 16U) & 0xFFU);
    pixel[3] = std::byte{0xFF};
}

void DrawDisk(
    capture::PixelBuffer& image,
    const std::int32_t center_x,
    const std::int32_t center_y,
    const std::int32_t radius,
    const std::uint32_t color
) {
    for (std::int32_t y = -radius; y <= radius; ++y) {
        for (std::int32_t x = -radius; x <= radius; ++x) {
            if ((x * x) + (y * y) <= radius * radius) {
                SetPixel(image, center_x + x, center_y + y, color);
            }
        }
    }
}

void DrawLine(
    capture::PixelBuffer& image,
    platform::PixelPoint start,
    const platform::PixelPoint end,
    const std::int32_t thickness,
    const std::uint32_t color
) {
    const std::int32_t dx = std::abs(end.x - start.x);
    const std::int32_t step_x = start.x < end.x ? 1 : -1;
    const std::int32_t dy = -std::abs(end.y - start.y);
    const std::int32_t step_y = start.y < end.y ? 1 : -1;
    std::int32_t error = dx + dy;
    const std::int32_t radius = std::max(1, thickness / 2);
    while (true) {
        DrawDisk(image, start.x, start.y, radius, color);
        if (start == end) {
            break;
        }
        const std::int32_t doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            start.x += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            start.y += step_y;
        }
    }
}

void DrawAnnotation(capture::PixelBuffer& image, const Annotation& annotation) {
    if (annotation.kind == AnnotationKind::line) {
        DrawLine(image, annotation.start, annotation.end,
            annotation.thickness, annotation.color);
        return;
    }
    const auto bounds = platform::Normalize(annotation.start, annotation.end);
    DrawLine(image, {bounds.left, bounds.top}, {bounds.right, bounds.top},
        annotation.thickness, annotation.color);
    DrawLine(image, {bounds.right, bounds.top}, {bounds.right, bounds.bottom},
        annotation.thickness, annotation.color);
    DrawLine(image, {bounds.right, bounds.bottom}, {bounds.left, bounds.bottom},
        annotation.thickness, annotation.color);
    DrawLine(image, {bounds.left, bounds.bottom}, {bounds.left, bounds.top},
        annotation.thickness, annotation.color);
}

}  // namespace

capture::PixelBuffer DocumentRenderer::Render(const DocumentModel& model) {
    capture::PixelBuffer output = capture::CreateOpaqueBuffer(
        model.source().width,
        model.source().height
    );
    if (output.empty()) {
        return {};
    }
    std::memcpy(
        output.pixels.data(),
        model.source().pixels.data(),
        std::min(output.pixels.size(), model.source().pixels.size())
    );
    for (const auto& annotation : model.state().annotations) {
        DrawAnnotation(output, annotation);
    }
    return output;
}

}  // namespace screenshooter::editor
