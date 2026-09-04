#pragma once

#include "editor/document_session.h"
#include "editor/command_history.h"
#include "performance/metrics.h"

#include <windows.h>

#include <memory>
#include <optional>

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
    enum class Tool {
        select, rectangle, line, ellipse, arrow, marker, text, blur, crop
    };

    EditorWindow(HINSTANCE instance, HWND host, DocumentSession session);

    [[nodiscard]] bool CreateNativeWindow();
    void CreateControls();
    void LayoutControls();
    void Paint();
    void DrawToolButton(const DRAWITEMSTRUCT& item) const;
    void DrawActionButton(const DRAWITEMSTRUCT& item) const;
    void DrawColorButton(const DRAWITEMSTRUCT& item) const;
    void DrawTextStyleButton(const DRAWITEMSTRUCT& item) const;
    void InvalidateToolButtons() const;
    void UpdatePropertyControls();
    void ChooseColor(std::uint32_t color);
    void ChooseFontSize();
    void ToggleTextStyle(int control_id);
    [[nodiscard]] std::optional<Tool> ToolForControlId(int control_id) const noexcept;
    [[nodiscard]] bool EnsurePaintBuffer(HDC reference, int width, int height);
    void ReleasePaintBuffer() noexcept;
    void InvalidateCanvas() const;
    void ZoomAt(POINT client_point, float factor);
    void CopyAndClose();
    void SaveAs();
    [[nodiscard]] bool SaveToDefaultFolder();
    void InsertImageFile(const wchar_t* path);
    [[nodiscard]] platform::PixelRect VisibleDocumentBounds() const noexcept;
    [[nodiscard]] platform::PixelPoint DocumentPoint(POINT client_point) const noexcept;
    [[nodiscard]] bool IsCanvasPoint(POINT client_point) const noexcept;
    void EnterCropMode();
    void ApplyCrop();
    void CancelCrop();
    void ClearCropSelection();
    void ShowCropActions(bool visible) const;
    void LayoutCropActions() const;
    void BeginDocumentAction(POINT client_point);
    void UpdateDocumentAction(POINT client_point, bool constrain_geometry);
    void EndDocumentAction(POINT client_point, bool constrain_geometry);
    void DeleteSelection();
    void Undo();
    void Redo();
    void BeginTextEdit(POINT client_point);
    void CommitTextEdit();
    void CopySelectedObject();
    void PasteObject();
    static LRESULT CALLBACK TextEditProcedure(
        HWND window, UINT message, WPARAM w_param, LPARAM l_param,
        UINT_PTR subclass_id, DWORD_PTR reference_data
    );
    static LRESULT CALLBACK ToolButtonProcedure(
        HWND window, UINT message, WPARAM w_param, LPARAM l_param,
        UINT_PTR subclass_id, DWORD_PTR reference_data
    );
    LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

    HINSTANCE instance_{};
    HWND host_{};
    HWND window_{};
    HWND copy_button_{};
    HWND save_button_{};
    HWND info_button_{};
    HWND settings_button_{};
    HWND select_button_{};
    HWND rectangle_button_{};
    HWND line_button_{};
    HWND ellipse_button_{};
    HWND arrow_button_{};
    HWND marker_button_{};
    HWND text_button_{};
    HWND blur_button_{};
    HWND crop_button_{};
    HWND apply_crop_button_{};
    HWND cancel_crop_button_{};
    HWND red_button_{};
    HWND green_button_{};
    HWND blue_button_{};
    HWND font_size_combo_{};
    HWND bold_button_{};
    HWND italic_button_{};
    HWND underline_button_{};
    HWND text_edit_{};
    HWND hovered_button_{};
    HDC paint_buffer_dc_{};
    HBITMAP paint_buffer_bitmap_{};
    HGDIOBJ paint_buffer_previous_{};
    HFONT text_edit_font_{};
    SIZE paint_buffer_size_{};
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
    bool resizing_{};
    ResizeHandle resize_handle_{ResizeHandle::none};
    bool crop_drafting_{};
    bool crop_selection_ready_{};
    bool crop_release_on_right_{true};
    bool crop_release_on_bottom_{true};
    bool marker_straight_drafting_{};
    bool creating_{};
    bool closing_after_export_{};
    platform::PixelPoint last_document_point_{};
    POINT last_client_point_{};
    platform::PixelPoint action_anchor_{};
    AnnotationId active_annotation_{};
    DocumentState action_before_;
    DocumentState crop_mode_before_;
    platform::PixelPoint text_origin_{};
    std::optional<Annotation> object_clipboard_;
    std::uint32_t current_color_{0xFFE53935};
    std::int32_t current_font_size_{24};
    bool current_font_bold_{};
    bool current_font_italic_{};
    bool current_font_underline_{};
    performance::MetricTimer first_paint_timer_;
    bool first_paint_logged_{};
};

}  // namespace screenshooter::editor
