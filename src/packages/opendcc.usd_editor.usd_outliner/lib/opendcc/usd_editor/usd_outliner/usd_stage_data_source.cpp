/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/usd_editor/usd_outliner/usd_stage_data_source.h"

#include "opendcc/app/core/command_utils.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/usd/layer_tree_watcher/layer_state_delegates_holder.h"
#include "opendcc/usd_editor/usd_outliner/usd_item_id.h"

#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/type.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/primFlags.h>
#include <pxr/usd/usd/schemaBase.h>
#include <pxr/usd/usd/schemaRegistry.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdUtils/pipeline.h>

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <set>
#include <vector>
#include <mutex>
#include <atomic>
#include <utility>
#include <QElapsedTimer>
#include <QTimer>
#include <pxr/base/tf/weakBase.h>

#ifdef OPENDCC_USD_OUTLINER_TESTS
#include "notice_test_access.h"
#endif

PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::CommandInterface;

namespace
{
    void ensure_undo_delegate(const UsdStageRefPtr& stage)
    {
        if (!Application::instance().is_ui_available() || stage != Application::instance().get_session()->get_current_stage())
        {
            auto layer_tree = std::make_shared<LayerTreeWatcher>(stage);
            LayerStateDelegatesHolder delegates(layer_tree);
            delegates.add_delegate(TfToken("UndoStateDelegate"));
        }
    }

    // Include overs, abstract and inactive prims, and instance proxies.
    Usd_PrimFlagsPredicate outliner_predicate()
    {
        return UsdTraverseInstanceProxies(UsdPrimIsDefined || !UsdPrimIsDefined);
    }

    bool affects_inherited_columns(const SdfPath& path)
    {
        if (!path.IsPrimPropertyPath())
            return false;
        const auto name = path.GetNameToken();
        return name == UsdGeomTokens->visibility || name == UsdGeomTokens->modelDrawMode || TfStringStartsWith(name.GetString(), "material:binding");
    }

    const QStringList& concrete_schema_types()
    {
        static const QStringList names = []() {
            QStringList result;
            std::set<TfType> types;
            TfType::Find<UsdSchemaBase>().GetAllDerivedTypes(&types);
            for (const TfType& type : types)
            {
                if (!UsdSchemaRegistry::IsConcrete(type))
                    continue;
                const TfToken name = UsdSchemaRegistry::GetSchemaTypeName(type);
                if (!name.IsEmpty())
                    result.push_back(QString::fromStdString(name.GetString()));
            }
            result.removeDuplicates();
            result.sort(Qt::CaseInsensitive);
            return result;
        }();
        return names;
    }

    bool is_spec_on_edit_target(const UsdPrim& prim, const UsdStageRefPtr& stage)
    {
        if (!stage || !prim)
            return true;
        const SdfLayerHandle edit_layer = stage->GetEditTarget().GetLayer();
        for (const SdfPrimSpecHandle& spec : prim.GetPrimStack())
            if (spec->GetLayer() == edit_layer)
                return true;
        return false;
    }

    bool can_rename_prim(const UsdPrim& prim)
    {
        if (!prim || prim.IsPseudoRoot() || prim.IsInstanceProxy() || prim.IsInPrototype())
            return false;

        const auto target = prim.GetStage()->GetEditTarget();
        return target.GetMapFunction().IsIdentityPathMapping() && target.GetLayer()->PermissionToEdit() &&
               target.GetPrimSpecForScenePath(prim.GetPath());
    }

    // Cache the edit-target badge separately from the base icon.
    QIcon prim_icon(const QString& resource_path, bool on_edit_target)
    {
        static QHash<QString, QIcon> cache;
        const QString key = resource_path + (on_edit_target ? QStringLiteral("|1") : QStringLiteral("|0"));
        const auto cached = cache.find(key);
        if (cached != cache.end())
            return cached.value();

        QPixmap pixmap(resource_path);
        QIcon icon;
        if (!pixmap.isNull())
        {
            if (!on_edit_target)
            {
                const QPixmap overlay(QStringLiteral(":/icons/is_not_on_edit_target"));
                if (!overlay.isNull())
                {
                    QPainter painter(&pixmap);
                    const int size = pixmap.width() / 2;
                    painter.drawPixmap(0, 0, size, size, overlay);
                }
            }
            icon = QIcon(pixmap);
        }
        cache.insert(key, icon);
        return icon;
    }
}

struct UsdStageDataSource::SearchIndex
{
    struct Entry
    {
        SceneItemId item;
        std::string name;
    };
    std::vector<Entry> entries;
    scene_outliner::CancellationToken content;

    SceneItemList search(const std::string& query, const SceneOutlinerSearchCompletion* completed) const
    {
        const auto needle = QString::fromStdString(query).toCaseFolded().toStdString();
        SceneItemList matches;
        for (const auto& entry : entries)
        {
            if ((completed && completed->is_cancelled()) || content.is_cancelled())
                return {};
            if (entry.name.find(needle) != std::string::npos)
                matches.push_back(entry.item);
        }
        std::sort(matches.begin(), matches.end(), [](const SceneItemId& lhs, const SceneItemId& rhs) { return usd_path_of(lhs) < usd_path_of(rhs); });
        return matches;
    }
};

struct UsdStageDataSource::NoticeQueue : std::enable_shared_from_this<NoticeQueue>
{
    std::mutex mutex;
    UsdStageDataSource* owner = nullptr;
    scene_outliner::CancellationToken content;
    std::vector<SdfPath> resynced;
    std::vector<SdfPath> changed;
    bool edit_target = false;
    bool scheduled = false;
#ifdef OPENDCC_USD_OUTLINER_TESTS
    std::function<void()> before_post;
    std::function<void()> after_detach;
#endif

    void post(std::vector<SdfPath> resync, std::vector<SdfPath> info, bool target = false)
    {
        if (resync.empty() && info.empty() && !target)
            return;
#ifdef OPENDCC_USD_OUTLINER_TESTS
        if (before_post)
            before_post();
#endif
        std::lock_guard<std::mutex> lock(mutex);
        if (!owner)
            return;
        content.cancel();
        resynced.insert(resynced.end(), resync.begin(), resync.end());
        changed.insert(changed.end(), info.begin(), info.end());
        edit_target |= target;
        if (scheduled)
            return;
        scheduled = true;
        QMetaObject::invokeMethod(
            owner,
            [state = shared_from_this()]() {
                if (state->owner)
                    state->owner->flush_notices();
            },
            Qt::QueuedConnection);
    }

    void detach()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            owner = nullptr;
            content.cancel();
        }
#ifdef OPENDCC_USD_OUTLINER_TESTS
        if (after_detach)
            after_detach();
#endif
    }

    scene_outliner::CancellationToken snapshot_content()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (content.is_cancelled())
            content.restart();
        return content;
    }

    void complete(SceneOutlinerSearchCompletion completion, SceneItemList matches, scene_outliner::CancellationToken content)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!owner || content.is_cancelled() || completion.is_cancelled())
            return;
        QMetaObject::invokeMethod(
            owner,
            [state = shared_from_this(), completion, matches = std::move(matches), content]() mutable {
                if (state->owner && state == state->owner->m_notices && !content.is_cancelled())
                    completion.complete(std::move(matches));
            },
            Qt::QueuedConnection);
    }
};

#ifdef OPENDCC_USD_OUTLINER_TESTS
void UsdOutlinerNoticeTestAccess::set_callbacks(UsdStageDataSource& source, std::function<void()> before_post, std::function<void()> after_detach)
{
    source.m_notices->before_post = std::move(before_post);
    source.m_notices->after_detach = std::move(after_detach);
}
#endif

struct UsdStageDataSource::NoticeListener : TfWeakBase
{
    std::shared_ptr<NoticeQueue> queue;
    TfNotice::Keys keys;

    NoticeListener(const UsdStageRefPtr& stage, std::shared_ptr<NoticeQueue> queue)
        : queue(std::move(queue))
    {
        keys.push_back(TfNotice::Register(TfCreateWeakPtr(this), &NoticeListener::objects_changed, stage));
        keys.push_back(TfNotice::Register(TfCreateWeakPtr(this), &NoticeListener::target_changed, stage));
    }
    ~NoticeListener() { TfNotice::RevokeAndWait(&keys); }

    void objects_changed(const UsdNotice::ObjectsChanged& notice, const UsdStageWeakPtr&)
    {
        const auto& resynced = notice.GetResyncedPaths();
        const auto& changed = notice.GetChangedInfoOnlyPaths();
        queue->post({ resynced.begin(), resynced.end() }, { changed.begin(), changed.end() });
    }
    void target_changed(const UsdNotice::StageEditTargetChanged&, const UsdStageWeakPtr&) { queue->post({}, {}, true); }
};

struct UsdStageDataSource::SnapshotBuild
{
    std::shared_ptr<SearchIndex> index = std::make_shared<SearchIndex>();
    std::shared_ptr<NoticeQueue> queue;
    UsdPrimRange range;
    UsdPrimRange::iterator iterator;
    SceneOutlinerSearchCompletion completion;
    std::string query;

    SnapshotBuild(const UsdStageRefPtr& stage)
        : range(UsdPrimRange::Stage(stage, outliner_predicate()))
        , iterator(range.begin())
    {
    }
};

void UsdStageDataSource::flush_notices()
{
    std::vector<SdfPath> resynced;
    std::vector<SdfPath> changed;
    bool edit_target;
    {
        std::lock_guard<std::mutex> lock(m_notices->mutex);
        resynced.swap(m_notices->resynced);
        changed.swap(m_notices->changed);
        edit_target = std::exchange(m_notices->edit_target, false);
        m_notices->scheduled = false;
    }
    m_search_index.reset();
    std::sort(resynced.begin(), resynced.end());
    resynced.erase(std::unique(resynced.begin(), resynced.end()), resynced.end());
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    on_objects_changed(resynced, changed);
    if (edit_target)
        m_batcher->add_changed({}, true);
}

void UsdStageDataSource::continue_snapshot(const std::shared_ptr<SnapshotBuild>& build) const
{
    if (build->completion.is_cancelled() || build->queue != m_notices || build->index->content.is_cancelled())
        return;
    QElapsedTimer timer;
    timer.start();
    int count = 0;
    while (build->iterator != build->range.end())
    {
        const UsdPrim prim = *build->iterator;
        if (!prim.IsPseudoRoot())
            build->index->entries.push_back(
                { make_usd_item_id(prim.GetPath()), QString::fromStdString(prim.GetName().GetString()).toCaseFolded().toStdString() });
        ++build->iterator;
        if (++count % 64 == 0 && timer.elapsed() >= 4)
        {
            QTimer::singleShot(0, this, [this, build]() { continue_snapshot(build); });
            return;
        }
    }
    m_search_index = build->index;
    dispatch_search(build->index, build->query, build->completion);
}

void UsdStageDataSource::dispatch_search(std::shared_ptr<const SearchIndex> index, const std::string& query,
                                         SceneOutlinerSearchCompletion completion) const
{
    m_search_dispatcher.Run([index = std::move(index), query, completion, queue = m_notices]() {
        if (completion.is_cancelled() || index->content.is_cancelled())
            return;
        auto matches = index->search(query, &completion);
        queue->complete(completion, std::move(matches), index->content);
    });
}

UsdStageDataSource::UsdStageDataSource(QObject* parent)
    : ISceneOutlinerDataSource(parent)
{
    m_batcher = new SceneOutlinerBatcher(this);
    connect(m_batcher, &SceneOutlinerBatcher::reset, this, [this]() { Q_EMIT changed(SceneOutlinerChange::reset()); });
    connect(m_batcher, &SceneOutlinerBatcher::resynced, this,
            [this](const SceneItemList& parents) { Q_EMIT changed(SceneOutlinerChange::added(parents)); });
    connect(m_batcher, &SceneOutlinerBatcher::items_changed, this,
            [this](const SceneItemList& items, bool affects_search) { Q_EMIT changed(SceneOutlinerChange::changed(items, affects_search)); });

    auto* session = Application::instance().get_session();
    set_stage(session ? session->get_current_stage() : UsdStageRefPtr());

    m_stage_changed_handle = Application::instance().register_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, [this]() {
        auto* session = Application::instance().get_session();
        set_stage(session ? session->get_current_stage() : UsdStageRefPtr());
    });
    m_time_changed_handle = Application::instance().register_event_callback(Application::EventType::CURRENT_TIME_CHANGED, [this]() {
        if (!m_stage)
            return;
        std::vector<SceneItemId> items;
        for (auto it = m_animated_visibility.begin(); it != m_animated_visibility.end();)
        {
            if (!m_stage->GetPrimAtPath(*it))
                it = m_animated_visibility.erase(it);
            else
            {
                items.push_back(make_usd_item_id(*it));
                ++it;
            }
        }
        if (!items.empty())
            m_batcher->add_changed(items, false);
    });
}

UsdStageDataSource::~UsdStageDataSource()
{
    if (m_notices)
        m_notices->detach();
    m_listener.reset();
    m_batcher->cancel();
    disconnect(m_batcher, nullptr, this, nullptr);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_stage_changed_handle);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_TIME_CHANGED, m_time_changed_handle);
    m_search_dispatcher.Cancel();
    m_search_dispatcher.Wait();
}

void UsdStageDataSource::set_stage(const UsdStageRefPtr& stage)
{
    if (stage == m_stage)
        return;
    if (m_notices)
        m_notices->detach();
    m_listener.reset();
    m_notices = std::make_shared<NoticeQueue>();
    m_notices->owner = this;
    m_search_index.reset();
    m_stage = stage;
    m_animated_visibility.clear();
    m_batcher->cancel();
    if (m_stage)
        m_listener = std::make_unique<NoticeListener>(m_stage, m_notices);
    Q_EMIT changed(SceneOutlinerChange::reset());
}

void UsdStageDataSource::on_objects_changed(const std::vector<SdfPath>& resynced, const std::vector<SdfPath>& changed)
{
    if (!m_batcher)
        return;
    if (!resynced.empty())
        m_search_index.reset();

    std::vector<SceneItemId> resync_parents;
    for (const SdfPath& path : resynced)
    {
        if (path == SdfPath::AbsoluteRootPath())
        {
            m_batcher->request_reset();
            return;
        }
        if (!path.IsAbsoluteRootOrPrimPath() && !path.IsPrimPropertyPath())
            continue;
        if (path.IsPrimPropertyPath())
        {
            if (affects_inherited_columns(path))
                m_batcher->add_changed({}, true);
            else
                m_batcher->add_changed({ make_usd_item_id(path.GetPrimPath()) }, true);
            continue;
        }
        // Reconcile the parent's children as well as the resynced subtree.
        resync_parents.push_back(make_usd_item_id(path.GetParentPath()));
    }
    if (!resync_parents.empty())
        m_batcher->add_resync(resync_parents);

    std::vector<SceneItemId> changed_items;
    for (const SdfPath& path : changed)
    {
        SdfPath prim_path;
        if (path.IsAbsoluteRootOrPrimPath())
            prim_path = path;
        else if (path.IsPrimPropertyPath())
            prim_path = path.GetPrimPath();
        else
            continue;
        if (affects_inherited_columns(path))
            m_batcher->add_changed({}, true);
        changed_items.push_back(make_usd_item_id(prim_path));
    }
    if (!changed_items.empty())
        m_batcher->add_changed(changed_items, true);
}

UsdPrim UsdStageDataSource::prim_for(const SceneItemId& item) const
{
    if (!m_stage || usd_path_of(item).IsEmpty())
        return UsdPrim();
    return m_stage->GetPrimAtPath(usd_path_of(item));
}

SceneItemList UsdStageDataSource::roots() const
{
    SceneItemList out;

    if (m_stage && m_stage->GetPseudoRoot())
        out.push_back(make_usd_item_id(SdfPath::AbsoluteRootPath()));

    return out;
}

SceneItemList UsdStageDataSource::children(const SceneItemId& parent) const
{
    SceneItemList out;

    const UsdPrim prim = prim_for(parent);
    if (!prim)
        return out;
    for (const UsdPrim& child : prim.GetFilteredChildren(outliner_predicate()))
        out.push_back(make_usd_item_id(child.GetPath()));

    return out;
}

SceneItemList UsdStageDataSource::search(const std::string& query) const
{
    auto matches = ISceneOutlinerDataSource::search(query);
    std::sort(matches.begin(), matches.end(), [](const SceneItemId& a, const SceneItemId& b) { return usd_path_of(a) < usd_path_of(b); });
    return matches;
}

void UsdStageDataSource::search_async(const std::string& query, SceneOutlinerSearchRequest* request) const
{
    auto completion = request->completion();
    if (query.empty() || !m_stage)
    {
        completion.complete({});
        return;
    }
    if (m_search_index && !m_search_index->content.is_cancelled())
    {
        dispatch_search(m_search_index, query, completion);
        return;
    }
    auto build = std::make_shared<SnapshotBuild>(m_stage);
    build->queue = m_notices;
    build->index->content = m_notices->snapshot_content();
    build->completion = completion;
    build->query = query;
    QTimer::singleShot(0, this, [this, build]() { continue_snapshot(build); });
}

SceneItemId UsdStageDataSource::parent(const SceneItemId& item) const
{
    const SdfPath path = usd_path_of(item);
    if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath())
        return SceneItemId();
    return make_usd_item_id(path.GetParentPath());
}

bool UsdStageDataSource::has_item(const SceneItemId& item) const
{
    return static_cast<bool>(prim_for(item));
}

bool UsdStageDataSource::has_children(const SceneItemId& item) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim)
        return false;
    return !prim.GetFilteredChildren(outliner_predicate()).empty();
}

std::string UsdStageDataSource::display_name(const SceneItemId& item) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim)
        return {};
    return prim.IsPseudoRoot() ? std::string("/") : prim.GetName().GetString();
}

std::string UsdStageDataSource::type_name(const SceneItemId& item) const
{
    const UsdPrim prim = prim_for(item);
    return prim ? prim.GetTypeName().GetString() : std::string();
}

std::string UsdStageDataSource::tooltip(const SceneItemId& item) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim)
        return {};
    if (prim.IsPseudoRoot())
        return "/";

    std::string text = prim.GetPath().GetString();
    const std::string type = prim.GetTypeName().GetString();
    if (!type.empty())
        text += "\nType: " + type;

    std::vector<std::string> tags;
    if (!prim.IsActive())
        tags.push_back("inactive");
    if (prim.IsAbstract())
        tags.push_back("abstract");
    else if (!prim.IsDefined())
        tags.push_back("over");
    if (prim.IsInstance())
        tags.push_back("instance");
    else if (prim.IsInstanceProxy())
        tags.push_back("instance proxy");
    else if (prim.IsInstanceable())
        tags.push_back("instanceable");
    if (!tags.empty())
        text += "\n(" + TfStringJoin(tags, ", ") + ")";
    return text;
}

QIcon UsdStageDataSource::icon(const SceneItemId& item) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim || prim.IsPseudoRoot())
        return {};

    const std::string type = TfStringToLower(prim.GetTypeName().GetString());
    TfToken kind;
    UsdModelAPI(prim).GetKind(&kind);
    const std::string kind_str = TfStringToLower(kind.GetString());

    // Use the first available icon alias.
    std::vector<std::string> candidates;
    if (type == "xform" && prim.IsInstanceable())
        candidates.push_back("xform_instance");
    if (type == "mesh" && (prim.IsInstance() || prim.IsInstanceProxy()))
        candidates.push_back("mesh_instance");
    if (kind_str == "assembly")
        candidates.push_back("package");
    else if (kind_str == "component")
        candidates.push_back("component");
    else if (kind_str == "subcomponent")
        candidates.push_back("subcomponent");
    if (!type.empty())
        candidates.push_back(type);
    candidates.push_back("withouttype");

    const bool on_target = is_spec_on_edit_target(prim, m_stage);
    for (const std::string& name : candidates)
    {
        const QString path = QStringLiteral(":/icons/") + QString::fromStdString(name);
        const QIcon icon = prim_icon(path, on_target);
        if (!icon.isNull())
            return icon;
    }
    return {};
}

int UsdStageDataSource::display_hints(const SceneItemId& item) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim || prim.IsPseudoRoot())
        return NONE;
    int hints = NONE;
    if (!prim.IsActive())
        hints |= DIMMED | STRIKEOUT;
    if (prim.IsAbstract() || !prim.IsDefined())
        hints |= ITALIC;
    return hints;
}

bool UsdStageDataSource::can_rename(const SceneItemId& item) const
{
    return can_rename_prim(prim_for(item));
}

void UsdStageDataSource::rename(const SceneItemId& item, const std::string& new_name)
{
    set_column_text(item, "name", new_name);
}

bool UsdStageDataSource::has_visibility(const SceneItemId& item) const
{
    const UsdPrim prim = prim_for(item);
    return prim && UsdGeomImageable(prim);
}

bool UsdStageDataSource::is_visible(const SceneItemId& item) const
{
    const auto prim = prim_for(item);
    track_animated_visibility(prim);
    const UsdGeomImageable img(prim);
    if (!img)
        return true;
    TfToken value = UsdGeomTokens->inherited;
    if (const UsdAttribute attr = img.GetVisibilityAttr())
        attr.Get(&value, UsdTimeCode(Application::instance().get_current_time()));
    return value != UsdGeomTokens->invisible;
}

void UsdStageDataSource::track_animated_visibility(const UsdPrim& prim) const
{
    if (!prim)
        return;
    for (auto current = prim; current && !current.IsPseudoRoot(); current = current.GetParent())
    {
        const UsdGeomImageable imageable(current);
        if (imageable && imageable.GetVisibilityAttr().ValueMightBeTimeVarying())
        {
            m_animated_visibility.insert(prim.GetPath());
            return;
        }
    }
    m_animated_visibility.erase(prim.GetPath());
}

void UsdStageDataSource::set_visible(const SceneItemId& item, bool visible)
{
    CommandInterface::execute("set_prim_visibility",
                              CommandArgs().arg(SdfPathVector { usd_path_of(item) }).arg(visible).kwarg("stage", UsdStageWeakPtr(m_stage)));
}

SceneOutlinerColumnList UsdStageDataSource::columns() const
{
    SceneOutlinerColumnList out;

    using Role = SceneOutlinerColumn::Role;
    auto add_toggle = [&out](const char* id, const char* title, const char* header, const char* on, const char* off, bool visible) {
        SceneOutlinerColumn c;
        c.id = id;
        // Icon-only headers still use the title in the column menu.
        c.title = QString::fromLatin1(title);
        c.role = Role::TOGGLE;
        c.header_icon = QString::fromLatin1(header);
        c.icon_on = QString::fromLatin1(on);
        c.icon_off = QString::fromLatin1(off);
        c.width = 24;
        c.default_visible = visible;
        out.push_back(c);
    };
    add_toggle("visibility", "Visibility", ":/icons/eye_open", ":/icons/eye_open", ":/icons/eye_closed", true);
    add_toggle("active", "Active", ":/icons/activation_on", ":/icons/activation_on", ":/icons/activation_off", false);
    add_toggle("payload", "Payload", ":/icons/payload_on", ":/icons/payload_on", ":/icons/payload_off", false);

    SceneOutlinerColumn name;
    name.id = "name";
    name.title = QStringLiteral("Name");
    name.role = Role::TREE;
    name.stretch = true;
    name.editable = true;
    out.push_back(name);

    SceneOutlinerColumn type;
    type.id = "type";
    type.title = QStringLiteral("Type");
    type.editable = true;
    type.default_visible = false;
    type.choices = concrete_schema_types();
    out.push_back(type);

    SceneOutlinerColumn kind;
    kind.id = "kind";
    kind.title = QStringLiteral("Kind");
    kind.editable = true;
    kind.default_visible = false;
    kind.choices = QStringList { QStringLiteral("model"), QStringLiteral("group"), QStringLiteral("assembly"), QStringLiteral("component"),
                                 QStringLiteral("subcomponent") };
    out.push_back(kind);

    SceneOutlinerColumn drawmode;
    drawmode.id = "drawmode";
    drawmode.title = QStringLiteral("Draw Mode");
    drawmode.editable = true;
    drawmode.default_visible = false;
    drawmode.choices =
        QStringList { QString::fromStdString(UsdGeomTokens->inherited.GetString()), QString::fromStdString(UsdGeomTokens->default_.GetString()),
                      QString::fromStdString(UsdGeomTokens->cards.GetString()), QString::fromStdString(UsdGeomTokens->bounds.GetString()),
                      QString::fromStdString(UsdGeomTokens->origin.GetString()) };
    out.push_back(drawmode);

    SceneOutlinerColumn material_full;
    material_full.id = "material:full";
    material_full.title = QStringLiteral("Full Material");
    material_full.default_visible = false;
    out.push_back(material_full);

    SceneOutlinerColumn material_preview;
    material_preview.id = "material:preview";
    material_preview.title = QStringLiteral("Preview Material");
    material_preview.default_visible = false;
    out.push_back(material_preview);

    for (const UsdUtilsRegisteredVariantSet& variant_set : UsdUtilsGetRegisteredVariantSets())
    {
        SceneOutlinerColumn variant;
        variant.id = "variant:" + variant_set.name;
        variant.title = QString::fromStdString(variant_set.name);
        variant.editable = true;
        variant.default_visible = false;
        out.push_back(variant);
    }

    return out;
}

std::string UsdStageDataSource::column_text(const SceneItemId& item, const std::string& column) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim)
        return {};
    const std::string& id = column;

    if (id == "kind")
    {
        TfToken kind;
        UsdModelAPI(prim).GetKind(&kind);
        return kind.GetString();
    }
    if (id == "drawmode")
        return UsdGeomModelAPI(prim).ComputeModelDrawMode().GetString();
    if (id == "material:full" || id == "material:preview")
    {
        UsdShadeMaterialBindingAPI binding(prim);
        UsdRelationship rel;
        const TfToken purpose = id == "material:preview" ? UsdShadeTokens->preview : UsdShadeTokens->full;
        const UsdShadeMaterial material = binding.ComputeBoundMaterial(purpose, &rel);
        return material.GetPrim().IsValid() ? material.GetPrim().GetPath().GetString() : std::string();
    }
    if (TfStringStartsWith(id, "variant:"))
    {
        if (!prim.HasVariantSets())
            return {};
        return prim.GetVariantSet(id.substr(8)).GetVariantSelection();
    }
    if (id == "name")
        return display_name(item);
    if (id == "type")
        return type_name(item);
    return {};
}

bool UsdStageDataSource::set_column_text(const SceneItemId& item, const std::string& column, const std::string& value)
{
    const std::string& id = column;
    const UsdPrim prim = prim_for(item);
    if (!prim || prim.IsPseudoRoot())
        return false;
    const auto normalized = QString::fromStdString(value).trimmed().toStdString();
    if (id == "name")
    {
        if (normalized.empty() || !can_rename_prim(prim))
            return false;
        return CommandInterface::execute(
                   "rename_prim", CommandArgs().arg(TfToken(normalized)).kwarg("path", prim.GetPath()).kwarg("stage", UsdStageWeakPtr(m_stage)))
            .is_successful();
    }

    CommandArgs args;
    args.arg(prim.GetPath()).kwarg("stage", UsdStageWeakPtr(m_stage));
    if (id == "type" || id == "kind" || id == "drawmode")
    {
        args.arg(TfToken(normalized));
        const char* command = id == "type" ? "set_prim_type" : id == "kind" ? "set_prim_kind" : "set_prim_draw_mode";
        return CommandInterface::execute(command, args).is_successful();
    }
    if (TfStringStartsWith(id, "variant:"))
        return CommandInterface::execute("set_variant_selection", args.arg(id.substr(8)).arg(value)).is_successful();
    return false;
}

bool UsdStageDataSource::toggle_state(const SceneItemId& item, const std::string& column) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim)
        return false;
    const std::string& id = column;
    if (id == "visibility")
        return is_visible(item);
    if (id == "active")
        return prim.IsActive();
    if (id == "payload")
        return prim.HasAuthoredPayloads() && prim.IsLoaded();
    return false;
}

bool UsdStageDataSource::toggle_enabled(const SceneItemId& item, const std::string& column) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim)
        return false;
    const std::string& id = column;
    if (id == "payload")
        return prim.HasAuthoredPayloads();
    if (id == "visibility")
    {
        if (!UsdGeomImageable(prim))
            return false;
        for (auto parent = prim.GetParent(); parent && !parent.IsPseudoRoot(); parent = parent.GetParent())
        {
            const UsdGeomImageable parent_img(parent);
            if (parent_img)
                return parent_img.ComputeVisibility(UsdTimeCode(Application::instance().get_current_time())) != UsdGeomTokens->invisible;
        }
        return true;
    }
    return true;
}

void UsdStageDataSource::toggle_column(const SceneItemId& item, const std::string& column)
{
    UsdPrim prim = prim_for(item);
    if (!prim)
        return;
    const std::string& id = column;
    if (id == "visibility")
    {
        set_visible(item, !is_visible(item));
        return;
    }
    if (id == "active")
    {
        CommandInterface::execute("set_prim_active",
                                  CommandArgs().arg(SdfPathVector { prim.GetPath() }).arg(!prim.IsActive()).kwarg("stage", UsdStageWeakPtr(m_stage)));
        return;
    }
    if (id == "payload" && prim.HasAuthoredPayloads())
    {
        CommandInterface::execute("set_payload_loaded",
                                  CommandArgs().arg(SdfPathVector { prim.GetPath() }).arg(!prim.IsLoaded()).kwarg("stage", UsdStageWeakPtr(m_stage)));
        return;
    }
}

bool UsdStageDataSource::can_reparent() const
{
    return m_stage && m_stage->GetEditTarget().GetMapFunction().IsIdentityPathMapping();
}

void UsdStageDataSource::reparent(const std::vector<SceneItemId>& items, const SceneItemId& new_parent)
{
    const SdfPath parent = usd_path_of(new_parent);
    if (!can_reparent() || parent.IsEmpty())
        return;
    SdfPathVector paths;
    paths.reserve(items.size());
    for (const SceneItemId& item : items)
    {
        const SdfPath path = usd_path_of(item);
        if (path.IsEmpty() || path == SdfPath::AbsoluteRootPath() || path.GetParentPath() == parent)
            continue;
        if (parent.HasPrefix(path))
            return;
        paths.push_back(path);
    }
    if (paths.empty())
        return;
    ensure_undo_delegate(m_stage);
    CommandInterface::execute(
        "parent_prim", CommandArgs().arg(parent).kwarg("paths", paths).kwarg("preserve_transform", true).kwarg("stage", UsdStageWeakPtr(m_stage)));
}

QString UsdStageDataSource::drag_mime_type() const
{
    // Shared with viewport material assignment and light-linking panels.
    return QStringLiteral("application/x-sdfpaths");
}

QByteArray UsdStageDataSource::drag_mime_data(const std::vector<SceneItemId>& items) const
{
    // Other panels expect newline-separated prim paths.
    QStringList paths;
    paths.reserve(static_cast<int>(items.size()));
    for (const SceneItemId& item : items)
    {
        const SdfPath path = usd_path_of(item);
        if (!path.IsEmpty() && path != SdfPath::AbsoluteRootPath())
            paths << QString::fromStdString(path.GetString());
    }
    return paths.join(QLatin1Char('\n')).toUtf8();
}

std::vector<ISceneOutlinerDataSource::Filter> UsdStageDataSource::filters() const
{
    std::vector<ISceneOutlinerDataSource::Filter> out;

    out.push_back({ "inactive", QStringLiteral("Inactive Prims"), true });
    out.push_back({ "undefined", QStringLiteral("Undefined Prims (Overs)"), true });
    out.push_back({ "abstract", QStringLiteral("Abstract Prims (Classes)"), false });

    return out;
}

bool UsdStageDataSource::matches_filter(const SceneItemId& item, const std::string& filter_id) const
{
    const UsdPrim prim = prim_for(item);
    if (!prim || prim.IsPseudoRoot())
        return false;
    const std::string& id = filter_id;
    if (id == "inactive")
        return !prim.IsActive();
    if (id == "undefined")
        return !prim.IsDefined();
    if (id == "abstract")
        return prim.IsAbstract();
    return false;
}

OPENDCC_NAMESPACE_CLOSE
