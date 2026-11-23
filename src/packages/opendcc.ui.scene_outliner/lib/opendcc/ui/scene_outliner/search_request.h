/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/item_id.h"
#include <QObject>
#include <memory>

OPENDCC_NAMESPACE_OPEN

class SceneOutlinerSearchState;

/** @brief Completion handle that workers may retain after a request is cancelled or destroyed. */
class OPENDCC_SCENE_OUTLINER_API SceneOutlinerSearchCompletion
{
public:
    SceneOutlinerSearchCompletion() = default;
    void complete(SceneItemList matches) const;
    bool is_cancelled() const;

private:
    friend class SceneOutlinerSearchRequest;
    explicit SceneOutlinerSearchCompletion(std::shared_ptr<SceneOutlinerSearchState> state);
    std::shared_ptr<SceneOutlinerSearchState> m_state;
};

/**
 * @brief One search and its result, delivered on the request's thread.
 * @note Use completion() in workers. Cancel or destroy the request to discard
 * queued results; finished() is emitted at most once.
 */
class OPENDCC_SCENE_OUTLINER_API SceneOutlinerSearchRequest : public QObject
{
    Q_OBJECT
public:
    explicit SceneOutlinerSearchRequest(QObject* parent = nullptr);
    ~SceneOutlinerSearchRequest() override;
    SceneOutlinerSearchCompletion completion() const;
    bool is_cancelled() const;
    bool is_finished() const;

public Q_SLOTS:
    void complete(const SceneItemList& matches);
    void cancel();

Q_SIGNALS:
    void finished(const SceneItemList& matches);
    void cancelled();

private:
    void deliver(const SceneItemList& matches);
    std::shared_ptr<SceneOutlinerSearchState> m_state;
};

OPENDCC_NAMESPACE_CLOSE
