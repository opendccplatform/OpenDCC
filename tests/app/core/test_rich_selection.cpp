/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/rich_selection.h"
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xform.h>
#include <algorithm>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    struct RichSelectionFixture
    {
        Application& app = Application::instance();
        UsdStageRefPtr previous_stage = app.get_session()->get_current_stage();
        bool had_radius = app.get_settings()->has("soft_selection.falloff_radius");
        float previous_radius = app.get_settings()->get("soft_selection.falloff_radius", 5.0f);
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        UsdGeomXform parent = UsdGeomXform::Define(stage, SdfPath("/World"));
        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/World/Mesh"));

        RichSelectionFixture()
        {
            mesh.CreatePointsAttr().Set(VtVec3fArray { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 }, { 4, 0, 0 } });
            mesh.CreateFaceVertexCountsAttr().Set(VtIntArray { 4 });
            mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray { 0, 1, 2, 3 });
            app.get_session()->set_current_stage(stage);
            app.get_settings()->set("soft_selection.falloff_radius", 2.0f);
        }

        ~RichSelectionFixture()
        {
            app.get_session()->close_stage(stage);
            if (previous_stage)
                app.get_session()->set_current_stage(previous_stage);
            if (had_radius)
                app.get_settings()->set("soft_selection.falloff_radius", previous_radius);
            else
                app.get_settings()->remove("soft_selection.falloff_radius");
        }

        RichSelection make_rich_selection(const SelectionList& selection)
        {
            RichSelection rich([](float distance) {
                const float radius = Application::instance().get_settings()->get("soft_selection.falloff_radius", 2.0f);
                return distance == 0 ? 1.0f : std::max(0.0f, 1.0f - distance / radius);
            });
            rich.set_soft_selection(selection);
            return rich;
        }
    };
}

DOCTEST_TEST_SUITE("RichSelection")
{
    DOCTEST_TEST_CASE_FIXTURE(RichSelectionFixture, "point_weights_and_radius")
    {
        SelectionList selection;
        selection.add_points(mesh.GetPath(), VtIntArray { 0 });
        auto rich = make_rich_selection(selection);
        const auto& weights = rich.get_weights(mesh.GetPath());
        DOCTEST_REQUIRE(weights.size() == 2);
        DOCTEST_CHECK(weights.at(0) == doctest::Approx(1.0f));
        DOCTEST_CHECK(weights.at(1) == doctest::Approx(0.5f));
        // The boundary has zero influence; points beyond it are excluded too.
        DOCTEST_CHECK(weights.count(2) == 0);
        DOCTEST_CHECK(weights.count(3) == 0);
        DOCTEST_CHECK(rich.get_weights(SdfPath("/Missing")).empty());
        app.get_settings()->set("soft_selection.falloff_radius", 0.0f);
        rich.update();
        DOCTEST_REQUIRE(rich.get_weights(mesh.GetPath()).size() == 1);
        DOCTEST_CHECK(rich.get_weights(mesh.GetPath()).at(0) == doctest::Approx(1.0f));
    }

    DOCTEST_TEST_CASE_FIXTURE(RichSelectionFixture, "nearest_selected_point")
    {
        SelectionList selection;
        selection.add_points(mesh.GetPath(), VtIntArray { 0, 2 });
        auto rich = make_rich_selection(selection);
        const auto& weights = rich.get_weights(mesh.GetPath());
        DOCTEST_REQUIRE(weights.size() == 3);
        DOCTEST_CHECK(weights.at(0) == doctest::Approx(1.0f));
        DOCTEST_CHECK(weights.at(1) == doctest::Approx(0.5f));
        DOCTEST_CHECK(weights.at(2) == doctest::Approx(1.0f));
        DOCTEST_CHECK(weights.count(3) == 0);
    }

    DOCTEST_TEST_CASE_FIXTURE(RichSelectionFixture, "edge_and_face_seeds")
    {
        SelectionList selection;
        DOCTEST_SUBCASE("edge")
        {
            selection.add_edges(mesh.GetPath(), VtIntArray { 0 });
        }
        DOCTEST_SUBCASE("face")
        {
            selection.add_elements(mesh.GetPath(), VtIntArray { 0 });
        }
        auto rich = make_rich_selection(selection);
        const auto& weights = rich.get_weights(mesh.GetPath());
        if (!selection.get_selection_data(mesh.GetPath()).get_edge_indices().empty())
        {
            DOCTEST_REQUIRE(weights.size() == 3);
            DOCTEST_CHECK(weights.at(0) == doctest::Approx(1.0f));
            DOCTEST_CHECK(weights.at(1) == doctest::Approx(1.0f));
            DOCTEST_CHECK(weights.at(2) == doctest::Approx(0.5f));
        }
        else
        {
            DOCTEST_REQUIRE(weights.size() == 4);
            for (const auto& weight : weights)
                DOCTEST_CHECK(weight.second == doctest::Approx(1.0f));
        }
    }

    DOCTEST_TEST_CASE_FIXTURE(RichSelectionFixture, "geometry_and_transform_updates")
    {
        SelectionList selection;
        selection.add_points(mesh.GetPath(), VtIntArray { 0 });
        auto rich = make_rich_selection(selection);
        // Falloff is measured in world space, including ancestor transforms.
        parent.AddScaleOp().Set(GfVec3f(2));
        rich.update();
        DOCTEST_REQUIRE(rich.get_weights(mesh.GetPath()).size() == 1);
        mesh.GetPointsAttr().Set(VtVec3fArray { { 0, 0, 0 }, { 0.25f, 0, 0 }, { 2, 0, 0 }, { 4, 0, 0 } });
        rich.update();
        DOCTEST_REQUIRE(rich.get_weights(mesh.GetPath()).size() == 2);
        DOCTEST_CHECK(rich.get_weights(mesh.GetPath()).at(1) == doctest::Approx(0.75f));
    }

    DOCTEST_TEST_CASE_FIXTURE(RichSelectionFixture, "replace_clear_and_color")
    {
        SelectionList selection;
        selection.add_points(mesh.GetPath(), VtIntArray { 0 });
        auto rich = make_rich_selection(selection);
        DOCTEST_CHECK_FALSE(rich.has_color_data());
        DOCTEST_CHECK(rich.get_soft_selection_color(0.5f) == GfVec3f(0));
        selection.clear();
        selection.add_points(mesh.GetPath(), VtIntArray { 3 });
        rich.set_soft_selection(selection);
        DOCTEST_REQUIRE(rich.get_weights(mesh.GetPath()).size() == 1);
        DOCTEST_CHECK(rich.get_weights(mesh.GetPath()).at(3) == doctest::Approx(1.0f));
        rich.set_soft_selection(SelectionList());
        DOCTEST_CHECK(rich.begin() == rich.end());
        rich.set_soft_selection(selection);
        rich.clear();
        DOCTEST_CHECK(rich.get_selection_list().empty());
        DOCTEST_CHECK(rich.begin() == rich.end());
        RichSelection colored([](float) { return 1.0f; }, [](float weight) { return GfVec3f(weight, 0, 1 - weight); });
        DOCTEST_CHECK(colored.has_color_data());
        DOCTEST_CHECK(colored.get_soft_selection_color(0.25f) == GfVec3f(0.25f, 0, 0.75f));
    }
}
OPENDCC_NAMESPACE_CLOSE
