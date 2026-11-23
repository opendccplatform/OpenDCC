/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/usd_outliner/api.h"
#include "opendcc/ui/scene_outliner/item_id.h"

#include <pxr/usd/sdf/path.h>

OPENDCC_NAMESPACE_OPEN

using UsdOutlinerPath = PXR_NS::SdfPath;

/**
 * @brief Identifies a prim by its path on the current stage.
 */
class OPENDCC_USD_OUTLINER_API UsdPrimItemData : public SceneItemId::Data
{
public:
    explicit UsdPrimItemData(const PXR_NS::SdfPath& path)
        : m_path(path)
    {
    }

    std::string type_id() const override { return "usd_prim"; }
    std::size_t hash() const override { return m_path.GetHash(); }
    bool equals(const Data& other) const override
    {
        const auto* data = dynamic_cast<const UsdPrimItemData*>(&other);
        return data && m_path == data->m_path;
    }

    const PXR_NS::SdfPath& path() const { return m_path; }

private:
    PXR_NS::SdfPath m_path;
};

inline SceneItemId make_usd_item_id(const UsdOutlinerPath& path)
{
    return make_scene_item_id<UsdPrimItemData>(path);
}

inline UsdOutlinerPath usd_path_of(const SceneItemId& id)
{
    const UsdPrimItemData* data = scene_item_data_cast<UsdPrimItemData>(id);
    return data ? data->path() : PXR_NS::SdfPath();
}

OPENDCC_NAMESPACE_CLOSE
