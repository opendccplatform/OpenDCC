// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "usd_clipboard.h"

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#if PXR_VERSION >= 2508
#include <pxr/usd/sdf/usdFileFormat.h>
#include <pxr/usd/sdf/usdcFileFormat.h>
#else
#include <pxr/usd/usd/usdFileFormat.h>
#include <pxr/usd/usd/usdcFileFormat.h>
#endif

#include "opendcc/base/vendor/ghc/filesystem.hpp"
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/core/application.h"

PXR_NAMESPACE_USING_DIRECTIVE;

OPENDCC_NAMESPACE_OPEN

UsdClipboard::UsdClipboard()
{
    auto clipboard_path = ghc::filesystem::temp_directory_path();
    clipboard_path.append("OpenDCCClipboard.usd");
    set_clipboard_path(clipboard_path.string());
#if PXR_VERSION >= 2508
    set_clipboard_file_format(SdfUsdcFileFormatTokens->Id.GetText());
#else
    set_clipboard_file_format(UsdUsdcFileFormatTokens->Id.GetText());
#endif

    if (!ghc::filesystem::exists(m_path_to_clipboard))
    {
        SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
        UsdStageRefPtr clipboard_stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);
        SdfCreatePrimInLayer(layer, SdfPath("/Clipboard"));
        set_clipboard(clipboard_stage);
    }
}

UsdClipboard::~UsdClipboard()
{
    if (m_clipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = PXR_NS::UsdUtilsStageCache::Get().Find(m_clipboardStageCacheId);
        PXR_NS::UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    if (m_tmpClipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = PXR_NS::UsdUtilsStageCache::Get().Find(m_tmpClipboardStageCacheId);
        PXR_NS::UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }
}

UsdStageWeakPtr UsdClipboard::get_clipboard()
{
    auto layer = SdfLayer::FindOrOpen(m_path_to_clipboard);
    if (!layer)
    {
        return {};
    }

    UsdStageRefPtr clipboard = UsdStage::Open(m_path_to_clipboard);

    if (m_clipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = UsdUtilsStageCache::Get().Find(m_clipboardStageCacheId);
        UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    m_clipboardStageCacheId = UsdUtilsStageCache::Get().Insert(clipboard);

    clipboard->Reload();

    return clipboard;
}

void UsdClipboard::clear_clipboard()
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
    UsdStageRefPtr stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);
    SdfCreatePrimInLayer(layer, SdfPath("/Clipboard"));
    set_clipboard(stage);
}

void UsdClipboard::set_clipboard(const UsdStageWeakPtr& clipboard)
{
    SdfFileFormat::FileFormatArguments args;
#if PXR_VERSION >= 2508
    args[SdfUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#else
    args[UsdUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#endif
    if (!clipboard->GetRootLayer()->Export(m_path_to_clipboard, "OpenDCCСlipboard", args))
    {
        return;
    }

    clipboard->Unload();
}

void UsdClipboard::set_clipboard_path(const std::string& clipboard_path)
{
    m_path_to_clipboard = clipboard_path;
}

void UsdClipboard::set_clipboard_file_format(const std::string& format)
{
    m_clipboard_file_format = format;
}

void UsdClipboard::save_clipboard_data(const UsdStageWeakPtr& stage)
{
    SdfFileFormat::FileFormatArguments args;
#if PXR_VERSION >= 2508
    args[SdfUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#else
    args[UsdUsdFileFormatTokens->FormatArg] = m_clipboard_file_format;
#endif
    if (!stage->GetRootLayer()->Export(m_path_to_clipboard, "OpenDCCСlipboard", args))
    {
        return;
    }

    stage->Unload();
}

void UsdClipboard::set_clipboard_attribute(const UsdAttribute& attribute)
{
    save_clipboard_data(attribute.GetStage());
}

void UsdClipboard::set_clipboard_stage(const UsdStageWeakPtr& stage)
{
    save_clipboard_data(stage);
}

UsdAttribute UsdClipboard::get_clipboard_attribute()
{
    auto clipboard_stage = get_clipboard();
    SdfPath attribute_path;

    auto custom_data = clipboard_stage->GetRootLayer()->GetCustomLayerData();
    auto data_type = custom_data.find("stored_data_type");
    if (data_type != custom_data.end() && data_type->second == "attribute")
    {
        auto attr_path = custom_data.find("attribute_path");
        attribute_path = SdfPath(attr_path->second.Get<std::string>());
    }

    if (attribute_path.IsEmpty())
    {
        return {};
    }

    return clipboard_stage->GetAttributeAtPath(attribute_path);
}

UsdStageWeakPtr UsdClipboard::get_clipboard_stage()
{
    auto clipboard_stage = get_clipboard();
    SdfPath attribute_path;

    auto custom_data = clipboard_stage->GetRootLayer()->GetCustomLayerData();
    auto data_type = custom_data.find("stored_data_type");
    if (data_type == custom_data.end() || data_type->second == "attribute")
    {
        return {};
    }
    else
    {
        return clipboard_stage;
    }
}

UsdStageWeakPtr UsdClipboard::get_new_clipboard_stage(const std::string& data_type)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
    UsdStageRefPtr stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);

    if (m_tmpClipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = UsdUtilsStageCache::Get().Find(m_tmpClipboardStageCacheId);
        UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    m_tmpClipboardStageCacheId = UsdUtilsStageCache::Get().Insert(stage);
    VtDictionary custom_data;
    custom_data["stored_data_type"] = data_type;
    layer->SetCustomLayerData(custom_data);

    return stage;
}

UsdAttribute UsdClipboard::get_new_clipboard_attribute(const SdfValueTypeName& type_name)
{
    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous();
    UsdStageRefPtr stage = UsdStage::Open(layer->GetIdentifier(), UsdStage::LoadNone);

    SdfCreatePrimInLayer(layer, SdfPath("/Clipboard"));

    if (m_tmpClipboardStageCacheId.IsValid())
    {
        auto clipboardStageRef = UsdUtilsStageCache::Get().Find(m_tmpClipboardStageCacheId);
        UsdUtilsStageCache::Get().Erase(clipboardStageRef);
    }

    m_tmpClipboardStageCacheId = UsdUtilsStageCache::Get().Insert(stage);
    auto clipboard_attribute = stage->GetPrimAtPath(SdfPath("/Clipboard")).CreateAttribute(TfToken("attribute"), type_name);
    VtDictionary custom_data;
    custom_data["stored_data_type"] = "attribute";
    custom_data["attribute_path"] = clipboard_attribute.GetPath().GetString();
    layer->SetCustomLayerData(custom_data);

    return clipboard_attribute;
}

OPENDCC_NAMESPACE_CLOSE

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
#include <pxr/usd/sdf/types.h>

OPENDCC_NAMESPACE_USING

DOCTEST_TEST_SUITE("clipboart_test")
{
    DOCTEST_TEST_CASE("copy_attribute_value")
    {
        UsdClipboard clipboard;
        UsdAttribute new_clipboard_attr = clipboard.get_new_clipboard_attribute(SdfValueTypeNames->Int);
        DOCTEST_CHECK(new_clipboard_attr.IsValid());
        new_clipboard_attr.Set(42);
        clipboard.set_clipboard_attribute(new_clipboard_attr);

        UsdAttribute attr = clipboard.get_clipboard_attribute();
        VtValue value;
        attr.Get(&value);

        std::vector<double> time_samples;
        attr.GetTimeSamples(&time_samples);

        DOCTEST_CHECK(time_samples.size() == 0);
        DOCTEST_CHECK(value.IsHolding<int>());
        DOCTEST_CHECK(value.Get<int>() == 42);
    }

    DOCTEST_TEST_CASE("copy_attribute_time_samples")
    {
        UsdClipboard clipboard;
        UsdAttribute new_clipboard_attr = clipboard.get_new_clipboard_attribute(SdfValueTypeNames->Bool);
        DOCTEST_CHECK(new_clipboard_attr.IsValid());
        new_clipboard_attr.Set(true, 0);
        new_clipboard_attr.Set(false, 1);
        clipboard.set_clipboard_attribute(new_clipboard_attr);

        UsdAttribute attr = clipboard.get_clipboard_attribute();
        VtValue value_1;
        attr.Get(&value_1, 0);
        VtValue value_2;
        attr.Get(&value_2, 1);

        std::vector<double> time_samples;
        attr.GetTimeSamples(&time_samples);

        DOCTEST_CHECK(time_samples.size() == 2);
        DOCTEST_CHECK(value_1.IsHolding<bool>());
        DOCTEST_CHECK(value_1.Get<bool>() == true);
        DOCTEST_CHECK(value_2.IsHolding<bool>());
        DOCTEST_CHECK(value_2.Get<bool>() == false);
    }

    DOCTEST_TEST_CASE("copy_prims")
    {
        UsdClipboard clipboard;
        auto new_clipboard_stage = clipboard.get_new_clipboard_stage("prims");
        DOCTEST_CHECK(!new_clipboard_stage.IsInvalid());

        new_clipboard_stage->DefinePrim(SdfPath("/test_sphere"), TfToken("Sphere"));
        new_clipboard_stage->DefinePrim(SdfPath("/test_cube"), TfToken("Cube"));
        new_clipboard_stage->DefinePrim(SdfPath("/test_cone"), TfToken("Cone"));
        new_clipboard_stage->DefinePrim(SdfPath("/test_plane"), TfToken("Plane"));
        clipboard.set_clipboard_stage(new_clipboard_stage);

        auto clipboard_stage = clipboard.get_clipboard_stage();
        auto prim = clipboard_stage->GetPrimAtPath(SdfPath("/test_sphere"));
        DOCTEST_CHECK(prim.IsValid());
        DOCTEST_CHECK(prim.GetName().GetString() == "test_sphere");
        DOCTEST_CHECK(prim.GetTypeName().GetString() == "Sphere");

        prim = clipboard_stage->GetPrimAtPath(SdfPath("/test_cube"));
        DOCTEST_CHECK(prim.IsValid());
        DOCTEST_CHECK(prim.GetName().GetString() == "test_cube");
        DOCTEST_CHECK(prim.GetTypeName().GetString() == "Cube");

        prim = clipboard_stage->GetPrimAtPath(SdfPath("/test_cone"));
        DOCTEST_CHECK(prim.IsValid());
        DOCTEST_CHECK(prim.GetName().GetString() == "test_cone");
        DOCTEST_CHECK(prim.GetTypeName().GetString() == "Cone");

        prim = clipboard_stage->GetPrimAtPath(SdfPath("/test_plane"));
        DOCTEST_CHECK(prim.IsValid());
        DOCTEST_CHECK(prim.GetName().GetString() == "test_plane");
        DOCTEST_CHECK(prim.GetTypeName().GetString() == "Plane");
    }

    DOCTEST_TEST_CASE("copy_prims_with_relationship")
    {
        UsdClipboard clipboard;
        auto new_clipboard_stage = clipboard.get_new_clipboard_stage("prims");
        DOCTEST_CHECK(!new_clipboard_stage.IsInvalid());

        auto sphere = new_clipboard_stage->DefinePrim(SdfPath("/test_sphere"), TfToken("Sphere"));
        auto cube = new_clipboard_stage->DefinePrim(SdfPath("/test_cube"), TfToken("Cube"));
        auto cone = new_clipboard_stage->DefinePrim(SdfPath("/test_cone"), TfToken("Cone"));
        auto plane = new_clipboard_stage->DefinePrim(SdfPath("/test_plane"), TfToken("Plane"));

        auto sphere_rel = sphere.CreateRelationship(TfToken("test_sphere_rel"));
        DOCTEST_CHECK(sphere_rel.IsValid());
        sphere_rel.AddTarget(cube.GetPath());
        sphere_rel.AddTarget(cone.GetPath());
        sphere_rel.AddTarget(plane.GetPath());
        auto cube_rel = cube.CreateRelationship(TfToken("test_cube_rel"));
        DOCTEST_CHECK(sphere_rel.IsValid());
        cube_rel.AddTarget(sphere.GetPath());
        cube_rel.AddTarget(cone.GetPath());
        auto cone_rel = cone.CreateRelationship(TfToken("test_cone_rel"));
        DOCTEST_CHECK(sphere_rel.IsValid());
        cone_rel.AddTarget(plane.GetPath());
        DOCTEST_CHECK(plane.CreateRelationship(TfToken("test_plane_rel")).IsValid());

        clipboard.set_clipboard_stage(new_clipboard_stage);
        auto clipboard_stage = clipboard.get_clipboard_stage();

        auto clipboard_sphere = clipboard_stage->GetPrimAtPath(SdfPath("/test_sphere"));
        auto clipboard_cube = clipboard_stage->GetPrimAtPath(SdfPath("/test_cube"));
        auto clipboard_cone = clipboard_stage->GetPrimAtPath(SdfPath("/test_cone"));
        auto clipboard_plane = clipboard_stage->GetPrimAtPath(SdfPath("/test_plane"));

        DOCTEST_CHECK(clipboard_sphere.IsValid());
        DOCTEST_CHECK(clipboard_cube.IsValid());
        DOCTEST_CHECK(clipboard_cone.IsValid());
        DOCTEST_CHECK(clipboard_plane.IsValid());

        SdfPathVector sphere_targets;
        auto clipboard_sphere_rel = clipboard_sphere.GetRelationship(TfToken("test_sphere_rel"));
        DOCTEST_CHECK(clipboard_sphere_rel.IsValid());
        clipboard_sphere_rel.GetTargets(&sphere_targets);
        DOCTEST_CHECK(sphere_targets.size() == 3);

        SdfPathVector cube_targets;
        auto clipboard_cube_rel = clipboard_cube.GetRelationship(TfToken("test_cube_rel"));
        DOCTEST_CHECK(clipboard_cube_rel.IsValid());
        clipboard_cube_rel.GetTargets(&cube_targets);
        DOCTEST_CHECK(cube_targets.size() == 2);

        SdfPathVector cone_targets;
        auto clipboard_cone_rel = clipboard_cone.GetRelationship(TfToken("test_cone_rel"));
        DOCTEST_CHECK(clipboard_cone_rel.IsValid());
        clipboard_cone_rel.GetTargets(&cone_targets);
        DOCTEST_CHECK(cone_targets.size() == 1);

        SdfPathVector plane_targets;
        auto clipboard_plane_rel = clipboard_plane.GetRelationship(TfToken("test_plane_rel"));
        DOCTEST_CHECK(clipboard_plane_rel.IsValid());
        clipboard_plane_rel.GetTargets(&plane_targets);
        DOCTEST_CHECK(plane_targets.size() == 0);
    }
}
