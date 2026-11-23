/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/base/pybind_bridge/shiboken_helpers.h"
#include <QObject>
#include <QPointer>
#include <initializer_list>
#include <memory>
#include <type_traits>

OPENDCC_NAMESPACE_OPEN

namespace base::pybind_bridge
{
    /** @brief Collect typed QObject dependencies while holding the GIL. */
    class PythonDependencies
    {
    public:
        PythonDependencies()
            : m_references(PyList_New(0))
        {
        }
        PythonDependencies(const PythonDependencies&) = delete;
        PythonDependencies& operator=(const PythonDependencies&) = delete;

        /** @return The borrowed wrapper retained by this collection, or null on error. */
        template <class T>
        PyObject* add(T* object)
        {
            static_assert(std::is_base_of_v<QObject, T>);
            if (m_references.isNull())
                return nullptr;
            Shiboken::AutoDecRef wrapper(Shiboken::Conversions::pointerToPython(reinterpret_cast<SbkObjectType*>(Shiboken::SbkType<T>()), object));
            if (wrapper.isNull() || PyList_Append(m_references.object(), wrapper.object()) < 0)
                return nullptr;
            return wrapper.object();
        }

        explicit operator bool() const { return !m_references.isNull(); }
        PyObject* get() { return m_references.object(); }

    private:
        Shiboken::AutoDecRef m_references;
    };

    namespace details
    {
        inline constexpr const char* dependency_attribute = "_opendcc_dependencies";

        inline void set_python_dependencies(PyObject* owner, PyObject* references)
        {
            Py_INCREF(owner);
            Shiboken::AutoDecRef keep_owner(owner);
            Shiboken::AutoDecRef attributes(PyObject_GenericGetDict(owner, nullptr));
            // Keep edges visible to Python GC and publish before decref can reenter a setter.
            if (!attributes.isNull())
                PyDict_SetItemString(attributes.object(), dependency_attribute, references);
        }
    }

    /**
     * @brief Refresh a wrapper's dependencies when its QObject emits a change signal.
     * @note The collector fills PythonDependencies and returns false if its snapshot is invalid.
     * Register one collector per wrapper. The caller holds the GIL.
     */
    template <class Owner, class References>
    void retain_dependencies(PyObject* wrapper, Owner* owner, References references, std::initializer_list<void (Owner::*)()> change_signals)
    {
        if (!Shiboken::Object::isValid(wrapper, false))
            return;
        const QPointer<Owner> self(owner);
        Shiboken::AutoDecRef attributes(PyObject_GenericGetDict(wrapper, nullptr));
        if (attributes.isNull() || PyDict_GetItemString(attributes.object(), details::dependency_attribute))
            return;
        Shiboken::AutoDecRef weak(PyWeakref_NewRef(wrapper, nullptr));
        if (weak.isNull() || !self)
            return;
        auto reference = std::make_shared<PythonReference>(weak.object());
        if (PyDict_SetItemString(attributes.object(), details::dependency_attribute, Py_None) < 0)
            return;
        QObject::connect(owner, &QObject::destroyed, owner, [reference]() {
            Shiboken::GilState gil;
            auto* wrapper = PyWeakref_GetObject(reference->get());
            if (wrapper != Py_None)
                details::set_python_dependencies(wrapper, Py_None);
        });
        const auto update = [self, reference, references]() {
            Shiboken::GilState gil;
            auto* wrapper = PyWeakref_GetObject(reference->get());
            if (!self || wrapper == Py_None ||
                Shiboken::BindingManager::instance().retrieveWrapper(self.data()) != reinterpret_cast<SbkObject*>(wrapper))
                return;
            Py_INCREF(wrapper);
            Shiboken::AutoDecRef keep_owner(wrapper);
            PythonDependencies dependencies;
            if (self && dependencies && references(self.data(), dependencies))
                details::set_python_dependencies(wrapper, dependencies.get());
        };
        for (const auto signal : change_signals)
            QObject::connect(owner, signal, owner, update);
        update();
    }
}

OPENDCC_NAMESPACE_CLOSE
