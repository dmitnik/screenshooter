#pragma once

#include "editor/document_session.h"
#include "editor/command_history.h"

#include <windows.h>

#include <memory>

namespace screenshooter::editor {

class EditorWindow final {
public:
    [[nodiscard]] static std::unique_ptr<EditorWindow> Create(
        HINSTANCE instance,
        HWND host,
        DocumentSession session
    );

    ~EditorWindow();
    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    [[nodiscard]] HWND window() const noexcept { return window_; }
    static LRESULT CALLBACK WindowProcedure(HWND, UINT, WPARAM, LPARAM);

private:
    enum class Tool { select, rectangle, line };

    EditorWindow(HINSTANCE instance, HWND host, DocumentSession session);

    [[nodiscard]] bool CreateNativeWindow();
    void CreateControls();
    void LayoutControls();
    void Paint();
    void ZoomAt(POINT client_point, float factor);
    void CopyAndClose();
    [[nodiscard]] platform::PixelPoint DocumentPoint(POINT client_point) const noexcept;
    [[nodiscard]] bool IsCanvasPoint(POINT client_point) const noexcept;
    void BeginDocumentAction(POINT client_point);
    void UpdateDocumentAction(POINT client_point);
    void EndDocumentAction(POINT client_point);
    void DeleteSelection();
    void Undo();
    void Redo();
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_{};
    HWND host_{};
    HWND window_{};
    HWND copy_button_{};
    HWND save_button_{};
    HWND share_button_{};
    HWND select_button_{};
    HWND rectangle_button_{};
    HWND line_button_{};
    DocumentSession session_;
    CommandHistory history_;
    Tool tool_{Tool::select};
    float scale_{1.0F};
    float offset_x_{};
    float offset_y_{};
    bool fit_{true};
    bool panning_{};
    POINT last_pan_{};
    bool dragging_{};
    bool creating_{};
    bool closing_after_export_{};
    platform::PixelPoint last_document_point_{};
    AnnotationId active_annotation_{};
    DocumentState action_before_;
};

}  // namespace screenshooter::editor
