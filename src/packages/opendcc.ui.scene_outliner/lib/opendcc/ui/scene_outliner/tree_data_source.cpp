/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/tree_data_source.h"

#include <QThread>
#include <QPointer>
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.ui.scene_outliner");

ISceneOutlinerDataSource::ISceneOutlinerDataSource(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<SceneOutlinerChange>();
    qRegisterMetaType<SceneOutlinerChange>("SceneOutlinerChange");
    qRegisterMetaType<SceneItemList>("SceneItemList");
}

bool ISceneOutlinerDataSource::has_children(const SceneItemId& item) const
{
    return !children(item).empty();
}

SceneItemList ISceneOutlinerDataSource::search(const std::string& query) const
{
    const QPointer<const ISceneOutlinerDataSource> self(this);
    SceneItemList out;

    if (query.empty())
        return out;
    const auto needle = QString::fromStdString(query).toCaseFolded();

    // Roots are navigation anchors, not search targets.
    SceneItemList stack;
    SceneItemSet visited;
    const auto root_items = roots();
    if (!self)
        return {};
    for (const auto& root : root_items)
    {
        auto child_items = children(root);
        if (!self)
            return {};
        stack.insert(stack.end(), child_items.begin(), child_items.end());
    }
    while (!stack.empty())
    {
        const SceneItemId item = stack.back();
        stack.pop_back();
        if (!item || !visited.insert(item).second)
            continue;
        const bool exists = has_item(item);
        if (!self)
            return {};
        if (!exists)
            continue;
        const auto name = display_name(item);
        if (!self)
            return {};
        if (QString::fromStdString(name).toCaseFolded().contains(needle))
            out.push_back(item);
        auto child_items = children(item);
        if (!self)
            return {};
        stack.insert(stack.end(), child_items.begin(), child_items.end());
    }

    return out;
}

void ISceneOutlinerDataSource::start_search(const std::string& query, SceneOutlinerSearchRequest* request) const
{
    if (!request || request->is_cancelled() || request->is_finished())
        return;
    if (request->thread() != thread() || QThread::currentThread() != thread())
    {
        OPENDCC_ERROR("Search requests must belong to the data source thread.");
        return;
    }
    connect(this, &QObject::destroyed, request, &SceneOutlinerSearchRequest::cancel);
    connect(this, &ISceneOutlinerDataSource::changed, request, [request](const SceneOutlinerChange& change) {
        if (change.type != SceneOutlinerChange::Type::ITEMS_CHANGED || change.affects_search)
            request->cancel();
    });
    search_async(query, request);
}

void ISceneOutlinerDataSource::search_async(const std::string& query, SceneOutlinerSearchRequest* request) const
{
    auto completed = request->completion();
    completed.complete(search(query));
}

OPENDCC_NAMESPACE_CLOSE
