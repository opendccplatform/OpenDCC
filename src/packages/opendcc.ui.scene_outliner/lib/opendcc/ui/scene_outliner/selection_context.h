/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/item_id.h"

#include <QObject>
#include <vector>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Synchronizes the outliner with backend selection.
 * @note Query and edit on the GUI thread; changed() may be emitted from workers.
 * Adapters route artist selection edits through the command system.
 */
class OPENDCC_SCENE_OUTLINER_API ISceneOutlinerSelection : public QObject
{
    Q_OBJECT
public:
    explicit ISceneOutlinerSelection(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    virtual ~ISceneOutlinerSelection() = default;

    virtual SceneItemList selected_items() const = 0;
    virtual void set_selection(const SceneItemList& items) = 0;
    virtual void add_to_selection(const SceneItemList& items) = 0;
    virtual void remove_from_selection(const SceneItemList& items) = 0;

    void notify() { Q_EMIT changed(); }

Q_SIGNALS:
    void changed();
};

OPENDCC_NAMESPACE_CLOSE
