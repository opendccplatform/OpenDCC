// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/ui/wrap_panel.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/base/pybind_bridge/boost_python.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/ui/main_window.h"
#include "opendcc/app/ui/panel_factory.h"
#include <DockAreaWidget.h>

OPENDCC_NAMESPACE_OPEN
using namespace pybind11;

ads::CDockWidget* create_panel(const std::string panel_type, bool floating = true, ads::CDockAreaWidget* parent_panel = nullptr,
                               ads::DockWidgetArea dock_area = ads::CenterDockWidgetArea, int site_index = 0)
{
    return ApplicationUI::instance().get_main_window()->create_panel(panel_type, floating, parent_panel, dock_area, site_index);
}

void load_panel_layout()
{
    ApplicationUI::instance().get_main_window()->load_panel_layout();
}

void arrange_splitters(ads::CDockWidget* dock_widget, const std::vector<double>& proportion)
{
    ApplicationUI::instance().get_main_window()->arrange_splitters(dock_widget, proportion);
}

void close_panels()
{
    MainWindow* main_window = ApplicationUI::instance().get_main_window();
    main_window->close_panels();
}

void save_layout(const std::string& filepath)
{
    MainWindow* main_window = ApplicationUI::instance().get_main_window();
    main_window->save_layout(filepath);
}

void load_layout(const std::string& filepath)
{
    MainWindow* main_window = ApplicationUI::instance().get_main_window();
    main_window->load_layout(filepath);
}

dict get_registered_panels(PanelFactory* self)
{
    dict result;
    for (const auto& [panel_name, entry] : self->get_registry())
    {
        dict obj;
        obj["label"] = entry.label;
        obj["icon"] = entry.icon;
        obj["group"] = entry.group;

        result[str(panel_name)] = obj;
    }
    return result;
}

void py_interp::bind::wrap_panel(pybind11::module& m)
{
    enum_<ads::DockWidgetArea>(m, "DockWidgetArea")
        .value("LeftDockWidgetArea", ads::DockWidgetArea::LeftDockWidgetArea)
        .value("RightDockWidgetArea", ads::DockWidgetArea::RightDockWidgetArea)
        .value("TopDockWidgetArea", ads::DockWidgetArea::TopDockWidgetArea)
        .value("BottomDockWidgetArea", ads::DockWidgetArea::BottomDockWidgetArea)
        .value("CenterDockWidgetArea", ads::DockWidgetArea::CenterDockWidgetArea)
        .value("InvalidDockWidgetArea", ads::DockWidgetArea::InvalidDockWidgetArea)
        .value("OuterDockAreas", ads::DockWidgetArea::OuterDockAreas)
        .value("AllDockAreas", ads::DockWidgetArea::AllDockAreas);

    auto panel_factory_cls =
        class_<PanelFactory>(m, "PanelFactory")
            .def_static("instance", &PanelFactory::instance, return_value_policy::reference)
            .def(
                "register_panel",
                [](PanelFactory* self, const std::string& type, PanelFactory::PanelFactoryWidgetCallback callback, const std::string& label = "",
                   bool singleton = false, const std::string& icon = "", const std::string& group = "") {
                    return self->register_panel(type, pybind_safe_callback(callback), label, singleton, icon, group);
                },
                arg("name"), arg("callback_fn"), arg("label") = std::string(), arg("singleton") = false, arg("icon") = std::string(),
                arg("group") = std::string())
            .def("unregister_panel", &PanelFactory::unregister_panel)
            .def("get_registered_panels", &get_registered_panels);

    class_<ads::CDockAreaWidget>(m, "CDockAreaWidget");
    class_<ads::CDockWidget>(m, "CDockWidget").def("dockAreaWidget", &ads::CDockWidget::dockAreaWidget, return_value_policy::reference);

    m.def("create_panel", &create_panel, return_value_policy::reference, arg("panel_type"), arg("floating") = true, arg("parent_panel") = nullptr,
          arg("dock_area") = ads::CenterDockWidgetArea, arg("site_index") = 0);
    m.def("load_panel_layout", load_panel_layout);

    m.def("arrange_splitters", arrange_splitters);
    m.def("close_panels", close_panels);
    m.def("save_layout", save_layout);
    m.def("load_layout", load_layout);
}

OPENDCC_NAMESPACE_CLOSE
