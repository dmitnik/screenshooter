#include "editor/document_renderer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

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
    const std::uint32_t alpha = (color >> 24U) & 0xFFU;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const std::uint32_t source = (color >> (channel * 8U)) & 0xFFU;
        const std::uint32_t destination = std::to_integer<std::uint32_t>(pixel[channel]);
        pixel[channel] = static_cast<std::byte>(
            ((source * alpha) + (destination * (255U - alpha))) / 255U
        );
    }
    pixel[3] = std::byte{0xFF};
}

void BlendPixelCoverage(
    capture::PixelBuffer& image,
    const std::int32_t x,
    const std::int32_t y,
    const std::uint32_t color,
    const std::uint8_t coverage
) {
    const std::uint32_t color_alpha = (color >> 24U) & 0xFFU;
    const std::uint32_t alpha = (color_alpha * coverage + 127U) / 255U;
    SetPixel(image, x, y, (color & 0x00FFFFFFU) | (alpha << 24U));
}

double DistanceToSegment(
    const double x,
    const double y,
    const platform::PixelPoint start,
    const platform::PixelPoint end
) noexcept {
    const double dx = static_cast<double>(end.x - start.x);
    const double dy = static_cast<double>(end.y - start.y);
    const double length_squared = (dx * dx) + (dy * dy);
    if (length_squared == 0.0) {
        return std::hypot(x - start.x, y - start.y);
    }
    const double projection = std::clamp(
        (((x - start.x) * dx) + ((y - start.y) * dy)) / length_squared,
        0.0,
        1.0
    );
    return std::hypot(
        x - (start.x + (projection * dx)),
        y - (start.y + (projection * dy))
    );
}

platform::PixelRect SegmentBounds(
    const platform::PixelPoint start,
    const platform::PixelPoint end,
    const double radius,
    const platform::PixelRect clip
) noexcept {
    const auto margin = static_cast<std::int32_t>(std::ceil(radius + 1.0));
    return platform::Intersect(
        {
            std::min(start.x, end.x) - margin,
            std::min(start.y, end.y) - margin,
            std::max(start.x, end.x) + margin + 1,
            std::max(start.y, end.y) + margin + 1,
        },
        clip
    );
}

void DrawAntialiasedSegment(
    capture::PixelBuffer& image,
    const platform::PixelPoint start,
    const platform::PixelPoint end,
    const double radius,
    const std::uint32_t color
) {
    const auto bounds = SegmentBounds(
        start, end, radius, {0, 0, image.width, image.height});
    for (std::int32_t y = bounds.top; y < bounds.bottom; ++y) {
        for (std::int32_t x = bounds.left; x < bounds.right; ++x) {
            const double amount = std::clamp(
                radius + 0.5 - DistanceToSegment(x, y, start, end),
                0.0,
                1.0
            );
            const auto coverage = static_cast<std::uint8_t>(
                std::lround(amount * 255.0));
            if (coverage != 0U) {
                BlendPixelCoverage(image, x, y, color, coverage);
            }
        }
    }
}

void MarkAntialiasedSegment(
    std::vector<std::uint8_t>& coverage,
    const platform::PixelRect bounds,
    const platform::PixelPoint start,
    const platform::PixelPoint end,
    const double radius
) {
    const auto segment_bounds = SegmentBounds(start, end, radius, bounds);
    for (std::int32_t y = segment_bounds.top; y < segment_bounds.bottom; ++y) {
        for (std::int32_t x = segment_bounds.left; x < segment_bounds.right; ++x) {
            const double amount = std::clamp(
                radius + 0.5 - DistanceToSegment(x, y, start, end),
                0.0,
                1.0
            );
            const auto value = static_cast<std::uint8_t>(std::lround(amount * 255.0));
            const auto index =
                static_cast<std::size_t>(y - bounds.top) *
                    static_cast<std::size_t>(bounds.width()) +
                static_cast<std::size_t>(x - bounds.left);
            coverage[index] = std::max(coverage[index], value);
        }
    }
}

void DrawStroke(
    capture::PixelBuffer& image,
    const std::vector<platform::PixelPoint>& points,
    const std::int32_t thickness,
    const std::uint32_t color
) {
    if (points.size() < 2) {
        return;
    }
    const double radius = std::max(0.5, thickness / 2.0);
    if (((color >> 24U) & 0xFFU) == 0xFFU) {
        for (std::size_t index = 1; index < points.size(); ++index) {
            DrawAntialiasedSegment(
                image, points[index - 1], points[index], radius, color);
        }
        return;
    }
    platform::PixelRect bounds{
        points.front().x,
        points.front().y,
        points.front().x + 1,
        points.front().y + 1,
    };
    for (const auto point : points) {
        bounds.left = std::min(bounds.left, point.x);
        bounds.top = std::min(bounds.top, point.y);
        bounds.right = std::max(bounds.right, point.x + 1);
        bounds.bottom = std::max(bounds.bottom, point.y + 1);
    }
    const auto margin = static_cast<std::int32_t>(std::ceil(radius + 1.0));
    bounds = platform::Intersect(
        {
            bounds.left - margin,
            bounds.top - margin,
            bounds.right + margin,
            bounds.bottom + margin,
        },
        {0, 0, image.width, image.height}
    );
    if (bounds.empty()) {
        return;
    }
    std::vector<std::uint8_t> coverage(
        static_cast<std::size_t>(bounds.width()) *
        static_cast<std::size_t>(bounds.height())
    );
    for (std::size_t index = 1; index < points.size(); ++index) {
        MarkAntialiasedSegment(
            coverage, bounds, points[index - 1], points[index], radius);
    }
    for (std::int32_t y = bounds.top; y < bounds.bottom; ++y) {
        for (std::int32_t x = bounds.left; x < bounds.right; ++x) {
            const auto index =
                static_cast<std::size_t>(y - bounds.top) *
                    static_cast<std::size_t>(bounds.width()) +
                static_cast<std::size_t>(x - bounds.left);
            if (coverage[index] != 0U) {
                BlendPixelCoverage(image, x, y, color, coverage[index]);
            }
        }
    }
}

void DrawLine(
    capture::PixelBuffer& image,
    const platform::PixelPoint start,
    const platform::PixelPoint end,
    const std::int32_t thickness,
    const std::uint32_t color
) {
    DrawStroke(image, {start, end}, thickness, color);
}

void ApplyPixelation(capture::PixelBuffer& image, platform::PixelRect area) {
    area = platform::Intersect(area, {0, 0, image.width, image.height});
    if (area.empty()) {
        return;
    }
    constexpr std::int32_t kBlockSize = 16;
    for (std::int32_t block_y = area.top; block_y < area.bottom;
         block_y += kBlockSize) {
        const auto block_bottom = std::min(block_y + kBlockSize, area.bottom);
        for (std::int32_t block_x = area.left; block_x < area.right;
             block_x += kBlockSize) {
            const auto block_right = std::min(block_x + kBlockSize, area.right);
            std::uint64_t sums[3]{};
            std::uint64_t pixel_count = 0;
            for (std::int32_t y = block_y; y < block_bottom; ++y) {
                for (std::int32_t x = block_x; x < block_right; ++x) {
                    const auto* pixel = image.row(y) +
                        (static_cast<std::size_t>(x) * 4U);
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        sums[channel] += std::to_integer<unsigned>(pixel[channel]);
                    }
                    ++pixel_count;
                }
            }
            std::byte average[3]{};
            for (std::size_t channel = 0; channel < 3; ++channel) {
                average[channel] = static_cast<std::byte>(sums[channel] / pixel_count);
            }
            for (std::int32_t y = block_y; y < block_bottom; ++y) {
                for (std::int32_t x = block_x; x < block_right; ++x) {
                    auto* pixel = image.row(y) + (static_cast<std::size_t>(x) * 4U);
                    pixel[0] = average[0];
                    pixel[1] = average[1];
                    pixel[2] = average[2];
                    pixel[3] = std::byte{0xFF};
                }
            }
        }
    }
}

void DrawAnnotation(
    capture::PixelBuffer& image, const Annotation& annotation, bool& drew_text
) {
    if (annotation.kind == AnnotationKind::blur) {
        ApplyPixelation(image, platform::Normalize(annotation.start, annotation.end));
        return;
    }
    if (annotation.kind == AnnotationKind::line) {
        DrawLine(image, annotation.start, annotation.end,
            annotation.thickness, annotation.color);
        return;
    }
    if (annotation.kind == AnnotationKind::arrow) {
        const double angle = std::atan2(
            annotation.end.y - annotation.start.y,
            annotation.end.x - annotation.start.x
        );
        constexpr double kSpread = 0.55;
        const double length = std::max(14.0, annotation.thickness * 4.0);
        std::vector<platform::PixelPoint> stroke{
            annotation.start,
            annotation.end,
        };
        for (const double direction : {angle + std::numbers::pi - kSpread,
                                      angle + std::numbers::pi + kSpread}) {
            const platform::PixelPoint head{
                annotation.end.x + static_cast<std::int32_t>(std::lround(std::cos(direction) * length)),
                annotation.end.y + static_cast<std::int32_t>(std::lround(std::sin(direction) * length)),
            };
            stroke.push_back(head);
            stroke.push_back(annotation.end);
        }
        DrawStroke(image, stroke, annotation.thickness, annotation.color);
        return;
    }
    if (annotation.kind == AnnotationKind::marker) {
        DrawStroke(image, annotation.points, annotation.thickness, annotation.color);
        return;
    }
    if (annotation.kind == AnnotationKind::image && annotation.image != nullptr) {
        const auto bounds = platform::Normalize(annotation.start, annotation.end);
        if (bounds.empty()) {
            return;
        }
        for (std::int32_t y = 0; y < bounds.height(); ++y) {
            const auto source_y = std::clamp(
                (y * annotation.image->height) / bounds.height(),
                0,
                annotation.image->height - 1
            );
            for (std::int32_t x = 0; x < bounds.width(); ++x) {
                const auto source_x = std::clamp(
                    (x * annotation.image->width) / bounds.width(),
                    0,
                    annotation.image->width - 1
                );
                const std::byte* source = annotation.image->row(source_y) +
                    (static_cast<std::size_t>(source_x) * 4);
                const std::uint32_t color =
                    (std::to_integer<std::uint32_t>(source[3]) << 24U) |
                    (std::to_integer<std::uint32_t>(source[2]) << 16U) |
                    (std::to_integer<std::uint32_t>(source[1]) << 8U) |
                    std::to_integer<std::uint32_t>(source[0]);
                SetPixel(image, bounds.left + x, bounds.top + y, color);
            }
        }
        return;
    }
    if (annotation.kind == AnnotationKind::text) {
        const auto bounds = platform::Normalize(annotation.start, annotation.end);
        const auto visible = platform::Intersect(bounds, {0, 0, image.width, image.height});
        // Preserve the existing opaque result even for text outside the canvas.
        drew_text = true;
        if (visible.empty()) {
            return;
        }
        const auto row_bytes = static_cast<std::size_t>(visible.width()) * 4;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = visible.width();
        info.bmiHeader.biHeight = -visible.height();
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = ::CreateCompatibleDC(nullptr);
        HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dc != nullptr && bitmap != nullptr && bits != nullptr) {
            HGDIOBJ old_bitmap = ::SelectObject(dc, bitmap);
            auto* text_pixels = static_cast<std::byte*>(bits);
            for (std::int32_t y = 0; y < visible.height(); ++y) {
                std::memcpy(text_pixels + static_cast<std::size_t>(y) * row_bytes,
                    image.row(visible.top + y) + static_cast<std::size_t>(visible.left) * 4,
                    row_bytes);
            }
            const int font_size = std::max(8, annotation.font_size);
            HFONT font = ::CreateFontW(-font_size, 0, 0, 0,
                annotation.font_bold ? FW_BOLD : FW_NORMAL,
                annotation.font_italic, annotation.font_underline,
                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HGDIOBJ old_font = ::SelectObject(dc, font);
            ::SetBkMode(dc, TRANSPARENT);
            ::SetTextColor(dc, RGB(
                (annotation.color >> 16U) & 0xFFU,
                (annotation.color >> 8U) & 0xFFU,
                annotation.color & 0xFFU));
            // Keep the original layout width and origin when text crosses an edge.
            RECT text_bounds{bounds.left - visible.left, bounds.top - visible.top,
                bounds.right - visible.left, bounds.bottom - visible.top};
            ::DrawTextW(dc, annotation.text.c_str(), -1, &text_bounds,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
            ::GdiFlush();
            for (std::int32_t y = 0; y < visible.height(); ++y) {
                std::memcpy(
                    image.row(visible.top + y) + static_cast<std::size_t>(visible.left) * 4,
                    text_pixels + static_cast<std::size_t>(y) * row_bytes, row_bytes);
            }
            ::SelectObject(dc, old_font);
            ::DeleteObject(font);
            ::SelectObject(dc, old_bitmap);
        }
        if (bitmap != nullptr) {
            ::DeleteObject(bitmap);
        }
        if (dc != nullptr) {
            ::DeleteDC(dc);
        }
        return;
    }
    const auto bounds = platform::Normalize(annotation.start, annotation.end);
    if (annotation.kind == AnnotationKind::ellipse) {
        const double center_x = (bounds.left + bounds.right) / 2.0;
        const double center_y = (bounds.top + bounds.bottom) / 2.0;
        const double radius_x = bounds.width() / 2.0;
        const double radius_y = bounds.height() / 2.0;
        std::vector<platform::PixelPoint> stroke;
        stroke.reserve(361);
        for (int degree = 0; degree <= 360; ++degree) {
            const double angle = degree * std::numbers::pi / 180.0;
            stroke.push_back({
                static_cast<std::int32_t>(std::lround(center_x + (std::cos(angle) * radius_x))),
                static_cast<std::int32_t>(std::lround(center_y + (std::sin(angle) * radius_y))),
            });
        }
        DrawStroke(image, stroke, annotation.thickness, annotation.color);
        return;
    }
    DrawStroke(
        image,
        {
            {bounds.left, bounds.top},
            {bounds.right, bounds.top},
            {bounds.right, bounds.bottom},
            {bounds.left, bounds.bottom},
            {bounds.left, bounds.top},
        },
        annotation.thickness,
        annotation.color
    );
}

}  // namespace

capture::PixelBuffer DocumentRenderer::Render(
    const DocumentModel& model,
    const bool apply_crop
) {
    capture::PixelBuffer output = model.source();
    if (output.empty()) {
        return {};
    }
    bool drew_text = false;
    for (const auto& annotation : model.state().annotations) {
        DrawAnnotation(output, annotation, drew_text);
    }
    if (drew_text) {
        // GDI does not maintain alpha. Preserve the previous opaque-text export
        // behavior with one pass per render, instead of one pass per text object.
        for (std::int32_t y = 0; y < output.height; ++y) {
            for (std::int32_t x = 0; x < output.width; ++x) {
                output.row(y)[static_cast<std::size_t>(x) * 4 + 3] = std::byte{0xFF};
            }
        }
    }
    if (apply_crop) {
        const platform::PixelRect full{0, 0, output.width, output.height};
        if (model.state().crop != full) {
            return capture::Crop(output, model.state().crop);
        }
    }
    return output;
}

const capture::PixelBuffer& DocumentRenderCache::Render(const DocumentModel& model) {
    const auto& state = model.state();
    if (rendered_.empty() || source_ != state.source || annotations_ != state.annotations) {
        auto rendered = DocumentRenderer::Render(model, false);
        if (rendered.empty()) {
            rendered_ = {};
            source_.reset();
            annotations_.clear();
            return rendered_;
        }
        annotations_ = state.annotations;
        source_ = state.source;
        rendered_ = std::move(rendered);
    }
    return rendered_;
}

}  // namespace screenshooter::editor
