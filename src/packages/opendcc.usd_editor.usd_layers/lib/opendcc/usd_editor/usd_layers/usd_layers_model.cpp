// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_layers/usd_layers_model.h"

#include "opendcc/base/commands_api/core/args.h"
#include "opendcc/base/commands_api/core/command_interface.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <QPersistentModelIndex>

#include <pxr/base/tf/notice.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/sdf/notice.h>
#include <pxr/usd/sdf/schema.h>

#include <algorithm>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_set>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE;

using base::commands::CommandArgs;
using base::commands::CommandInterface;

struct UsdLayersChanges
{
    enum LayerChange
    {
        ContentChanged = 1 << 0,
        CheckReferences = 1 << 1,
        Rebuild = 1 << 2
    };

    std::unordered_map<SdfLayerHandle, int, TfHash> layers;
    std::set<std::string> muted_or_unmuted;
    bool edit_target = false;
};

struct UsdLayersStageListener : public TfWeakBase
{
    UsdLayersStageListener(UsdLayersModel* model, const UsdStageRefPtr& stage)
        : m_model(model)
    {
        const auto self = TfCreateWeakPtr(this);
        m_keys.push_back(TfNotice::Register(self, &UsdLayersStageListener::on_layers_changed));
        m_keys.push_back(TfNotice::Register(self, &UsdLayersStageListener::on_layer_dirtiness_changed, SdfLayerHandle()));
        m_keys.push_back(TfNotice::Register(self, &UsdLayersStageListener::on_edit_target_changed, stage));
        m_keys.push_back(TfNotice::Register(self, &UsdLayersStageListener::on_layer_muting_changed, stage));
    }

    ~UsdLayersStageListener() { TfNotice::RevokeAndWait(&m_keys); }

    void clear_pending()
    {
        const std::lock_guard<std::mutex> lock(m_pending->mutex);
        m_pending->changes = {};
    }

private:
    struct Pending
    {
        std::mutex mutex;
        UsdLayersChanges changes;
        bool scheduled = false;
    };

    void on_layers_changed(const SdfNotice::LayersDidChange& notice)
    {
        const std::lock_guard<std::mutex> lock(m_pending->mutex);
        for (const auto& change : notice.GetChangeListVec())
        {
            auto& flags = m_pending->changes.layers[change.first];
            flags |= UsdLayersChanges::ContentChanged;
            for (const auto& entry : change.second.GetEntryList())
            {
                const auto& change_flags = entry.second.flags;
                if (!entry.second.subLayerChanges.empty() || change_flags.didReplaceContent || change_flags.didReloadContent ||
                    change_flags.didChangeIdentifier || change_flags.didChangeResolvedPath)
                {
                    flags |= UsdLayersChanges::Rebuild;
                    break;
                }
                if (change_flags.didChangePrimReferences || change_flags.didAddNonInertPrim || change_flags.didRemoveNonInertPrim ||
                    entry.second.HasInfoChange(SdfFieldKeys->Payload))
                    flags |= UsdLayersChanges::CheckReferences;
            }
        }
        schedule_flush();
    }

    void on_layer_dirtiness_changed(const SdfNotice::LayerDirtinessChanged&, const SdfLayerHandle& layer)
    {
        const std::lock_guard<std::mutex> lock(m_pending->mutex);
        m_pending->changes.layers.try_emplace(layer, 0);
        schedule_flush();
    }

    void on_edit_target_changed(const UsdNotice::StageEditTargetChanged&, const UsdStageWeakPtr&)
    {
        const std::lock_guard<std::mutex> lock(m_pending->mutex);
        m_pending->changes.edit_target = true;
        schedule_flush();
    }

    void on_layer_muting_changed(const UsdNotice::LayerMutingChanged& notice, const UsdStageWeakPtr&)
    {
        const std::lock_guard<std::mutex> lock(m_pending->mutex);
        m_pending->changes.muted_or_unmuted.insert(notice.GetMutedLayers().begin(), notice.GetMutedLayers().end());
        m_pending->changes.muted_or_unmuted.insert(notice.GetUnmutedLayers().begin(), notice.GetUnmutedLayers().end());
        schedule_flush();
    }

    void schedule_flush()
    {
        if (m_pending->scheduled)
            return;
        m_pending->scheduled = true;

        // A queued flush from a previous stage expires with its listener.
        QMetaObject::invokeMethod(
            m_model,
            [model = m_model, pending = std::weak_ptr<Pending>(m_pending)]() {
                const auto state = pending.lock();
                if (!state || !model->m_stage_listener || state != model->m_stage_listener->m_pending)
                    return;

                UsdLayersChanges changes;
                {
                    const std::lock_guard<std::mutex> lock(state->mutex);
                    std::swap(changes, state->changes);
                    state->scheduled = false;
                }
                model->apply_changes(changes);
            },
            Qt::QueuedConnection);
    }

    UsdLayersModel* const m_model;
    const std::shared_ptr<Pending> m_pending = std::make_shared<Pending>();
    TfNotice::Keys m_keys;
};

UsdLayersModel::LayerItem::LayerItem(const PXR_NS::SdfLayerRefPtr& layer_, LayerItem* parent_, bool is_ref_)
    : layer(layer_)
    , parent(parent_)
    , is_ref(is_ref_)
{
    if (parent)
        row = static_cast<int>(parent->children.size());
}

UsdLayersModel::UsdLayersModel(QObject* parent)
    : QAbstractItemModel(parent)

{
}

UsdLayersModel::~UsdLayersModel()
{
    m_stage_listener.reset();
}

void UsdLayersModel::set_stage(const PXR_NS::UsdStageRefPtr& stage)
{
    if (m_stage == stage)
    {
        refresh();
        return;
    }

    m_stage_listener.reset();
    beginResetModel();
    m_stage = stage;
    if (m_stage)
        m_stage_listener = std::make_unique<UsdLayersStageListener>(this, m_stage);
    rebuild();
    endResetModel();
}

PXR_NS::UsdStageRefPtr UsdLayersModel::stage() const
{
    return m_stage;
}

void UsdLayersModel::refresh()
{
    if (m_stage_listener)
        m_stage_listener->clear_pending();
    beginResetModel();
    rebuild();
    endResetModel();
}

void UsdLayersModel::set_current_stage()
{
    set_stage(Application::instance().get_session()->get_current_stage());
}

bool UsdLayersModel::is_model_valid() const
{
    return m_stage && !m_root_items.empty();
}

PXR_NS::SdfLayerHandle UsdLayersModel::layer_for_index(const QModelIndex& index) const
{
    return PXR_NS::SdfLayer::Find(index.data(LayerIdentifierRole).toString().toStdString());
}

UsdLayersModel::LayerItem* UsdLayersModel::item_for_index(const QModelIndex& index) const
{
    if (!index.isValid() || index.model() != this)
        return nullptr;
    return static_cast<LayerItem*>(index.internalPointer());
}

QModelIndex UsdLayersModel::index_for_layer(const PXR_NS::SdfLayerHandle& layer, int column) const
{
    return find_index_for_layer(QModelIndex(), layer, column);
}

void UsdLayersModel::set_edit_target_layer(const PXR_NS::SdfLayerHandle& layer)
{
    if (!m_stage || !layer || m_stage->IsLayerMuted(layer->GetIdentifier()))
        return;
    CommandInterface::execute("set_edit_target", CommandArgs().arg(layer->GetIdentifier()));
}

void UsdLayersModel::toggle_muted(const QModelIndex& index)
{
    const PXR_NS::SdfLayerHandle layer = layer_for_index(index);
    if (!m_stage || !layer || m_stage->GetRootLayer()->GetIdentifier() == layer->GetIdentifier())
        return;

    const bool new_muted = !m_stage->IsLayerMuted(layer->GetIdentifier());
    CommandInterface::execute("mute_layer", CommandArgs().arg(layer->GetIdentifier()).kwarg("mute", new_muted));
}

PXR_NS::SdfLayerHandle UsdLayersModel::add_sublayer(const PXR_NS::SdfLayerHandle& parent_layer)
{
    if (!parent_layer)
        return {};

    const auto result = CommandInterface::execute(
        "add_sublayer", CommandArgs().arg(parent_layer->GetIdentifier()).arg(std::string("layer.usda")).kwarg("anonymous", true));
    if (!result.is_successful())
        return {};
    return PXR_NS::SdfLayer::Find(result.get_result<std::string>()->get_value());
}

QModelIndex UsdLayersModel::index(int row, int column, const QModelIndex& parent_index) const
{
    if (!hasIndex(row, column, parent_index))
        return {};

    if (!parent_index.isValid())
    {
        if (row >= static_cast<int>(m_root_items.size()))
            return {};
        return createIndex(row, column, m_root_items[row].get());
    }

    const LayerItem* parent_item = item_for_index(parent_index);
    if (!parent_item || row >= static_cast<int>(parent_item->children.size()))
        return {};
    return createIndex(row, column, parent_item->children[row].get());
}

QModelIndex UsdLayersModel::parent(const QModelIndex& child) const
{
    const LayerItem* item = item_for_index(child);
    if (!item || !item->parent)
        return {};
    return createIndex(item->parent->row, 0, item->parent);
}

int UsdLayersModel::rowCount(const QModelIndex& parent_index) const
{
    if (parent_index.column() > 0)
        return 0;
    if (!parent_index.isValid())
        return static_cast<int>(m_root_items.size());
    const LayerItem* item = item_for_index(parent_index);
    return item ? static_cast<int>(item->children.size()) : 0;
}

int UsdLayersModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QVariant UsdLayersModel::data(const QModelIndex& index, int role) const
{
    LayerItem* item = item_for_index(index);
    if (!item || !item->layer || !m_stage)
        return {};

    const PXR_NS::SdfLayerHandle layer = item->layer;
    const int column = index.column();

    if (role == Qt::DisplayRole)
    {
        switch (column)
        {
        case NameColumn:
        {
            const auto name = layer->GetDisplayName();
            if (layer->IsAnonymous())
            {
                const auto extension = "." + layer->GetFileExtension();
                const bool is_session = layer == m_stage->GetSessionLayer();
                if (name.empty() || name == extension || (is_session && name == "-session" + extension))
                    return is_session ? tr("Session Layer") : tr("Anonymous Layer");
            }
            return QString::fromStdString(name);
        }
        case PathColumn:
            return QString::fromStdString(layer->GetIdentifier());
        case ResolvedPathColumn:
            return QString::fromStdString(layer->GetRealPath());
        case VersionColumn:
            return QString::fromStdString(layer->GetVersion());
        default:
            return {};
        }
    }

    if (role == LayerIdentifierRole)
        return QString::fromStdString(layer->GetIdentifier());
    if (role == LayerIsReferenceRole)
        return item->is_ref;
    if (role == LayerIsMutedRole)
        return m_stage->IsLayerMuted(layer->GetIdentifier());
    if (role == LayerIsEditTargetRole)
        return layer == m_stage->GetEditTarget().GetLayer();
    if (role == LayerIsEditTargetParentRole)
        return m_edit_target_parents.contains(item);
    if (role == LayerIsRootRole)
        return layer == m_stage->GetRootLayer();
    if (role == LayerIsDirtyRole)
        return layer->IsDirty() || layer->IsAnonymous();
    if (role == LayerIsEmptyRole)
        return layer->IsEmpty();

    return {};
}

QVariant UsdLayersModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractItemModel::headerData(section, orientation, role);

    return m_headers.value(section);
}

bool UsdLayersModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant& value, int role)
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= columnCount())
        return false;
    if (m_headers.value(section) == value)
        return true;
    m_headers.insert(section, value);
    Q_EMIT headerDataChanged(orientation, section, section);
    return true;
}

namespace
{
    const QString layer_move_mime_type = QStringLiteral("application/x-opendcc-layer-move");

    int sublayer_slot(const SdfLayerHandle& parent, const SdfLayerHandle& child)
    {
        if (!parent || !child)
            return -1;

        int slot = 0;
        for (const auto& path : parent->GetSubLayerPaths())
        {
            if (SdfLayer::FindRelativeToLayer(parent, path) == child)
                return slot;
            ++slot;
        }
        return -1;
    }

    bool is_reference_branch(QModelIndex index)
    {
        for (; index.isValid(); index = index.parent())
        {
            if (index.data(UsdLayersModel::LayerIsReferenceRole).toBool())
                return true;
        }
        return false;
    }

    struct LayerMove
    {
        std::string parent;
        std::string layer;
        std::string destination;
        int index;
    };

    std::optional<LayerMove> layer_move(const UsdLayersModel& model, const QMimeData* mime, int row, const QModelIndex& parent_index)
    {
        if (!mime || !mime->hasFormat(layer_move_mime_type) || !model.flags(parent_index).testFlag(Qt::ItemIsDropEnabled))
            return {};

        const auto bytes = mime->data(layer_move_mime_type);
        QDataStream stream(bytes);
        QString source_id, child_id;
        stream >> source_id >> child_id;
        if (stream.status() != QDataStream::Ok || source_id.isEmpty() || child_id.isEmpty())
            return {};

        const auto source = SdfLayer::Find(source_id.toStdString());
        const auto child = SdfLayer::Find(child_id.toStdString());
        const auto destination = model.layer_for_index(parent_index);
        const int from = sublayer_slot(source, child);
        if (from < 0 || !source->PermissionToEdit() || !destination)
            return {};

        for (auto ancestor = parent_index; ancestor.isValid(); ancestor = ancestor.parent())
        {
            if (ancestor.data(UsdLayersModel::LayerIdentifierRole).toString() == child_id)
                return {};
        }
        if (source != destination && sublayer_slot(destination, child) >= 0)
            return {};

        int to = static_cast<int>(destination->GetSubLayerPaths().size());
        if (row >= 0 && row < model.rowCount(parent_index))
        {
            const auto next_index = model.index(row, UsdLayersModel::NameColumn, parent_index);
            if (!next_index.data(UsdLayersModel::LayerIsReferenceRole).toBool())
            {
                to = sublayer_slot(destination, model.layer_for_index(next_index));
                if (to < 0)
                    return {};
            }
        }
        if (source == destination && from < to)
            --to;
        if (source == destination && from == to)
            return {};
        return LayerMove { source->GetIdentifier(), child->GetIdentifier(), destination->GetIdentifier(), to };
    }
}

Qt::ItemFlags UsdLayersModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    auto result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (is_reference_branch(index))
        return result;

    const auto layer = layer_for_index(index);
    if (layer && layer->PermissionToEdit())
        result |= Qt::ItemIsDropEnabled;

    const auto parent = layer_for_index(index.parent());
    if (parent && layer && parent->PermissionToEdit())
        result |= Qt::ItemIsDragEnabled;
    return result;
}

Qt::DropActions UsdLayersModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QStringList UsdLayersModel::mimeTypes() const
{
    return { layer_move_mime_type };
}

QMimeData* UsdLayersModel::mimeData(const QModelIndexList& indexes) const
{
    QModelIndex layer;
    for (const auto& index : indexes)
    {
        const auto name_index = index.sibling(index.row(), NameColumn);
        if (!flags(name_index).testFlag(Qt::ItemIsDragEnabled) || (layer.isValid() && layer != name_index))
            return nullptr;
        layer = name_index;
    }
    if (!layer.isValid())
        return nullptr;

    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << layer.parent().data(LayerIdentifierRole).toString() << layer.data(LayerIdentifierRole).toString();
    auto* mime = new QMimeData;
    mime->setData(layer_move_mime_type, bytes);
    return mime;
}

bool UsdLayersModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int, const QModelIndex& parent) const
{
    return action == Qt::MoveAction && layer_move(*this, data, row, parent).has_value();
}

bool UsdLayersModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int, const QModelIndex& parent)
{
    if (action != Qt::MoveAction)
        return false;
    const auto move = layer_move(*this, data, row, parent);
    if (!move)
        return false;

    return CommandInterface::execute(
               "move_sublayer", CommandArgs().arg(move->parent).arg(move->layer).arg(move->index).kwarg("new_parent_identifier", move->destination))
        .is_successful();
}

void UsdLayersModel::apply_changes(const UsdLayersChanges& changes)
{
    ItemChanges item_changes;
    const bool has_extra_columns = !changes.layers.empty() && columnCount() > ColumnCount;
    for (const auto& [layer, flags] : changes.layers)
    {
        const auto found = m_layers.find(layer);
        if (found == m_layers.end())
            continue;

        auto& state = found->second;
        if ((flags & UsdLayersChanges::Rebuild) ||
            ((flags & UsdLayersChanges::CheckReferences) && state.references != layer->GetExternalReferences()))
        {
            beginResetModel();
            rebuild();
            endResetModel();
            return;
        }

        const bool dirty = layer->IsDirty() || layer->IsAnonymous();
        const bool empty = layer->IsEmpty();
        QVector<int> roles;
        if (state.dirty != dirty)
            roles.push_back(LayerIsDirtyRole);
        if (state.empty != empty)
            roles.push_back(LayerIsEmptyRole);
        if (has_extra_columns && (flags & UsdLayersChanges::ContentChanged))
            roles.push_back(Qt::DisplayRole);
        if (!roles.isEmpty())
            for (auto* item : state.items)
                item_changes.insert(item, roles);
        state.dirty = dirty;
        state.empty = empty;
    }

    const auto add_role = [&](const SdfLayerHandle& layer, int role) {
        const auto found = m_layers.find(layer);
        if (found != m_layers.end())
            for (auto* item : found->second.items)
                item_changes[item].push_back(role);
    };

    for (const auto& identifier : changes.muted_or_unmuted)
        add_role(SdfLayer::Find(identifier), LayerIsMutedRole);

    if (changes.edit_target && m_stage && m_edit_target != m_stage->GetEditTarget().GetLayer())
    {
        const auto previous_target = m_edit_target;
        const auto previous_parents = std::move(m_edit_target_parents);
        update_edit_target_parents();
        add_role(previous_target, LayerIsEditTargetRole);
        add_role(m_edit_target, LayerIsEditTargetRole);
        for (auto* item : previous_parents)
            if (!m_edit_target_parents.contains(item))
                item_changes[item].push_back(LayerIsEditTargetParentRole);
        for (auto* item : m_edit_target_parents)
            if (!previous_parents.contains(item))
                item_changes[item].push_back(LayerIsEditTargetParentRole);
    }

    notify_item_changes(item_changes);
}

namespace
{
    using LayerChildren = std::vector<std::pair<SdfLayerRefPtr, bool>>;

    LayerChildren resolve_children(const SdfLayerHandle& layer, std::set<std::string> references)
    {
        LayerChildren children;
        const auto append_child = [&](const std::string& path, bool is_reference) {
            if (auto child = SdfLayer::FindOrOpenRelativeToLayer(layer, path))
                children.emplace_back(std::move(child), is_reference);
        };
        for (const auto& path : layer->GetSubLayerPaths())
        {
            append_child(path, false);
            references.erase(path);
        }
        references.erase(std::string());
        for (const auto& reference : references)
            if (SdfLayer::IsAnonymousLayerIdentifier(reference))
                append_child(reference, true);
        for (const auto& reference : references)
            if (!SdfLayer::IsAnonymousLayerIdentifier(reference))
                append_child(reference, true);
        return children;
    }
}

void UsdLayersModel::rebuild()
{
    m_root_items.clear();
    m_layers.clear();
    m_edit_target_parents.clear();
    m_edit_target.Reset();
    if (!m_stage)
        return;

    std::unordered_map<SdfLayerHandle, LayerChildren, TfHash> children_by_layer;
    std::unordered_set<SdfLayerHandle, TfHash> ancestors;
    std::function<void(const SdfLayerRefPtr&, LayerItem*, bool)> add_item = [&](const SdfLayerRefPtr& layer, LayerItem* parent, bool is_ref) {
        if (!layer || !ancestors.insert(layer).second)
            return;

        auto& siblings = parent ? parent->children : m_root_items;
        auto new_item = std::make_unique<LayerItem>(layer, parent, is_ref);
        auto* item = new_item.get();
        item->row = static_cast<int>(siblings.size());
        siblings.push_back(std::move(new_item));

        auto [state, first_occurrence] = m_layers.try_emplace(layer);
        state->second.items.push_back(item);
        if (first_occurrence)
        {
            state->second.references = layer->GetExternalReferences();
            state->second.dirty = layer->IsDirty() || layer->IsAnonymous();
            state->second.empty = layer->IsEmpty();
            children_by_layer.emplace(layer, resolve_children(layer, state->second.references));
        }
        for (const auto& [child, is_reference] : children_by_layer.at(layer))
            add_item(child, item, is_reference);
        ancestors.erase(layer);
    };

    if (const auto session = m_stage->GetSessionLayer())
        add_item(SdfLayer::FindOrOpen(session->GetIdentifier()), nullptr, false);
    if (const auto root = m_stage->GetRootLayer())
        add_item(SdfLayer::FindOrOpen(root->GetIdentifier()), nullptr, false);
    update_edit_target_parents();
}

void UsdLayersModel::update_edit_target_parents()
{
    m_edit_target_parents.clear();
    m_edit_target = m_stage->GetEditTarget().GetLayer();
    const auto found = m_layers.find(m_edit_target);
    if (found == m_layers.end())
        return;

    for (const auto* item : found->second.items)
        for (auto* ancestor = item->parent; ancestor && !m_edit_target_parents.contains(ancestor); ancestor = ancestor->parent)
            m_edit_target_parents.insert(ancestor);
}

QModelIndex UsdLayersModel::index_for_item(const LayerItem* item) const
{
    const auto parent = item->parent ? index_for_item(item->parent) : QModelIndex();
    if (item->parent && !parent.isValid())
        return {};
    const auto result = index(item->row, NameColumn, parent);
    return item_for_index(result) == item ? result : QModelIndex();
}

QModelIndex UsdLayersModel::find_index_for_layer(const QModelIndex& parent_index, const PXR_NS::SdfLayerHandle& layer, int column) const
{
    if (!layer)
        return {};

    for (int row = 0; row < rowCount(parent_index); ++row)
    {
        const QModelIndex current = index(row, column, parent_index);
        if (current.data(LayerIdentifierRole).toString() == QString::fromStdString(layer->GetIdentifier()))
            return current;

        const QModelIndex found = find_index_for_layer(index(row, 0, parent_index), layer, column);
        if (found.isValid())
            return found;
    }
    return {};
}

void UsdLayersModel::notify_item_changes(const ItemChanges& changes)
{
    struct Update
    {
        QPersistentModelIndex first;
        QPersistentModelIndex last;
        QVector<int> roles;
    };
    std::vector<Update> updates;
    updates.reserve(changes.size());
    QVector<int> fallback_roles;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
    {
        const auto first = index_for_item(it.key());
        if (first.isValid())
        {
            const auto last = first.siblingAtColumn(columnCount(first.parent()) - 1);
            updates.push_back({ first, last, it.value() });
        }
        else
        {
            // A package can supply its own row structure using the same layer roles.
            for (int role : it.value())
                if (!fallback_roles.contains(role))
                    fallback_roles.push_back(role);
        }
    }

    for (const auto& update : updates)
        if (update.first.isValid() && update.last.isValid())
            Q_EMIT dataChanged(update.first, update.last, update.roles);
    if (!fallback_roles.empty())
        emit_all_data_changed(fallback_roles);
}

void UsdLayersModel::emit_all_data_changed(const QVector<int>& roles)
{
    std::function<void(const QModelIndex&)> emit_for_parent = [&](const QModelIndex& parent) {
        const int rows = rowCount(parent);
        if (rows <= 0)
            return;

        Q_EMIT dataChanged(index(0, 0, parent), index(rows - 1, columnCount(parent) - 1, parent), roles);

        for (int row = 0; row < rows; ++row)
            emit_for_parent(index(row, 0, parent));
    };
    emit_for_parent(QModelIndex());
}

UsdLayersProxyModel::UsdLayersProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterKeyColumn(UsdLayersModel::NameColumn);
    setRecursiveFilteringEnabled(true);
}

void UsdLayersProxyModel::set_show_references(bool show)
{
    if (m_show_references == show)
        return;
    m_show_references = show;
    invalidateFilter();
}

void UsdLayersProxyModel::set_show_only_dirty(bool show)
{
    if (m_show_only_dirty == show)
        return;
    m_show_only_dirty = show;
    invalidateFilter();
}

void UsdLayersProxyModel::set_show_only_anonymous(bool show)
{
    if (m_show_only_anonymous == show)
        return;
    m_show_only_anonymous = show;
    invalidateFilter();
}

void UsdLayersProxyModel::set_show_only_empty(bool show)
{
    if (m_show_only_empty == show)
        return;
    m_show_only_empty = show;
    invalidateFilter();
}

bool UsdLayersProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    const QModelIndex source_index = sourceModel()->index(source_row, UsdLayersModel::NameColumn, source_parent);
    const auto* model = qobject_cast<const UsdLayersModel*>(sourceModel());
    const auto layer = model ? model->layer_for_index(source_index) : PXR_NS::SdfLayerHandle();
    if (!layer)
        return false;

    if (!m_show_references)
    {
        for (QModelIndex current = source_index; current.isValid(); current = current.parent())
        {
            if (current.data(UsdLayersModel::LayerIsReferenceRole).toBool())
                return false;
        }
    }
    if (m_show_only_dirty && !(layer->IsDirty() || layer->IsAnonymous()))
        return false;
    if (m_show_only_anonymous && !layer->IsAnonymous())
        return false;
    if (m_show_only_empty && !layer->IsEmpty())
        return false;

    return QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent);
}

OPENDCC_NAMESPACE_CLOSE
