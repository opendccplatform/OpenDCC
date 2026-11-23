/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/ui/scene_outliner/tree_data_source.h"
#include <map>
#include <utility>

OPENDCC_NAMESPACE_OPEN

namespace scene_outliner_tests
{
    class TestId : public SceneItemId::Data
    {
    public:
        explicit TestId(std::string path)
            : path(std::move(path))
        {
        }
        std::string type_id() const override { return "test"; }
        size_t hash() const override { return std::hash<std::string>()(path); }
        bool equals(const Data& other) const override { return path == static_cast<const TestId&>(other).path; }
        std::string path;
    };
    inline SceneItemId id(const std::string& path)
    {
        return make_scene_item_id<TestId>(path);
    }

    class TestSource : public ISceneOutlinerDataSource
    {
    public:
        std::map<std::string, std::vector<std::string>> tree { { "/", { "/A", "/B" } }, { "/A", { "/A/leaf" } }, { "/B", {} }, { "/A/leaf", {} } };
        mutable std::map<std::string, int> queries;
        SceneItemList roots() const override
        {
            SceneItemList out;
            out.push_back(id("/"));
            return out;
        }
        SceneItemList children(const SceneItemId& item) const override
        {
            SceneItemList out;

            const auto path = scene_item_data_cast<TestId>(item)->path;
            ++queries[path];
            const auto found = tree.find(path);
            if (found != tree.end())
                for (const auto& child : found->second)
                    out.push_back(id(child));

            return out;
        }
        SceneItemId parent(const SceneItemId& item) const override
        {
            const auto path = scene_item_data_cast<TestId>(item)->path;
            if (path == "/")
                return {};
            const auto slash = path.find_last_of('/');
            return id(slash == 0 ? "/" : path.substr(0, slash));
        }
        bool has_item(const SceneItemId& item) const override
        {
            const auto* data = scene_item_data_cast<TestId>(item);
            return data && tree.count(data->path) != 0;
        }
        std::string display_name(const SceneItemId& item) const override { return scene_item_data_cast<TestId>(item)->path; }
        SceneOutlinerColumnList columns() const override
        {
            SceneOutlinerColumnList out;

            SceneOutlinerColumn name;
            name.id = "name";
            name.title = QStringLiteral("Name");
            name.role = SceneOutlinerColumn::Role::TREE;
            name.stretch = true;
            out.push_back(name);
            SceneOutlinerColumn type;
            type.id = "type";
            type.title = QStringLiteral("Type");
            out.push_back(type);

            return out;
        }
        std::string column_text(const SceneItemId& item, const std::string& column) const override
        {
            return column == "name" ? display_name(item) : type_name(item);
        }
    };

}

OPENDCC_NAMESPACE_CLOSE
