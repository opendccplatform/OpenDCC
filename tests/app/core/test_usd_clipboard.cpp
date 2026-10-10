// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/usd_clipboard.h"
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/relationship.h>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
#include <pxr/usd/sdf/types.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

DOCTEST_TEST_SUITE("UsdClipboard")
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

OPENDCC_NAMESPACE_CLOSE
