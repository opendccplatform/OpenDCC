// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "wrap_undo.h"
#include "opendcc/base/pybind_bridge/shiboken.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/base/commands_api/core/command.h"

OPENDCC_NAMESPACE_OPEN

using namespace commands;
using namespace pybind11;

void py_interp::bind::wrap_undo(pybind11::module& m)
{
    class_<UndoStack>(m, "UndoStack")
        .def("is_enabled", &UndoStack::is_enabled)
        .def("set_enabled", &UndoStack::set_enabled)
        .def("get_undo_limit", &UndoStack::get_undo_limit)
        .def("get_size", &UndoStack::get_size)
        .def("can_undo", &UndoStack::can_undo)
        .def("can_redo", &UndoStack::can_redo)
        .def("set_undo_limit", &UndoStack::set_undo_limit)
        .def("push", overload_cast<std::shared_ptr<base::commands::UndoCommand>, bool>(&UndoStack::push), arg("command"), arg("execute") = false)
        .def("undo", &UndoStack::undo)
        .def("redo", &UndoStack::redo)
        .def("clear", &UndoStack::clear);
}

OPENDCC_NAMESPACE_CLOSE
