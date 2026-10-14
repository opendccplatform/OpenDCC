// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/command_utils.h"
#include "opendcc/base/logging/logger.h"
#include <stdexcept>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/primSpec.h>
#include <regex>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/transform.h>
#include <pxr/usd/sdf/relationshipSpec.h>
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/core/undo/router.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/pcp/node.h"
#include <filesystem>
#include <pxr/usd/sdf/fileFormat.h>
#include <pxr/usd/sdf/copyUtils.h>

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.commands");

PXR_NAMESPACE_USING_DIRECTIVE;

TfToken commands::utils::get_new_name_for_prim(const TfToken& name_candidate, const UsdPrim& parent_prim, const SdfPathVector& additional_paths)
{
    if (!parent_prim)
        return TfToken();
#if PXR_VERSION < 2108
    TfTokenVector existing_names;
    for (const auto& child : parent_prim.GetAllChildren())
        existing_names.push_back(child.GetName());
#else
    auto existing_names = parent_prim.GetAllChildrenNames();
#endif
    for (const auto path : additional_paths)
    {
        if (path.GetParentPath() == parent_prim.GetPath())
            existing_names.push_back(path.GetNameToken());
    }

    return get_new_name(name_candidate, existing_names);
}

PXR_NS::TfToken commands::utils::get_new_name(const PXR_NS::TfToken& name_candidate, const PXR_NS::TfTokenVector& existing_names)
{
    auto iter_in_reparented = std::find(existing_names.begin(), existing_names.end(), name_candidate);
    if (iter_in_reparented == existing_names.end())
        return name_candidate;

    std::regex expr(R"(\d*$)");
    const auto& target_prim_name = name_candidate.GetString();
    std::smatch sm;
    std::regex_search(target_prim_name, sm, expr);
    const int num_start_index = sm.position();

    std::vector<std::string> reserved_names;
    for (const auto& name : existing_names)
    {
        const auto& name_str = name.GetString();
        std::regex_search(name.GetString(), sm, expr);
        if (name_str.compare(0, sm.position(), name_candidate.GetString(), 0, num_start_index) == 0)
            reserved_names.push_back(name_str.substr(num_start_index));
    }

    int padding = 0;
    const auto pad_end = target_prim_name.find_first_not_of('0', num_start_index);
    if (pad_end == -1)
        padding = target_prim_name.back() == '0' ? target_prim_name.size() - num_start_index - 1 : 0;
    else
        padding = pad_end - num_start_index;

    unsigned long long max_num;
    std::string num_str = std::string(padding, '0');
    try
    {
        max_num = std::stoull(name_candidate.GetText() + num_start_index);
        num_str += std::to_string(max_num);
    }
    catch (const std::logic_error&)
    {
        num_str += "1";
        max_num = 1;
    }

    while (true)
    {
        bool has_duplicate = false;
        for (int i = 0; i < reserved_names.size() && !has_duplicate; i++)
        {
            if (reserved_names[i] == num_str)
                has_duplicate = true;
        }

        if (!has_duplicate)
            break;

        ++max_num;
        auto new_num_str = std::string(padding, '0') + std::to_string(max_num);
        if (new_num_str.size() > num_str.size() && padding != 0)
        {
            num_str = new_num_str.substr(1);
            padding--;
        }
        else
        {
            num_str = new_num_str;
        }
    };
    return TfToken(name_candidate.GetString().substr(0, num_start_index) + num_str);
}

void commands::utils::rename_targets(const UsdStageRefPtr& stage, const SdfPath& old_path, const SdfPath& new_path)
{
    SdfPath prim_path = old_path;
    std::function<void(const SdfPrimSpecHandle&, const SdfPath&)> traverse = [&](const SdfPrimSpecHandle& prim, const SdfPath& new_path) {
        const auto& layer = stage->GetEditTarget().GetLayer();
        for (const auto& child : prim->GetNameChildren())
        {
            traverse(child, new_path);
        }

        for (const auto& relationship : prim->GetRelationships())
        {
            auto targets = relationship->GetTargetPathList();
            for (auto& target_index : targets.GetAddedOrExplicitItems())
            {
                auto target = (SdfPathKeyPolicy::value_type)target_index;
                if (target.HasPrefix(prim_path))
                {
                    targets.ReplaceItemEdits(target, target.ReplacePrefix(prim_path, new_path));
                }
            }
        }

        for (const auto& attribute : prim->GetAttributes())
        {
            auto connections = attribute->GetConnectionPathList();
            for (const auto& connection : connections.GetAddedOrExplicitItems())
            {
                auto target = (SdfPathKeyPolicy::value_type)connection;
                if (target.HasPrefix(prim_path))
                {
                    connections.ReplaceItemEdits(target, target.ReplacePrefix(prim_path, new_path));
                }
            }
        }
    };

    traverse(stage->GetEditTarget().GetLayer()->GetPseudoRoot(), new_path);
}

void commands::utils::delete_targets(const PXR_NS::UsdStageRefPtr& stage, const PXR_NS::SdfPath& remove_path)
{
    SdfPath prim_path = remove_path;
    std::function<void(const SdfPrimSpecHandle&, const SdfPath&)> traverse = [&](const SdfPrimSpecHandle& prim, const SdfPath& new_path) {
        const auto& layer = stage->GetEditTarget().GetLayer();
        for (const auto& child : prim->GetNameChildren())
        {
            traverse(child, new_path);
        }

        for (const auto& relationship : prim->GetRelationships())
        {
            auto targets = relationship->GetTargetPathList();
            for (auto& target_index : targets.GetAddedOrExplicitItems())
            {
                auto target = (SdfPathKeyPolicy::value_type)target_index;
                if (target.HasPrefix(prim_path))
                {
                    targets.RemoveItemEdits(target);
                }
            }
        }

        for (const auto& attribute : prim->GetAttributes())
        {
            auto connections = attribute->GetConnectionPathList();
            for (const auto& connection : connections.GetAddedOrExplicitItems())
            {
                auto target = (SdfPathKeyPolicy::value_type)connection;
                if (target.HasPrefix(prim_path))
                {
                    connections.RemoveItemEdits(target);
                }
            }
        }
    };

    traverse(stage->GetEditTarget().GetLayer()->GetPseudoRoot(), remove_path);
}

void commands::utils::preserve_transform(const UsdPrim& prim, const UsdPrim& parent)
{
    auto current_id = Application::instance().get_session()->get_current_stage_id();
    auto xformable_prim = UsdGeomXformable(prim);

    if (!xformable_prim || !parent || xformable_prim.TransformMightBeTimeVarying())
    {
        return;
    }

    GfMatrix4d inverse_parent_transform;
    if (auto xform_parent = UsdGeomXformable(parent))
        inverse_parent_transform = xform_parent.ComputeLocalToWorldTransform(Application::instance().get_current_time()).GetInverse();
    else
        inverse_parent_transform.SetIdentity();

    auto world_prim_transform = xformable_prim.ComputeLocalToWorldTransform(Application::instance().get_current_time());
    auto new_prim_local_transform = world_prim_transform * inverse_parent_transform;

    UsdGeomXformCommonAPI xform_common_api(xformable_prim);
    GfVec3d translation;
    GfVec3f rotation, scale, pivot;
    UsdGeomXformCommonAPI::RotationOrder rotation_order;
    if (!xform_common_api.GetXformVectorsByAccumulation(&translation, &rotation, &scale, &pivot, &rotation_order, UsdTimeCode::Default()))
    {
        xformable_prim.MakeMatrixXform().Set(new_prim_local_transform);
        return;
    }
    GfTransform transform;
    transform.SetPivotPosition(pivot);
    transform.SetMatrix(new_prim_local_transform);
    if (GfIsClose(transform.GetPivotOrientation().GetAngle(), 0, 0.001))
    {
        xformable_prim.ClearXformOpOrder();

        translation = transform.GetTranslation();
        rotation = GfVec3f(transform.GetRotation().Decompose(GfVec3d::ZAxis(), GfVec3d::YAxis(), GfVec3d::XAxis()));
        scale = GfVec3f(transform.GetScale());

        xform_common_api = UsdGeomXformCommonAPI(xformable_prim);
        if (!GfIsClose(translation, GfVec3f(0), 0.0001))
        {
            xform_common_api.SetTranslate(translation);
        }
        if (!GfIsClose(rotation, GfVec3f(0), 0.0001))
        {
            xform_common_api.SetRotate({ rotation[2], rotation[1], rotation[0] }, rotation_order);
        }
        if (!GfIsClose(scale, GfVec3f(1), 0.0001))
        {
            xform_common_api.SetScale(scale);
        }
        if (!GfIsClose(pivot, GfVec3f(0), 0.0001))
        {
            xform_common_api.SetPivot(pivot);
        }
    }
    else
    {
        xformable_prim.MakeMatrixXform().Set(new_prim_local_transform);
    }
}

SdfPath commands::utils::get_common_parent(const SdfPathVector& paths)
{
    if (paths.empty())
    {
        return SdfPath::AbsoluteRootPath();
    }

    const auto& parent = paths[0].GetParentPath();
    for (const auto& path : paths)
    {
        if (path.GetParentPath() != parent)
        {
            return SdfPath::AbsoluteRootPath();
        }
    }
    return parent;
}

void commands::utils::apply_schema_to_spec(const std::string& schema_name, const std::vector<SdfPrimSpecHandle>& prim_specs)
{
    auto schema_type = TfType::FindByName(schema_name);

    if (!schema_type)
    {
        OPENDCC_ERROR("Failed to apply schema '{}': schema doesn't exist.", schema_name.c_str());
        throw std::runtime_error("Failed to apply schema: schema doesn't exist.");
    }
#if PXR_VERSION >= 2005
    auto schema_type_name = UsdSchemaRegistry::GetInstance().GetSchemaTypeName(schema_type);
    if (schema_type_name.IsEmpty())
    {
        OPENDCC_ERROR("Failed to apply schema '{}': prim definition doesn't exist.", schema_name.c_str());
        throw std::runtime_error("Failed to apply schema: prim definition doesn't exist.");
    }
#else
    auto schema_spec = UsdSchemaRegistry::GetPrimDefinition(TfToken(schema_name));
    if (!schema_spec)
    {
        OPENDCC_ERROR("Failed to apply schema '{}': prim definition doesn't exist.", schema_name.c_str());
        throw std::runtime_error("Failed to apply schema: prim definition doesn't exist.");
    }
#endif
    for (auto& prim_spec : prim_specs)
    {
        auto item_list = prim_spec->GetInfo(TfToken("apiSchemas")).Get<SdfTokenListOp>();
        auto items = item_list.GetPrependedItems();
        items.push_back(TfToken(schema_name));
        item_list.SetPrependedItems(items);
        prim_spec->SetInfo(TfToken("apiSchemas"), VtValue(item_list));
    }
}

void commands::utils::select_prims(const SdfPathVector& new_selection)
{
    const auto& cur_selection = Application::instance().get_prim_selection();

    class SelectionInverse : public commands::Edit
    {
    private:
        SdfPathVector m_old_selection;
        SdfPathVector m_new_selection;

    public:
        SelectionInverse(const SdfPathVector& old_selection, const SdfPathVector& new_selection)
            : m_old_selection(old_selection)
            , m_new_selection(new_selection)
        {
        }
        virtual bool operator()() override
        {
            Application::instance().set_prim_selection(m_old_selection);
            commands::UndoRouter::add_inverse(std::make_shared<SelectionInverse>(m_new_selection, m_old_selection));
            return true;
        }

        virtual bool merge_with(const Edit* other) override { return true; }

        virtual size_t get_edit_type_id() const override { return commands::get_edit_type_id<SelectionInverse>(); }
    };
    Application::instance().set_prim_selection(new_selection);
    commands::UndoRouter::add_inverse(std::make_shared<SelectionInverse>(cur_selection, new_selection));
}

void commands::utils::flatten_prim(const PXR_NS::UsdPrim& src_prim, const PXR_NS::SdfPath& dst_path, const PXR_NS::SdfLayerHandle layer,
                                   bool copy_children)
{
    const auto path = src_prim.GetPath();
    // const auto layer = src_prim.GetStage()->GetEditTarget().GetLayer();

    auto flatten_stage = UsdStage::CreateInMemory();
    auto check_ancestral = [](const UsdPrim& prim) {
        std::function<bool(const PcpNodeRef&)> check_node = [&check_node](const PcpNodeRef& node) {
            auto result = node.IsDueToAncestor();
            if (!result)
            {
                auto range = node.GetChildrenRange();
                for (auto it = range.first; it != range.second; ++it)
                {
                    result |= check_node(*it);
                    if (result)
                        break;
                }
            }
            return result;
        };

        return check_node(prim.GetPrimIndex().GetRootNode());
    };

    SdfPrimSpecHandleVector prim_stacks;
    if (check_ancestral(src_prim))
    {
        prim_stacks = src_prim.GetPrimStack();
    }
    else
    {
        for (const auto& l : src_prim.GetStage()->GetLayerStack())
        {
            if (auto spec = l->GetPrimAtPath(src_prim.GetPath()))
                prim_stacks.push_back(spec);
        }
    }

    for (const auto& spec : prim_stacks)
    {
        auto src_layer = spec->GetLayer();
        SdfLayerRefPtr dst_layer = nullptr;
        if (src_layer->IsAnonymous())
        {
            dst_layer = SdfLayer::CreateAnonymous();
        }
        else
        {
            namespace fs = std::filesystem;
            const auto& real_path = src_layer->GetRealPath();
            const auto fs_path = fs::u8path(real_path);
            size_t counter = 0;
            while (!dst_layer)
            {
                const auto tmp_file_name =
                    fs_path.parent_path() / fs::u8path(fs_path.stem().u8string() + std::to_string(counter) + fs_path.extension().u8string());
                counter++;

                const auto tmp_file_name_str = tmp_file_name.u8string();
                auto test_tmp = SdfLayer::FindOrOpen(tmp_file_name_str);
                if (test_tmp)
                    continue;
                auto format = SdfFileFormat::FindByExtension(fs_path.extension().u8string());
                dst_layer = SdfLayer::New(format, tmp_file_name_str);
            }
        }

        SdfCreatePrimInLayer(dst_layer, src_prim.GetPrimPath());
        if (copy_children)
        {
            SdfCopySpec(src_layer, spec->GetPath(), dst_layer, src_prim.GetPrimPath());
        }
        else
        {
            copy_spec_without_children(src_layer, spec->GetPath(), dst_layer, src_prim.GetPrimPath());
        }
        flatten_stage->GetRootLayer()->GetSubLayerPaths().push_back(dst_layer->GetIdentifier());
    }
    auto flatten_layer = flatten_stage->Flatten();
    SdfCreatePrimInLayer(layer, dst_path);
    if (copy_children)
    {
        SdfCopySpec(flatten_layer, src_prim.GetPath(), layer, dst_path);
    }
    else
    {
        copy_spec_without_children(flatten_layer, src_prim.GetPath(), layer, dst_path);
    }
}

bool commands::utils::copy_spec_without_children(const PXR_NS::SdfLayerHandle& srcLayer, const PXR_NS::SdfPath& srcPath,
                                                 const PXR_NS::SdfLayerHandle& dstLayer, const PXR_NS::SdfPath& dstPath)
{
#if PXR_VERSION >= 2405
    using Optional = std::optional<VtValue>;
#else
    using Optional = boost::optional<VtValue>;
#endif
    return SdfCopySpec(
        srcLayer, srcPath, dstLayer, dstPath,
        [](SdfSpecType specType, const TfToken& field, const SdfLayerHandle& srcLayer, const SdfPath& srcPath, bool fieldInSrc,
           const SdfLayerHandle& dstLayer, const SdfPath& dstPath, bool fieldInDst, Optional* valueToCopy) {
            return SdfShouldCopyValue(srcPath, dstPath, specType, field, srcLayer, srcPath, fieldInSrc, dstLayer, dstPath, fieldInDst, valueToCopy);
        },
        [](const TfToken& childrenField, const SdfLayerHandle& srcLayer, const SdfPath& srcPath, bool fieldInSrc, const SdfLayerHandle& dstLayer,
           const SdfPath& dstPath, bool fieldInDst, Optional* srcChildren, Optional* dstChildren) {
            if (childrenField == "properties")
                return true;
            else
                return false;
        });
}

OPENDCC_NAMESPACE_CLOSE
