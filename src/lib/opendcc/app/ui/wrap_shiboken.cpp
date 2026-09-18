// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/ui/wrap_shiboken.h"
#include "opendcc/base/logging/logger.h"

#include <shiboken.h>
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

// shiboken names these globals after the binding generation, and the generated wrappers
// reference them by that exact name, so they must match the PySide in use.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define DCC_PYSIDE_PKG "PySide6"
PyTypeObject **SbkPySide6_QtCoreTypes = NULL;
PyTypeObject **SbkPySide6_QtGuiTypes = NULL;
PyTypeObject **SbkPySide6_QtWidgetsTypes = NULL;
#define DCC_SBK_QTCORE_TYPES SbkPySide6_QtCoreTypes
#define DCC_SBK_QTGUI_TYPES SbkPySide6_QtGuiTypes
#define DCC_SBK_QTWIDGETS_TYPES SbkPySide6_QtWidgetsTypes
#else
#define DCC_PYSIDE_PKG "PySide2"
PyTypeObject **SbkPySide2_QtCoreTypes = NULL;
PyTypeObject **SbkPySide2_QtGuiTypes = NULL;
PyTypeObject **SbkPySide2_QtWidgetsTypes = NULL;
#define DCC_SBK_QTCORE_TYPES SbkPySide2_QtCoreTypes
#define DCC_SBK_QTGUI_TYPES SbkPySide2_QtGuiTypes
#define DCC_SBK_QTWIDGETS_TYPES SbkPySide2_QtWidgetsTypes
#endif

OPENDCC_NAMESPACE_OPEN

// inspired by https://github.com/cryos/avogadro/blob/master/libavogadro/src/python/sip.cpp
namespace py_interp
{

    namespace bind
    {
        bool wrap_shiboken()
        {
            Shiboken::AutoDecRef core_module(Shiboken::Module::import(DCC_PYSIDE_PKG ".QtCore"));
            if (core_module.isNull())
                return false;

            DCC_SBK_QTCORE_TYPES = Shiboken::Module::getTypes(core_module);

            Shiboken::AutoDecRef gui_module(Shiboken::Module::import(DCC_PYSIDE_PKG ".QtGui"));
            if (gui_module.isNull())
                return false;
            DCC_SBK_QTGUI_TYPES = Shiboken::Module::getTypes(gui_module);

            Shiboken::AutoDecRef widgets_module(Shiboken::Module::import(DCC_PYSIDE_PKG ".QtWidgets"));
            if (widgets_module.isNull())
                return false;
            DCC_SBK_QTWIDGETS_TYPES = Shiboken::Module::getTypes(widgets_module);

            return true;
        }

    }
}
OPENDCC_NAMESPACE_CLOSE
