#include "editor/command_history.h"
#include "editor/document_renderer.h"

#include <catch2/catch_test_macros.hpp>

using screenshooter::capture::CreateOpaqueBuffer;
using screenshooter::editor::Annotation;
using screenshooter::editor::AnnotationKind;
using screenshooter::editor::CommandHistory;
using screenshooter::editor::ConstrainShapeEnd;
using screenshooter::editor::DocumentModel;
using screenshooter::editor::DocumentRenderer;
using screenshooter::editor::DefaultArrowStart;
using screenshooter::editor::HitResizeHandle;
using screenshooter::editor::ResizeAnnotationEndpoint;
using screenshooter::editor::ResizeHandle;
using screenshooter::editor::UpdateMarkerStroke;
using screenshooter::platform::PixelPoint;
using screenshooter::platform::PixelRect;

TEST_CASE("Hit testing returns the topmost annotation") {
    DocumentModel model(CreateOpaqueBuffer(100, 100));
    const auto lower = model.Add(Annotation{
        .kind = AnnotationKind::line,
        .start = {10, 10},
        .end = {80, 80},
    });
    const auto upper = model.Add(Annotation{
        .kind = AnnotationKind::line,
        .start = {10, 80},
        .end = {80, 10},
    });

    REQUIRE(model.HitTest(PixelPoint{45, 45}) == upper);
    REQUIRE(lower != upper);
}

TEST_CASE("Filled blur annotation can be selected from its interior") {
    DocumentModel model(CreateOpaqueBuffer(100, 100));
    const auto blur = model.Add(Annotation{
        .kind = AnnotationKind::blur,
        .start = {10, 10},
        .end = {80, 80},
    });
    REQUIRE(model.HitTest(PixelPoint{45, 45}) == blur);
}

TEST_CASE("One drag transaction contains many move events") {
    DocumentModel model(CreateOpaqueBuffer(100, 100));
    const auto id = model.Add(Annotation{
        .kind = AnnotationKind::rectangle,
        .start = {10, 10},
        .end = {30, 30},
    });
    model.MarkClean();
    CommandHistory history;
    const auto before = model.Snapshot();
    for (int event = 0; event < 100; ++event) {
        REQUIRE(model.Move(id, PixelPoint{1, 0}));
    }
    history.Commit(before, model.Snapshot());

    REQUIRE(history.undo_size() == 1);
    REQUIRE(model.Find(id)->start == PixelPoint{110, 10});
    REQUIRE(history.Undo(model));
    REQUIRE(model.Find(id)->start == PixelPoint{10, 10});
    REQUIRE_FALSE(model.dirty());
    REQUIRE(history.Redo(model));
    REQUIRE(model.Find(id)->start == PixelPoint{110, 10});
    REQUIRE(model.dirty());
}

TEST_CASE("Selected annotation exposes both resize endpoints") {
    Annotation annotation{
        .kind = AnnotationKind::arrow,
        .start = {20, 30},
        .end = {80, 60},
    };

    REQUIRE(HitResizeHandle(annotation, {23, 32}, 5) == ResizeHandle::start);
    REQUIRE(HitResizeHandle(annotation, {77, 63}, 5) == ResizeHandle::end);
    REQUIRE(HitResizeHandle(annotation, {50, 45}, 5) == ResizeHandle::none);

    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::start, {10, 70}, {0, 0, 120, 100}, false));
    REQUIRE(annotation.start == PixelPoint{10, 70});
    REQUIRE(annotation.end == PixelPoint{80, 60});
    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::end, {100, 20}, {0, 0, 120, 100}, false));
    REQUIRE(annotation.end == PixelPoint{100, 20});
}

TEST_CASE("Marker resize endpoints update the visible stroke") {
    Annotation marker{
        .kind = AnnotationKind::marker,
        .start = {10, 20},
        .end = {50, 30},
        .points = {{10, 20}, {30, 25}, {50, 30}},
    };

    REQUIRE(ResizeAnnotationEndpoint(
        marker, ResizeHandle::start, {5, 15}, {0, 0, 100, 100}, false));
    REQUIRE(marker.start == PixelPoint{5, 15});
    REQUIRE(marker.points.front() == PixelPoint{5, 15});
    REQUIRE(ResizeAnnotationEndpoint(
        marker, ResizeHandle::end, {60, 40}, {0, 0, 100, 100}, false));
    REQUIRE(marker.end == PixelPoint{60, 40});
    REQUIRE(marker.points.back() == PixelPoint{60, 40});
    REQUIRE_FALSE(ResizeAnnotationEndpoint(
        marker, ResizeHandle::none, {0, 0}, {0, 0, 100, 100}, false));
}

TEST_CASE("Shift constraints apply while resizing existing annotations") {
    const PixelRect bounds{0, 0, 200, 200};
    Annotation arrow{
        .kind = AnnotationKind::arrow,
        .start = {20, 40},
        .end = {100, 70},
    };
    REQUIRE(ResizeAnnotationEndpoint(
        arrow, ResizeHandle::start, {30, 65}, bounds, true));
    REQUIRE(arrow.start == PixelPoint{30, 70});

    Annotation rectangle{
        .kind = AnnotationKind::rectangle,
        .start = {20, 20},
        .end = {50, 40},
    };
    REQUIRE(ResizeAnnotationEndpoint(
        rectangle, ResizeHandle::end, {80, 45}, bounds, true));
    REQUIRE(rectangle.end == PixelPoint{80, 80});

    Annotation marker{
        .kind = AnnotationKind::marker,
        .start = {10, 20},
        .end = {60, 35},
        .points = {{10, 20}, {30, 45}, {60, 35}},
    };
    REQUIRE(ResizeAnnotationEndpoint(
        marker, ResizeHandle::end, {90, 80}, bounds, true));
    REQUIRE(marker.end == PixelPoint{90, 20});
    REQUIRE(marker.points == std::vector<PixelPoint>{{10, 20}, {90, 20}});
}

TEST_CASE("History enforces its configured boundary") {
    DocumentModel model(CreateOpaqueBuffer(20, 20));
    const auto id = model.Add(Annotation{
        .kind = AnnotationKind::line,
        .start = {1, 1},
        .end = {2, 2},
    });
    CommandHistory history(3);
    for (int command = 0; command < 5; ++command) {
        const auto before = model.Snapshot();
        REQUIRE(model.Move(id, PixelPoint{1, 0}));
        history.Commit(before, model.Snapshot());
    }
    REQUIRE(history.undo_size() == 3);
}

TEST_CASE("Renderer preserves source and draws document annotations") {
    auto source = CreateOpaqueBuffer(20, 20);
    source.row(0)[0] = std::byte{0x23};
    DocumentModel model(std::move(source));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::line,
        .start = {4, 4},
        .end = {14, 4},
        .color = 0xFFFF0000,
        .thickness = 2,
    }));

    const auto rendered = DocumentRenderer::Render(model);
    REQUIRE(std::to_integer<unsigned>(rendered.row(0)[0]) == 0x23U);
    REQUIRE(std::to_integer<unsigned>(rendered.row(4)[(8 * 4) + 2]) == 0xFFU);
}

TEST_CASE("Marker is one movable hit-testable object") {
    DocumentModel model(CreateOpaqueBuffer(100, 100));
    const auto id = model.Add(Annotation{
        .kind = AnnotationKind::marker,
        .start = {10, 20},
        .end = {50, 20},
        .color = 0x66FFEB3B,
        .thickness = 18,
        .points = {{10, 20}, {30, 22}, {50, 20}},
    });
    REQUIRE(model.HitTest(PixelPoint{30, 22}) == id);
    REQUIRE(model.Move(id, PixelPoint{5, 7}));
    REQUIRE(model.Find(id)->points.front() == PixelPoint{15, 27});
}

TEST_CASE("Shift marker discards freehand points and becomes one straight line") {
    Annotation marker{
        .kind = AnnotationKind::marker,
        .start = {10, 20},
        .end = {30, 25},
        .points = {{10, 20}, {20, 30}, {30, 25}},
    };
    UpdateMarkerStroke(marker, {70, 45}, true);
    REQUIRE(marker.end == PixelPoint{70, 20});
    REQUIRE(marker.points == std::vector<PixelPoint>{{10, 20}, {70, 20}});
}

TEST_CASE("Transparent marker blends each covered pixel only once") {
    auto source = CreateOpaqueBuffer(40, 30);
    for (auto& value : source.pixels) {
        value = std::byte{0xFF};
    }
    DocumentModel model(std::move(source));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::marker,
        .start = {5, 15},
        .end = {35, 15},
        .color = 0x66FFEB3B,
        .thickness = 18,
        .points = {{5, 15}, {20, 15}, {35, 15}},
    }));
    const auto rendered = DocumentRenderer::Render(model);
    const auto* center = rendered.row(15) + (20 * 4);
    REQUIRE(std::to_integer<unsigned>(center[0]) == 176U);
    REQUIRE(std::to_integer<unsigned>(center[1]) == 247U);
    REQUIRE(std::to_integer<unsigned>(center[2]) == 255U);
    REQUIRE(std::to_integer<unsigned>(center[3]) == 255U);
}

TEST_CASE("Shape renderer draws a visible arrow and ellipse") {
    DocumentModel model(CreateOpaqueBuffer(120, 120));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::arrow,
        .start = {10, 10},
        .end = {80, 10},
    }));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::ellipse,
        .start = {20, 30},
        .end = {90, 80},
    }));
    const auto rendered = DocumentRenderer::Render(model);
    REQUIRE(rendered != model.source());
    REQUIRE(std::to_integer<unsigned>(rendered.row(10)[40 * 4 + 2]) > 0U);
}

TEST_CASE("Shape strokes antialias their diagonal edges") {
    DocumentModel model(CreateOpaqueBuffer(80, 60));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::arrow,
        .start = {10, 10},
        .end = {60, 37},
        .color = 0xFFFF0000,
        .thickness = 4,
    }));
    const auto rendered = DocumentRenderer::Render(model);
    bool found_solid = false;
    bool found_antialiased_edge = false;
    for (std::int32_t y = 0; y < rendered.height; ++y) {
        for (std::int32_t x = 0; x < rendered.width; ++x) {
            const auto red = std::to_integer<unsigned>(rendered.row(y)[x * 4 + 2]);
            found_solid = found_solid || red == 0xFFU;
            found_antialiased_edge = found_antialiased_edge ||
                (red > 0U && red < 0xFFU);
        }
    }
    REQUIRE(found_solid);
    REQUIRE(found_antialiased_edge);
}

TEST_CASE("Default click arrow keeps its tip and places a short tail behind it") {
    const PixelRect bounds{0, 0, 200, 100};
    REQUIRE(DefaultArrowStart({50, 50}, bounds) == PixelPoint{30, 50});
    REQUIRE(DefaultArrowStart({10, 50}, bounds) == PixelPoint{30, 50});
    REQUIRE(DefaultArrowStart({190, 50}, bounds) == PixelPoint{170, 50});

    const PixelRect narrow_bounds{0, 0, 10, 10};
    REQUIRE(DefaultArrowStart({5, 5}, narrow_bounds) == PixelPoint{0, 5});
}

TEST_CASE("Shift constraint snaps lines and arrows to 45 degree increments") {
    const PixelRect bounds{0, 0, 200, 200};
    const PixelPoint start{50, 50};

    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::line, start, {90, 58}, bounds) == PixelPoint{90, 50});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::line, start, {58, 90}, bounds) == PixelPoint{50, 90});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::line, start, {10, 42}, bounds) == PixelPoint{10, 50});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::line, start, {58, 10}, bounds) == PixelPoint{50, 10});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::arrow, start, {80, 70}, bounds) == PixelPoint{80, 80});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::arrow, start, {28, 80}, bounds) == PixelPoint{20, 80});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::arrow, start, {80, 28}, bounds) == PixelPoint{80, 20});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::arrow, start, {28, 20}, bounds) == PixelPoint{20, 20});
}

TEST_CASE("Shift constraint turns rectangles and ellipses into equal-sided shapes") {
    const PixelRect bounds{0, 0, 200, 200};
    const PixelPoint start{50, 50};

    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::rectangle, start, {80, 65}, bounds) == PixelPoint{80, 80});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::rectangle, start, {30, 75}, bounds) == PixelPoint{25, 75});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::ellipse, start, {65, 20}, bounds) == PixelPoint{80, 20});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::ellipse, {190, 190}, {199, 195}, bounds) == PixelPoint{199, 199});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::rectangle, {190, 50}, {190, 100}, bounds) == PixelPoint{140, 100});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::ellipse, {50, 190}, {100, 190}, bounds) == PixelPoint{100, 140});
}

TEST_CASE("Shift constraint leaves unrelated tools unchanged") {
    const PixelRect bounds{0, 0, 200, 200};
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::blur, {50, 50}, {90, 65}, bounds) == PixelPoint{90, 65});
    REQUIRE(ConstrainShapeEnd(
        AnnotationKind::marker, {50, 50}, {90, 65}, bounds) == PixelPoint{90, 65});
}

TEST_CASE("Pixelation consumes lower raster while later annotations stay sharp") {
    auto source = CreateOpaqueBuffer(40, 20);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 20; x < source.width; ++x) {
            source.row(y)[(x * 4)] = std::byte{0xFF};
            source.row(y)[(x * 4) + 1] = std::byte{0xFF};
            source.row(y)[(x * 4) + 2] = std::byte{0xFF};
        }
    }
    DocumentModel model(std::move(source));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::blur,
        .start = {0, 0},
        .end = {40, 20},
    }));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::line,
        .start = {5, 10},
        .end = {35, 10},
        .color = 0xFFFF0000,
        .thickness = 2,
    }));
    const auto rendered = DocumentRenderer::Render(model);
    REQUIRE(std::to_integer<unsigned>(rendered.row(5)[19 * 4]) > 0U);
    REQUIRE(std::to_integer<unsigned>(rendered.row(10)[20 * 4 + 2]) == 0xFFU);
}

TEST_CASE("Pixelation fills each mosaic block with one averaged color") {
    auto source = CreateOpaqueBuffer(32, 16);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            source.row(y)[x * 4] = static_cast<std::byte>(x * 4);
        }
    }
    DocumentModel model(std::move(source));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::blur,
        .start = {0, 0},
        .end = {32, 16},
    }));

    const auto rendered = DocumentRenderer::Render(model);
    REQUIRE(std::to_integer<unsigned>(rendered.row(0)[0]) ==
        std::to_integer<unsigned>(rendered.row(15)[15 * 4]));
    REQUIRE(std::to_integer<unsigned>(rendered.row(0)[0]) !=
        std::to_integer<unsigned>(rendered.row(0)[16 * 4]));
}

TEST_CASE("Crop preview is non-destructive and controls exported dimensions") {
    DocumentModel model(CreateOpaqueBuffer(80, 60));
    model.SetCrop({10, 15, 70, 50});
    REQUIRE(model.source().width == 80);
    REQUIRE(model.source().height == 60);
    REQUIRE(DocumentRenderer::Render(model).width == 60);
    REQUIRE(DocumentRenderer::Render(model).height == 35);
    REQUIRE(DocumentRenderer::Render(model, false).width == 80);
}

TEST_CASE("Applied crop becomes the new source and supports another crop") {
    DocumentModel model(CreateOpaqueBuffer(80, 60));
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::rectangle,
        .start = {15, 20},
        .end = {35, 40},
    }));
    const auto before = model.Snapshot();
    model.SetCrop({10, 15, 70, 50});
    REQUIRE(model.ApplyCrop());
    REQUIRE(model.source().width == 60);
    REQUIRE(model.source().height == 35);
    REQUIRE(model.state().crop == PixelRect{0, 0, 60, 35});
    REQUIRE(model.state().annotations.front().start == PixelPoint{5, 5});

    CommandHistory history;
    history.Commit(before, model.Snapshot());
    REQUIRE(history.Undo(model));
    REQUIRE(model.source().width == 80);
    REQUIRE(model.source().height == 60);
    REQUIRE(history.Redo(model));
    REQUIRE(model.source().width == 60);

    model.SetCrop({5, 5, 30, 25});
    REQUIRE(model.ApplyCrop());
    REQUIRE(model.source().width == 25);
    REQUIRE(model.source().height == 20);
}

TEST_CASE("Applied crop marks an otherwise clean document as changed") {
    DocumentModel model(CreateOpaqueBuffer(40, 30));
    model.SetCrop({5, 5, 30, 20});
    REQUIRE(model.ApplyCrop());
    REQUIRE(model.dirty());
}

TEST_CASE("Image resize uses source aspect ratio after free distortion") {
    const auto image = std::make_shared<const screenshooter::capture::PixelBuffer>(
        CreateOpaqueBuffer(200, 100));
    Annotation annotation{
        .kind = AnnotationKind::image,
        .start = {50, 50},
        .end = {100, 80},
        .image = image,
    };
    const PixelRect bounds{0, 0, 300, 250};

    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::end, {170, 100}, bounds, true));
    REQUIRE(annotation.start == PixelPoint{50, 50});
    REQUIRE(annotation.end == PixelPoint{170, 110});

    annotation.start = {100, 100};
    annotation.end = {250, 150};
    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::start, {100, 100}, bounds, true));
    REQUIRE(annotation.start == PixelPoint{100, 75});
    REQUIRE(annotation.end == PixelPoint{250, 150});
}

TEST_CASE("Image aspect constraint respects document edges in every direction") {
    const auto image = std::make_shared<const screenshooter::capture::PixelBuffer>(
        CreateOpaqueBuffer(200, 100));
    const PixelRect bounds{0, 0, 300, 250};
    Annotation annotation{
        .kind = AnnotationKind::image,
        .start = {50, 50},
        .end = {100, 80},
        .image = image,
    };

    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::end, {0, 0}, bounds, true));
    REQUIRE(annotation.end == PixelPoint{0, 25});

    annotation.start = {250, 200};
    annotation.end = {200, 150};
    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::end, {299, 249}, bounds, true));
    REQUIRE(annotation.end == PixelPoint{299, 225});

    annotation.start = {150, 125};
    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::end, {299, 0}, bounds, true));
    REQUIRE(annotation.end == PixelPoint{299, 50});

    annotation.start = {150, 125};
    REQUIRE(ResizeAnnotationEndpoint(
        annotation, ResizeHandle::end, {0, 249}, bounds, true));
    REQUIRE(annotation.end == PixelPoint{0, 200});
}

TEST_CASE("Text appearance survives snapshots and undo redo") {
    DocumentModel model(CreateOpaqueBuffer(320, 160));
    const auto id = model.Add(Annotation{
        .kind = AnnotationKind::text,
        .start = {10, 10},
        .end = {300, 130},
        .color = 0xFF1E88E5,
        .font_size = 40,
        .font_bold = true,
        .font_italic = true,
        .font_underline = true,
        .text = L"Screenshooter",
    });
    CommandHistory history;
    const auto before = model.Snapshot();
    model.Find(id)->font_size = 48;
    model.Find(id)->color = 0xFF2E7D32;
    history.Commit(before, model.Snapshot());

    REQUIRE(history.Undo(model));
    REQUIRE(model.Find(id)->font_size == 40);
    REQUIRE(model.Find(id)->color == 0xFF1E88E5);
    REQUIRE(model.Find(id)->font_bold);
    REQUIRE(model.Find(id)->font_italic);
    REQUIRE(model.Find(id)->font_underline);
    REQUIRE(history.Redo(model));
    REQUIRE(model.Find(id)->font_size == 48);
    REQUIRE(model.Find(id)->color == 0xFF2E7D32);
}

TEST_CASE("Text renderer applies size and font styles") {
    const auto render = [](const int size, const bool bold, const bool italic,
                           const bool underline, const std::uint32_t color) {
        DocumentModel model(CreateOpaqueBuffer(320, 160));
        static_cast<void>(model.Add(Annotation{
            .kind = AnnotationKind::text,
            .start = {10, 10},
            .end = {300, 140},
            .color = color,
            .font_size = size,
            .font_bold = bold,
            .font_italic = italic,
            .font_underline = underline,
            .text = L"Style check",
        }));
        return DocumentRenderer::Render(model);
    };
    const auto normal = render(24, false, false, false, 0xFFE53935);
    REQUIRE(normal != render(48, false, false, false, 0xFFE53935));
    REQUIRE(normal != render(24, true, false, false, 0xFFE53935));
    REQUIRE(normal != render(24, false, true, false, 0xFFE53935));
    REQUIRE(normal != render(24, false, false, true, 0xFFE53935));
    REQUIRE(normal != render(24, false, false, false, 0xFF1E88E5));
}
