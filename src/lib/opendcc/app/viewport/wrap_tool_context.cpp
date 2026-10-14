// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/wrap_tool_context.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/app/viewport/iviewport_tool_context.h"
#include <QMouseEvent>
#include "opendcc/app/viewport/viewport_ui_draw_manager.h"
#include "opendcc/app/core/application.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE
using namespace pybind11;

struct IViewportToolContextWrap : IViewportToolContext
{
    using IViewportToolContext::IViewportToolContext;

    bool on_mouse_press(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(bool, IViewportToolContext, on_mouse_press, mouse_event, viewport_view, draw_manager);
    }

    bool on_mouse_move(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(bool, IViewportToolContext, on_mouse_move, mouse_event, viewport_view, draw_manager);
    }

    bool on_mouse_release(const ViewportMouseEvent& mouse_event, const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(bool, IViewportToolContext, on_mouse_release, mouse_event, viewport_view, draw_manager);
    }

    void draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, IViewportToolContext, draw, viewport_view, draw_manager);
    }

    TfToken get_name() const override { OPENDCC_PYBIND11_OVERRIDE_PURE_EXCEPTION_SAFE(TfToken, IViewportToolContext, get_name); }
};

void py_interp::bind::wrap_tool_context(pybind11::module& m)
{
    auto drw_mngr_cls = class_<ViewportUiDrawManager>(m, "ViewportUiDrawManager")
                            .def("begin_drawable", &ViewportUiDrawManager::begin_drawable, arg("selection_id") = 0u)
                            .def("set_color", (void(ViewportUiDrawManager::*)(const GfVec3f&)) & ViewportUiDrawManager::set_color)
                            .def("set_color", (void(ViewportUiDrawManager::*)(const GfVec4f&)) & ViewportUiDrawManager::set_color)
                            .def("set_mvp_matrix", &ViewportUiDrawManager::set_mvp_matrix)
                            .def("set_prim_type", &ViewportUiDrawManager::set_prim_type)
                            .def("line", &ViewportUiDrawManager::line)
                            .def("rect2d", &ViewportUiDrawManager::rect2d)
                            .def("mesh", &ViewportUiDrawManager::mesh)
                            .def("end_drawable", &ViewportUiDrawManager::end_drawable)
                            .def("execute_draw_queue", &ViewportUiDrawManager::execute_draw_queue);

    enum_<ViewportUiDrawManager::PaintStyle>(drw_mngr_cls, "PaintStyle")
        .value("FLAT", ViewportUiDrawManager::PaintStyle::FLAT)
        .value("STIPPLED", ViewportUiDrawManager::PaintStyle::STIPPLED)
        .value("SHADED", ViewportUiDrawManager::PaintStyle::SHADED)
        .export_values();
    enum_<ViewportUiDrawManager::PrimitiveType>(drw_mngr_cls, "PrimitiveType")
        .value("LinesStrip", ViewportUiDrawManager::PrimitiveType::PrimitiveTypeLinesStrip)
        .value("LinesLoop", ViewportUiDrawManager::PrimitiveType::PrimitiveTypeLinesLoop)
        .value("TriangleFan", ViewportUiDrawManager::PrimitiveType::PrimitiveTypeTriangleFan)
        .export_values();

    class_<ViewportMouseEvent>(m, "ViewportMouseEvent")
        .def(init<int, int, const QPoint&, Qt::MouseButton, Qt::MouseButtons, Qt::KeyboardModifiers>())
        .def("x", &ViewportMouseEvent::x)
        .def("y", &ViewportMouseEvent::y)
        .def("global_pos", &ViewportMouseEvent::global_pos)
        .def("button", &ViewportMouseEvent::button)
        .def("buttons", &ViewportMouseEvent::buttons)
        .def("modifiers", &ViewportMouseEvent::modifiers);

    class_<IViewportToolContext, IViewportToolContextWrap>(m, "IViewportToolContext")
        .def("on_mouse_press", &IViewportToolContext::on_mouse_press)
        .def("on_mouse_move", &IViewportToolContext::on_mouse_move)
        .def("on_mouse_release", &IViewportToolContext::on_mouse_release)
        .def("draw", &IViewportToolContext::draw)
        .def("get_name", &IViewportToolContext::get_name);

    class_<ViewportToolContextRegistry, std::unique_ptr<ViewportToolContextRegistry, nodelete>>(m, "ViewportToolContextRegistry")
        .def_static("register_tool_context",
                    [](const TfToken& context, const TfToken& name, ViewportToolContextRegistry::ViewportToolContextRegistryCallback callback) {
                        return ViewportToolContextRegistry::register_tool_context(context, name, pybind_safe_callback(callback));
                    })
        .def_static("unregister_tool_context", &ViewportToolContextRegistry::unregister_tool_context)
        .def_static("create_tool_context", &ViewportToolContextRegistry::create_tool_context, return_value_policy::reference);
}

OPENDCC_NAMESPACE_CLOSE
