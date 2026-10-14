// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/visibility_mask.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/ui/application_ui.h"
#include <pxr/base/tf/stl.h>

PXR_NAMESPACE_OPEN_SCOPE
TF_DEFINE_PUBLIC_TOKENS(PrimVisibilityTypes, OPENDCC_PRIM_VISIBILITY_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.visibility");

bool VisibilityMask::is_visible(const TfToken& type, const PXR_NS::TfToken& group /*= PXR_NS::TfToken()*/) const
{
    if (auto group_vis_map = TfMapLookupPtr(m_visibility_map, group))
        return group_vis_map->find(type) == group_vis_map->end();
    return true;
}

void VisibilityMask::set_visible(bool visible, const PXR_NS::TfToken& type, const PXR_NS::TfToken& group /*= PXR_NS::TfToken()*/)
{
    if (!visible)
    {
        auto group_vis_map = TfMapLookupPtr(m_visibility_map, group);
        if (!group_vis_map)
            group_vis_map = &m_visibility_map[group];
        const auto result = group_vis_map->insert(type);
        m_is_dirty = result.second;
    }
    else
    {
        auto group_iter = m_visibility_map.find(group);
        if (group_iter != m_visibility_map.end())
        {
            m_is_dirty = group_iter->second.erase(type) != 0;
            if (group_iter->second.empty())
            {
                m_visibility_map.erase(group_iter);
            }
        }
    }
}

bool VisibilityMask::is_dirty() const
{
    return m_is_dirty;
}

void VisibilityMask::mark_clean()
{
    m_is_dirty = false;
}

bool OPENDCC_NAMESPACE::PrimVisibilityRegistry::register_prim_type(const PXR_NS::TfToken& type, const PXR_NS::TfToken& group,
                                                                   const std::string& ui_name)
{
    auto& self = instance();
    PrimVisibilityType new_type = { group, type, ui_name };
    for (const auto& entry : self.m_prim_visibility_data)
    {
        if (new_type == entry)
        {
            OPENDCC_ERROR("Failed to add new prim visibility type: type '{}' with group '{}' already exists.", type.GetText(), group.GetText());
            return false;
        }
    }
    self.m_prim_visibility_data.push_back(new_type);
    return true;
}

bool OPENDCC_NAMESPACE::PrimVisibilityRegistry::unregister_prim_type(const PXR_NS::TfToken& type, const PXR_NS::TfToken& group)
{
    auto& self = instance();
    self.m_prim_visibility_data.erase(
        self.m_prim_visibility_data.end(),
        std::remove_if(self.m_prim_visibility_data.begin(), self.m_prim_visibility_data.end(),
                       [&type, &group](const PrimVisibilityType& vis_type) { return type == vis_type.type && group == vis_type.group; }));
    return true;
}

const std::vector<OPENDCC_NAMESPACE::PrimVisibilityRegistry::PrimVisibilityType>& OPENDCC_NAMESPACE::PrimVisibilityRegistry::
    get_prim_visibility_types()
{
    return instance().m_prim_visibility_data;
}

OPENDCC_NAMESPACE::PrimVisibilityRegistry::PrimVisibilityRegistry()
{
    // Built-in types
    m_prim_visibility_data.emplace_back(PrimVisibilityType { TfToken(""), TfToken("mesh"), i18n("visibility_mask.common", "Mesh").toStdString() });
    m_prim_visibility_data.emplace_back(
        PrimVisibilityType { TfToken(""), TfToken("basisCurves"), i18n("visibility_mask.common", "Basis Curves").toStdString() });
    m_prim_visibility_data.emplace_back(
        PrimVisibilityType { TfToken(""), TfToken("camera"), i18n("visibility_mask.common", "Camera").toStdString() });
    m_prim_visibility_data.emplace_back(PrimVisibilityType { TfToken(""), TfToken("light"), i18n("visibility_mask.common", "Light").toStdString() });
}

OPENDCC_NAMESPACE::PrimVisibilityRegistry::CallbackHandle OPENDCC_NAMESPACE::PrimVisibilityRegistry::register_visibility_types_changes(
    std::function<void()> callback)
{
    return instance().m_dispatcher.appendListener(EventType::VISIBILITY_TYPES_CHANGED, callback);
}

void OPENDCC_NAMESPACE::PrimVisibilityRegistry::unregister_visibility_types_changes(CallbackHandle handle)
{
    instance().m_dispatcher.removeListener(EventType::VISIBILITY_TYPES_CHANGED, handle);
}

OPENDCC_NAMESPACE::PrimVisibilityRegistry& PrimVisibilityRegistry::instance()
{
    static PrimVisibilityRegistry instance;
    return instance;
}

OPENDCC_NAMESPACE_CLOSE
