/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/usd_editor/usd_outliner/usd_selection_context.h"

#include "opendcc/app/core/selection_list.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/usd_editor/usd_outliner/usd_item_id.h"
#include <QPointer>
#include <algorithm>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::CommandInterface;

namespace
{
    SdfPathVector paths_of(const std::vector<SceneItemId>& items)
    {
        SdfPathVector paths;
        paths.reserve(items.size());
        for (const SceneItemId& item : items)
        {
            const SdfPath path = usd_path_of(item);
            if (!path.IsEmpty())
                paths.push_back(path);
        }
        return paths;
    }
}

struct UsdSelectionContext::SelectionUpdate
{
    explicit SelectionUpdate(UsdSelectionContext* context)
        : context(context)
        , previous(std::exchange(context->m_updating_selection, true))
    {
    }

    ~SelectionUpdate()
    {
        if (context)
            context->m_updating_selection = previous;
    }

    QPointer<UsdSelectionContext> context;
    bool previous;
};

UsdSelectionContext::UsdSelectionContext(QObject* parent)
    : ISceneOutlinerSelection(parent)
{
    m_selection_changed_handle = Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this]() {
        if (!m_updating_selection)
            m_custom_items.clear();
        Q_EMIT changed();
    });
    m_stage_changed_handle = Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this]() {
        m_custom_items.clear();
        Q_EMIT changed();
    });
}

UsdSelectionContext::~UsdSelectionContext()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_changed_handle);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_stage_changed_handle);
}

SceneItemList UsdSelectionContext::selected_items() const
{
    SceneItemList out;

    for (const SdfPath& path : Application::instance().get_prim_selection())
        out.push_back(make_usd_item_id(path));
    out.insert(out.end(), m_custom_items.begin(), m_custom_items.end());

    return out;
}

void UsdSelectionContext::set_selection(const std::vector<SceneItemId>& items)
{
    const SdfPathVector paths = paths_of(items);
    const SelectionUpdate update(this);
    m_custom_items.clear();
    for (const auto& item : items)
        if (item && usd_path_of(item).IsEmpty())
            m_custom_items.push_back(item);
    if (paths.empty())
        CommandInterface::execute("select", CommandArgs().arg(SelectionList()).kwarg("clear", true));
    else
        CommandInterface::execute("select", CommandArgs().arg(SelectionList(paths)).kwarg("replace", true));
    if (update.context)
        Q_EMIT changed();
}

void UsdSelectionContext::add_to_selection(const std::vector<SceneItemId>& items)
{
    const SelectionUpdate update(this);
    for (const auto& item : items)
        if (item && usd_path_of(item).IsEmpty() && std::find(m_custom_items.begin(), m_custom_items.end(), item) == m_custom_items.end())
            m_custom_items.push_back(item);
    const SdfPathVector paths = paths_of(items);
    if (!paths.empty())
        CommandInterface::execute("select", CommandArgs().arg(SelectionList(paths)).kwarg("add", true));
    if (update.context)
        Q_EMIT changed();
}

void UsdSelectionContext::remove_from_selection(const std::vector<SceneItemId>& items)
{
    const SelectionUpdate update(this);
    m_custom_items.erase(std::remove_if(m_custom_items.begin(), m_custom_items.end(),
                                        [&](const SceneItemId& item) { return std::find(items.begin(), items.end(), item) != items.end(); }),
                         m_custom_items.end());
    const SdfPathVector paths = paths_of(items);
    if (!paths.empty())
        CommandInterface::execute("select", CommandArgs().arg(SelectionList(paths)).kwarg("remove", true));
    if (update.context)
        Q_EMIT changed();
}

OPENDCC_NAMESPACE_CLOSE
