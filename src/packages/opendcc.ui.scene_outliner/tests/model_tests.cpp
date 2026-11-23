/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_test_utils.h"
#include "opendcc/ui/scene_outliner/outliner_model.h"

#include <QMimeData>
#include <QPersistentModelIndex>

#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

#include <algorithm>

OPENDCC_NAMESPACE_OPEN

using namespace scene_outliner_tests;

DOCTEST_TEST_CASE("outliner discards children returned after a source reset or replacement" * doctest::test_suite("SceneOutlinerModel"))
{
    class Source : public TestSource
    {
    public:
        mutable std::function<void()> during_children;
        SceneItemList children(const SceneItemId& item) const override
        {
            auto children = TestSource::children(item);
            if (item == id("/A") && during_children)
            {
                auto callback = std::move(during_children);
                callback();
            }
            return children;
        }
    };
    for (const bool replace : { false, true })
        for (int query = 0; query < 3; ++query)
        {
            DOCTEST_CAPTURE(replace);
            DOCTEST_CAPTURE(query);
            auto source = std::make_unique<Source>();
            const QPointer<Source> weak = source.get();
            TestSource fresh;
            fresh.tree["/A"].clear();
            fresh.tree.erase("/A/leaf");
            SceneOutlinerModel model;
            model.set_data_source(source.get());
            const auto a = model.ensure_index_for_item(id("/A"));
            DOCTEST_REQUIRE(a.isValid());
            QPersistentModelIndex persistent(a);
            source->during_children = [&]() {
                if (replace)
                    model.set_data_source(&fresh);
                else
                {
                    auto* retained = weak.data();
                    retained->tree = fresh.tree;
                    retained->notify(SceneOutlinerChange::reset());
                }
                DOCTEST_CHECK(weak);
            };
            if (query == 0)
                DOCTEST_CHECK(model.rowCount(a) == 0);
            else if (query == 1)
                DOCTEST_CHECK_FALSE(model.index(0, 0, a).isValid());
            else
                DOCTEST_CHECK_FALSE(model.ensure_index_for_item(id("/A/leaf")).isValid());
            DOCTEST_CHECK_FALSE(persistent.isValid());
            DOCTEST_CHECK(weak);
            const auto current = model.ensure_index_for_item(id("/A"));
            DOCTEST_REQUIRE(current.isValid());
            DOCTEST_CHECK(model.rowCount(current) == 0);
            DOCTEST_CHECK_FALSE(model.ensure_index_for_item(id("/A/leaf")).isValid());
        }
}

DOCTEST_TEST_CASE("outliner incrementally remaps persistent indices" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    const auto root = model.index(0, 0);
    DOCTEST_CHECK(model.rowCount(root) == 2);
    QPersistentModelIndex a(model.index(0, 0, root));
    QPersistentModelIndex b(model.index(1, 0, root));
    QPersistentModelIndex leaf(model.index(0, 0, a));
    source.tree["/"] = { "/B", "/A", "/C" };
    source.tree["/C"] = {};
    source.notify(SceneOutlinerChange::added({ id("/") }));
    DOCTEST_CHECK(model.rowCount(root) == 3);
    DOCTEST_CHECK(a.row() == 1);
    DOCTEST_CHECK(b.row() == 0);
    DOCTEST_CHECK(leaf.isValid());
    DOCTEST_CHECK(leaf.parent() == a);
    source.tree["/A"].clear();
    source.tree.erase("/A/leaf");
    source.notify(SceneOutlinerChange::removed({ id("/A") }));
    DOCTEST_CHECK_FALSE(leaf.isValid());
    DOCTEST_CHECK(a.isValid());
    DOCTEST_CHECK_FALSE(model.setData(a, QStringLiteral("rejected"), Qt::EditRole));
    source.notify(SceneOutlinerChange::reset());
    DOCTEST_CHECK_FALSE(a.isValid());
}

DOCTEST_TEST_CASE("row insertions and removals preserve sibling and descendant indices" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    const auto root = model.index(0, 0);
    QPersistentModelIndex a(model.ensure_index_for_item(id("/A")));
    QPersistentModelIndex b(model.ensure_index_for_item(id("/B"), 1));
    QPersistentModelIndex leaf(model.ensure_index_for_item(id("/A/leaf")));
    int inserted = 0, removed = 0, layouts = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsAboutToBeInserted, [&](const QModelIndex& parent, int first, int last) {
        DOCTEST_CHECK(parent == root);
        DOCTEST_CHECK(first == 0);
        DOCTEST_CHECK(last == 0);
        DOCTEST_CHECK(model.rowCount(root) == 2);
    });
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&]() { ++inserted; });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&]() { ++removed; });
    QObject::connect(&model, &QAbstractItemModel::layoutChanged, [&]() { ++layouts; });
    source.tree["/"].insert(source.tree["/"].begin(), "/New");
    source.tree["/New"] = {};
    source.notify(SceneOutlinerChange::added({ id("/") }));
    DOCTEST_CHECK(inserted == 1);
    DOCTEST_CHECK(a.row() == 1);
    DOCTEST_CHECK(b.row() == 2);
    DOCTEST_CHECK(b.column() == 1);
    DOCTEST_CHECK(leaf.parent() == a);
    QPersistentModelIndex added(model.ensure_index_for_item(id("/New")));
    source.tree["/"].erase(source.tree["/"].begin());
    source.tree.erase("/New");
    source.notify(SceneOutlinerChange::removed({ id("/") }));
    DOCTEST_CHECK(removed == 1);
    DOCTEST_CHECK(layouts == 0);
    DOCTEST_CHECK_FALSE(added.isValid());
    DOCTEST_CHECK(a.row() == 0);
    DOCTEST_CHECK(b.row() == 1);
    DOCTEST_CHECK(leaf.parent() == a);
}

DOCTEST_TEST_CASE("outliner drag payload captures items and rejects foreign or stale drags" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    SceneOutlinerModel other;
    model.set_data_source(&source);
    other.set_data_source(&source);
    const auto a = model.ensure_index_for_item(id("/A"));
    std::unique_ptr<QMimeData> mime(model.mimeData({ a }));
    DOCTEST_CHECK(model.items_for_mime_data(mime.get()) == SceneItemList { id("/A") });
    DOCTEST_CHECK(other.items_for_mime_data(mime.get()).empty());
    QMimeData transported;
    for (const auto& format : mime->formats())
        transported.setData(format, mime->data(format));
    DOCTEST_CHECK(model.items_for_mime_data(&transported) == SceneItemList { id("/A") });
    QMimeData foreign;
    foreign.setText(QStringLiteral("unrelated text"));
    foreign.setData(QStringLiteral("application/x-sdfpaths"), QByteArray("/A"));
    DOCTEST_CHECK(model.items_for_mime_data(&foreign).empty());
    source.tree.erase("/A");
    DOCTEST_CHECK(model.items_for_mime_data(mime.get()).empty());
    source.tree["/A"] = {};
    source.notify(SceneOutlinerChange::reset());
    DOCTEST_CHECK(model.items_for_mime_data(mime.get()).empty());
}

DOCTEST_TEST_CASE("layout listeners can create persistent indices during resync" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    QPersistentModelIndex index;
    DOCTEST_REQUIRE(model.ensure_index_for_item(id("/A")).row() == 0);
    QObject::connect(&model, &QAbstractItemModel::layoutAboutToBeChanged, [&]() { index = model.ensure_index_for_item(id("/A")); });
    source.tree["/"] = { "/B", "/A" };
    source.notify(SceneOutlinerChange::added({ id("/") }));
    DOCTEST_REQUIRE(index.isValid());
    DOCTEST_CHECK(index.row() == 1);
    DOCTEST_CHECK(model.item_for_index(index) == id("/A"));
}

DOCTEST_TEST_CASE("external traversal tolerates cycles and duplicate children" * doctest::test_suite("SceneOutlinerModel"))
{
    class CyclicSource : public TestSource
    {
    public:
        SceneItemId parent(const SceneItemId& item) const override
        {
            if (item == id("/A"))
                return id("/A/leaf");
            return TestSource::parent(item);
        }
    } source;
    source.tree["/"] = { "/B" };
    SceneOutlinerModel model;
    model.set_data_source(&source);
    DOCTEST_CHECK_FALSE(model.ensure_index_for_item(id("/A")).isValid());
    DOCTEST_CHECK_FALSE(model.ensure_index_for_item(id("/missing")).isValid());
    source.tree["/"] = { "/A", "/A" };
    source.tree["/A/leaf"] = { "/A" };
    auto matches = source.search("a");
    DOCTEST_CHECK(matches.size() == 2);
}

DOCTEST_TEST_CASE("removed item data is released after layout delivery" * doctest::test_suite("SceneOutlinerModel"))
{
    class TrackedId : public TestId
    {
    public:
        explicit TrackedId(int& alive)
            : TestId("/removed")
            , alive(alive)
        {
            ++alive;
        }
        ~TrackedId() override { --alive; }
        int& alive;
    };
    class Source : public TestSource
    {
    public:
        mutable int alive = 0;
        SceneItemList children(const SceneItemId& item) const override
        {
            SceneItemList out;

            if (item == id("/") && tree.count("/removed"))
                out.push_back(make_scene_item_id<TrackedId>(alive));

            return out;
        }
    } source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    bool removing = false;
    QPersistentModelIndex removed;
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&]() {
        if (removing)
        {
            DOCTEST_CHECK_FALSE(removed.isValid());
            DOCTEST_CHECK(source.alive > 0);
        }
    });
    for (int i = 0; i < 100; ++i)
    {
        source.tree["/removed"] = {};
        source.notify(SceneOutlinerChange::added({ id("/") }));
        removed = model.index(0, 0, model.index(0, 0));
        DOCTEST_REQUIRE(removed.isValid());
        source.tree.erase("/removed");
        removing = true;
        source.notify(SceneOutlinerChange::removed({ id("/") }));
        removing = false;
        DOCTEST_CHECK(source.alive == 0);
    }
}

DOCTEST_TEST_CASE("bulk display refresh preserves cached hierarchy and persistent indices" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    QPersistentModelIndex leaf(model.ensure_index_for_item(id("/A/leaf")));
    model.ensure_index_for_item(id("/B"));
    const auto queries = source.queries;
    int changes = 0, resets = 0, layouts = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&](const QModelIndex& first, const QModelIndex& last) {
        DOCTEST_CHECK(first.parent() == last.parent());
        DOCTEST_CHECK(first.column() == 0);
        DOCTEST_CHECK(last.column() == model.columnCount() - 1);
        ++changes;
    });
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&]() { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::layoutChanged, [&]() { ++layouts; });
    source.notify(SceneOutlinerChange::changed({}));
    DOCTEST_CHECK(changes == 3);
    DOCTEST_CHECK(resets == 0);
    DOCTEST_CHECK(layouts == 0);
    DOCTEST_CHECK(leaf.isValid());
    DOCTEST_CHECK(source.queries == queries);
}

DOCTEST_TEST_CASE("deleting a QObject source resets every borrowing model" * doctest::test_suite("SceneOutlinerModel"))
{
    QObject owner;
    auto* source = new TestSource();
    source->setParent(&owner);
    SceneOutlinerModel first, second;
    first.set_data_source(source);
    second.set_data_source(source);
    QPersistentModelIndex index(first.ensure_index_for_item(id("/A/leaf")));
    DOCTEST_REQUIRE(index.isValid());
    delete source;
    DOCTEST_CHECK(first.data_source() == nullptr);
    DOCTEST_CHECK(second.data_source() == nullptr);
    DOCTEST_CHECK_FALSE(index.isValid());
    DOCTEST_CHECK(first.rowCount() == 0);
    DOCTEST_CHECK(second.columnCount() == 0);
}

DOCTEST_TEST_CASE("only column zero can parent outliner rows" * doctest::test_suite("SceneOutlinerModel"))
{
    TestSource source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    const auto parent = model.ensure_index_for_item(id("/A"), 1);
    DOCTEST_REQUIRE(parent.isValid());
    DOCTEST_CHECK(model.rowCount(parent) == 0);
    DOCTEST_CHECK_FALSE(model.hasChildren(parent));
    DOCTEST_CHECK_FALSE(model.index(0, 0, parent).isValid());
    const auto hierarchy_parent = parent.sibling(parent.row(), 0);
    DOCTEST_CHECK(model.index(0, 0, hierarchy_parent).isValid());
}

DOCTEST_TEST_CASE("multiple roots remain distinct through lazy updates" * doctest::test_suite("SceneOutlinerModel"))
{
    class Forest : public TestSource
    {
    public:
        SceneItemList root_items { id("/A"), id("/B") };
        SceneItemList roots() const override { return root_items; }
        SceneItemId parent(const SceneItemId& item) const override
        {
            const auto parent = TestSource::parent(item);
            return parent == id("/") ? SceneItemId() : parent;
        }
    } source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    DOCTEST_CHECK(model.rowCount() == 2);
    DOCTEST_CHECK_FALSE(model.root_item());
    const auto a = model.index(0, 0);
    const QPersistentModelIndex b(model.index(1, 0));
    DOCTEST_CHECK(model.item_for_index(a) == id("/A"));
    DOCTEST_CHECK(model.item_for_index(b) == id("/B"));
    DOCTEST_CHECK_FALSE(a.parent().isValid());
    DOCTEST_CHECK_FALSE(b.parent().isValid());
    DOCTEST_CHECK(model.ensure_index_for_item(id("/A/leaf")).parent() == a);
    source.tree["/B"].push_back("/B/leaf");
    source.tree["/B/leaf"] = {};
    source.notify(SceneOutlinerChange::added({ id("/B") }));
    DOCTEST_CHECK(model.ensure_index_for_item(id("/B/leaf")).parent() == b);
    source.tree["/C"] = {};
    source.root_items.push_back(id("/C"));
    source.notify(SceneOutlinerChange::added({ SceneItemId() }));
    DOCTEST_CHECK(model.rowCount() == 3);
    DOCTEST_CHECK(model.index_for_item(id("/B")) == b);
    DOCTEST_CHECK(model.ensure_index_for_item(id("/C")).row() == 2);
    source.root_items.erase(source.root_items.begin());
    source.notify(SceneOutlinerChange::removed({ SceneItemId() }));
    DOCTEST_CHECK(model.rowCount() == 2);
    DOCTEST_CHECK(b.isValid());
    DOCTEST_CHECK(b.row() == 0);
    DOCTEST_CHECK_FALSE(model.index_for_item(id("/A")).isValid());
    DOCTEST_CHECK_FALSE(model.ensure_index_for_item(id("/A/leaf")).isValid());
    source.root_items.pop_back();
    source.tree.erase("/C");
    source.notify(SceneOutlinerChange::removed({ SceneItemId() }));
    DOCTEST_CHECK(model.rowCount() == 1);
    DOCTEST_CHECK(model.item_for_index(model.index(0, 0)) == id("/B"));
    source.root_items.clear();
    source.notify(SceneOutlinerChange::removed({ SceneItemId() }));
    DOCTEST_CHECK(model.rowCount() == 0);
    DOCTEST_CHECK_FALSE(model.hasChildren());
    source.notify(SceneOutlinerChange::reset());
    source.root_items = { id("/A") };
    source.notify(SceneOutlinerChange::added({ SceneItemId() }));
    DOCTEST_CHECK(model.rowCount() == 1);
    DOCTEST_CHECK(model.root_item() == id("/A"));
    source.root_items.push_back(id("/B"));
    source.notify(SceneOutlinerChange::added({ SceneItemId() }));
    DOCTEST_CHECK(model.rowCount() == 2);
    DOCTEST_CHECK_FALSE(model.root_item());
}

DOCTEST_TEST_CASE("export capability and toggle enablement are independent of reparenting" * doctest::test_suite("SceneOutlinerModel"))
{
    class Source : public TestSource
    {
    public:
        bool enabled = false;
        int toggles = 0;
        QString drag_mime_type() const override { return QStringLiteral("application/x-export-only"); }
        QByteArray drag_mime_data(const SceneItemList&) const override { return QByteArray("exported"); }
        SceneOutlinerColumnList columns() const override
        {
            auto result = TestSource::columns();
            SceneOutlinerColumn toggle;
            toggle.id = "toggle";
            toggle.role = SceneOutlinerColumn::Role::TOGGLE;
            result.push_back(toggle);
            return result;
        }
        bool toggle_enabled(const SceneItemId&, const std::string&) const override { return enabled; }
        void toggle_column(const SceneItemId&, const std::string&) override { ++toggles; }
    } source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    const auto index = model.ensure_index_for_item(id("/A"));
    DOCTEST_CHECK(bool(model.flags(index) & Qt::ItemIsDragEnabled));
    DOCTEST_CHECK_FALSE(bool(model.flags(index) & Qt::ItemIsDropEnabled));
    const std::unique_ptr<QMimeData> mime(model.mimeData({ index }));
    DOCTEST_REQUIRE(mime);
    DOCTEST_CHECK(mime->data(QStringLiteral("application/x-export-only")) == QByteArray("exported"));
    const auto toggle = index.sibling(index.row(), 2);
    model.toggle_column(toggle);
    DOCTEST_CHECK(source.toggles == 0);
    source.enabled = true;
    model.toggle_column(toggle);
    DOCTEST_CHECK(source.toggles == 1);
}

DOCTEST_TEST_CASE("persistent items survive moves and removal of their former parent or root" * doctest::test_suite("SceneOutlinerModel"))
{
    class Source : public TestSource
    {
    public:
        bool forest = false;
        SceneItemList root_items { id("/A"), id("/B") };
        SceneItemList roots() const override { return forest ? root_items : TestSource::roots(); }
        SceneItemId parent(const SceneItemId& item) const override
        {
            if (forest && (item == id("/A") || item == id("/B")))
                return {};
            const auto path = scene_item_data_cast<TestId>(item)->path;
            for (const auto& entry : tree)
                if (std::find(entry.second.begin(), entry.second.end(), path) != entry.second.end())
                    return id(entry.first);
            return {};
        }
    };
    for (const auto removal : { "none", "delete", "detach_root" })
    {
        DOCTEST_CAPTURE(removal);
        Source source;
        source.forest = std::string(removal) == "detach_root";
        source.tree["/A"].push_back("/A/orphan");
        source.tree["/A/orphan"] = {};
        SceneOutlinerModel model;
        model.set_data_source(&source);
        QPersistentModelIndex leaf(model.ensure_index_for_item(id("/A/leaf")));
        QPersistentModelIndex orphan(model.ensure_index_for_item(id("/A/orphan")));
        QPersistentModelIndex destination(model.ensure_index_for_item(id("/B")));
        source.tree["/A"] = { "/A/orphan" };
        source.tree["/B"] = { "/B/first", "/A/leaf" };
        source.tree["/B/first"] = {};
        SceneItemList parents { id("/A"), id("/B") };
        if (std::string(removal) == "delete")
        {
            source.tree["/"] = { "/B" };
            source.tree.erase("/A");
            source.tree.erase("/A/orphan");
            parents = { id("/"), id("/B") };
        }
        else if (source.forest)
        {
            source.root_items = { id("/B") };
            parents = { SceneItemId(), id("/B") };
        }
        source.notify(SceneOutlinerChange::added(parents));
        DOCTEST_REQUIRE(leaf.isValid());
        DOCTEST_CHECK(leaf.parent() == destination);
        DOCTEST_CHECK(leaf.row() == 1);
        DOCTEST_CHECK(model.item_for_index(leaf) == id("/A/leaf"));
        DOCTEST_CHECK(model.ensure_index_for_item(id("/A/leaf")) == leaf);
        DOCTEST_CHECK(model.rowCount(destination) == 2);
        DOCTEST_CHECK(model.index(1, 0, destination) == leaf);
        if (std::string(removal) != "none")
        {
            DOCTEST_CHECK_FALSE(orphan.isValid());
            DOCTEST_CHECK_FALSE(model.index_for_item(id("/A")).isValid());
            DOCTEST_CHECK_FALSE(model.ensure_index_for_item(id("/A/orphan")).isValid());
        }
    }
}

DOCTEST_TEST_CASE("materialized deep trees reconcile and remove descendants without recursive traversal" * doctest::test_suite("SceneOutlinerModel"))
{
    class Source : public TestSource
    {
    public:
        int depth = 2048;
        SceneItemList roots() const override { return { id("0") }; }
        SceneItemList children(const SceneItemId& item) const override
        {
            const auto number = std::stoi(scene_item_data_cast<TestId>(item)->path);
            return number < depth ? SceneItemList { id(std::to_string(number + 1)) } : SceneItemList();
        }
        SceneItemId parent(const SceneItemId& item) const override
        {
            const auto number = std::stoi(scene_item_data_cast<TestId>(item)->path);
            return number ? id(std::to_string(number - 1)) : SceneItemId();
        }
        bool has_item(const SceneItemId& item) const override
        {
            const auto* data = scene_item_data_cast<TestId>(item);
            return data && std::stoi(data->path) <= depth;
        }
    } source;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    const auto last = id(std::to_string(source.depth));
    QPersistentModelIndex leaf(model.ensure_index_for_item(last));
    DOCTEST_REQUIRE(leaf.isValid());
    source.notify(SceneOutlinerChange::added({ id("0") }));
    DOCTEST_CHECK(model.index_for_item(last) == leaf);
    DOCTEST_CHECK(model.item_for_index(leaf.parent()) == id(std::to_string(source.depth - 1)));
    source.depth = 0;
    source.notify(SceneOutlinerChange::removed({ id("0") }));
    DOCTEST_CHECK_FALSE(leaf.isValid());
    DOCTEST_CHECK_FALSE(model.index_for_item(last).isValid());
    DOCTEST_CHECK(model.rowCount(model.index(0, 0)) == 0);
}

OPENDCC_NAMESPACE_CLOSE
