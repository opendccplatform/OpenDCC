/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/usd_editor/usd_outliner/usd_stage_data_source.h"
#include "opendcc/usd_editor/usd_outliner/usd_item_id.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"
#include <pybind11/eval.h>
#include "notice_test_access.h"
#include <QCoreApplication>
#include <QTimer>
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
#include <thread>
#include <future>
#include <atomic>
#include "opendcc/ui/scene_outliner/outliner_model.h"

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    struct RenameRecordingFixture
    {
        UsdStageRefPtr previous = Application::instance().get_session()->get_current_stage();
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        std::string recording;
        using PythonCommandInterface = base::commands::python::PythonCommandInterface;
        PythonCommandInterface::EventDispatcherHandle handle;

        RenameRecordingFixture()
        {
            Application::instance().get_session()->set_current_stage(stage);
            handle = PythonCommandInterface::instance().register_event_callback([this](const auto& command, const auto& args, const auto&) {
                recording = PythonCommandInterface::generate_python_cmd_str(command, args);
            });
        }
        ~RenameRecordingFixture()
        {
            PythonCommandInterface::instance().unregister_event_callback(handle);
            Application::instance().get_undo_stack()->clear();
            if (previous)
                Application::instance().get_session()->set_current_stage(previous);
            Application::instance().get_session()->close_stage(stage);
        }
    };

    std::unique_ptr<QCoreApplication> test_application()
    {
        static int argc = 1;
        static char name[] = "outliner-tests";
        static char* argv[] = { name, nullptr };
        if (!QCoreApplication::instance())
            return std::make_unique<QCoreApplication>(argc, argv);
        return {};
    }
}

DOCTEST_TEST_CASE_FIXTURE(RenameRecordingFixture, "USD rename recordings replay both adapter entry points" * doctest::test_suite("UsdOutlinerWork"))
{
    auto app = test_application();
    UsdStageDataSource source;
    auto* undo = Application::instance().get_undo_stack();
    for (const std::string name : { std::string("Renamed"), std::string(u8"\u041f\u0440\u0438\u043c_\u6a21\u578b") })
    {
        if (!SdfPath::IsValidIdentifier(name))
            continue;
        for (const bool column_edit : { false, true })
        {
            DOCTEST_CAPTURE(name);
            DOCTEST_CAPTURE(column_edit);
            const SdfPath original("/Original");
            const auto renamed = SdfPath::AbsoluteRootPath().AppendChild(TfToken(name));
            stage->DefinePrim(original);
            std::string before;
            DOCTEST_REQUIRE(stage->GetRootLayer()->ExportToString(&before));
            undo->clear();
            recording.clear();
            if (column_edit)
                DOCTEST_REQUIRE(source.set_column_text(make_usd_item_id(original), "name", name));
            else
                source.rename(make_usd_item_id(original), name);
            DOCTEST_REQUIRE(stage->GetPrimAtPath(renamed));
            DOCTEST_REQUIRE(recording.find("opendcc.cmds.rename_prim(") == 0);
            const auto code = recording;
            undo->undo();
            std::string restored;
            DOCTEST_REQUIRE(stage->GetRootLayer()->ExportToString(&restored));
            DOCTEST_REQUIRE(restored == before);
            {
                pybind11::gil_scoped_acquire gil;
                pybind11::dict globals;
                pybind11::exec("import opendcc.cmds\nfrom pxr import Sdf, Usd", globals);
                DOCTEST_REQUIRE(pybind11::eval(code, globals).attr("is_successful")().cast<bool>());
            }
            DOCTEST_CHECK(stage->GetPrimAtPath(renamed));
            DOCTEST_CHECK_FALSE(stage->GetPrimAtPath(original));
            undo->undo();
            DOCTEST_REQUIRE(stage->GetRootLayer()->ExportToString(&restored));
            DOCTEST_CHECK(restored == before);
            stage->RemovePrim(original);
        }
    }
    auto temporary_stage = UsdStage::CreateInMemory();
    SdfLayerHandle temporary_layer = temporary_stage->GetRootLayer();
    temporary_stage->DefinePrim(SdfPath("/Original"));
    source.set_stage(temporary_stage);
    undo->clear();
    source.rename(make_usd_item_id(SdfPath("/Original")), "Renamed");
    DOCTEST_REQUIRE(temporary_stage->GetPrimAtPath(SdfPath("/Renamed")));
    source.set_stage(stage);
    temporary_stage.Reset();
    DOCTEST_REQUIRE_FALSE(temporary_layer);
    DOCTEST_CHECK_NOTHROW(undo->undo());
    DOCTEST_CHECK_NOTHROW(undo->redo());
}

DOCTEST_TEST_CASE("USD Work search delivers live-stage results on the request thread" * doctest::test_suite("UsdOutlinerWork"))
{
    auto app = test_application();
    auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/Root"));
    for (int i = 0; i != 128; ++i)
        stage->DefinePrim(SdfPath("/Root/Child" + std::to_string(i)));
    UsdStageDataSource source;
    source.set_stage(stage);
    DOCTEST_REQUIRE(source.children(make_usd_item_id(SdfPath("/Root"))).size() == 128);
    auto expected = source.search("child99");
    DOCTEST_REQUIRE(expected.size() == 1);
    SceneOutlinerSearchRequest request;
    SceneItemList actual;
    bool completed = false;
    const auto owner = std::this_thread::get_id();
    QObject::connect(&request, &SceneOutlinerSearchRequest::finished, &request, [&](const SceneItemList& matches) {
        DOCTEST_CHECK(std::this_thread::get_id() == owner);
        actual = matches;
        completed = true;
    });
    int heartbeats = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&]() { ++heartbeats; });
    heartbeat.start(0);
    const auto search_started = std::chrono::steady_clock::now();
    source.start_search("CHILD99", &request);
    DOCTEST_CHECK_FALSE(completed);
    while (!completed && std::chrono::steady_clock::now() - search_started < std::chrono::seconds(5))
        QCoreApplication::processEvents();
    DOCTEST_REQUIRE(completed);
    DOCTEST_CHECK(heartbeats > 0);
    DOCTEST_CHECK(actual == expected);
    stage->RemovePrim(SdfPath("/Root/Child99"));
    DOCTEST_CHECK(source.search("child99").empty());
    for (int i = 0; i != 5; ++i)
        QCoreApplication::processEvents();
    QObject requests;
    int completed_searches = 0;
    for (int i = 0; i != 2; ++i)
    {
        auto* next = new SceneOutlinerSearchRequest(&requests);
        QObject::connect(next, &SceneOutlinerSearchRequest::finished, &requests, [&](const SceneItemList&) { ++completed_searches; });
        source.start_search("Child", next);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (completed_searches != 2 && std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents();
    DOCTEST_CHECK(completed_searches == 2);
    SceneOutlinerSearchRequest cancelled;
    source.start_search("Child", &cancelled);
    source.set_stage(nullptr);
    DOCTEST_CHECK(cancelled.is_cancelled());
    DOCTEST_CHECK(source.roots().empty());
}

DOCTEST_TEST_CASE("USD worker edits reconcile only on the owner thread" * doctest::test_suite("UsdOutlinerWork"))
{
    auto app = test_application();
    auto stage = UsdStage::CreateInMemory();
    stage->DefinePrim(SdfPath("/Root"));
    UsdStageDataSource source;
    source.set_stage(stage);
    SceneOutlinerModel model;
    model.set_data_source(&source);
    model.ensure_index_for_item(make_usd_item_id(SdfPath("/Root")));
    const auto owner = std::this_thread::get_id();
    int deliveries = 0;
    QObject::connect(&source, &ISceneOutlinerDataSource::changed, &model, [&](const SceneOutlinerChange&) {
        DOCTEST_CHECK(std::this_thread::get_id() == owner);
        ++deliveries;
    });
    std::promise<void> ready;
    std::promise<void> release;
    auto released = release.get_future();
    std::thread writer([&]() {
        ready.set_value();
        released.wait();
        for (int i = 0; i != 100; ++i)
            stage->DefinePrim(SdfPath("/Root/Child" + std::to_string(i)));
    });
    ready.get_future().wait();
    release.set_value();
    // No stage reads or event processing until the exclusive writer finishes.
    writer.join();
    DOCTEST_CHECK(deliveries == 0);
    for (int i = 0; i != 5; ++i)
        QCoreApplication::processEvents();
    DOCTEST_CHECK(deliveries == 1);
    DOCTEST_CHECK(model.ensure_index_for_item(make_usd_item_id(SdfPath("/Root/Child99"))).isValid());

    SceneOutlinerSearchRequest obsolete;
    int stale_results = 0;
    QObject::connect(&obsolete, &SceneOutlinerSearchRequest::finished, &obsolete, [&](const SceneItemList&) { ++stale_results; });
    source.start_search("Child", &obsolete);
    std::thread update([&]() { stage->RemovePrim(SdfPath("/Root/Child99")); });
    update.join();
    for (int i = 0; i != 5; ++i)
        QCoreApplication::processEvents();
    DOCTEST_CHECK(obsolete.is_cancelled());
    DOCTEST_CHECK(stale_results == 0);
}

DOCTEST_TEST_CASE("USD concurrent notices survive stage replacement and destruction" * doctest::test_suite("UsdOutlinerWork"))
{
    auto app = test_application();
    auto stage = UsdStage::CreateInMemory();
    auto replacement = UsdStage::CreateInMemory();
    replacement->DefinePrim(SdfPath("/Root"));
    auto source = std::make_unique<UsdStageDataSource>();
    source->set_stage(stage);
    int deliveries = 0;
    QObject observer;
    QObject::connect(source.get(), &ISceneOutlinerDataSource::changed, &observer, [&](const SceneOutlinerChange&) { ++deliveries; });
    std::promise<void> release;
    auto released = release.get_future().share();
    std::vector<std::thread> producers;
    for (int i = 0; i != 4; ++i)
        producers.emplace_back([&]() {
            released.wait();
            // Synthetic notices exercise concurrent delivery without concurrent USD writes.
            for (int n = 0; n != 100; ++n)
                UsdNotice::StageEditTargetChanged(stage).Send(UsdStageWeakPtr(stage));
        });
    release.set_value();
    for (auto& producer : producers)
        producer.join();
    DOCTEST_CHECK(deliveries == 0);
    for (int i = 0; i != 5; ++i)
        QCoreApplication::processEvents();
    DOCTEST_CHECK(deliveries == 1);
    std::thread stale([&]() { UsdNotice::StageEditTargetChanged(stage).Send(UsdStageWeakPtr(stage)); });
    stale.join();
    source->set_stage(replacement);
    DOCTEST_CHECK(deliveries == 2);
    for (int i = 0; i != 5; ++i)
        QCoreApplication::processEvents();
    DOCTEST_CHECK(deliveries == 2);

    SceneOutlinerSearchRequest pending;
    source->start_search("Root", &pending);
    std::promise<void> entered;
    auto inside_callback = entered.get_future();
    std::promise<void> detached;
    auto delivery_disabled = detached.get_future();
    std::promise<void> release_callback;
    auto callback_released = release_callback.get_future();
    std::atomic<bool> finished { false };
    UsdOutlinerNoticeTestAccess::set_callbacks(
        *source,
        [&]() {
            entered.set_value();
            callback_released.wait();
        },
        [&]() {
            DOCTEST_CHECK_FALSE(finished);
            detached.set_value();
        });
    std::thread closing([&]() {
        UsdNotice::StageEditTargetChanged(replacement).Send(UsdStageWeakPtr(replacement));
        finished = true;
    });
    inside_callback.wait();
    std::thread releasing([&]() {
        delivery_disabled.wait();
        release_callback.set_value();
    });
    source.reset();
    DOCTEST_CHECK(pending.is_cancelled());
    releasing.join();
    closing.join();
    DOCTEST_CHECK(finished);
    for (int i = 0; i != 5; ++i)
        QCoreApplication::processEvents();
    DOCTEST_CHECK(deliveries == 2);
}

OPENDCC_NAMESPACE_CLOSE
