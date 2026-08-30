#include "editor/document_renderer.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

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

void DrawAnnotation(capture::PixelBuffer& image, const Annotation& annotation) {
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
        DrawLine(image, annotation.start, annotation.end,
            annotation.thickness, annotation.color);
        const double angle = std::atan2(
            annotation.end.y - annotation.start.y,
            annotation.end.x - annotation.start.x
        );
        constexpr double kSpread = 0.55;
        const double length = std::max(14.0, annotation.thickness * 4.0);
        for (const double direction : {angle + std::numbers::pi - kSpread,
                                      angle + std::numbers::pi + kSpread}) {
            const platform::PixelPoint head{
                annotation.end.x + static_cast<std::int32_t>(std::lround(std::cos(direction) * length)),
                annotation.end.y + static_cast<std::int32_t>(std::lround(std::sin(direction) * length)),
            };
            DrawLine(image, annotation.end, head,
                annotation.thickness, annotation.color);
        }
        return;
    }
    if (annotation.kind == AnnotationKind::curved_arrow && !annotation.points.empty()) {
        platform::PixelPoint previous = annotation.start;
        for (int step = 1; step <= 48; ++step) {
            const double t = step / 48.0;
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
            if ((step / 4) % 2 == 0) {
                DrawLine(image, previous, current,
                    annotation.thickness, annotation.color);
            }
            previous = current;
        }
        const double angle = std::atan2(
            annotation.end.y - annotation.points.front().y,
            annotation.end.x - annotation.points.front().x
        );
        constexpr double kSpread = 0.55;
        const double length = std::max(14.0, annotation.thickness * 4.0);
        for (const double direction : {angle + std::numbers::pi - kSpread,
                                      angle + std::numbers::pi + kSpread}) {
            const platform::PixelPoint head{
                annotation.end.x + static_cast<std::int32_t>(std::lround(std::cos(direction) * length)),
                annotation.end.y + static_cast<std::int32_t>(std::lround(std::sin(direction) * length)),
            };
            DrawLine(image, annotation.end, head,
                annotation.thickness, annotation.color);
        }
        return;
    }
    if (annotation.kind == AnnotationKind::marker) {
        for (std::size_t index = 1; index < annotation.points.size(); ++index) {
            DrawLine(image, annotation.points[index - 1], annotation.points[index],
                annotation.thickness, annotation.color);
        }
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
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = image.width;
        info.bmiHeader.biHeight = -image.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = ::CreateCompatibleDC(nullptr);
        HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dc != nullptr && bitmap != nullptr && bits != nullptr) {
            HGDIOBJ old_bitmap = ::SelectObject(dc, bitmap);
            std::memcpy(bits, image.pixels.data(), image.pixels.size());
            const int font_size = std::max(18, annotation.thickness * 6);
            HFONT font = ::CreateFontW(-font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HGDIOBJ old_font = ::SelectObject(dc, font);
            ::SetBkMode(dc, TRANSPARENT);
            ::SetTextColor(dc, RGB(
                (annotation.color >> 16U) & 0xFFU,
                (annotation.color >> 8U) & 0xFFU,
                annotation.color & 0xFFU));
            const auto bounds = platform::Normalize(annotation.start, annotation.end);
            RECT text_bounds{bounds.left, bounds.top, bounds.right, bounds.bottom};
            ::DrawTextW(dc, annotation.text.c_str(), -1, &text_bounds,
                DT_LEFT | DT_TOP | DT_WORDBREAK);
            std::memcpy(image.pixels.data(), bits, image.pixels.size());
            for (std::int32_t y = 0; y < image.height; ++y) {
                for (std::int32_t x = 0; x < image.width; ++x) {
                    image.row(y)[(x * 4) + 3] = std::byte{0xFF};
                }
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
        platform::PixelPoint previous{
            static_cast<std::int32_t>(std::lround(center_x + radius_x)),
            static_cast<std::int32_t>(std::lround(center_y)),
        };
        for (int degree = 1; degree <= 360; ++degree) {
            const double angle = degree * std::numbers::pi / 180.0;
            const platform::PixelPoint current{
                static_cast<std::int32_t>(std::lround(center_x + (std::cos(angle) * radius_x))),
                static_cast<std::int32_t>(std::lround(center_y + (std::sin(angle) * radius_y))),
            };
            DrawLine(image, previous, current, annotation.thickness, annotation.color);
            previous = current;
        }
        return;
    }
    if (annotation.kind == AnnotationKind::star) {
        const double center_x = (bounds.left + bounds.right) / 2.0;
        const double center_y = (bounds.top + bounds.bottom) / 2.0;
        const double outer = std::min(bounds.width(), bounds.height()) / 2.0;
        const double inner = outer * 0.45;
        platform::PixelPoint first{};
        platform::PixelPoint previous{};
        for (int index = 0; index < 10; ++index) {
            const double angle = (-std::numbers::pi / 2.0) +
                (index * std::numbers::pi / 5.0);
            const double radius = index % 2 == 0 ? outer : inner;
            const platform::PixelPoint current{
                static_cast<std::int32_t>(std::lround(center_x + std::cos(angle) * radius)),
                static_cast<std::int32_t>(std::lround(center_y + std::sin(angle) * radius)),
            };
            if (index == 0) {
                first = current;
            } else {
                DrawLine(image, previous, current, annotation.thickness, annotation.color);
            }
            previous = current;
        }
        DrawLine(image, previous, first, annotation.thickness, annotation.color);
        return;
    }
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

capture::PixelBuffer DocumentRenderer::Render(
    const DocumentModel& model,
    const bool apply_crop
) {
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
    if (apply_crop) {
        const platform::PixelRect full{0, 0, output.width, output.height};
        if (model.state().crop != full) {
            return capture::Crop(output, model.state().crop);
        }
    }
    return output;
}

}  // namespace screenshooter::editor
