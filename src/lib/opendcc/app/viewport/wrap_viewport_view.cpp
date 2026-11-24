// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/wrap_viewport_view.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/app/viewport/viewport_view.h"
#include "opendcc/app/viewport/viewport_camera_labels.h"
#include <tuple>

OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfCamera, "pxr.Gf.Camera")

OPENDCC_NAMESPACE_OPEN
using namespace pybind11;

void py_interp::bind::wrap_viewport_view(pybind11::module& m)
{
    enum_<ViewportCameraController::ViewPreset>(m, "ViewportViewPreset")
        .value("DEFAULT", ViewportCameraController::ViewPreset::DEFAULT)
        .value("PERSPECTIVE", ViewportCameraController::ViewPreset::PERSPECTIVE)
        .value("TOP", ViewportCameraController::ViewPreset::TOP)
        .value("FRONT", ViewportCameraController::ViewPreset::FRONT)
        .value("SIDE", ViewportCameraController::ViewPreset::SIDE);
    class_<ViewportView, std::shared_ptr<ViewportView>>(m, "ViewportView")
        .def("get_camera", &ViewportView::get_camera)
        .def("set_view_preset", &ViewportView::set_view_preset)
        .def("get_view_preset", &ViewportView::get_view_preset)
        .def_static("get_view_preset_label",
                    [](ViewportCameraController::ViewPreset preset) { return details::get_view_preset_label(preset).toStdString(); })
        .def_static("get_preset_camera_settings", &ViewportCameraController::get_preset_camera_settings)
        .def_static("preview_preset_camera_settings", &ViewportCameraController::preview_preset_camera_settings)
        .def_static("finish_preset_camera_settings_edit", &ViewportCameraController::finish_preset_camera_settings_edit)
        .def("get_navigation_camera", &ViewportView::get_navigation_camera)
        .def("set_navigation_camera", &ViewportView::set_navigation_camera)
        .def("get_viewport_dimensions",
             [](const ViewportView& view) {
                 const auto dimensions = view.get_viewport_dimensions();
                 return std::make_tuple(dimensions.x, dimensions.y, dimensions.width, dimensions.height);
             })
        .def("grab_framebuffer", &ViewportView::grab_framebuffer)
        .def("set_color_mode", &ViewportView::set_color_mode)
        .def("get_color_mode", &ViewportView::get_color_mode)
        .def("set_ocio_view", &ViewportView::set_ocio_view)
        .def("get_ocio_view", &ViewportView::get_ocio_view)
        .def("set_input_color_space", &ViewportView::set_input_color_space)
        .def("get_input_color_space", &ViewportView::get_input_color_space)
        .def("set_gamma", &ViewportView::set_gamma)
        .def("get_gamma", &ViewportView::get_gamma)
        .def("set_exposure", &ViewportView::set_exposure)
        .def("get_exposure", &ViewportView::get_exposure)
        .def("get_widget", &ViewportView::get_widget, return_value_policy::reference)
        .def("pick_single_prim", &ViewportView::pick_single_prim)
        .def("pick_multiple_prims", &ViewportView::pick_multiple_prims)
        .def("set_rollover_prim", &ViewportView::set_rollover_prim)
        .def("look_through", &ViewportView::look_through)
        .def("get_scene_context_type", &ViewportView::get_scene_context_type)
        .def_static("get_render_plugins", &ViewportView::get_render_plugins)
        .def_static("get_render_display_name", &ViewportView::get_render_display_name);
}
OPENDCC_NAMESPACE_CLOSE
