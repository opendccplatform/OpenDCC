/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/ui/scene_outliner/outliner_widget.h"
#include "opendcc/ui/scene_outliner/outliner_model.h"
#include "opendcc/ui/scene_outliner/outliner_tree_view.h"
#include "opendcc/ui/scene_outliner/tree_data_source.h"
#include "model_test_utils.h"

#include <QApplication>
#include <QLineEdit>
#include <thread>
#include <functional>
#include <new>
#include <utility>

#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

namespace
{
    class WidgetItemData : public SceneItemId::Data
    {
    public:
        explicit WidgetItemData(std::string name)
            : name(std::move(name))
        {
        }
        std::string type_id() const override { return "widget_test"; }
        size_t hash() const override { return std::hash<std::string>()(name); }
        bool equals(const Data& other) const override { return name == static_cast<const WidgetItemData&>(other).name; }
        std::string name;
    };

    class DeferredWidgetSource : public ISceneOutlinerDataSource
    {
    public:
        SceneItemId root = make_scene_item_id<WidgetItemData>("root");
        SceneItemId leaf = make_scene_item_id<WidgetItemData>("leaf");
        mutable std::vector<SceneOutlinerSearchCompletion> requests;
        mutable std::vector<QPointer<SceneOutlinerSearchRequest>> search_requests;

        SceneItemList roots() const override
        {
            SceneItemList out;
            out.push_back(root);
            return out;
        }
        SceneItemList children(const SceneItemId& item) const override
        {
            SceneItemList out;

            if (item == root)
                out.push_back(leaf);

            return out;
        }
        SceneItemId parent(const SceneItemId& item) const override { return item == leaf ? root : SceneItemId(); }
        bool has_item(const SceneItemId& item) const override { return item == root || item == leaf; }
        std::string display_name(const SceneItemId& item) const override { return item == root ? "root" : "leaf"; }
        SceneOutlinerColumnList columns() const override
        {
            SceneOutlinerColumnList out;

            SceneOutlinerColumn column;
            column.id = "name";
            column.role = SceneOutlinerColumn::Role::TREE;
            out.push_back(column);

            return out;
        }
        void search_async(const std::string&, SceneOutlinerSearchRequest* request) const override
        {
            requests.push_back(request->completion());
            search_requests.push_back(request);
        }
    };

}

DOCTEST_TEST_CASE("rebinding the same borrowed source preserves the pending search" * doctest::test_suite("SceneOutlinerWidget"))
{
    int argc = 1;
    char name[] = "outliner-widget-tests";
    char* argv[] = { name, nullptr };
    QApplication app(argc, argv);
    DeferredWidgetSource source;
    SceneOutliner widget(&source);
    auto* filter = widget.findChild<QLineEdit*>();
    DOCTEST_REQUIRE(filter);
    filter->setText(QStringLiteral("leaf"));
    DOCTEST_REQUIRE(QMetaObject::invokeMethod(&widget, "on_filter_timeout", Qt::DirectConnection));
    DOCTEST_REQUIRE(widget.search_pending());
    DOCTEST_REQUIRE(source.requests.size() == 1);
    int resets = 0;
    QObject::connect(widget.model(), &QAbstractItemModel::modelReset, [&]() { ++resets; });

    widget.set_data_source(&source);
    DOCTEST_CHECK(resets == 0);
    DOCTEST_CHECK(source.requests.size() == 1);
    source.requests.front().complete({ source.leaf });
    QCoreApplication::processEvents();
    DOCTEST_CHECK_FALSE(widget.search_pending());
    DOCTEST_CHECK(widget.model()->index_for_item(source.leaf).isValid());
}

DOCTEST_TEST_CASE("cancellation handlers may delete their request while another view searches" * doctest::test_suite("SceneOutlinerWidget"))
{
    int argc = 1;
    char name[] = "outliner-widget-tests";
    char* argv[] = { name, nullptr };
    QApplication app(argc, argv);
    for (const bool close : { false, true })
    {
        DeferredWidgetSource source;
        auto first = std::make_unique<SceneOutliner>(&source);
        SceneOutliner second(&source);
        for (auto* widget : { first.get(), &second })
        {
            widget->findChild<QLineEdit*>()->setText(QStringLiteral("leaf"));
            DOCTEST_REQUIRE(QMetaObject::invokeMethod(widget, "on_filter_timeout", Qt::DirectConnection));
        }
        DOCTEST_REQUIRE(source.requests.size() == 2);
        auto* request = source.search_requests.front().data();
        QObject::connect(request, &SceneOutlinerSearchRequest::cancelled, [request]() { delete request; });
        std::thread worker([first_result = source.requests[0], second_result = source.requests[1], leaf = source.leaf]() {
            first_result.complete({ leaf });
            second_result.complete({ leaf });
        });
        worker.join();
        if (close)
            first.reset();
        else
            first->set_data_source(nullptr);
        DOCTEST_CHECK(source.requests.front().is_cancelled());
        DOCTEST_CHECK_FALSE(source.search_requests.front());
        DOCTEST_CHECK_FALSE(source.requests[1].is_cancelled());
        QCoreApplication::processEvents();
        DOCTEST_CHECK_FALSE(second.search_pending());
        DOCTEST_CHECK(second.data_source() == &source);
        DOCTEST_CHECK(second.model()->ensure_index_for_item(source.leaf).isValid());
    }
}

DOCTEST_TEST_CASE("filter refresh stops when selection replaces its widget at the same address" * doctest::test_suite("SceneOutlinerWidget"))
{
    int argc = 1;
    char name[] = "outliner-widget-tests";
    char* argv[] = { name, nullptr };
    QApplication app(argc, argv);
    class Source : public DeferredWidgetSource
    {
    public:
        mutable std::function<void()> on_filters;
        std::vector<Filter> filters() const override
        {
            if (auto callback = std::exchange(on_filters, {}))
                callback();
            return {};
        }
    } source;
    class Selection : public ISceneOutlinerSelection
    {
    public:
        mutable std::function<void()> on_selection;
        SceneItemList selected_items() const override
        {
            if (auto callback = std::exchange(on_selection, {}))
                callback();
            return {};
        }
        void set_selection(const SceneItemList&) override {}
        void add_to_selection(const SceneItemList&) override {}
        void remove_from_selection(const SceneItemList&) override {}
    } selection;
    auto widget = std::make_unique<SceneOutliner>(&source, &selection);
    const QPointer<SceneOutliner> original(widget.get());
    int emissions = 0;
    source.on_filters = [&]() {
        selection.on_selection = [&]() {
            // Reuse storage deterministically so a signal on the deleted sender is observable.
            widget->~SceneOutliner();
            new (widget.get()) SceneOutliner();
            QObject::connect(widget.get(), &SceneOutliner::filters_changed, [&]() { ++emissions; });
        };
    };
    source.notify(SceneOutlinerChange::reset());
    DOCTEST_CHECK_FALSE(original);
    DOCTEST_CHECK(emissions == 0);
}

DOCTEST_TEST_CASE("branch traversal stops at a match or a source reset" * doctest::test_suite("SceneOutlinerWidget"))
{
    int argc = 1;
    char name[] = "outliner-widget-tests";
    char* argv[] = { name, nullptr };
    QApplication app(argc, argv);
    using namespace scene_outliner_tests;
    TestSource source;
    SceneOutliner widget(&source);
    auto* view = widget.tree_view();
    const auto root = id("/");
    const auto matches = [](const SceneItemId& item) {
        return item == id("/A") || item == id("/A/leaf");
    };

    view->set_expanded_items({});
    view->expand_branch_to(root, matches);
    DOCTEST_CHECK(view->expanded_items() == SceneItemList { root });
    view->expand_branch(root);
    view->collapse_branch_to(root, matches);
    DOCTEST_CHECK(view->expanded_items() == SceneItemList { root });

    for (const auto operation : { &SceneOutlinerTreeView::expand_branch_to, &SceneOutlinerTreeView::collapse_branch_to })
    {
        int calls = 0;
        (view->*operation)(root, [&](const SceneItemId&) {
            ++calls;
            source.notify(SceneOutlinerChange::reset());
            return false;
        });
        DOCTEST_CHECK(calls == 1);
    }
}

OPENDCC_NAMESPACE_CLOSE
