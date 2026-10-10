// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/layer_tree_watcher/layer_tree_watcher.h"
#include <pxr/usd/pcp/node.h>
#include <pxr/usd/pcp/layerStack.h>
#include "pxr/usd/sdf/notice.h"
#include "pxr/usd/ar/resolver.h"
#include <pxr/usd/usd/prim.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    const std::string SUBLAYERS_CHANGED = "sublayers_changed";
};

LayerTreeWatcher::SublayersChangedWatcher::SublayersChangedWatcher(LayerTreeWatcher* layer_tree)
    : m_layer_tree(layer_tree)
{
    m_key = TfNotice::Register(TfWeakPtr<LayerTreeWatcher::SublayersChangedWatcher>(this), &SublayersChangedWatcher::on_layers_changed);
}

void LayerTreeWatcher::SublayersChangedWatcher::on_layers_changed(const PXR_NS::SdfNotice::LayersDidChange& notice)
{
    auto& layers = m_layer_tree->m_layers;

#if PXR_VERSION >= 2002
    auto change_list = notice.GetChangeListVec();
#else
    auto change_list = notice.GetChangeListMap();
#endif
    for (const auto& change : change_list)
    {
        const auto& changed_layer_id = change.first->GetIdentifier();
        for (const auto& entry : change.second.GetEntryList())
        {
            if (entry.second.flags.didChangeIdentifier)
            {
                m_layer_tree->rename_layer(entry.second.oldIdentifier, changed_layer_id);
            }

            for (const auto& sublayer_change : entry.second.subLayerChanges)
            {
                auto changed_sublayer = m_layer_tree->get_layer(sublayer_change.first, changed_layer_id);
                if (!changed_sublayer && sublayer_change.second == SdfChangeList::SubLayerChangeType::SubLayerAdded)
                {
                    TF_CODING_ERROR("Failed to find layer with identifier '%s' and anchor '%s'.", sublayer_change.first.c_str(),
                                    changed_layer_id.c_str());
                    continue;
                }
                const auto& changed_sublayer_id = changed_sublayer ? changed_sublayer->GetIdentifier()
                                                                   : m_layer_tree->get_layer_identifier(sublayer_change.first, changed_layer_id);
                switch (sublayer_change.second)
                {
                case SdfChangeList::SubLayerChangeType::SubLayerAdded:
                    m_layer_tree->add_sublayer(changed_sublayer, changed_layer_id);
                    break;
                case SdfChangeList::SubLayerChangeType::SubLayerRemoved:
                    m_layer_tree->remove_sublayer(changed_sublayer_id, changed_layer_id);
                    break;
                }
            }
        }
    }
}

LayerTreeWatcher::LayerTreeWatcher(PXR_NS::UsdStageRefPtr stage)
{
    if (!stage)
        return;

    auto root_prim = stage->GetPseudoRoot();
    if (!root_prim)
        return;

    add_sublayer(stage->GetRootLayer(), "");
    add_sublayer(stage->GetSessionLayer(), "");
    m_watcher = std::make_unique<SublayersChangedWatcher>(this);
}

const std::set<std::string>& LayerTreeWatcher::get_child_layers(PXR_NS::SdfLayerHandle layer) const
{
    static const std::set<std::string> empty;
    if (!layer)
        return empty;

    return get_child_layers(layer->GetIdentifier());
}

const std::set<std::string>& LayerTreeWatcher::get_child_layers(const std::string& identifier) const
{
    static const std::set<std::string> empty;

    auto iter = m_layers.find(identifier);
    return iter == m_layers.end() ? empty : iter->second.sublayers;
}

bool LayerTreeWatcher::contains(PXR_NS::SdfLayerHandle layer) const
{
    return layer && contains(layer->GetIdentifier());
}

bool LayerTreeWatcher::contains(const std::string& identifier) const
{
    return m_layers.find(identifier) != m_layers.end();
}

SdfLayerRefPtrVector LayerTreeWatcher::get_all_layers() const
{
    SdfLayerRefPtrVector result(m_layers.size());
    std::transform(m_layers.begin(), m_layers.end(), result.begin(), [this](const std::pair<std::string, LayerData>& val) {
        const auto parent_id = val.second.parents.empty() ? "" : *val.second.parents.begin();
        const auto layer = get_layer(val.first, parent_id);
        TF_VERIFY(layer, "Failed to find layer with identifier '%s'. Layer tree might be corrupted.", val.first.c_str());
        return layer;
    });
    return result;
}

SdfLayerRefPtr LayerTreeWatcher::get_layer(const std::string& identifier, const std::string& anchor) const
{
    return SdfLayer::FindOrOpen(get_layer_identifier(identifier, anchor));
}

LayerTreeWatcher::SublayersChangedDispatcherHandle LayerTreeWatcher::register_sublayers_changed_callback(
    const std::function<void(std::string, std::string, SublayerChangeType)>& callback)
{
    return m_sublayers_changed_dispatcher.appendListener("sublayers_changed", callback);
}

void LayerTreeWatcher::unregister_sublayers_changed_callback(const SublayersChangedDispatcherHandle& handle)
{
    m_sublayers_changed_dispatcher.removeListener("sublayers_changed", handle);
}

void LayerTreeWatcher::add_sublayer(SdfLayerHandle layer, const std::string& parent)
{
    if (!layer)
        return;

    m_sublayers_changed_dispatcher.dispatch(SUBLAYERS_CHANGED, layer->GetIdentifier(), parent, LayerTreeWatcher::SublayerChangeType::Added);

    auto parent_iter = m_layers.find(parent);
    if (parent_iter != m_layers.end())
        parent_iter->second.sublayers.insert(layer->GetIdentifier());

    const auto layer_iter = m_layers.find(layer->GetIdentifier());
    if (layer_iter != m_layers.end())
    {
        layer_iter->second.parents.insert(parent);

        m_sublayers_changed_dispatcher.dispatch(SUBLAYERS_CHANGED, layer->GetIdentifier(), parent, LayerTreeWatcher::SublayerChangeType::Added);
        return;
    }

    const auto external_references = layer->GetExternalReferences();
    m_layers[layer->GetIdentifier()] = LayerData { {}, parent };

    for (const auto& identifier : external_references)
    {
        add_sublayer(get_layer(identifier, layer->GetIdentifier()), layer->GetIdentifier());
    }
}

void LayerTreeWatcher::remove_sublayer(const std::string& layer, const std::string& parent)
{
    auto parent_iter = m_layers.find(parent);
    if (parent_iter != m_layers.end())
        parent_iter->second.sublayers.erase(layer);

    m_sublayers_changed_dispatcher.dispatch(SUBLAYERS_CHANGED, layer, parent, LayerTreeWatcher::SublayerChangeType::Removed);

    auto removed_layer_iter = m_layers.find(layer);
    if (removed_layer_iter == m_layers.end())
        return;

    auto& removed_layer = removed_layer_iter->second;
    removed_layer.parents.erase(parent);
    if (removed_layer.parents.empty())
    {
        auto sublayers = removed_layer.sublayers;
        for (const auto& child : sublayers)
        {
            auto& child_layer = m_layers[child];
            if (child_layer.parents.size() == 1)
                remove_sublayer(child, layer);
        }
        m_layers.erase(layer);
    }
}

void LayerTreeWatcher::rename_layer(const std::string& old_identifier, const std::string& new_identifier)
{
    auto layer_iter = m_layers.find(old_identifier);
    if (layer_iter == m_layers.end())
    {
        return;
    }

    for (const auto& parent : layer_iter->second.parents)
    {
        auto it = m_layers.find(parent);
        if (it == m_layers.end())
        {
            continue;
        }
        it->second.sublayers.erase(old_identifier);
        it->second.sublayers.insert(new_identifier);
    }
    for (const auto& child : layer_iter->second.sublayers)
    {
        auto it = m_layers.find(child);
        if (it == m_layers.end())
        {
            continue;
        }
        it->second.parents.erase(old_identifier);
        it->second.parents.insert(new_identifier);
    }

    auto layer_data = layer_iter->second;
    m_layers.erase(old_identifier);
    m_layers.emplace(new_identifier, std::move(layer_data));
}

std::string LayerTreeWatcher::get_layer_identifier(const std::string& identifier, const std::string& anchor /*= std::string()*/) const
{
    if (SdfLayer::IsAnonymousLayerIdentifier(identifier))
        return identifier;
#if !defined(AR_VERSION) || AR_VERSION == 1
    return ArGetResolver().IsRelativePath(identifier) ? ArGetResolver().AnchorRelativePath(anchor, identifier) : identifier;
#else
    return ArGetResolver().CreateIdentifier(identifier, ArResolvedPath(anchor));
#endif
}
OPENDCC_NAMESPACE_CLOSE
