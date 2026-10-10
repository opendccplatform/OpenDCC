// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/usd_ipc_serialization/usd_edits.h"
#include "opendcc/usd/usd_ipc_serialization/serialization.h"
#include <pxr/usd/sdf/layerStateDelegate.h>

#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

DOCTEST_TEST_SUITE("apply_usd_edits")
{
    DOCTEST_TEST_CASE("usd_set_field")
    {
        auto stage = UsdStage::CreateInMemory();
        auto sphere = stage->DefinePrim(SdfPath("/test_prim"), TfToken("Sphere"));
        auto attr = sphere.CreateAttribute(TfToken("radius"), SdfValueTypeNames->Float);
        auto state_delegate = stage->GetEditTarget().GetLayer()->GetStateDelegate();

        {
            UsdEditSetField field_edit(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/test_prim.radius"), TfToken("default"),
                                       VtValue(5.0f));
            field_edit.apply(state_delegate);
            float actual_val;
            attr.Get<float>(&actual_val);
            DOCTEST_CHECK(actual_val == 5.0f);
        }
        {
            UsdEditSetFieldDictValueByKey field_edit(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/test_prim.radius"),
                                                     SdfFieldKeys->CustomData, TfToken("in"), VtValue(42));
            field_edit.apply(state_delegate);
            int actual_val;
            attr.GetMetadataByDictKey<int>(SdfFieldKeys->CustomData, TfToken("in"), &actual_val);
            DOCTEST_CHECK(actual_val == 42);
        }
        {
            UsdEditSetTimesample field_edit(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/test_prim.radius"), 2.25, VtValue(47.52f));
            field_edit.apply(state_delegate);
            float actual_val;
            attr.Get<float>(&actual_val, 2.25);
            DOCTEST_CHECK(actual_val == 47.52f);
        }
    }

    DOCTEST_TEST_CASE("usd_create_delete_spec")
    {
        auto stage = UsdStage::CreateInMemory();
        auto state_delegate = stage->GetEditTarget().GetLayer()->GetStateDelegate();

        UsdEditCreateSpec create_spec(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/test_prim"), SdfSpecType::SdfSpecTypePrim, true);
        create_spec.apply(state_delegate);
        DOCTEST_CHECK(stage->GetEditTarget().GetLayer()->GetPrimAtPath(SdfPath("/test_prim")));

        UsdEditDeleteSpec delete_spec(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/test_prim"), true);
        delete_spec.apply(state_delegate);
        DOCTEST_CHECK(!stage->GetEditTarget().GetLayer()->GetPrimAtPath(SdfPath("/test_prim")));
    }

    DOCTEST_TEST_CASE("usd_move_spec")
    {
        auto stage = UsdStage::CreateInMemory();
        auto sphere1 = stage->DefinePrim(SdfPath("/test_prim1"), TfToken("Sphere"));
        auto sphere2 = stage->DefinePrim(SdfPath("/test_prim2"), TfToken("Sphere"));
        auto state_delegate = stage->GetEditTarget().GetLayer()->GetStateDelegate();

        state_delegate->SetField(SdfPath("/"), TfToken("primChildren"), VtValue(TfTokenVector { TfToken("test_prim1") }));
        UsdEditMoveSpec move_spec(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/test_prim2"), SdfPath("/test_prim1/test_prim2"));
        move_spec.apply(state_delegate);
        state_delegate->SetField(SdfPath("/test_prim1"), TfToken("primChildren"), VtValue(TfTokenVector { TfToken("test_prim2") }));

        DOCTEST_CHECK(stage->GetEditTarget().GetLayer()->GetPrimAtPath(SdfPath("/test_prim1/test_prim2")));
        DOCTEST_CHECK(!stage->GetEditTarget().GetLayer()->GetPrimAtPath(SdfPath("/test_prim2")));
    }

    DOCTEST_TEST_CASE("usd_push_pop_child")
    {
        auto stage = UsdStage::CreateInMemory();
        auto sphere1 = stage->DefinePrim(SdfPath("/test_prim1"), TfToken("Sphere"));
        auto state_delegate = stage->GetEditTarget().GetLayer()->GetStateDelegate();

        TfTokenVector children;
        auto root = stage->GetPrimAtPath(SdfPath("/"));
        root.GetMetadata(TfToken("primChildren"), &children);
        DOCTEST_CHECK(children.size() == 1);

        {
            UsdEditPushChild push_child(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/"), TfToken("primChildren"),
                                        TfToken("test_prim2"));
            push_child.apply(state_delegate);
            root.GetMetadata(TfToken("primChildren"), &children);
            DOCTEST_CHECK(children.size() == 2);
        }

        {
            UsdEditPopChild pop_child(stage->GetEditTarget().GetLayer()->GetIdentifier(), SdfPath("/"), TfToken("primChildren"),
                                      TfToken("test_prim2"));
            pop_child.apply(state_delegate);
            root.GetMetadata(TfToken("primChildren"), &children);
            DOCTEST_CHECK(children.size() == 1);
        }
    }
}

DOCTEST_TEST_SUITE("usd_edits_serialization")
{
    template <class TEdit, class... TArgs>
    bool check_usd_edit_serialization(TArgs && ... args)
    {
        TEdit expected(std::string("anon:1234155462"), std::forward<TArgs>(args)...);
        auto buffer = expected.write();
        auto actual_base = UsdEditBase::read(buffer);
        auto actual = dynamic_cast<TEdit*>(actual_base.get());
        return expected == *actual;
    }

    DOCTEST_TEST_CASE("usd_edits_serialization")
    {
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditSetField>(SdfPath("/test_prim.radius"), TfToken("default"), VtValue(5.0f)));
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditSetFieldDictValueByKey>(SdfPath("/test_prim.radius"), SdfFieldKeys->CustomData,
                                                                                  TfToken("in"), VtValue(42)));
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditSetTimesample>(SdfPath("/test_prim.radius"), 2.25, VtValue(47.52f)));
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditMoveSpec>(SdfPath("/test_prim"), SdfPath("/test_prim/test_prim2")));
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditCreateSpec>(SdfPath("/test_prim"), SdfSpecType::SdfSpecTypePrim, true));
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditDeleteSpec>(SdfPath("/test_prim"), false));
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditPushChild>(SdfPath("/"), TfToken("primChildren"), TfToken("test_prim2")));
        DOCTEST_CHECK(check_usd_edit_serialization<UsdEditPopChild>(SdfPath("/"), TfToken("primChildren"), TfToken("test_prim2")));
    }
}

OPENDCC_NAMESPACE_CLOSE
