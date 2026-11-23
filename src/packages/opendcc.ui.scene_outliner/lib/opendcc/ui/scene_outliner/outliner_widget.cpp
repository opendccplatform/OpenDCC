/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/outliner_widget.h"

#include "opendcc/base/logging/logging_utils.h"
#include "opendcc/ui/scene_outliner/outliner_model.h"
#include "opendcc/ui/scene_outliner/outliner_proxy_model.h"
#include "opendcc/ui/scene_outliner/outliner_tree_view.h"
#include "opendcc/ui/scene_outliner/search_request.h"

#include <QAction>
#include <QElapsedTimer>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_set>
#include <utility>

OPENDCC_NAMESPACE_OPEN

namespace
{
    class SelectionSyncGuard
    {
    public:
        SelectionSyncGuard(SceneOutliner* owner, bool& syncing)
            : m_owner(owner)
            , m_syncing(syncing)
            , m_previous(std::exchange(syncing, true))
        {
        }
        SelectionSyncGuard(const SelectionSyncGuard&) = delete;
        SelectionSyncGuard& operator=(const SelectionSyncGuard&) = delete;
        ~SelectionSyncGuard()
        {
            if (m_owner)
                m_syncing = m_previous;
        }

    private:
        QPointer<SceneOutliner> m_owner;
        bool& m_syncing;
        bool m_previous;
    };

    QItemSelection row_selection(const QList<QPersistentModelIndex>& indexes, const QAbstractItemModel& model)
    {
        QHash<QModelIndex, std::vector<int>> siblings;
        for (const auto& index : indexes)
            if (index.isValid() && index.model() == &model)
                siblings[index.parent()].push_back(index.row());

        QItemSelection selection;
        for (auto group = siblings.begin(); group != siblings.end(); ++group)
        {
            const auto parent = group.key();
            const int last_column = model.columnCount(parent) - 1;
            auto& rows = group.value();
            std::sort(rows.begin(), rows.end());
            int first = rows.front();
            int last = first;
            for (const auto row : rows)
            {
                if (row > last + 1)
                {
                    selection.select(model.index(first, 0, parent), model.index(last, last_column, parent));
                    first = row;
                }
                last = row;
            }
            selection.select(model.index(first, 0, parent), model.index(last, last_column, parent));
        }
        return selection;
    }
}

SceneOutliner::SceneOutliner(ISceneOutlinerDataSource* source, ISceneOutlinerSelection* selection, QWidget* parent)
    : QWidget(parent)
{
    m_model = new SceneOutlinerModel(this);
    m_proxy = new SceneOutlinerProxyModel(this);
    m_proxy->setSourceModel(m_model);

    m_view = new SceneOutlinerTreeView(this);
    m_view->set_models(m_proxy, m_model);
    // A source replacement may change the column schema.
    connect(m_proxy, &QAbstractItemModel::modelReset, m_view, &SceneOutlinerTreeView::configure_columns);
    connect(m_proxy, &QAbstractItemModel::modelReset, m_view, &SceneOutlinerTreeView::apply_root_index);
    // Model updates and filtering can remove view rows without changing the external selection.
    connect_model();
    connect(m_proxy, &SceneOutlinerProxyModel::category_filter_changed, this, [this]() { apply_selection_from_context(true); });

    auto* menu_bar = m_menu_bar = new QMenuBar(this);
    connect(m_proxy, &QAbstractItemModel::modelReset, this, [this]() { m_filters_dirty = true; });

    m_search_button = new QToolButton(this);
    const QIcon search_icon = QIcon(QStringLiteral(":/icons/small_search"));
    if (!search_icon.isNull())
        m_search_button->setIcon(search_icon);
    else
        m_search_button->setText(tr("Find"));
    m_search_button->setIconSize(QSize(16, 16));
    m_search_button->setAutoRaise(true);
    m_search_button->setCheckable(true);
    m_search_button->setFocusPolicy(Qt::NoFocus);
    m_search_button->setToolTip(tr("Search"));
    menu_bar->setCornerWidget(m_search_button, Qt::TopRightCorner);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search..."));
    m_filter->setClearButtonEnabled(true);
    m_filter->setVisible(false);

    // Coalesce keystrokes before starting a backend search.
    m_filter_timer = new QTimer(this);
    m_filter_timer->setSingleShot(true);
    m_filter_timer->setInterval(200);
    connect(m_filter_timer, &QTimer::timeout, this, &SceneOutliner::on_filter_timeout);

    auto* layout = new QVBoxLayout(this);
    layout->setMenuBar(menu_bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_filter);
    layout->addWidget(m_view);

    m_view->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_filter, &QLineEdit::textChanged, this, &SceneOutliner::on_filter_text_changed);
    connect(m_search_button, &QToolButton::toggled, this, &SceneOutliner::toggle_search);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SceneOutliner::on_view_selection_changed);
    connect(m_view, &QWidget::customContextMenuRequested, this, &SceneOutliner::on_context_menu);

    m_model->set_data_source(source);
    set_selection_context(selection);
    refresh_filters();
}

void SceneOutliner::refresh_filters()
{
    m_filters_dirty = false;
    const QPointer<SceneOutliner> self(this);
    const auto model = m_model;
    const auto source = model->data_source();
    const auto operation = m_search_token;
    auto filters = source ? source->filters() : std::vector<ISceneOutlinerDataSource::Filter>();
    if (!self || !model || model != m_model || operation.is_cancelled() || source != model->data_source())
        return;

    if (m_filter_source != source)
    {
        m_filter_source = source;
        m_hidden_filters.clear();
        m_known_filters.clear();
    }
    if (!source)
        return;

    QSet<QString> known_filters;
    for (const ISceneOutlinerDataSource::Filter& filter : filters)
    {
        const QString id = QString::fromStdString(filter.id);
        known_filters.insert(id);
        if (!m_known_filters.contains(id) && !filter.shown_by_default)
            m_hidden_filters.insert(id);
    }
    m_known_filters = std::move(known_filters);
    m_hidden_filters.intersect(m_known_filters);
    update_hidden_filters();
    if (self)
        Q_EMIT filters_changed();
}

void SceneOutliner::set_filter_shown(const QString& id, bool shown)
{
    if (shown)
        m_hidden_filters.remove(id);
    else
        m_hidden_filters.insert(id);
    update_hidden_filters();
}

void SceneOutliner::set_search_text(const QString& text)
{
    m_search_button->setChecked(!text.isEmpty());
    m_filter->setText(text);
}

QString SceneOutliner::search_text() const
{
    return m_filter->text();
}

void SceneOutliner::update_hidden_filters()
{
    const QPointer<SceneOutliner> self(this);
    {
        SelectionSyncGuard syncing(this, m_syncing_selection);
        m_proxy->set_hidden_filters(m_hidden_filters);
    }
    if (self)
        apply_selection_from_context();
}

SceneItemList SceneOutliner::selected_items() const
{
    SceneItemList items;
    const auto rows = m_view->selectionModel()->selectedRows(0);
    items.reserve(static_cast<std::size_t>(rows.size()));
    for (const auto& index : rows)
    {
        const auto item = m_model->item_for_index(m_proxy->mapToSource(index));
        if (item)
            items.push_back(item);
    }
    return items;
}

void SceneOutliner::edit_item(const SceneItemId& item)
{
    const QModelIndex source = m_model->index_for_item(item, m_model->tree_column());
    if (!source.isValid())
        return;
    const QModelIndex proxy_index = m_proxy->mapFromSource(source);
    if (proxy_index.isValid())
        m_view->edit(proxy_index);
}

void SceneOutliner::edit_current()
{
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid())
        return;
    const QModelIndex name = current.sibling(current.row(), m_model->tree_column());
    m_view->edit(name);
}

SceneOutlinerModel* SceneOutliner::model() const
{
    return m_model;
}

ISceneOutlinerDataSource* SceneOutliner::data_source() const
{
    return m_model->data_source();
}

QModelIndex SceneOutliner::index_for_item(const SceneItemId& item) const
{
    return m_model->ensure_index_for_item(item);
}

SceneItemId SceneOutliner::item_for_index(const QModelIndex& index) const
{
    return m_model->item_for_index(index);
}

void SceneOutliner::on_context_menu(const QPoint& pos)
{
    const QPointer<SceneOutliner> self(this);
    // Menu actions use the backend selection.
    const QModelIndex clicked = m_view->indexAt(pos);
    if (clicked.isValid() && !m_view->selectionModel()->isSelected(clicked))
    {
        m_view->selectionModel()->select(clicked, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        if (!self)
            return;
        m_view->setCurrentIndex(clicked);
    }

    if (!self)
        return;
    const QPointer<QMenu> menu = new QMenu(this);
    Q_EMIT context_menu_requested(menu.data());
    if (self && menu && !menu->isEmpty())
        menu->exec(m_view->viewport()->mapToGlobal(pos));
    if (menu)
        menu->deleteLater();
}

SceneOutliner::~SceneOutliner()
{
    cancel_search();
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    // Python may release providers during QWidget destruction, resetting the child model.
    disconnect(m_proxy, nullptr, this, nullptr);
    disconnect(m_view->selectionModel(), nullptr, this, nullptr);
    disconnect_selection_context();
}

void SceneOutliner::set_data_source(ISceneOutlinerDataSource* source)
{
    if (source && source->thread() != thread())
    {
        OPENDCC_ERROR("The data source must belong to the widget thread.");
        return;
    }
    if (source == m_model->data_source())
    {
        m_model->set_data_source(source);
        return;
    }
    const QPointer<SceneOutliner> self(this);
    invalidate_search();
    m_model->set_data_source(source);
    if (!self)
        return;
    apply_selection_from_context();
}

void SceneOutliner::connect_model()
{
    const auto model = m_model;
    connect(m_model, &QObject::destroyed, this, [this, model]() {
        if (model == m_model)
            reset_model();
    });
    connect(m_model, &SceneOutlinerModel::source_about_to_change, this, [this, model](bool affects_search) {
        if (model != m_model)
            return;
        if (affects_search)
            cancel_search();
    });
    connect(m_model, &SceneOutlinerModel::source_changed, this, [this, model](bool affects_search) {
        if (model != m_model)
            return;
        const QPointer<SceneOutliner> self(this);
        // Provider callbacks may destroy the widget; Qt's reset must have returned first.
        if (m_filters_dirty)
            refresh_filters();
        if (!self || model != m_model)
            return;
        if (affects_search && (m_search_pending || !m_pending_filter.isEmpty()))
            on_filter_timeout();
        else
            apply_selection_from_context(true);
    });
}

void SceneOutliner::reset_model()
{
    const QPointer<SceneOutliner> self(this);
    const auto model = m_model;
    cancel_search();
    if (!self || model != m_model)
        return;
    {
        SelectionSyncGuard syncing(this, m_syncing_selection);
        m_model = new SceneOutlinerModel(this);
        connect_model();
        m_proxy->setSourceModel(m_model);
        if (!self)
            return;
        m_view->set_models(m_proxy, m_model);
        if (!self)
            return;
        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SceneOutliner::on_view_selection_changed,
                Qt::UniqueConnection);
        refresh_filters();
    }
    if (!self)
        return;
    on_filter_timeout();
    if (self)
        Q_EMIT model_changed();
}

void SceneOutliner::set_selection_context(ISceneOutlinerSelection* selection)
{
    if (selection && selection->thread() != thread())
    {
        OPENDCC_ERROR("The selection adapter must belong to the widget thread.");
        return;
    }
    if (selection && selection == m_selection)
        return;
    const QPointer<SceneOutliner> self(this);
    disconnect_selection_context();
    m_selection = selection;
    connect_selection_context();
    apply_selection_from_context();
    if (self)
        Q_EMIT selection_context_changed();
}

void SceneOutliner::connect_selection_context()
{
    m_selection_context = new QObject(this);
    if (!m_selection)
        return;
    connect(m_selection, &ISceneOutlinerSelection::changed, m_selection_context, [this]() { apply_selection_from_context(true); });
    connect(m_selection, &QObject::destroyed, m_selection_context, [this]() { set_selection_context(nullptr); });
}

void SceneOutliner::disconnect_selection_context()
{
    delete m_selection_context.data();
    m_selection_context = nullptr;
}

void SceneOutliner::toggle_search(bool on)
{
    m_filter->setVisible(on);
    if (on)
        m_filter->setFocus();
    else
        m_filter->clear(); // clearing resets the filter via textChanged
}

void SceneOutliner::on_filter_text_changed(const QString& text)
{
    const QPointer<SceneOutliner> self(this);
    m_pending_filter = text;
    const auto operation = cancel_search();
    if (!self || operation.is_cancelled())
        return;
    m_filter_timer->start();
}

void SceneOutliner::on_filter_timeout()
{
    const QPointer<SceneOutliner> self(this);
    m_filter_timer->stop();
    {
        SelectionSyncGuard syncing(this, m_syncing_selection);
        update_search_filter();
    }
    if (self)
        apply_selection_from_context();
}

scene_outliner::CancellationToken SceneOutliner::invalidate_search()
{
    return m_search_token.restart();
}

scene_outliner::CancellationToken SceneOutliner::cancel_search()
{
    const auto operation = invalidate_search();
    if (!m_search_request)
        return operation;
    const auto request = m_search_request;
    m_search_request = nullptr;
    disconnect(request, nullptr, this, nullptr);
    request->cancel();
    if (request)
        request->deleteLater();
    return operation;
}

void SceneOutliner::update_search_filter()
{
    const QPointer<SceneOutliner> self(this);
    const auto operation = cancel_search();
    if (!self || operation.is_cancelled())
        return;
    if (m_pending_filter.isEmpty() || !m_model->data_source())
    {
        apply_search_results({});
        return;
    }

    m_search_pending = true;
    auto* request = new SceneOutlinerSearchRequest(this);
    m_search_request = request;
    connect(request, &SceneOutlinerSearchRequest::finished, this, [this, operation, request](const SceneItemList& matches) {
        if (operation.is_cancelled())
            return;
        m_search_request = nullptr;
        request->deleteLater();
        apply_search_results(matches);
    });
    connect(request, &SceneOutlinerSearchRequest::cancelled, this, [this, operation]() {
        if (!operation.is_cancelled())
            m_search_pending = false;
    });
    m_model->data_source()->start_search(m_pending_filter.toStdString(), request);
}

struct SceneOutliner::SearchResults
{
    scene_outliner::CancellationToken operation;
    bool active = false;
    SceneItemList matches;
    size_t next_match = 0;
    SceneItemSet accept;
    SceneItemSet expand_seen;
    SceneItemList to_expand;
    SceneItemList parents;
    size_t next_parent = 0;
    QModelIndex parent_index;
    int next_row = 0;
    int row_count = 0;
    bool preparing = false;
    SceneOutlinerProxyModel::SearchRows rows;
    scene_outliner::CancellationToken filtering;
};

void SceneOutliner::apply_search_results(SceneItemList matches)
{
    auto results = std::make_shared<SearchResults>();
    results->operation = m_search_token;
    results->active = !m_pending_filter.isEmpty();
    results->matches = std::move(matches);
    results->accept.reserve(results->matches.size());
    m_search_pending = true;
    continue_search_results(results);
}

void SceneOutliner::continue_search_results(const std::shared_ptr<SearchResults>& results)
{
    if (results->operation.is_cancelled())
        return;
    QElapsedTimer timer;
    timer.start();
    if (collect_search_matches(results, timer) && prepare_search_rows(results, timer))
        finish_search_results(results);
}

bool SceneOutliner::defer_search_results(const std::shared_ptr<SearchResults>& results, const QElapsedTimer& timer)
{
    if (timer.elapsed() < 4)
        return false;
    QTimer::singleShot(0, this, [this, results]() { continue_search_results(results); });
    return true;
}

bool SceneOutliner::collect_search_matches(const std::shared_ptr<SearchResults>& results, const QElapsedTimer& timer)
{
    const SceneOutlinerProxyModel::SearchCheck is_current = [self = QPointer<SceneOutliner>(this), operation = results->operation]() {
        return self && !operation.is_cancelled();
    };
    QPointer<ISceneOutlinerDataSource> source = m_model->data_source();
    // Keep backend queries and cold row allocation out of a single long GUI turn.
    while (source && results->next_match < results->matches.size())
    {
        const auto& match = results->matches[results->next_match++];
        const bool exists = source->has_item(match);
        if (!is_current())
            return false;
        if (exists)
        {
            results->accept.insert(match);
            auto cur = source->parent(match);
            if (!is_current())
                return false;
            while (cur)
            {
                results->accept.insert(cur);
                if (!results->expand_seen.insert(cur).second)
                    break;
                results->to_expand.push_back(cur);
                cur = source->parent(cur);
                if (!is_current())
                    return false;
            }
        }
        if (defer_search_results(results, timer))
            return false;
    }
    return is_current();
}

bool SceneOutliner::prepare_search_rows(const std::shared_ptr<SearchResults>& results, const QElapsedTimer& timer)
{
    const SceneOutlinerProxyModel::SearchCheck is_current = [self = QPointer<SceneOutliner>(this), operation = results->operation]() {
        return self && !operation.is_cancelled();
    };
    if (!results->preparing)
    {
        results->preparing = true;
        results->filtering = m_proxy->m_filter_token;
        SceneItemSet seen;
        const auto add_parent = [&](const SceneItemId& item) {
            if (item && seen.insert(item).second)
                results->parents.push_back(item);
        };
        add_parent(m_model->root_item());
        for (const auto& item : m_view->expanded_items())
            add_parent(item);
        for (const auto& item : results->to_expand)
            add_parent(item);
    }
    while (results->next_parent < results->parents.size())
    {
        if (!results->parent_index.isValid())
        {
            results->parent_index = m_model->ensure_index_for_item(results->parents[results->next_parent]);
            if (!is_current())
                return false;
            if (auto* proxy = m_model->proxy_from_index(results->parent_index))
                m_model->load_children(proxy);
            if (!is_current())
                return false;
            results->row_count = results->parent_index.isValid() ? m_model->rowCount(results->parent_index) : 0;
            if (!is_current())
                return false;
            results->next_row = 0;
        }
        while (results->next_row < results->row_count)
        {
            m_proxy->prepare_search_row(results->next_row++, results->parent_index, results->active, results->accept, is_current, results->rows);
            if (!is_current())
                return false;
            if (defer_search_results(results, timer))
                return false;
        }
        results->parent_index = {};
        ++results->next_parent;
        if (defer_search_results(results, timer))
            return false;
    }
    return is_current();
}

void SceneOutliner::finish_search_results(const std::shared_ptr<SearchResults>& results)
{
    const QPointer<SceneOutliner> self(this);
    if (results->filtering.is_cancelled())
        // Use live filtering if columns changed; continuous updates must not restart preparation indefinitely.
        results->rows.clear();
    {
        SelectionSyncGuard syncing(this, m_syncing_selection);
        m_proxy->apply_search_filter(results->active, std::move(results->accept), std::move(results->rows));
        if (!self)
            return;
        if (!results->operation.is_cancelled())
        {
            m_view->apply_root_index();
            if (!self)
                return;
            if (!results->operation.is_cancelled())
                m_view->expand_items(results->to_expand);
        }
    }
    if (!self)
        return;
    // A replacement search may finish synchronously while selection syncing is suppressed.
    apply_selection_from_context();
    if (self && !results->operation.is_cancelled())
        m_search_pending = false;
}

void SceneOutliner::update_ancestor_highlight()
{
    auto* selection_model = m_view->selectionModel();
    const auto selection = selection_model->selection();
    SceneItemList ancestors;
    QSet<QModelIndex> visited;
    const auto add_ancestors = [&](QModelIndex parent) {
        for (; parent.isValid() && !visited.contains(parent); parent = parent.parent())
        {
            visited.insert(parent);
            const SceneItemId item = m_model->item_for_index(m_proxy->mapToSource(parent));
            if (item)
                ancestors.push_back(item);
        }
    };

    // Native and Python clients can also select individual cells.
    const auto full_rows = std::all_of(selection.begin(), selection.end(), [this](const QItemSelectionRange& range) {
        return range.left() == 0 && range.right() == m_proxy->columnCount(range.parent()) - 1;
    });
    if (full_rows)
        for (const auto& range : selection)
            add_ancestors(range.parent());
    else
        for (const auto& row : selection_model->selectedRows(0))
            add_ancestors(row.parent());
    m_proxy->set_highlighted(ancestors);
}

void SceneOutliner::on_view_selection_changed()
{
    if (m_syncing_selection || !m_model || m_model->m_updating || m_proxy->m_filtering)
        return;
    const QPointer<SceneOutliner> self(this);
    update_ancestor_highlight();

    if (!self || !m_selection)
        return;

    {
        SelectionSyncGuard syncing(this, m_syncing_selection);
        m_selection->set_selection(selected_items());
    }
    if (self && m_selection_sync_pending)
        apply_selection_from_context();
}

void SceneOutliner::apply_selection_from_context(bool wait_for_rows)
{
    if (m_syncing_selection)
    {
        m_selection_sync_pending = true;
        return;
    }
    const QPointer<SceneOutliner> self(this);
    do
    {
        m_selection_sync_pending = false;
        if (!m_selection)
        {
            update_ancestor_highlight();
            return;
        }
        SelectionSyncGuard syncing(this, m_syncing_selection);
        const auto context = m_selection_context;
        const auto search = m_search_token;
        const auto is_current = [&]() {
            return self && context == m_selection_context && !search.is_cancelled();
        };
        auto items = m_selection->selected_items();
        if (!self)
            return;
        if (!is_current())
        {
            m_selection_sync_pending = true;
            continue;
        }
        QList<QPersistentModelIndex> indexes;
        indexes.reserve(items.size());
        bool awaiting_rows = false;
        auto* pending = wait_for_rows && m_view->selectionModel()->hasSelection() ? &awaiting_rows : nullptr;
        for (const SceneItemId& item : items)
        {
            // Materialize selected items without changing the user's expansion state.
            const QModelIndex source = m_model->materialize_index(item, 0, pending);
            if (!is_current() || awaiting_rows)
                break;
            if (!source.isValid())
                continue;
            const QModelIndex proxy_index = m_proxy->mapFromSource(source);
            if (!is_current())
                break;
            if (proxy_index.isValid())
                indexes.append(proxy_index);
        }
        if (!self)
            return;
        if (!is_current())
        {
            m_selection_sync_pending = true;
            continue;
        }
        if (awaiting_rows)
            continue;
        const auto selection = row_selection(indexes, *m_proxy);
        m_view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        if (!self)
            return;
        update_ancestor_highlight();
    } while (self && m_selection_sync_pending);
}

OPENDCC_NAMESPACE_CLOSE
