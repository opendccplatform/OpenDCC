/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/ui/scene_outliner/api.h"
#include "opendcc/ui/scene_outliner/item_id.h"
#include "opendcc/ui/scene_outliner/search_request.h"

#include <QIcon>
#include <QObject>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <string>
#include <vector>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Column definition supplied by a data source.
 * @note Exactly one column should have Role::TREE for hierarchy navigation.
 */
struct SceneOutlinerColumn
{
    enum class Role
    {
        TREE, ///< The hierarchy/name column (icon + display text + branches).
        TEXT, ///< A plain text cell (optionally an editable combo via `choices`).
        TOGGLE, ///< An on/off icon cell, flipped by a single click.
    };

    std::string id; ///< Stable column key the data source dispatches on.
    QString title; ///< Header text (ignored if `header_icon` is set).
    QString header_icon; ///< Resource path for an icon header (toggle columns).
    Role role = Role::TEXT;
    QString icon_on; ///< Toggle "on" icon resource (Role::TOGGLE).
    QString icon_off; ///< Toggle "off" icon resource (Role::TOGGLE).
    int width = -1; ///< Initial pixel width, or -1 for the view default; fixed for non-stretched toggles.
    bool stretch = false; ///< Stretch to fill available space.
    bool editable = false;
    bool default_visible = true;
    bool interactive = true; ///< Toggle columns: false = read-only state indicator.
    QStringList choices; ///< Suggested values for an editable combo box.
};

using SceneOutlinerColumnList = std::vector<SceneOutlinerColumn>;

/**
 * @brief Backend change notification.
 * @note Adapters can coalesce updates with SceneOutlinerBatcher.
 */
struct SceneOutlinerChange
{
    enum class Type
    {
        ITEMS_ADDED, ///< Children were added under `parents`.
        ITEMS_REMOVED, ///< Children were removed under `parents`.
        ITEMS_CHANGED, ///< Per-item display data of `items` changed (no topology change).
        RESET ///< The whole tree changed; consumers rebuild from scratch.
    };

    Type type = Type::RESET;
    /// Parents to reconcile for ITEMS_ADDED and ITEMS_REMOVED; an invalid ID updates roots. Unused for RESET.
    SceneItemList parents;
    /// For ITEMS_CHANGED: affected items; empty refreshes all materialized rows.
    SceneItemList items;

    /// False for display-only updates that leave search results unchanged.
    bool affects_search = true;

    static SceneOutlinerChange reset() { return { Type::RESET, {}, {} }; }
    static SceneOutlinerChange added(SceneItemList parents) { return { Type::ITEMS_ADDED, std::move(parents), {} }; }
    static SceneOutlinerChange removed(SceneItemList parents) { return { Type::ITEMS_REMOVED, std::move(parents), {} }; }
    static SceneOutlinerChange changed(SceneItemList items, bool affects_search = true)
    {
        return { Type::ITEMS_CHANGED, {}, std::move(items), affects_search };
    }
};

struct SceneOutlinerFilter
{
    std::string id;
    QString label;
    bool shown_by_default = true;
};

/**
 * @brief Supplies hierarchy, display data and editing capabilities to the model.
 * @note Queries and edits run on the model's thread. Async backends must
 * synchronize backend reads and writes before notifying.
 * changed() may be emitted from workers. Artist edits must use commands.
 */
class OPENDCC_SCENE_OUTLINER_API ISceneOutlinerDataSource : public QObject
{
    Q_OBJECT
public:
    explicit ISceneOutlinerDataSource(QObject* parent = nullptr);
    virtual ~ISceneOutlinerDataSource() = default;

    /// The top-level item(s). Most sources return a single pseudo-root.
    virtual SceneItemList roots() const = 0;
    /// Immediate children of `parent`, in display order.
    virtual SceneItemList children(const SceneItemId& parent) const = 0;
    /// Parent of `item`, or an invalid `SceneItemId` for a root.
    virtual SceneItemId parent(const SceneItemId& item) const = 0;
    /// Whether item still exists; used to discard removed subtrees during resync.
    virtual bool has_item(const SceneItemId& item) const = 0;
    /// Cheap "might have children" hint; the model may still call `children()`.
    virtual bool has_children(const SceneItemId& item) const;

    virtual std::string display_name(const SceneItemId& item) const = 0;
    /// Optional display type; empty by default.
    virtual std::string type_name(const SceneItemId& item) const { return {}; }
    virtual QIcon icon(const SceneItemId& item) const { return {}; }
    /// Tooltip, defaulting to the display name.
    virtual std::string tooltip(const SceneItemId& item) const { return display_name(item); }

    /// Row appearance flags, combined with bitwise OR.
    enum DisplayHint
    {
        NONE = 0,
        DIMMED = 1 << 0, ///< Greyed-out foreground.
        ITALIC = 1 << 1, ///< Italic text.
        STRIKEOUT = 1 << 2, ///< Struck-through text.
    };
    virtual int display_hints(const SceneItemId& item) const { return NONE; }

    virtual bool has_visibility(const SceneItemId& item) const { return false; }
    virtual bool is_visible(const SceneItemId& item) const { return true; }
    virtual void set_visible(const SceneItemId& item, bool visible) {}

    virtual bool can_rename(const SceneItemId& item) const { return false; }
    /// Rename `item` to `new_name`. The adapter is responsible for routing the
    /// edit through the command system so it is undoable.
    virtual void rename(const SceneItemId& item, const std::string& new_name) {}

    /// Column definitions supplied by this data source.
    virtual SceneOutlinerColumnList columns() const { return {}; }

    virtual std::string column_text(const SceneItemId& item, const std::string& column) const { return {}; }

    /// Commit an editable text/combo cell. Return true if handled. The adapter
    /// routes the edit through the command system so it is undoable.
    virtual bool set_column_text(const SceneItemId& item, const std::string& column, const std::string& value) { return false; }

    /// On/off state of a Toggle column.
    virtual bool toggle_state(const SceneItemId& item, const std::string& column) const { return false; }
    /// Whether a Toggle is meaningful for this item (rendered faded if not).
    virtual bool toggle_enabled(const SceneItemId& item, const std::string& column) const { return true; }
    /// Flip a Toggle column (undoable, adapter-routed).
    virtual void toggle_column(const SceneItemId& item, const std::string& column) {}

    virtual bool can_reparent() const { return false; }
    /// Reparent `items` under `new_parent` (undoable, adapter-routed).
    virtual void reparent(const SceneItemList& items, const SceneItemId& new_parent) {}

    /// MIME type and encoded item data for drags to other panels.
    virtual QString drag_mime_type() const { return {}; }
    virtual QByteArray drag_mime_data(const SceneItemList& items) const { return {}; }

    /// Filter categories shown as checkboxes in the Show menu.
    /// Disabled categories hide matching items and their subtrees.

    using Filter = SceneOutlinerFilter;
    virtual std::vector<SceneOutlinerFilter> filters() const { return {}; }
    /// Whether item belongs to the filter_id category.
    virtual bool matches_filter(const SceneItemId& item, const std::string& filter_id) const { return false; }

    /**
     * @brief Find case-insensitive display-name matches below the roots.
     * @note Search excludes the roots and tolerates repeated or cyclic child
     * links. Override this traversal when the backend has a more efficient index.
     */
    virtual SceneItemList search(const std::string& query) const;

    /**
     * @brief Complete a search synchronously or asynchronously.
     * @note Call start_search() to bind cancellation to this source. Overrides
     * finish the request synchronously, or retain its completion() handle for a worker.
     * Backends must synchronize reads and writes. The default calls search().
     */
    virtual void search_async(const std::string& query, SceneOutlinerSearchRequest* request) const;
    void start_search(const std::string& query, SceneOutlinerSearchRequest* request) const;

    void notify(const SceneOutlinerChange& change) { Q_EMIT changed(change); }

Q_SIGNALS:
    void changed(const SceneOutlinerChange& change);
};

OPENDCC_NAMESPACE_CLOSE

Q_DECLARE_METATYPE(OPENDCC_NAMESPACE::SceneOutlinerChange)
