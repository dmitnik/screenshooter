#include "editor/command_history.h"
#include "editor/document_renderer.h"

#include <catch2/catch_test_macros.hpp>

using screenshooter::capture::CreateOpaqueBuffer;
using screenshooter::editor::Annotation;
using screenshooter::editor::AnnotationKind;
using screenshooter::editor::CommandHistory;
using screenshooter::editor::DocumentModel;
using screenshooter::editor::DocumentRenderer;
using screenshooter::editor::DefaultArrowStart;
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

TEST_CASE("Default click arrow keeps its tip and places a short tail behind it") {
    const PixelRect bounds{0, 0, 200, 100};
    REQUIRE(DefaultArrowStart({50, 50}, bounds) == PixelPoint{30, 50});
    REQUIRE(DefaultArrowStart({10, 50}, bounds) == PixelPoint{30, 50});
    REQUIRE(DefaultArrowStart({190, 50}, bounds) == PixelPoint{170, 50});

    const PixelRect narrow_bounds{0, 0, 10, 10};
    REQUIRE(DefaultArrowStart({5, 5}, narrow_bounds) == PixelPoint{0, 5});
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
