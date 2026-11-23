/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/notice_batcher.h"

#include <QMetaObject>
#include <QPointer>
#include <utility>

OPENDCC_NAMESPACE_OPEN

SceneOutlinerBatcher::SceneOutlinerBatcher(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<SceneItemList>();
}

SceneOutlinerBatcher::~SceneOutlinerBatcher() = default;

void SceneOutlinerBatcher::add_resync(const ItemList& parents)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.reset)
        return;
    for (const auto& parent : parents)
        m_pending.parents.insert(parent);
    schedule();
}

void SceneOutlinerBatcher::add_changed(const ItemList& items, bool affects_search)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_pending.reset)
        return;
    m_pending.affects_search |= affects_search;
    if (items.empty())
    {
        m_pending.all_changed = true;
        m_pending.items.clear();
    }
    else if (!m_pending.all_changed)
        for (const auto& item : items)
            m_pending.items.insert(item);
    schedule();
}

void SceneOutlinerBatcher::request_reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_delivery.cancel();
    m_pending = {};
    m_pending.reset = true;
    schedule();
}

void SceneOutlinerBatcher::cancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_delivery.cancel();
    m_pending = {};
}

void SceneOutlinerBatcher::schedule()
{
    if (m_scheduled)
        return;
    m_scheduled = true;
    // Posts to this object's thread event loop: coalesces to one flush per
    // event-loop turn and marshals background-thread notices to the UI thread.
    QMetaObject::invokeMethod(
        this, [this]() { flush(); }, Qt::QueuedConnection);
}

void SceneOutlinerBatcher::flush()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_scheduled = false;
    const auto pending = std::exchange(m_pending, {});
    const auto delivery = m_delivery.restart();
    lock.unlock();

    if (pending.reset)
    {
        Q_EMIT reset();
        return;
    }

    if (!pending.parents.empty())
    {
        const QPointer<SceneOutlinerBatcher> self(this);
        Q_EMIT resynced(ItemList(pending.parents.begin(), pending.parents.end()));
        if (!self || delivery.is_cancelled())
            return;
    }

    if (pending.all_changed || !pending.items.empty())
        Q_EMIT items_changed(ItemList(pending.items.begin(), pending.items.end()), pending.affects_search);
}

OPENDCC_NAMESPACE_CLOSE
