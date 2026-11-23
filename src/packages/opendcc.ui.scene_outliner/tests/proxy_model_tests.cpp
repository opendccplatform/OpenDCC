/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_test_utils.h"
#include "opendcc/ui/scene_outliner/outliner_model.h"
#include "opendcc/ui/scene_outliner/outliner_proxy_model.h"

#include <QPersistentModelIndex>

#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

using namespace scene_outliner_tests;

DOCTEST_TEST_CASE("outliner lazily queries wide trees and reveals search ancestors" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    source.tree["/"].clear();
    for (int i = 0; i < 64; ++i)
    {
        const auto path = "/item" + std::to_string(i);
        source.tree["/"].push_back(path);
        source.tree[path] = {};
    }
    source.tree["/"].push_back("/A");
    SceneOutlinerModel model;
    model.set_data_source(&source);
    DOCTEST_CHECK(source.queries.empty());
    DOCTEST_CHECK(model.rowCount(model.index(0, 0)) == 65);
    DOCTEST_CHECK(source.queries.size() == 1);
    const auto leaf = model.ensure_index_for_item(id("/A/leaf"));
    DOCTEST_REQUIRE(leaf.isValid());
    DOCTEST_CHECK(source.queries.size() == 2);
    SceneOutlinerProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.set_search_filter(true, { id("/"), id("/A"), id("/A/leaf") });
    DOCTEST_CHECK(proxy.rowCount() == 1);
    const auto root = proxy.index(0, 0);
    DOCTEST_CHECK(proxy.rowCount(root) == 1);
    DOCTEST_CHECK(proxy.rowCount(proxy.index(0, 0, root)) == 1);
    model.set_data_source(nullptr);
    DOCTEST_CHECK(model.rowCount() == 0);
}

DOCTEST_TEST_CASE("filtered subtree changes report their parent and skip unrelated backend queries" * doctest::test_suite("SceneOutlinerModel"))
{
    class Source : public TestSource
    {
    public:
        mutable SceneItemSet filtered;
        bool matches_filter(const SceneItemId& item, const std::string&) const override
        {
            filtered.insert(item);
            return false;
        }
    } source;
    for (int i = 0; i < 64; ++i)
    {
        const auto path = "/unrelated" + std::to_string(i);
        source.tree["/"].push_back(path);
        source.tree[path] = {};
    }
    source.tree["/A"].push_back("/A/second");
    source.tree["/A/second"] = {};
    SceneOutlinerModel model;
    model.set_data_source(&source);
    const auto a = model.ensure_index_for_item(id("/A"));
    QPersistentModelIndex survivor(model.ensure_index_for_item(id("/A/leaf")));
    SceneOutlinerProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.set_hidden_filters({ QStringLiteral("hidden") });
    proxy.set_search_filter(true, { id("/"), id("/A"), id("/A/leaf") });
    source.filtered.clear();
    proxy.invalidate();
    DOCTEST_REQUIRE(proxy.rowCount(proxy.index(0, 0)) == 1);
    DOCTEST_CHECK_FALSE(source.filtered.count(id("/B")));
    DOCTEST_CHECK_FALSE(source.filtered.count(id("/unrelated63")));
    int layouts = 0;
    QObject::connect(&model, &QAbstractItemModel::layoutChanged,
                     [&](const QList<QPersistentModelIndex>& parents, QAbstractItemModel::LayoutChangeHint) {
                         ++layouts;
                         DOCTEST_REQUIRE(parents.size() == 1);
                         DOCTEST_CHECK(parents.front() == a);
                     });
    source.tree["/A"] = { "/A/second", "/A/leaf" };
    source.notify(SceneOutlinerChange::added({ id("/A"), id("/A") }));
    DOCTEST_CHECK(layouts == 1);
    DOCTEST_CHECK(survivor.isValid());
    DOCTEST_CHECK(survivor.row() == 1);
    DOCTEST_CHECK_FALSE(source.filtered.count(id("/unrelated63")));
    source.tree["/A"] = { "/A/leaf", "/A/second" };
    source.notify(SceneOutlinerChange::removed({ id("/A") }));
    DOCTEST_CHECK(layouts == 2);
    DOCTEST_CHECK(survivor.isValid());
}

DOCTEST_TEST_CASE("search acceptance deltas refilter only changed rows" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    SceneOutlinerProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.set_search_filter(true, { id("/"), id("/A"), id("/A/leaf") });
    const auto root = proxy.index(0, 0);
    DOCTEST_REQUIRE(proxy.rowCount(root) == 1);
    int updates = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&](const QModelIndex& first, const QModelIndex& last, const auto& roles) {
        ++updates;
        DOCTEST_CHECK(first == last);
        DOCTEST_CHECK(roles.contains(SceneOutlinerModel::SEARCH_FILTER));
    });
    proxy.set_search_filter(true, { id("/"), id("/A"), id("/A/leaf"), id("/B") });
    DOCTEST_CHECK(proxy.rowCount(root) == 2);
    DOCTEST_CHECK(updates == 1);
    proxy.set_search_filter(true, { id("/"), id("/A"), id("/A/leaf") });
    DOCTEST_CHECK(proxy.rowCount(root) == 1);
    DOCTEST_CHECK(updates == 2);
    proxy.set_search_filter(true, { id("/"), id("/A"), id("/A/leaf") });
    DOCTEST_CHECK(updates == 2);
}

DOCTEST_TEST_CASE("category filters hide descendant rows through their parent" * doctest::test_suite("SceneOutlinerModel"))
{
    class Source : public TestSource
    {
    public:
        bool blocked = true;
        bool matches_filter(const SceneItemId& item, const std::string&) const override { return blocked && item == id("/A"); }
    } source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    const auto leaf = model.ensure_index_for_item(id("/A/leaf"));
    SceneOutlinerProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.set_hidden_filters({ QStringLiteral("blocked") });
    DOCTEST_CHECK_FALSE(proxy.mapFromSource(leaf).isValid());
    source.blocked = false;
    source.notify(SceneOutlinerChange::changed({ id("/A") }, false));
    DOCTEST_CHECK(proxy.mapFromSource(leaf).isValid());
    source.blocked = true;
    source.notify(SceneOutlinerChange::changed({ id("/A") }, false));
    DOCTEST_CHECK_FALSE(proxy.mapFromSource(leaf).isValid());
}

DOCTEST_TEST_CASE("search filtering does not materialize rejected rows" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    SceneOutlinerProxyModel proxy;
    proxy.set_search_filter(true, { id("/"), id("/A"), id("/A/leaf") });
    proxy.setSourceModel(&model);
    const auto root = proxy.index(0, 0);
    DOCTEST_REQUIRE(proxy.rowCount(root) == 1);
    const auto a = proxy.index(0, 0, root);
    DOCTEST_CHECK(a.data().toString() == QStringLiteral("/A"));
    DOCTEST_CHECK_FALSE(model.index_for_item(id("/B")).isValid());
    QPersistentModelIndex leaf(model.ensure_index_for_item(id("/A/leaf")));
    proxy.set_search_filter(false, {});
    DOCTEST_CHECK(proxy.rowCount(root) == 2);
    DOCTEST_CHECK(leaf.isValid());
    DOCTEST_CHECK(proxy.mapFromSource(leaf).isValid());
    proxy.set_search_filter(true, { id("/"), id("/B") });
    DOCTEST_CHECK(proxy.rowCount(root) == 1);
    DOCTEST_CHECK_FALSE(proxy.mapFromSource(leaf).isValid());
    proxy.set_search_filter(false, {});
    DOCTEST_CHECK(proxy.rowCount(root) == 2);
    DOCTEST_CHECK(proxy.mapFromSource(leaf).isValid());
}

OPENDCC_NAMESPACE_CLOSE
