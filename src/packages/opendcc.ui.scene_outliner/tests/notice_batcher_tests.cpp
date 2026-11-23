/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "model_test_utils.h"
#include "opendcc/ui/scene_outliner/outliner_model.h"
#include "opendcc/ui/scene_outliner/notice_batcher.h"

#include <QCoreApplication>

#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

#include <thread>

OPENDCC_NAMESPACE_OPEN

using namespace scene_outliner_tests;

DOCTEST_TEST_CASE("outliner batches repeated notices and reset supersedes pending work" * doctest::test_suite("SceneOutlinerModel"))
{
    int argc = 1;
    char name[] = "outliner_test";
    char* argv[] = { name, nullptr };
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);
    SceneOutlinerBatcher batcher;
    int changes = 0;
    int resets = 0;
    QObject::connect(&batcher, &SceneOutlinerBatcher::items_changed, [&](const auto& items, bool) {
        ++changes;
        DOCTEST_CHECK(items.size() == 1);
    });
    QObject::connect(&batcher, &SceneOutlinerBatcher::reset, [&]() { ++resets; });
    for (int i = 0; i < 100; ++i)
        batcher.add_changed({ id("/A") });
    DOCTEST_CHECK(changes == 0);
    QCoreApplication::processEvents();
    DOCTEST_CHECK(changes == 1);
    batcher.add_changed({ id("/B") });
    batcher.request_reset();
    QCoreApplication::processEvents();
    DOCTEST_CHECK(changes == 1);
    DOCTEST_CHECK(resets == 1);
    batcher.request_reset();
    batcher.cancel();
    QCoreApplication::processEvents();
    DOCTEST_CHECK(resets == 1);
}

DOCTEST_TEST_CASE("worker notices are delivered on the model thread and stale notices are discarded" * doctest::test_suite("SceneOutlinerModel"))
{
    int argc = 1;
    char name[] = "outliner-tests";
    char* argv[] = { name, nullptr };
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);
    TestSource source, replacement;
    SceneOutlinerModel model;
    model.set_data_source(&source);
    int resets = 0;
    const auto owner = std::this_thread::get_id();
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&]() {
        DOCTEST_CHECK(bool(std::this_thread::get_id() == owner));
        ++resets;
    });
    std::thread worker([&]() { source.notify(SceneOutlinerChange::reset()); });
    worker.join();
    DOCTEST_CHECK(resets == 0);
    QCoreApplication::processEvents();
    DOCTEST_CHECK(resets == 1);
    std::thread stale([&]() { source.notify(SceneOutlinerChange::reset()); });
    stale.join();
    model.set_data_source(&replacement);
    DOCTEST_CHECK(resets == 2);
    QCoreApplication::processEvents();
    DOCTEST_CHECK(resets == 2);
    auto temporary = std::make_unique<SceneOutlinerModel>();
    temporary->set_data_source(&source);
    std::thread closing([&]() { source.notify(SceneOutlinerChange::reset()); });
    closing.join();
    temporary.reset();
    QCoreApplication::processEvents();
    SceneOutlinerModel other;
    other.set_data_source(&source);
    DOCTEST_CHECK_FALSE(model.item_for_index(other.index(0, 0)));
}

DOCTEST_TEST_CASE("batcher accepts concurrent producers without duplicate deliveries" * doctest::test_suite("SceneOutlinerModel"))
{
    int argc = 1;
    char name[] = "outliner-tests";
    char* argv[] = { name, nullptr };
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);
    SceneOutlinerBatcher batcher;
    int delivered = 0;
    QObject::connect(&batcher, &SceneOutlinerBatcher::items_changed, [&](const auto& items, bool) { delivered += static_cast<int>(items.size()); });
    std::vector<std::thread> workers;
    for (int worker = 0; worker != 4; ++worker)
        workers.emplace_back([&]() {
            for (int i = 0; i != 16; ++i)
                batcher.add_changed({ id("/" + std::to_string(i)) });
        });
    for (auto& worker : workers)
        worker.join();
    QCoreApplication::processEvents();
    DOCTEST_CHECK(delivered == 16);
}

DOCTEST_TEST_CASE("batched search relevance survives display-only superseding updates" * doctest::test_suite("SceneOutlinerModel"))
{
    int argc = 1;
    char name[] = "outliner-tests";
    char* argv[] = { name, nullptr };
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);
    SceneOutlinerBatcher batcher;
    int calls = 0;
    bool affects_search = false;
    QObject::connect(&batcher, &SceneOutlinerBatcher::items_changed, [&](const SceneItemList& items, bool search) {
        ++calls;
        affects_search = search;
        DOCTEST_CHECK(items.empty());
    });
    batcher.add_changed({ id("/A") }, true);
    batcher.add_changed({}, false);
    QCoreApplication::processEvents();
    DOCTEST_CHECK(calls == 1);
    DOCTEST_CHECK(affects_search);
    batcher.add_changed({}, false);
    QCoreApplication::processEvents();
    DOCTEST_CHECK(calls == 2);
    DOCTEST_CHECK_FALSE(affects_search);
    batcher.add_changed({}, true);
    batcher.cancel();
    batcher.add_changed({}, false);
    QCoreApplication::processEvents();
    DOCTEST_CHECK(calls == 3);
    DOCTEST_CHECK_FALSE(affects_search);
}

DOCTEST_TEST_CASE("batcher coalesces bulk display refresh and cancels pending refresh" * doctest::test_suite("SceneOutlinerModel"))
{
    int argc = 1;
    char name[] = "outliner-tests";
    char* argv[] = { name, nullptr };
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);
    SceneOutlinerBatcher batcher;
    int changes = 0;
    QObject::connect(&batcher, &SceneOutlinerBatcher::items_changed, [&](const auto& items, bool) {
        DOCTEST_CHECK(items.empty());
        ++changes;
    });
    batcher.add_changed({ id("/A") });
    batcher.add_changed({});
    batcher.add_changed({ id("/B") });
    QCoreApplication::processEvents();
    DOCTEST_CHECK(changes == 1);
    batcher.add_changed({});
    batcher.cancel();
    QCoreApplication::processEvents();
    DOCTEST_CHECK(changes == 1);
}

DOCTEST_TEST_CASE("batcher signals support queued item delivery" * doctest::test_suite("SceneOutlinerModel"))
{
    int argc = 1;
    char name[] = "outliner-tests";
    char* argv[] = { name, nullptr };
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);
    SceneOutlinerBatcher batcher;
    QObject receiver;
    SceneItemList delivered;
    QObject::connect(
        &batcher, &SceneOutlinerBatcher::items_changed, &receiver, [&](const SceneItemList& items, bool) { delivered = items; },
        Qt::QueuedConnection);
    std::thread worker([&]() { batcher.add_changed({ id("/A") }); });
    worker.join();
    DOCTEST_CHECK(delivered.empty());
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    DOCTEST_REQUIRE(delivered.size() == 1);
    DOCTEST_CHECK(delivered.front() == id("/A"));
}

DOCTEST_TEST_CASE("batcher stops a flush when a receiver cancels resets or destroys it" * doctest::test_suite("SceneOutlinerModel"))
{
    int argc = 1;
    char name[] = "outliner-tests";
    char* argv[] = { name, nullptr };
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);
    for (int action = 0; action != 3; ++action)
    {
        auto batcher = std::make_unique<SceneOutlinerBatcher>();
        int resyncs = 0, changes = 0, resets = 0;
        QObject::connect(batcher.get(), &SceneOutlinerBatcher::resynced, [&](const SceneItemList&) {
            ++resyncs;
            if (action == 0)
                batcher->cancel();
            else if (action == 1)
                batcher->request_reset();
            else
                batcher.reset();
        });
        QObject::connect(batcher.get(), &SceneOutlinerBatcher::items_changed, [&](const SceneItemList&, bool) { ++changes; });
        QObject::connect(batcher.get(), &SceneOutlinerBatcher::reset, [&]() { ++resets; });
        batcher->add_resync({ id("/") });
        batcher->add_changed({ id("/A") });
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        DOCTEST_CHECK(resyncs == 1);
        DOCTEST_CHECK(changes == 0);
        DOCTEST_CHECK(resets == (action == 1 ? 1 : 0));
        if (batcher)
        {
            batcher->add_changed({ id("/B") });
            QCoreApplication::processEvents();
            DOCTEST_CHECK(changes == 1);
        }
    }
}

OPENDCC_NAMESPACE_CLOSE
