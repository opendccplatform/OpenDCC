/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/details/cancellation_token.h"
#include "opendcc/ui/scene_outliner/item_id.h"

#include <QObject>

#include <mutex>
#include <unordered_set>
#include <vector>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Coalesces backend changes into one queued event-loop delivery.
 * @note Construct and destroy on the UI thread. Producers may add changes
 * from workers; callbacks run on the owning thread after releasing the lock.
 */
class OPENDCC_SCENE_OUTLINER_API SceneOutlinerBatcher : public QObject
{
    Q_OBJECT
public:
    using ItemList = SceneItemList;

    explicit SceneOutlinerBatcher(QObject* parent = nullptr);
    ~SceneOutlinerBatcher() override;

    /// Accumulate a structural resync of the given parents (their child sets
    /// changed).
    void add_resync(const ItemList& parents);
    /// Accumulate data changes; an empty list refreshes all materialized rows.
    void add_changed(const ItemList& items, bool affects_search = true);
    /// Request a full rebuild; supersedes any pending resync/changed work.
    void request_reset();

    /// Drop anything pending without delivering it.
    void cancel();

Q_SIGNALS:
    /// Child lists to reconcile after a structural change.
    void resynced(const SceneItemList& parents);
    /// Column data to refresh; an empty list refreshes all materialized rows.
    void items_changed(const SceneItemList& items, bool affects_search);
    /// A full rebuild supersedes the accumulated changes.
    void reset();

private:
    void schedule();
    void flush();

    struct PendingNotices
    {
        SceneItemSet parents;
        SceneItemSet items;
        bool reset = false;
        bool all_changed = false;
        bool affects_search = false;
    };

    std::mutex m_mutex;
    PendingNotices m_pending;
    bool m_scheduled = false;
    scene_outliner::CancellationToken m_delivery;
};

OPENDCC_NAMESPACE_CLOSE
