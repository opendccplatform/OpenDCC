/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd_editor/usd_outliner/api.h"
#include "opendcc/app/core/application.h"
#include "opendcc/ui/scene_outliner/notice_batcher.h"
#include "opendcc/ui/scene_outliner/tree_data_source.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/base/work/dispatcher.h>

#include <memory>
#include <unordered_set>

OPENDCC_NAMESPACE_OPEN

using UsdOutlinerStage = PXR_NS::UsdStageRefPtr;

/**
 * @brief Composed USD hierarchy with path identities and batched stage notices.
 * @note Construct, query, edit and destroy on the GUI thread. Searches capture
 * names and paths on that thread; workers only read the immutable index.
 * USD notices may arrive on any thread. Callers exclude stage readers during writes.
 */
class OPENDCC_USD_OUTLINER_API UsdStageDataSource : public ISceneOutlinerDataSource
{
    Q_OBJECT
public:
    explicit UsdStageDataSource(QObject* parent = nullptr);
    ~UsdStageDataSource() override;

    /// Display this stage until the next application stage change; nullptr clears it.
    void set_stage(const UsdOutlinerStage& stage);
    UsdOutlinerStage stage() const { return m_stage; }

    SceneItemList roots() const override;
    SceneItemList children(const SceneItemId& parent) const override;
    SceneItemId parent(const SceneItemId& item) const override;
    bool has_item(const SceneItemId& item) const override;
    bool has_children(const SceneItemId& item) const override;

    std::string display_name(const SceneItemId& item) const override;
    std::string type_name(const SceneItemId& item) const override;
    std::string tooltip(const SceneItemId& item) const override;
    QIcon icon(const SceneItemId& item) const override;
    int display_hints(const SceneItemId& item) const override;

    bool has_visibility(const SceneItemId& item) const override;
    bool is_visible(const SceneItemId& item) const override;
    void set_visible(const SceneItemId& item, bool visible) override;

    bool can_rename(const SceneItemId& item) const override;
    void rename(const SceneItemId& item, const std::string& new_name) override;

    SceneOutlinerColumnList columns() const override;
    std::string column_text(const SceneItemId& item, const std::string& column) const override;
    bool set_column_text(const SceneItemId& item, const std::string& column, const std::string& value) override;
    bool toggle_state(const SceneItemId& item, const std::string& column) const override;
    bool toggle_enabled(const SceneItemId& item, const std::string& column) const override;
    void toggle_column(const SceneItemId& item, const std::string& column) override;

    bool can_reparent() const override;
    void reparent(const std::vector<SceneItemId>& items, const SceneItemId& new_parent) override;

    QString drag_mime_type() const override;
    QByteArray drag_mime_data(const std::vector<SceneItemId>& items) const override;

    std::vector<SceneOutlinerFilter> filters() const override;
    bool matches_filter(const SceneItemId& item, const std::string& filter_id) const override;
    SceneItemList search(const std::string& query) const override;
    void search_async(const std::string& query, SceneOutlinerSearchRequest* request) const override;

private:
    friend class UsdOutlinerNoticeTestAccess;
    struct SearchIndex;
    struct SnapshotBuild;
    struct NoticeQueue;
    struct NoticeListener;
    void continue_snapshot(const std::shared_ptr<SnapshotBuild>& build) const;
    void dispatch_search(std::shared_ptr<const SearchIndex> index, const std::string& query, SceneOutlinerSearchCompletion completion) const;
    void flush_notices();
    PXR_NS::UsdPrim prim_for(const SceneItemId& item) const;
    void on_objects_changed(const std::vector<PXR_NS::SdfPath>& resynced, const std::vector<PXR_NS::SdfPath>& changed);
    void track_animated_visibility(const PXR_NS::UsdPrim& prim) const;

    PXR_NS::UsdStageRefPtr m_stage;
    std::shared_ptr<NoticeQueue> m_notices;
    std::unique_ptr<NoticeListener> m_listener;
    SceneOutlinerBatcher* m_batcher = nullptr;
    Application::CallbackHandle m_stage_changed_handle;
    Application::CallbackHandle m_time_changed_handle;
    mutable std::unordered_set<PXR_NS::SdfPath, PXR_NS::SdfPath::Hash> m_animated_visibility;
    mutable PXR_NS::WorkDispatcher m_search_dispatcher;
    mutable std::shared_ptr<const SearchIndex> m_search_index;
};

OPENDCC_NAMESPACE_CLOSE
