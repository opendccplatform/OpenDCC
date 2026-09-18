// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_hydra_engine.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

void ViewportView::set_gl_widget(ViewportGLWidget* widget)
{
    m_gl_widget = widget;
}

std::pair<PXR_NS::HdxPickHitVector, bool> ViewportView::intersect(const GfVec2f& point, SelectionList::SelectionMask target,
                                                                  bool resolve_to_usd /*= false*/,
                                                                  const PXR_NS::HdRprimCollection* custom_collection /*= nullptr*/,
                                                                  const TfTokenVector& render_tags /*= TfTokenVector()*/)
{
    return m_gl_widget->intersect(point, target, resolve_to_usd, custom_collection, render_tags);
}

std::pair<PXR_NS::HdxPickHitVector, bool> ViewportView::intersect(const GfVec2f& start, const GfVec2f& end, SelectionList::SelectionMask target,
                                                                  bool resolve_to_usd /*= false*/,
                                                                  const PXR_NS::HdRprimCollection* custom_collection /*= nullptr*/,
                                                                  const TfTokenVector& render_tags /*= TfTokenVector()*/)
{
    return m_gl_widget->intersect(start, end, target, resolve_to_usd, custom_collection, render_tags);
}

SelectionList ViewportView::pick_single_prim(const GfVec2f& point, SelectionList::SelectionMask pick_target)
{
    return m_gl_widget->pick_single_prim(point, pick_target);
}

SelectionList ViewportView::pick_multiple_prims(const GfVec2f& start, const GfVec2f& end, SelectionList::SelectionMask pick_target)
{
    return m_gl_widget->pick_multiple_prim(start, end, pick_target);
}

void ViewportView::set_rollover_prim(const SdfPath& path)
{
    m_gl_widget->set_rollover_prims({ path });
}

void ViewportView::look_through(const SdfPath& path)
{
    m_gl_widget->get_camera_controller()->set_follow_prim(path);
}

GfCamera ViewportView::get_camera() const
{
    return m_gl_widget->get_camera_controller()->get_gf_camera();
}

ViewportDimensions ViewportView::get_viewport_dimensions() const
{
    // devicePixelRatio() is qreal and ViewportDimensions holds ints; Qt6 build flags treat
    // the implicit narrowing in a braced initialiser as an error.
    return { 0, 0, static_cast<int>(m_gl_widget->width() * m_gl_widget->devicePixelRatio()),
             static_cast<int>(m_gl_widget->height() * m_gl_widget->devicePixelRatio()) };
}

void ViewportView::set_selected(const SelectionList& selection_list, const RichSelection& rich_selection)
{
    if (auto engine = m_gl_widget->get_engine())
        engine->set_selected(selection_list, rich_selection);
}

TfToken ViewportView::get_scene_context_type() const
{
    return m_gl_widget->get_scene_context_type();
}

TfTokenVector ViewportView::get_render_plugins()
{
    return ViewportHydraEngine::get_render_plugins();
}

std::string ViewportView::get_render_display_name(const TfToken& plugin_name)
{
    return ViewportHydraEngine::get_render_display_name(plugin_name);
}
OPENDCC_NAMESPACE_CLOSE
