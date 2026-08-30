#include "editor/command_history.h"
#include "editor/document_renderer.h"

#include <catch2/catch_test_macros.hpp>

using screenshooter::capture::CreateOpaqueBuffer;
using screenshooter::editor::Annotation;
using screenshooter::editor::AnnotationKind;
using screenshooter::editor::CommandHistory;
using screenshooter::editor::DocumentModel;
using screenshooter::editor::DocumentRenderer;
using screenshooter::platform::PixelPoint;

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

TEST_CASE("Extended shape renderer draws arrow ellipse and star") {
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
    static_cast<void>(model.Add(Annotation{
        .kind = AnnotationKind::star,
        .start = {30, 30},
        .end = {100, 100},
    }));
    const auto rendered = DocumentRenderer::Render(model);
    REQUIRE(rendered != model.source());
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

TEST_CASE("Crop is non-destructive and controls exported dimensions") {
    DocumentModel model(CreateOpaqueBuffer(80, 60));
    model.SetCrop({10, 15, 70, 50});
    REQUIRE(model.source().width == 80);
    REQUIRE(model.source().height == 60);
    REQUIRE(DocumentRenderer::Render(model).width == 60);
    REQUIRE(DocumentRenderer::Render(model).height == 35);
    REQUIRE(DocumentRenderer::Render(model, false).width == 80);
}
