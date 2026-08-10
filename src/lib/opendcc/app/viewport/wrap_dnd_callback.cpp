// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/wrap_dnd_callback.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/app/viewport/viewport_dnd_callback_registry.h"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <pxr/base/tf/pyLock.h>
#include <pxr/base/tf/pyError.h>
#include "opendcc/app/viewport/viewport_view.h"

OPENDCC_NAMESPACE_OPEN

using namespace pybind11;

struct ViewportDndCallbackWrap : ViewportDndCallback
{
    using ViewportDndCallback::ViewportDndCallback;

    void on_enter(std::shared_ptr<ViewportView> view, QDragEnterEvent* event) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, ViewportDndCallback, on_enter, view, event);
    }
    void on_move(std::shared_ptr<ViewportView> view, QDragMoveEvent* event) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, ViewportDndCallback, on_move, view, event);
    }
    void on_drop(std::shared_ptr<ViewportView> view, QDropEvent* event) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, ViewportDndCallback, on_drop, view, event);
    }
    void on_leave(std::shared_ptr<ViewportView> view, QDragLeaveEvent* event) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, ViewportDndCallback, on_leave, view, event);
    }
    void on_view_destroyed(std::shared_ptr<ViewportView> view) override
    {
        OPENDCC_PYBIND11_OVERRIDE_EXCEPTION_SAFE(void, ViewportDndCallback, on_view_destroyed, view);
    }
};

void py_interp::bind::wrap_dnd_callback(pybind11::module& m)
{
    class_<ViewportDndCallback, ViewportDndCallbackWrap, smart_holder>(m, "ViewportDndCallback")
        .def(init<>())
        .def("on_enter", &ViewportDndCallback::on_enter)
        .def("on_move", &ViewportDndCallback::on_move)
        .def("on_drop", &ViewportDndCallback::on_drop)
        .def("on_leave", &ViewportDndCallback::on_leave)
        .def("on_view_destroyed", &ViewportDndCallback::on_view_destroyed);

    class_<ViewportDndCallbackRegistry>(m, "ViewportDndCallbackRegistry")
        .def_static("register_callback", &ViewportDndCallbackRegistry::register_callback, "context_type"_a, "callback"_a)
        .def_static("unregister_callback", &ViewportDndCallbackRegistry::unregister_callback, "context_type"_a, "callback"_a);
}

OPENDCC_NAMESPACE_CLOSE
