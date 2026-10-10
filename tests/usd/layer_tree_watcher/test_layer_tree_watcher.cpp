// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/layer_tree_watcher/layer_tree_watcher.h"
#include <pxr/usd/usd/stage.h>
#include <fstream>

#include <opendcc/base/vendor/ghc/filesystem.hpp>
#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

class SetUp
{
private:
    static ghc::filesystem::path m_tmp_dir;

public:
    SetUp()
    {
        m_tmp_dir = ghc::filesystem::temp_directory_path() / "layer_tree_tests";
        ghc::filesystem::remove_all(m_tmp_dir);

        ghc::filesystem::create_directory(m_tmp_dir);
        ghc::filesystem::create_directory(m_tmp_dir / "sub");
        ghc::filesystem::create_directory(m_tmp_dir / "sub/directory");

        std::ofstream(m_tmp_dir.generic_string() + "/empty.usda") << "#usda 1.0";
        std::ofstream(m_tmp_dir.generic_string() + "/empty2.usda") << "#usda 1.0";
        std::ofstream(m_tmp_dir.generic_string() + "/rel.usda") << "#usda 1.0\n(subLayers = [@./empty.usda@])";
        std::ofstream(m_tmp_dir.generic_string() + "/abs.usda")
            << "#usda 1.0\n(subLayers = [@" << (m_tmp_dir / "empty.usda").generic_string() << "@])";
        std::ofstream(m_tmp_dir.generic_string() + "/sub/directory/subdir.usda") << "#usda 1.0\n(subLayers = [@../../empty2.usda@])";
    }
    ~SetUp() { ghc::filesystem::remove_all(m_tmp_dir); }

    static std::string get_tmp_dir()
    {
        std::string result = m_tmp_dir.generic_string();
        return result;
    }
};

ghc::filesystem::path SetUp::m_tmp_dir;

DOCTEST_TEST_SUITE("LayerTreeWatcherTests")
{
    DOCTEST_TEST_CASE("empty_tree")
    {
        auto stage = UsdStage::CreateInMemory();
        LayerTreeWatcher layer_tree_watcher(stage);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 2);
    }

    DOCTEST_TEST_CASE("init_with_anon_sublayers")
    {
        auto sublayer1 = SdfLayer::CreateAnonymous();
        auto sublayer2 = SdfLayer::CreateAnonymous();
        auto stage = UsdStage::CreateInMemory();
        stage->GetRootLayer()->InsertSubLayerPath(sublayer1->GetIdentifier());
        stage->GetRootLayer()->InsertSubLayerPath(sublayer2->GetIdentifier());
        LayerTreeWatcher layer_tree_watcher(stage);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 4);
        DOCTEST_CHECK(layer_tree_watcher.contains(sublayer1));
        DOCTEST_CHECK(layer_tree_watcher.contains(sublayer2));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "init_with_existing_sublayers")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/abs.usda");
        LayerTreeWatcher layer_tree_watcher(stage);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/empty.usda")));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "init_with_nonexported_sublayers")
    {
        auto new_layer = SdfLayer::CreateNew(SetUp::get_tmp_dir() + "/nonexported.usda");
        auto stage = UsdStage::CreateInMemory();
        stage->GetRootLayer()->InsertSubLayerPath(new_layer->GetIdentifier());
        LayerTreeWatcher layer_tree_watcher(stage);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/nonexported.usda")));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "init_with_rel_sublayers")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/rel.usda");
        LayerTreeWatcher layer_tree_watcher(stage);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/empty.usda")));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "init_with_rel_sublayer_in_subdir")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/sub/directory/subdir.usda");
        LayerTreeWatcher layer_tree_watcher(stage);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/empty2.usda")));
    }

    DOCTEST_TEST_CASE("add_anon_sublayers_to_root")
    {
        auto sublayer1 = SdfLayer::CreateAnonymous();
        auto sublayer2 = SdfLayer::CreateAnonymous();
        auto stage = UsdStage::CreateInMemory();

        LayerTreeWatcher layer_tree_watcher(stage);
        stage->GetRootLayer()->InsertSubLayerPath(sublayer1->GetIdentifier());
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(sublayer1));

        stage->GetRootLayer()->InsertSubLayerPath(sublayer2->GetIdentifier());
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 4);
        DOCTEST_CHECK(layer_tree_watcher.contains(sublayer2));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "add_anon_sublayers_to_existing_stage_root")
    {
        auto sublayer1 = SdfLayer::CreateAnonymous();
        auto sublayer2 = SdfLayer::CreateAnonymous();
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/empty.usda");

        LayerTreeWatcher layer_tree_watcher(stage);
        stage->GetRootLayer()->InsertSubLayerPath(sublayer1->GetIdentifier());
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(sublayer1));

        stage->GetRootLayer()->InsertSubLayerPath(sublayer2->GetIdentifier());
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 4);
        DOCTEST_CHECK(layer_tree_watcher.contains(sublayer2));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "add_existing_sublayers_to_root")
    {
        auto stage = UsdStage::CreateInMemory();

        LayerTreeWatcher layer_tree_watcher(stage);
        auto sublayer = SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/empty.usda");
        stage->GetRootLayer()->InsertSubLayerPath(sublayer->GetIdentifier());
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(sublayer));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "add_nonexported_sublayers_to_root")
    {
        auto stage = UsdStage::CreateInMemory();

        LayerTreeWatcher layer_tree_watcher(stage);
        auto new_layer = SdfLayer::CreateNew(SetUp::get_tmp_dir() + "/nonexported.usda");

        stage->GetRootLayer()->InsertSubLayerPath(new_layer->GetIdentifier());
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(new_layer));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "add_from_subdirectory")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/empty.usda");
        LayerTreeWatcher layer_tree_watcher(stage);
        stage->GetRootLayer()->InsertSubLayerPath("./sub/directory/subdir.usda");

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 4);
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/sub/directory/subdir.usda")));
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/empty2.usda")));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "add_to_session_layer")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/empty.usda");

        LayerTreeWatcher layer_tree_watcher(stage);
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 2);
        stage->GetSessionLayer()->InsertSubLayerPath(SetUp::get_tmp_dir() + "/empty2.usda");

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::Find(SetUp::get_tmp_dir() + "/empty.usda")));
        DOCTEST_CHECK(layer_tree_watcher.contains(stage->GetSessionLayer()));
        DOCTEST_CHECK(layer_tree_watcher.contains(SdfLayer::Find(stage->GetSessionLayer()->GetSubLayerPaths().front())));
    }

    DOCTEST_TEST_CASE("remove_anon_sublayers_from_root")
    {
        auto sublayer1 = SdfLayer::CreateAnonymous();
        auto sublayer2 = SdfLayer::CreateAnonymous();
        auto stage = UsdStage::CreateInMemory();
        stage->GetRootLayer()->InsertSubLayerPath(sublayer1->GetIdentifier());
        stage->GetRootLayer()->InsertSubLayerPath(sublayer2->GetIdentifier());

        LayerTreeWatcher layer_tree_watcher(stage);
        stage->GetRootLayer()->RemoveSubLayerPath(0);
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        DOCTEST_CHECK(!layer_tree_watcher.contains(sublayer1));

        stage->GetRootLayer()->RemoveSubLayerPath(0);
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 2);
        DOCTEST_CHECK(!layer_tree_watcher.contains(sublayer2));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "remove_existing_sublayers_from_root")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/abs.usda");

        LayerTreeWatcher layer_tree_watcher(stage);
        auto sublayer = SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/empty.usda");
        stage->GetRootLayer()->RemoveSubLayerPath(0);
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 2);
        DOCTEST_CHECK(!layer_tree_watcher.contains(sublayer));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "remove_nonexported_sublayers_from_root")
    {
        auto stage = UsdStage::CreateInMemory();
        auto new_layer = SdfLayer::CreateNew(SetUp::get_tmp_dir() + "/nonexported.usda");
        stage->GetRootLayer()->InsertSubLayerPath(new_layer->GetIdentifier());

        LayerTreeWatcher layer_tree_watcher(stage);
        stage->GetRootLayer()->RemoveSubLayerPath(0);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 2);
        DOCTEST_CHECK(!layer_tree_watcher.contains(new_layer));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "remove_from_subdirectory")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/empty.usda");
        stage->GetRootLayer()->InsertSubLayerPath("./sub/directory/subdir.usda");
        LayerTreeWatcher layer_tree_watcher(stage);
        stage->GetRootLayer()->RemoveSubLayerPath(0);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 2);
        DOCTEST_CHECK(layer_tree_watcher.contains(stage->GetRootLayer()));
    }

    DOCTEST_TEST_CASE_FIXTURE(SetUp, "remove_from_session_layer")
    {
        auto stage = UsdStage::Open(SetUp::get_tmp_dir() + "/empty.usda");
        stage->GetSessionLayer()->InsertSubLayerPath(SetUp::get_tmp_dir() + "/empty2.usda");

        LayerTreeWatcher layer_tree_watcher(stage);
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 3);
        auto sublayer = SdfLayer::FindOrOpen(SetUp::get_tmp_dir() + "/empty2.usda");
        stage->GetSessionLayer()->RemoveSubLayerPath(0);

        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 2);
        DOCTEST_CHECK(!layer_tree_watcher.contains(sublayer));
    }

    DOCTEST_TEST_CASE("change_layer_identifier")
    {
        auto layer = SdfLayer::CreateAnonymous();
        auto child_layer = SdfLayer::CreateAnonymous();
        layer->InsertSubLayerPath(child_layer->GetIdentifier());
        auto stage = UsdStage::CreateInMemory();

        LayerTreeWatcher layer_tree_watcher(stage);
        stage->GetRootLayer()->InsertSubLayerPath(layer->GetIdentifier());
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 4);
        DOCTEST_CHECK(layer_tree_watcher.contains(layer));
        DOCTEST_CHECK(layer_tree_watcher.contains(child_layer));

        auto old_identifier = layer->GetIdentifier();
        layer->SetIdentifier(SetUp::get_tmp_dir() + "/temp.usda");
        auto new_identifier = layer->GetIdentifier();
        DOCTEST_CHECK(layer_tree_watcher.get_all_layers().size() == 4);
        DOCTEST_CHECK(layer_tree_watcher.contains(layer));
        DOCTEST_CHECK(layer_tree_watcher.contains(child_layer));

        DOCTEST_CHECK(!layer_tree_watcher.contains(old_identifier));
        DOCTEST_CHECK(layer_tree_watcher.contains(layer->GetIdentifier()));
        auto child_layers = layer_tree_watcher.get_child_layers(layer->GetIdentifier());
        DOCTEST_CHECK(child_layers.size() == 1);
        DOCTEST_CHECK(*child_layers.begin() == child_layer->GetIdentifier());
        auto root_sublayers = layer_tree_watcher.get_child_layers(stage->GetRootLayer()->GetIdentifier());
        DOCTEST_CHECK(root_sublayers.size() == 1);
        DOCTEST_CHECK(*root_sublayers.begin() == layer->GetIdentifier());
    }
}

OPENDCC_NAMESPACE_CLOSE
