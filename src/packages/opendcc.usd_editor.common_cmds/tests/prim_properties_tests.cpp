/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/python_bindings/python_command_interface.h"

#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/modelAPI.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pybind11/eval.h>

#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandArgs;
using base::commands::CommandInterface;
using base::commands::python::PythonCommandInterface;
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    struct RecordingFixture
    {
        UsdStageRefPtr previous = Application::instance().get_session()->get_current_stage();
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        std::string recording;
        PythonCommandInterface::EventDispatcherHandle handle;

        RecordingFixture()
        {
            UsdGeomXform::Define(stage, SdfPath("/Root"));
            Application::instance().get_session()->set_current_stage(stage);
            handle = PythonCommandInterface::instance().register_event_callback([this](const auto& command, const auto& args, const auto&) {
                recording = PythonCommandInterface::generate_python_cmd_str(command, args);
            });
        }
        ~RecordingFixture()
        {
            PythonCommandInterface::instance().unregister_event_callback(handle);
            if (previous)
                Application::instance().get_session()->set_current_stage(previous);
            Application::instance().get_session()->close_stage(stage);
        }

        void replay()
        {
            DOCTEST_REQUIRE(recording.find("opendcc.cmds.") == 0);
            const auto code = recording;
            pybind11::gil_scoped_acquire gil;
            pybind11::dict globals;
            pybind11::exec("import opendcc.cmds\nfrom pxr import Sdf, Usd", globals);
            const auto result = pybind11::eval(code, globals);
            DOCTEST_CHECK(result.attr("is_successful")().cast<bool>());
        }
    };
}

DOCTEST_TEST_CASE_FIXTURE(RecordingFixture, "prim property command recordings replay paths, tokens, strings and booleans" *
                                                doctest::test_suite("UsdPrimPropertiesRecording"))
{
    DOCTEST_REQUIRE(CommandInterface::execute("set_prim_kind", CommandArgs().arg(SdfPath("/Root")).arg(TfToken("component"))).is_successful());
    stage->GetPrimAtPath(SdfPath("/Root")).ClearMetadata(TfToken("kind"));
    replay();
    TfToken kind;
    DOCTEST_CHECK(stage->GetPrimAtPath(SdfPath("/Root")).GetMetadata(TfToken("kind"), &kind));
    DOCTEST_CHECK(kind == TfToken("component"));
    DOCTEST_REQUIRE(CommandInterface::execute("set_prim_active", CommandArgs().arg(SdfPathVector { SdfPath("/Root") }).arg(false)).is_successful());
    stage->GetPrimAtPath(SdfPath("/Root")).SetActive(true);
    replay();
    DOCTEST_CHECK_FALSE(stage->GetPrimAtPath(SdfPath("/Root")).IsActive());
    stage->GetPrimAtPath(SdfPath("/Root")).SetActive(true);
    auto set = stage->GetPrimAtPath(SdfPath("/Root")).GetVariantSets().AddVariantSet("look");
    set.AddVariant("red");
    set.AddVariant("blue");
    DOCTEST_REQUIRE(
        CommandInterface::execute("set_variant_selection", CommandArgs().arg(SdfPath("/Root")).arg(std::string("look")).arg(std::string("red")))
            .is_successful());
    set.SetVariantSelection("blue");
    replay();
    DOCTEST_CHECK(set.GetVariantSelection() == "red");
    DOCTEST_REQUIRE(CommandInterface::execute("set_prim_kind",
                                              CommandArgs().arg(SdfPath("/Root")).arg(TfToken("assembly")).kwarg("stage", UsdStageWeakPtr(stage)))
                        .is_successful());
    stage->GetPrimAtPath(SdfPath("/Root")).ClearMetadata(TfToken("kind"));
    replay();
    DOCTEST_CHECK(stage->GetPrimAtPath(SdfPath("/Root")).GetMetadata(TfToken("kind"), &kind));
    DOCTEST_CHECK(kind == TfToken("assembly"));

    DOCTEST_REQUIRE(CommandInterface::execute("set_prim_type", CommandArgs().arg(SdfPath("/Root")).arg(TfToken("Scope"))).is_successful());
    stage->GetPrimAtPath(SdfPath("/Root")).SetTypeName(TfToken("Xform"));
    replay();
    DOCTEST_CHECK(stage->GetPrimAtPath(SdfPath("/Root")).GetTypeName() == TfToken("Scope"));
    stage->GetPrimAtPath(SdfPath("/Root")).SetTypeName(TfToken("Xform"));
    DOCTEST_REQUIRE(
        CommandInterface::execute("set_prim_visibility", CommandArgs().arg(SdfPathVector { SdfPath("/Root") }).arg(false)).is_successful());
    UsdGeomImageable(stage->GetPrimAtPath(SdfPath("/Root"))).GetVisibilityAttr().Clear();
    replay();
    TfToken visibility;
    DOCTEST_REQUIRE(UsdGeomImageable(stage->GetPrimAtPath(SdfPath("/Root"))).GetVisibilityAttr().Get(&visibility));
    DOCTEST_CHECK(visibility == TfToken("invisible"));
    DOCTEST_REQUIRE(CommandInterface::execute("set_prim_draw_mode", CommandArgs().arg(SdfPath("/Root")).arg(TfToken("cards"))).is_successful());
    UsdGeomModelAPI(stage->GetPrimAtPath(SdfPath("/Root"))).GetModelDrawModeAttr().Clear();
    replay();
    TfToken draw_mode;
    DOCTEST_REQUIRE(UsdGeomModelAPI(stage->GetPrimAtPath(SdfPath("/Root"))).GetModelDrawModeAttr().Get(&draw_mode));
    DOCTEST_CHECK(draw_mode == TfToken("cards"));
    auto payload_stage = UsdStage::CreateInMemory();
    UsdGeomXform::Define(payload_stage, SdfPath("/Asset/Inside"));
    auto payload = UsdGeomXform::Define(stage, SdfPath("/Payload")).GetPrim();
    payload.GetPayloads().AddPayload(payload_stage->GetRootLayer()->GetIdentifier(), SdfPath("/Asset"));
    DOCTEST_REQUIRE(
        CommandInterface::execute("set_payload_loaded", CommandArgs().arg(SdfPathVector { SdfPath("/Payload") }).arg(false)).is_successful());
    payload.Load();
    replay();
    DOCTEST_CHECK_FALSE(payload.IsLoaded());

    DOCTEST_REQUIRE(CommandInterface::execute("remove_prims", CommandArgs().arg(SdfPathVector { SdfPath("/Root") })).is_successful());
    UsdGeomXform::Define(stage, SdfPath("/Root"));
    replay();
    DOCTEST_CHECK_FALSE(stage->GetPrimAtPath(SdfPath("/Root")));
}

DOCTEST_TEST_CASE_FIXTURE(RecordingFixture, "rename command recordings preserve Unicode paths" * doctest::test_suite("UsdPrimPropertiesRecording"))
{
    const std::string unicode_name = u8"\u041f\u0440\u0438\u043c_\u6a21\u578b";
    if (SdfPath::IsValidIdentifier(unicode_name))
    {
        const auto path = SdfPath::AbsoluteRootPath().AppendChild(TfToken(unicode_name));
        stage->DefinePrim(SdfPath("/Original"));
        DOCTEST_REQUIRE(
            CommandInterface::execute("rename_prim", CommandArgs().arg(TfToken(unicode_name)).kwarg("path", SdfPath("/Original"))).is_successful());
        DOCTEST_REQUIRE(stage->GetPrimAtPath(path));
        stage->RemovePrim(path);
        stage->DefinePrim(SdfPath("/Original"));
        replay();
        DOCTEST_CHECK(stage->GetPrimAtPath(path));
        DOCTEST_CHECK_FALSE(stage->GetPrimAtPath(SdfPath("/Original")));
    }
}

OPENDCC_NAMESPACE_CLOSE
