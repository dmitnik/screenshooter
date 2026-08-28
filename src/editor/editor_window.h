#pragma once

#include "editor/document_session.h"

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
    EditorWindow(HINSTANCE instance, HWND host, DocumentSession session);

    [[nodiscard]] bool CreateNativeWindow();
    void CreateControls();
    void LayoutControls();
    void Paint();
    void ZoomAt(POINT client_point, float factor);
    void CopyAndClose();
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_{};
    HWND host_{};
    HWND window_{};
    HWND copy_button_{};
    HWND save_button_{};
    HWND share_button_{};
    DocumentSession session_;
    float scale_{1.0F};
    float offset_x_{};
    float offset_y_{};
    bool fit_{true};
    bool panning_{};
    POINT last_pan_{};
};

}  // namespace screenshooter::editor
