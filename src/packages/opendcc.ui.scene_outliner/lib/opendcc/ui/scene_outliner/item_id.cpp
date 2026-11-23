/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/item_id.h"

OPENDCC_NAMESPACE_OPEN

namespace
{
    class ExtensionItemData : public SceneItemId::Data
    {
    public:
        ExtensionItemData(std::string owner, std::string key)
            : owner(std::move(owner))
            , key(std::move(key))
        {
        }
        std::string type_id() const override { return "outliner_extension"; }
        size_t hash() const override { return std::hash<std::string>()(owner) ^ (std::hash<std::string>()(key) << 1); }
        bool equals(const Data& other) const override
        {
            const auto* rhs = dynamic_cast<const ExtensionItemData*>(&other);
            return rhs && owner == rhs->owner && key == rhs->key;
        }
        std::string owner, key;
    };
}

SceneItemId::SceneItemId(const std::string& owner, const std::string& key)
{
    if (!owner.empty() && !key.empty())
        m_data = std::make_shared<ExtensionItemData>(owner, key);
}

std::string SceneItemId::owner() const
{
    const auto* value = scene_item_data_cast<ExtensionItemData>(*this);
    return value ? value->owner : std::string();
}

std::string SceneItemId::key() const
{
    const auto* value = scene_item_data_cast<ExtensionItemData>(*this);
    return value ? value->key : std::string();
}

bool SceneItemId::operator==(const SceneItemId& other) const
{
    if (m_data == other.m_data)
        return true;
    if (!m_data || !other.m_data)
        return false;
    if (m_data->type_id() != other.m_data->type_id())
        return false;
    return m_data->equals(*other.m_data);
}

OPENDCC_NAMESPACE_CLOSE
