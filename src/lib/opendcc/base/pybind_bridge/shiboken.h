/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <pyside6_qtcore_python.h>
#include <pyside6_qtgui_python.h>
#include <pyside6_qtwidgets_python.h>
#else
#include <pyside2_qtcore_python.h>
#include <pyside2_qtgui_python.h>
#include <pyside2_qtwidgets_python.h>
#endif

#include "opendcc/opendcc.h"
#include <type_traits>
#include "opendcc/base/pybind_bridge/pybind11.h"
#include <shiboken.h>
#include <QMainWindow>
#include <QUndoStack>
#include <QSettings>
#include <QMenu>
#include <QDragEnterEvent>
#include <QContextMenuEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QPixmap>

OPENDCC_NAMESPACE_OPEN

template <class T>
struct OPENDCC_API_HIDDEN qt_type_caster
{
    static_assert(std::is_same_v<std::decay_t<T>, T>, "T must be decayed");
    using base_type = T;
    using underlying_type = T*;

    static constexpr auto is_qobject = std::is_base_of_v<QObject, T>;
    static constexpr auto is_copyable = !is_qobject && std::is_copy_assignable_v<base_type>;

    static bool load(underlying_type* value, pybind11::handle src)
    {
        if (!Shiboken::Object::isValid(src.ptr()))
            return false;

        PythonToCppFunc converter =
            Shiboken::Conversions::isPythonToCppPointerConvertible(reinterpret_cast<SbkObjectType*>(Shiboken::SbkType<base_type>()), src.ptr());
        if (!converter)
            return false;

        if constexpr (!is_qobject)
        {
            converter(src.ptr(), value);
        }
        else
        {
            // We don't transfer ownership for QObject derived types, since that kills python signals and slots
            // instead we increment ref count
            // and make callback to decrease it when underlying QObject is destroyed
            src.inc_ref();
            converter(src.ptr(), value);

            QObject* qobject = reinterpret_cast<QObject*>(*value);
            QObject::connect(qobject, &QObject::destroyed, [src]() {
                if (src && src.ref_count() > 1)
                    src.dec_ref();
            });
        }
        return true;
    }

    static pybind11::handle cast(underlying_type src)
    {
        auto obj = Shiboken::Conversions::pointerToPython(reinterpret_cast<SbkObjectType*>(Shiboken::SbkType<base_type>()), src);
        if (!obj)
            return pybind11::none();
        return pybind11::handle(obj).inc_ref();
    }
    template <class U = base_type, class = std::enable_if_t<!is_qobject && std::is_copy_assignable_v<U>>>
    static pybind11::handle cast(base_type src)
    {
        auto obj = Shiboken::Conversions::copyToPython(reinterpret_cast<SbkObjectType*>(Shiboken::SbkType<base_type>()), &src);
        if (!obj)
            return pybind11::none();
        return pybind11::handle(obj).inc_ref();
    }
};

OPENDCC_NAMESPACE_CLOSE

#define OPENDCC_PYBIND_SHIBOKEN_BRIDGE(cpp_type)                                                  \
    namespace pybind11                                                                            \
    {                                                                                             \
        namespace detail                                                                          \
        {                                                                                         \
            template <>                                                                           \
            struct type_caster<cpp_type> : type_caster_base<cpp_type>                             \
            {                                                                                     \
                using qt_type_caster = OPENDCC_NAMESPACE::qt_type_caster<cpp_type>;               \
                using base_type = qt_type_caster::base_type;                                      \
                using underlying_type = qt_type_caster::underlying_type;                          \
                                                                                                  \
                bool load(handle src, bool)                                                       \
                {                                                                                 \
                    return qt_type_caster::load(reinterpret_cast<underlying_type*>(&value), src); \
                }                                                                                 \
                template <class U>                                                                \
                static handle cast(U&& src, return_value_policy, handle)                          \
                {                                                                                 \
                    return qt_type_caster::cast(std::forward<U>(src));                            \
                }                                                                                 \
            };                                                                                    \
        }                                                                                         \
    }

OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QObject);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QWidget);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QMainWindow);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QMenu);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QContextMenuEvent);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QUndoStack);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QDragEnterEvent);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QDragMoveEvent);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QDragLeaveEvent);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QDropEvent);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QSettings);
OPENDCC_PYBIND_SHIBOKEN_BRIDGE(QPixmap);
