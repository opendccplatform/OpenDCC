/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include <pxr/base/gf/camera.h>
#include <memory>
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include <QImage>
#include <QPointer>
#include "opendcc/app/core/rich_selection.h"
#include "opendcc/app/viewport/viewport_scene_context.h"

class QWidget;

PXR_NAMESPACE_OPEN_SCOPE
class GfVec2f;
class SdfPath;
PXR_NAMESPACE_CLOSE_SCOPE

OPENDCC_NAMESPACE_OPEN

// TODO:
// Refactor this class later. I suggest to inject ViewportHydraEngine
// here and move all engine-related logic in this class.
// It is almost all `set_*` methods of ViewportGLWidget, methods related to picking and hiding/deactivating prims.
class ViewportGLWidget;

struct ViewportDimensions
{
    int x;
    int y;
    int width;
    int height;
};

class OPENDCC_API ViewportView
{
public:
    ViewportView() = default;
    void set_gl_widget(ViewportGLWidget* widget);

    std::pair<PXR_NS::HdxPickHitVector, bool> intersect(const PXR_NS::GfVec2f& point, SelectionList::SelectionMask target,
                                                        bool resolve_to_usd = false, const PXR_NS::HdRprimCollection* custom_collection = nullptr,
                                                        const PXR_NS::TfTokenVector& render_tags = PXR_NS::TfTokenVector());
    std::pair<PXR_NS::HdxPickHitVector, bool> intersect(const PXR_NS::GfVec2f& start, const PXR_NS::GfVec2f& end, SelectionList::SelectionMask target,
                                                        bool resolve_to_usd = false, const PXR_NS::HdRprimCollection* custom_collection = nullptr,
                                                        const PXR_NS::TfTokenVector& render_tags = PXR_NS::TfTokenVector());
    SelectionList pick_single_prim(const PXR_NS::GfVec2f& point, SelectionList::SelectionMask pick_target);
    SelectionList pick_multiple_prims(const PXR_NS::GfVec2f& start, const PXR_NS::GfVec2f& end, SelectionList::SelectionMask pick_target);
    void set_rollover_prim(const PXR_NS::SdfPath& path);

    void set_view_preset(ViewportCameraController::ViewPreset preset);
    ViewportCameraController::ViewPreset get_view_preset() const;
    PXR_NS::GfCamera get_navigation_camera() const;
    void set_navigation_camera(const PXR_NS::GfCamera& camera);
    void look_through(const PXR_NS::SdfPath& path);
    PXR_NS::GfCamera get_camera() const;
    ViewportDimensions get_viewport_dimensions() const;
    /** @brief Return the viewport widget, owned by the application UI. */
    QWidget* get_widget() const;
    /** @brief Capture the rendered viewport framebuffer. Requires a GUI thread and a valid GL context. */
    QImage grab_framebuffer() const;
    /** @brief Set per-viewport color correction: "disabled", "sRGB", or "openColorIO". */
    void set_color_mode(const std::string& mode);
    /** @brief Return the per-viewport color correction mode. */
    std::string get_color_mode() const;
    /** @brief Select an OCIO view on the current configuration's default display. */
    void set_ocio_view(const std::string& view);
    /** @brief Return the selected OCIO view. */
    std::string get_ocio_view() const;
    /** @brief Set the input color space used by OCIO correction in this viewport. */
    void set_input_color_space(const std::string& color_space);
    /** @brief Return the viewport's OCIO input color space. */
    std::string get_input_color_space() const;
    /** @brief Set the display gamma applied after the OCIO view transform. */
    void set_gamma(float gamma);
    /** @brief Return the viewport's display gamma. */
    float get_gamma() const;
    /** @brief Set exposure in stops before the OCIO view transform. */
    void set_exposure(float exposure);
    /** @brief Return the viewport's exposure in stops. */
    float get_exposure() const;
    void set_selected(const SelectionList& selection_list, const RichSelection& rich_selection = RichSelection());
    PXR_NS::TfToken get_scene_context_type() const;
    static PXR_NS::TfTokenVector get_render_plugins();
    static std::string get_render_display_name(const PXR_NS::TfToken& plugin_name);

private:
    ViewportGLWidget* get_gl_widget() const;
    QPointer<ViewportGLWidget> m_gl_widget;
};

using ViewportViewPtr = std::shared_ptr<ViewportView>;

OPENDCC_NAMESPACE_CLOSE
