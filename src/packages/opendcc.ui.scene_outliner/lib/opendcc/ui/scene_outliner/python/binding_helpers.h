/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/base/pybind_bridge/shiboken_dependencies.h"
#include "opendcc/ui/scene_outliner/outliner_widget.h"
#include "opendcc/ui/scene_outliner/outliner_model.h"
#include <QThread>

OPENDCC_NAMESPACE_OPEN

namespace outliner_bindings
{
    using base::pybind_bridge::PythonDependencies;
    using base::pybind_bridge::retain_dependencies;

    inline void initialize(PyObject* owner, SceneOutlinerModel* model)
    {
        const auto references = [](SceneOutlinerModel* model, PythonDependencies& dependencies) {
            const QPointer<SceneOutlinerModel> self(model);
            auto* source = model->data_source();
            auto* pending = model->pending_data_source();
            const auto is_current = [&]() {
                return self && source == model->data_source() && pending == model->pending_data_source();
            };
            return dependencies.add(source) && is_current() && dependencies.add(pending) && is_current();
        };
        retain_dependencies(owner, model, references, { &SceneOutlinerModel::data_source_changed });
    }

    inline void initialize(PyObject* owner, SceneOutliner* outliner)
    {
        const auto references = [](SceneOutliner* outliner, PythonDependencies& dependencies) {
            const QPointer<SceneOutliner> self(outliner);
            const QPointer<SceneOutlinerModel> model(outliner->model());
            auto* wrapper = dependencies.add(model.data());
            if (!wrapper)
                return false;
            initialize(wrapper, model.data());
            return self && model == outliner->model() && dependencies.add(outliner->selection_context());
        };
        retain_dependencies(owner, outliner, references, { &SceneOutliner::model_changed, &SceneOutliner::selection_context_changed });
    }
}

OPENDCC_NAMESPACE_CLOSE
