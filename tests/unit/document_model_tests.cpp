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
