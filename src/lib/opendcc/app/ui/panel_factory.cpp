// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/ui/panel_factory.h"
#include <QWidget>
#include <QVariant>

OPENDCC_NAMESPACE_OPEN

PanelFactory& PanelFactory::instance()
{
    static PanelFactory instance;
    return instance;
}

bool PanelFactory::register_panel(const std::string& type, PanelFactoryWidgetCallback callback, const std::string& label, bool singleton,
                                  const std::string& icon, const std::string& group)
{
    auto find_it = m_registry_map.find(type);
    if (find_it == m_registry_map.end())
    {
        Entry entry;
        entry.type = type;
        entry.callback_fn = callback;
        if (entry.label.empty())
        {
            entry.label = label;
        }
        else
        {
            entry.label = type;
        }
        entry.singleton = singleton;
        entry.icon = icon;
        entry.group = group;

        m_registry_map.insert(std::make_pair(type, entry));
        return true;
    }
    return false;
}

bool PanelFactory::unregister_panel(const std::string& type)
{
    auto find_it = m_registry_map.find(type);
    if (find_it != m_registry_map.end())
    {
        m_registry_map.erase(find_it);
        return true;
    }
    return false;
}

std::string PanelFactory::get_panel_title(const std::string& type)
{
    auto find_it = m_registry_map.find(type);
    if (find_it != m_registry_map.end())
    {
        return find_it->second.label;
    }
    return "";
}

QWidget* PanelFactory::create_panel_widget(const std::string& type)
{
    auto find_it = m_registry_map.find(type);
    if (find_it != m_registry_map.end())
    {
        return create_panel_widget(find_it->second);
    }
    return nullptr;
}

QWidget* PanelFactory::create_panel_widget(const Entry& entry)
{
    return entry.callback_fn();
}

const std::unordered_map<std::string, PanelFactory::Entry>& PanelFactory::get_registry() const
{
    return m_registry_map;
}

OPENDCC_NAMESPACE_CLOSE
