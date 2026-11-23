/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include <QMetaType>

#include <string>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include <unordered_set>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Opaque item identity supplied by a data source.
 * @note Data type ID, hash and equality must remain stable while an ID is in
 * use. Adapters recover their data with scene_item_data_cast<T>().
 */
class OPENDCC_SCENE_OUTLINER_API SceneItemId
{
public:
    /// Backend-supplied identity data. Subclass this in an adapter.
    class Data
    {
    public:
        virtual ~Data() = default;

        /// Dispatch key, e.g. "scene_node", "material_node". Stable for the lifetime
        /// of the item.
        virtual std::string type_id() const = 0;
        virtual std::size_t hash() const = 0;
        /// `other` is guaranteed to be non-null and of the same `type_id()`.
        virtual bool equals(const Data& other) const = 0;
    };

    using DataPtr = std::shared_ptr<const Data>;

    SceneItemId() = default;
    SceneItemId(const std::string& owner, const std::string& key);
    std::string owner() const;
    std::string key() const;
    explicit SceneItemId(DataPtr data)
        : m_data(std::move(data))
    {
    }

    bool is_valid() const { return static_cast<bool>(m_data); }
    explicit operator bool() const { return is_valid(); }

    std::string type_id() const { return m_data ? m_data->type_id() : std::string(); }
    std::size_t hash() const { return m_data ? m_data->hash() : 0; }

    const DataPtr& data() const { return m_data; }

    bool operator==(const SceneItemId& other) const;
    bool operator!=(const SceneItemId& other) const { return !(*this == other); }

    struct Hash
    {
        std::size_t operator()(const SceneItemId& id) const { return id.hash(); }
    };

private:
    DataPtr m_data;
};

inline std::size_t hash_scene_item_id(const SceneItemId& item)
{
    return item.hash();
}

using SceneItemList = std::vector<SceneItemId>;
using SceneItemSet = std::unordered_set<SceneItemId, SceneItemId::Hash>;

/// Build a `SceneItemId` wrapping a concrete adapter data type.
template <class DataT, class... Args>
SceneItemId make_scene_item_id(Args&&... args)
{
    return SceneItemId(std::make_shared<const DataT>(std::forward<Args>(args)...));
}

/// Recover an adapter's item data from a `SceneItemId`, or nullptr if the
/// data is empty or of a different type.
template <class DataT>
const DataT* scene_item_data_cast(const SceneItemId& id)
{
    return dynamic_cast<const DataT*>(id.data().get());
}

OPENDCC_NAMESPACE_CLOSE

Q_DECLARE_METATYPE(OPENDCC_NAMESPACE::SceneItemList)
