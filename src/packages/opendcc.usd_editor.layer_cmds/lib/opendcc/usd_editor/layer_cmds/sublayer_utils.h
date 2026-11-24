/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"

#include <pxr/usd/sdf/layer.h>

#include <string>
#include <vector>
#include <functional>
#include <set>

OPENDCC_NAMESPACE_OPEN

namespace commands
{
    namespace utils
    {
        inline std::vector<PXR_NS::SdfLayerRefPtr> retain_layer_tree(const PXR_NS::SdfLayerRefPtr& root)
        {
            std::vector<PXR_NS::SdfLayerRefPtr> layers;
            std::set<std::string> visited;
            std::function<void(const PXR_NS::SdfLayerRefPtr&)> retain = [&](const PXR_NS::SdfLayerRefPtr& layer) {
                if (!layer || !visited.insert(layer->GetIdentifier()).second)
                    return;
                layers.push_back(layer);
                for (const auto& path : layer->GetSubLayerPaths())
                    retain(PXR_NS::SdfLayer::FindRelativeToLayer(layer, path));
            };
            retain(root);
            return layers;
        }

        inline bool is_sublayer_reachable(const PXR_NS::SdfLayerRefPtr& from, const PXR_NS::SdfLayerHandle& target)
        {
            std::set<std::string> visited;
            std::function<bool(const PXR_NS::SdfLayerRefPtr&)> contains = [&](const PXR_NS::SdfLayerRefPtr& layer) {
                if (!layer || !visited.insert(layer->GetIdentifier()).second)
                    return false;
                if (layer == target)
                    return true;
                for (const auto& path : layer->GetSubLayerPaths())
                {
                    if (contains(PXR_NS::SdfLayer::FindOrOpenRelativeToLayer(layer, path)))
                        return true;
                }
                return false;
            };
            return contains(from);
        }

        inline void swap_sublayers(const PXR_NS::SdfLayerRefPtr& layer, std::vector<std::string>& paths, std::vector<PXR_NS::SdfLayerOffset>& offsets)
        {
            const auto current = layer->GetSubLayerPaths();
            std::vector<std::string> current_paths(current.begin(), current.end());
            auto current_offsets = layer->GetSubLayerOffsets();
            layer->SetSubLayerPaths(paths);
            for (size_t i = 0; i < offsets.size(); ++i)
                layer->SetSubLayerOffset(offsets[i], i);
            paths.swap(current_paths);
            offsets.swap(current_offsets);
        }

        // Sublayers may be identified by their authored relative path or resolved identifier.
        inline int find_sublayer_index(const PXR_NS::SdfLayerHandle& parent_layer, const std::string& sublayer_identifier)
        {
            if (!parent_layer)
                return -1;

            const PXR_NS::SdfSubLayerProxy paths = parent_layer->GetSubLayerPaths();
            for (size_t i = 0; i < paths.size(); ++i)
            {
                if (paths[i] == sublayer_identifier)
                    return static_cast<int>(i);
            }

            for (size_t i = 0; i < paths.size(); ++i)
            {
                if (auto resolved = PXR_NS::SdfLayer::FindRelativeToLayer(parent_layer, paths[i]))
                {
                    if (resolved->GetIdentifier() == sublayer_identifier)
                        return static_cast<int>(i);
                }
            }
            return -1;
        }
    }
}

OPENDCC_NAMESPACE_CLOSE
