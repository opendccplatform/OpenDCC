/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/usd_outliner/api.h"
#include "opendcc/app/core/application.h"
#include "opendcc/ui/scene_outliner/selection_context.h"

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Adapts application prim selection to the outliner.
 * @note Selection edits use commands for undo and Python recording.
 */
class OPENDCC_USD_OUTLINER_API UsdSelectionContext : public ISceneOutlinerSelection
{
    Q_OBJECT
public:
    explicit UsdSelectionContext(QObject* parent = nullptr);
    ~UsdSelectionContext() override;

    SceneItemList selected_items() const override;
    void set_selection(const std::vector<SceneItemId>& items) override;
    void add_to_selection(const std::vector<SceneItemId>& items) override;
    void remove_from_selection(const std::vector<SceneItemId>& items) override;

private:
    struct SelectionUpdate;
    std::vector<SceneItemId> m_custom_items;
    bool m_updating_selection = false;
    Application::CallbackHandle m_selection_changed_handle;
    Application::CallbackHandle m_stage_changed_handle;
};

OPENDCC_NAMESPACE_CLOSE
