// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/ui/node_icon_registry.h"
#include "opendcc/base/logging/logger.h"
#include "QPainter"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.ui.icons");

PXR_NAMESPACE_USING_DIRECTIVE

NodeIconRegistry& NodeIconRegistry::instance()
{
    static NodeIconRegistry instance;
    return instance;
}

void NodeIconRegistry::register_icon(const TfToken& context_type, const std::string& node_type, const std::string& icon_path,
                                     const std::string& svg_path)
{
    register_icon(context_type, node_type, QPixmap(QString::fromStdString(icon_path)), svg_path);
}

void NodeIconRegistry::register_icon(const TfToken& context_type, const std::string& node_type, const QPixmap& pixmap, const std::string& svg_path)
{
    if (pixmap.isNull())
    {
        OPENDCC_WARN("Failed to register icon for node type '{}' in '{}' context: pixmap is null.", node_type.c_str(), context_type.GetText());
        return;
    }

    auto& node_icons = m_registry[context_type];
    auto node_iter = node_icons.find(node_type);
    if (node_iter != node_icons.end())
    {
        OPENDCC_WARN("Unable to register icon for node type '{}' in '{}' context: icon with the same node type already registered.",
                     node_type.c_str(), context_type.GetText());
        return;
    }
    auto& per_flags = node_icons[node_type];

    NodeIcon node_icon(pixmap);
    if (svg_path != "")
    {
        node_icon.set_svg(svg_path);
    }

    per_flags[IconFlags::NONE] = node_icon;

    {
        QPixmap is_not_on_edit_target_icon(":/icons/is_not_on_edit_target");
        QPixmap edited_pixmap(pixmap);
        QPainter painter(&edited_pixmap);
        painter.drawPixmap(0, 0, 10, 10, is_not_on_edit_target_icon);

        NodeIcon edited_node_icon(edited_pixmap);

        per_flags[IconFlags::NOT_ON_EDIT_TARGET] = edited_node_icon;
    }
}

void NodeIconRegistry::unregister_icon(const TfToken& context_type, const std::string& node_type)
{
    auto& node_icons = m_registry[context_type];
    auto iter = node_icons.find(node_type);
    if (iter == node_icons.end())
    {
        OPENDCC_WARN("Unable to unregister icon path for node type '{}' in '{}' context: icon with specified node type is not registered.",
                     node_type.c_str(), context_type.GetText());
        return;
    }
    node_icons.erase(iter);
}

const QPixmap& NodeIconRegistry::get_icon(const TfToken& context_type, const std::string& node_type, IconFlags flags /*= IconFlags::NONE*/)
{
    static QPixmap empty_pixmap = QPixmap(":icons/withouttype");
    auto& node_icons = m_registry[context_type];
    auto node_iter = node_icons.find(node_type);
    if (node_iter == node_icons.end())
    {
        OPENDCC_DEBUG("Unable to get icon for node type '{}' in '{}' context: icon with specified node type is not registered.", node_type.c_str(),
                      context_type.GetText());
        return empty_pixmap;
    }

    auto pixmap_iter = node_iter->second.find(flags);
    if (pixmap_iter == node_iter->second.end())
    {
        OPENDCC_DEBUG("Unable to get icon for node type '{}' in '{}' context: icon with specified flags doesn't exist.", node_type.c_str(),
                      context_type.GetText());
        return empty_pixmap;
    }
    return pixmap_iter->second.get_icon();
}

const std::string& NodeIconRegistry::get_svg(const TfToken& context_type, const std::string& node_type, IconFlags flags /*= IconFlags::NONE*/)
{
    static std::string empty_svg = ":icons/node_editor/withouttype";
    auto& node_icons = m_registry[context_type];
    auto node_iter = node_icons.find(node_type);
    if (node_iter == node_icons.end())
    {
        OPENDCC_DEBUG("Unable to get icon for node type '{}' in '{}' context: icon with specified node type is not registered.", node_type.c_str(),
                      context_type.GetText());
        return empty_svg;
    }

    auto pixmap_iter = node_iter->second.find(flags);
    if (pixmap_iter == node_iter->second.end())
    {
        OPENDCC_DEBUG("Unable to get icon for node type '{}' in '{}' context: icon with specified flags doesn't exist.", node_type.c_str(),
                      context_type.GetText());
        return empty_svg;
    }
    return pixmap_iter->second.get_svg();
}

bool NodeIconRegistry::is_icon_exists(const PXR_NS::TfToken& context_type, const std::string& node_type, IconFlags flags /*= IconFlags::NONE*/)
{
    auto& node_icons = m_registry[context_type];
    auto node_iter = node_icons.find(node_type);
    if (node_iter == node_icons.end())
    {
        return false;
    }
    auto pixmap_iter = node_iter->second.find(flags);
    if (pixmap_iter == node_iter->second.end())
    {
        return false;
    }
    return true;
}

bool NodeIconRegistry::is_svg_exists(const PXR_NS::TfToken& context_type, const std::string& node_type, IconFlags flags /*= IconFlags::NONE*/)
{
    auto& node_icons = m_registry[context_type];
    auto node_iter = node_icons.find(node_type);
    if (node_iter == node_icons.end())
    {
        return false;
    }
    auto pixmap_iter = node_iter->second.find(flags);
    if (pixmap_iter == node_iter->second.end())
    {
        return false;
    }
    return pixmap_iter->second.has_svg();
}

OPENDCC_NAMESPACE_CLOSE
