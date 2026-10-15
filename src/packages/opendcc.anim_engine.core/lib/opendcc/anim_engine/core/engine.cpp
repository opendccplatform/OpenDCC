// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include "opendcc/anim_engine/core/engine.h"
#include "opendcc/anim_engine/core/commands.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/session.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/anim_engine/core/session.h"
#include "opendcc/anim_engine/core/utils.h"
#include "opendcc/app/core/undo/block.h"
#include <fstream>
#include <iostream>
#include "opendcc/base/commands_api/core/command_registry.h"
#include "opendcc/anim_engine/schema/tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE;

OPENDCC_NAMESPACE_OPEN

using base::commands::CommandInterface;
using base::commands::CommandArgs;
using base::commands::CommandRegistry;

AnimEngine::AnimEngine(UsdStageRefPtr stage)
{
    m_stage_listener.init(stage, { UsdAnimEngineTokens->anim }, this);
}

AnimEngine::~AnimEngine() {}

void AnimEngine::update(const std::unordered_set<SdfPath, SdfPath::Hash>& attrs_to_update,
                        const std::unordered_set<SdfPath, SdfPath::Hash>& attrs_to_remove)
{
    auto stage = m_stage_listener.stage();
    if (!stage)
    {
        return;
    }

    CurveIdsList curves_for_remove;

    for (auto path : attrs_to_remove)
    {
        auto attr = stage->GetAttributeAtPath(path);
        auto ids = get_ids_for_attr(path);
        for (auto& id : ids)
        {
            curves_for_remove.push_back(id);
        }
    }

    std::vector<AnimEngineCurve> curves_for_create;
    for (auto path : attrs_to_update)
    {
        auto attr = stage->GetAttributeAtPath(path);
        auto existing_curves_ids_for_the_attr = get_ids_for_attr(path);
        auto curves = AnimEngineCurve::create_from_metadata(attr);

        for (size_t i = 0; i < curves.size(); ++i)
        {
            curves_for_create.push_back(std::move(curves[i]));
        }
    }

    remove_curves_direct(curves_for_remove);

    add_curves_direct(curves_for_create, false);
}

const std::set<AnimEngine::CurveId>& AnimEngine::curves(const SdfPath& prim_path) const
{
    static std::set<CurveId> empty;
    auto it = m_prim_path_to_curves_map.find(prim_path);
    if (it != m_prim_path_to_curves_map.end())
    {
        return it->second;
    }
    else
        return empty;
}

std::shared_ptr<const AnimEngineCurve> AnimEngine::get_curve(CurveId curve_id) const
{
    auto it = m_curves.find(curve_id);
    if (it != m_curves.end())
        return it->second;
    else
        return nullptr;
}

AnimEngine::CurveIdToKeysIdsMap AnimEngine::add_keys_direct(const CurveIdToKeyframesMap& keys, bool reset_id, bool send_notification)
{
    if (keys.empty())
        return CurveIdToKeysIdsMap();
    auto mute_scope = m_stage_listener.create_mute_scope();

    CurveIdToKeysIdsMap all_ids;
    for (auto& it : keys)
    {
        CurveId curve_id = it.first;
        auto& curve_ids = all_ids[curve_id];
        ANIM_CURVES_CHECK_AND_CONTINUE(m_curves.find(it.first) != m_curves.end());
        AnimEngineCurve& curve = *m_curves[curve_id];

        for (auto& key : it.second)
        {
            auto key_id = curve.add_key(key, reset_id);
            curve_ids.insert(key_id);
        }
        curve.save_to_attribute_metadata(m_stage_listener, m_save_on_current_layer);
    }
    if (send_notification)
    {
        m_dispatcher_for_keys_update.dispatch(EventType::KEYFRAMES_ADDED, all_ids);
        on_changed();
    }
    return all_ids;
}

void AnimEngine::remove_keys_direct(const CurveIdToKeysIdsMap& ids)
{
    if (ids.empty())
        return;
    auto mute_scope = m_stage_listener.create_mute_scope();
    std::vector<CurveId> curves_to_remove;
    for (auto& it : ids)
    {
        CurveId curve_id = it.first;
        auto& keys_ids = it.second;
        auto curve_it = m_curves.find(curve_id);
        ANIM_CURVES_CHECK_AND_CONTINUE(curve_it != m_curves.end());
        ANIM_CURVES_CHECK_AND_CONTINUE(keys_ids.size() < curve_it->second->keyframeCount());
        curve_it->second->remove_keys_by_ids(keys_ids);
        curve_it->second->save_to_attribute_metadata(m_stage_listener, m_save_on_current_layer);
    }
    m_dispatcher_for_keys_update.dispatch(EventType::KEYFRAMES_REMOVED, ids);
    on_changed();
}

void AnimEngine::create_animation_on_selected_prims(AttributesScope attribute_scope)
{
    SelectionList selection = Application::instance().get_selection();

    auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
        return;
    auto engine = AnimEngineSession::instance().current_engine();
    if (!engine)
        return;

    std::vector<TfToken> attributes_tokens;
    switch (attribute_scope)
    {
    case AnimEngine::AttributesScope::TRANSLATE:
        attributes_tokens = { TfToken("xformOp:translate") };
        break;
    case AnimEngine::AttributesScope::ROTATE:
        attributes_tokens = { TfToken("xformOp:rotateXYZ") };
        break;
    case AnimEngine::AttributesScope::SCALE:
        attributes_tokens = { TfToken("xformOp:scale") };
        break;
    case AnimEngine::AttributesScope::ALL:
        attributes_tokens = { TfToken("xformOp:translate"), TfToken("xformOp:rotateXYZ"), TfToken("xformOp:scale") };
        break;
    default:
        break;
    }

    std::vector<UsdAttribute> attrs;
    std::vector<uint32_t> components;
    CurveIdToKeyframesMap keyframes_list;

    auto layer = stage->GetSessionLayer();
    if (!layer)
        return;

    for (auto it : selection)
    {
        SdfPath path = it.first;

        UsdPrim prim = stage->GetPrimAtPath(path);
        auto xform_api = UsdGeomXformCommonAPI(prim);
        if (!xform_api)
            continue;

        for (TfToken attribute_token : attributes_tokens)
        {
            auto attribute = prim.GetAttribute(attribute_token);

            if (!attribute || !attribute.HasValue() || !attribute.HasAuthoredValue() || !attribute.IsAuthored())
            {
                if (attribute_token == TfToken("xformOp:translate"))
                    xform_api.SetTranslate({ 0, 0, 0 });
                else if (attribute_token == TfToken("xformOp:rotateXYZ"))
                    xform_api.SetRotate({ 0, 0, 0 });
                else if (attribute_token == TfToken("xformOp:scale"))
                    xform_api.SetScale({ 1, 1, 1 });
            }
        }
    }
    {
        UsdEditContext context(stage, layer);

        for (auto it : selection)
        {
            SdfPath path = it.first;

            UsdPrim prim = stage->GetPrimAtPath(path);
            auto xform_api = UsdGeomXformCommonAPI(prim);
            if (!xform_api)
                continue;

            for (TfToken attribute_token : attributes_tokens)
            {
                auto attribute = prim.GetAttribute(attribute_token);

                if (!attribute || attribute.GetNumTimeSamples() > 0)
                    continue;

                for (uint32_t component_idx = 0; component_idx < 3; ++component_idx)
                {
                    auto id_and_curve = engine->id_and_curve(attribute, component_idx);

                    if (id_and_curve.first.valid())
                    {
                        double value = 0;
                        bool ok = get_usd_attribute_component(attribute, component_idx, value);
                        if (!ok)
                            continue;

                        auto curve = id_and_curve.second;
                        adsk::Keyframe key;
                        key.time = Application::instance().get_current_time();
                        key.value = value;
                        key.tanIn.type = adsk::TangentType::Auto;
                        key.tanOut.type = adsk::TangentType::Auto;
                        key.linearInterpolation = false;
                        key.quaternionW = 1.0;
                        key.id = curve->generate_unique_key_id();

                        keyframes_list[id_and_curve.first].push_back(key);
                    }
                    else
                    {
                        attrs.push_back(attribute);
                        components.push_back(component_idx);
                    }
                }
            }
        }
    } // EditContext

    create_animation_curve_and_add_keys(attrs, components, keyframes_list);
    // add_keys();
}

void AnimEngine::set_keys_direct(const CurveIdToKeyframesMap& keys, bool send_notification /*= true*/)
{
    if (keys.empty())
        return;
    auto scope = m_stage_listener.create_mute_scope();
    CurveIdToKeysIdsMap all_ids;
    for (auto& it : keys)
    {
        if (it.second.size() == 0)
            continue;

        CurveId curve_id = it.first;
        auto& curve_ids = all_ids[curve_id];
        AnimEngineCurve& curve = *m_curves[curve_id];
        auto map = curve.compute_id_to_idx_map();
        for (auto& key : it.second)
        {
            curve[map[key.id]] = key;
            curve_ids.insert(key.id);
        }

        curve.compute_tangents();

        curve.save_to_attribute_metadata(m_stage_listener, m_save_on_current_layer);
    }
    if (send_notification)
        m_dispatcher_for_keys_update.dispatch(EventType::KEYFRAMES_CHANGED, all_ids);
    on_changed();
}

void AnimEngine::set_save_on_current_layer(bool save_on_current_layer)
{
    if (save_on_current_layer == m_save_on_current_layer)
    {
        return;
    }
    m_save_on_current_layer = save_on_current_layer;
    AnimEngineOptionChanged::dispatch(AnimEngineOption::isSaveOnCurrentLayer);
}

void AnimEngine::set_infinity_type_direct(const CurveIdsList& curve_ids, adsk::InfinityType infinity, bool is_pre_infinity)
{
    if (curve_ids.empty())
        return;
    auto mute_scope = m_stage_listener.create_mute_scope();

    for (auto curve_id : curve_ids)
    {
        if (is_pre_infinity)
            m_curves[curve_id]->set_pre_infinity_type(infinity);
        else
            m_curves[curve_id]->set_post_infinity_type(infinity);
        m_curves[curve_id]->save_to_attribute_metadata(m_stage_listener, m_save_on_current_layer);
    }

    m_dispatcher_for_curve_update.dispatch(EventType::INFINITY_CHANGED, curve_ids);
    on_changed();
}

void AnimEngine::set_infinity_type_direct(const std::map<AnimEngine::CurveId, adsk::InfinityType>& infinity_values, bool is_pre_infinity)
{
    if (infinity_values.empty())
        return;
    CurveIdsList curve_ids;
    auto mute_scope = m_stage_listener.create_mute_scope();
    for (auto it : infinity_values)
    {
        if (is_pre_infinity)
            m_curves[it.first]->set_pre_infinity_type(it.second);
        else
            m_curves[it.first]->set_post_infinity_type(it.second);
        curve_ids.push_back(it.first);
        m_curves[it.first]->save_to_attribute_metadata(m_stage_listener, m_save_on_current_layer);
    }
    m_dispatcher_for_curve_update.dispatch(EventType::INFINITY_CHANGED, curve_ids);
    on_changed();
}

AnimEngine::CurveId AnimEngine::get_or_generate_id(UsdAttribute attr, uint32_t component) const
{
    return CurveId(attr.GetPath(), component);
}

AnimEngine::CurveIdsList AnimEngine::add_curves_direct(const std::vector<AnimEngineCurve>& curves, bool store_to_stage)
{
    CurveIdsList ids;
    for (size_t i = 0; i < curves.size(); ++i)
    {
        ids.push_back(get_or_generate_id(curves[i].attribute(), curves[i].component_idx()));
    }
    add_curves_direct(curves, ids, store_to_stage);
    return ids;
}

void AnimEngine::add_curves_direct(const std::vector<AnimEngineCurve>& curves, const CurveIdsList& ids, bool store_to_stage)
{
    if (curves.empty())
        return;
    auto mute_scope = m_stage_listener.create_mute_scope();
    ANIM_CURVES_CHECK_AND_RETURN(curves.size() == ids.size());
    CurveIdsList ids_to_create;
    CurveIdsList ids_to_update;
    for (size_t i = 0; i < curves.size(); ++i)
    {
        auto curve_id = ids[i];
        if (m_curves.find(curve_id) == m_curves.end())
        {
            ids_to_create.push_back(curve_id);
        }
        else
        {
            ids_to_update.push_back(curve_id);
        }
        m_curves[curve_id] = std::make_shared<AnimEngineCurve>(curves[i]);
        m_prim_path_to_curves_map[curves[i].attribute().GetPrimPath()].insert(curve_id);
        if (store_to_stage)
        {
            curves[i].save_to_attribute_metadata(m_stage_listener, m_save_on_current_layer);
        }
    }
    if (ids_to_update.size() > 0)
    {
        m_dispatcher_for_curve_update.dispatch(EventType::KEYFRAMES_CHANGED, ids_to_update);
        m_dispatcher_for_curve_update.dispatch(EventType::INFINITY_CHANGED, ids_to_update);
    }
    if (ids_to_create.size() > 0)
    {
        m_dispatcher_for_curve_update.dispatch(EventType::CURVES_ADDED, ids_to_create);
    }
    on_changed();
}

void AnimEngine::remove_curves_direct(const CurveIdsList& ids)
{
    if (ids.empty())
        return;

    for (auto curve_id : ids)
    {
        auto it = m_curves.find(curve_id);
        if (it != m_curves.end())
        {
            m_curves[curve_id]->remove_from_metadata(m_stage_listener);
            auto prim_path = m_curves[curve_id]->attribute().GetPrimPath();
            m_curves.erase(curve_id);
            auto prim_it = m_prim_path_to_curves_map.find(prim_path);
            prim_it->second.erase(curve_id);
            if (prim_it->second.size() == 0)
                m_prim_path_to_curves_map.erase(prim_it);
        }
    }
    m_dispatcher_for_curve_update.dispatch(EventType::CURVES_REMOVED, ids);
}

void AnimEngine::remove_keys(const CurveIdToKeysIdsMap& ids)
{
    if (ids.empty())
        return;
    CommandInterface::execute("anim_engine_remove_curves_and_keys", CommandArgs().kwarg("key_ids", ids));
}

void AnimEngine::remove_curves(const CurveIdsList& ids)
{
    if (ids.empty())
        return;
    CommandInterface::execute("anim_engine_remove_curves_and_keys", CommandArgs().kwarg("curve_ids", ids));
}

void AnimEngine::add_keys(const CurveIdToKeyframesMap& keys)
{
    if (keys.empty())
        return;
    CommandInterface::execute("anim_engine_add_curves_and_keys", CommandArgs().kwarg("keyframes", keys));
}

void AnimEngine::set_infinity_type(const CurveIdsList& curve_ids, adsk::InfinityType infinity, bool is_pre_infinity)
{
    if (curve_ids.empty())
        return;
    CommandInterface::execute("anim_engine_change_infinity_type", CommandArgs().arg(curve_ids).arg(infinity).arg(is_pre_infinity));
}

AnimEngine::CurveIdsList AnimEngine::key_attributes(const UsdAttributeVector& attrs, std::vector<uint32_t> components)
{
    return create_animation_curve_and_add_keys(attrs, components, CurveIdToKeyframesMap());
}

AnimEngine::CurveId AnimEngine::create_animation_curve(UsdAttribute attr, uint32_t component)
{
    auto ids_list = create_animation_curve_and_add_keys(UsdAttributeVector(1, attr), std::vector<uint32_t>(1, component), CurveIdToKeyframesMap());
    if (ids_list.empty())
        return CurveId();
    else
        return ids_list[0];
}

void AnimEngine::apply_euler_filter(AnimEngine::CurveId curves_id, AnimEngineCurveCPtr curve, AnimEngine::CurveIdToKeyframesMap& start_keyframes_list,
                                    AnimEngine::CurveIdToKeyframesMap& end_keyframes_list)
{
    start_keyframes_list.erase(curves_id);
    end_keyframes_list.erase(curves_id);
    if (curve->keyframeCount() < 2)
        return;

    auto prev_value = curve->at(0).value;
    for (size_t key_idx = 1; key_idx < curve->keyframeCount(); ++key_idx)
    {
        adsk::Keyframe current_key = curve->at(key_idx);
        double delta = current_key.value - prev_value;

        if (std::abs(delta) > 180)
        {
            start_keyframes_list[curves_id].push_back(current_key);
            current_key.value -= std::round(delta / 360) * 360;
            prev_value = current_key.value;
            end_keyframes_list[curves_id].push_back(current_key);
        }
    }
}

void AnimEngine::euler_filter(const std::set<CurveId>& curves_ids)
{
    AnimEngine::CurveIdToKeyframesMap start_keyframes_list;
    AnimEngine::CurveIdToKeyframesMap end_keyframes_list;

    for (auto curves_id : curves_ids)
    {
        auto curve = get_curve(curves_id);
        if (!curve)
        {
            OPENDCC_WARN("coding error: invalid curve id");
            continue;
        }

        if (curve->attribute().GetName().GetString().rfind("xformOp:rotate", 0) == 0)
            apply_euler_filter(curves_id, curve, start_keyframes_list, end_keyframes_list);
    }

    if (start_keyframes_list.size() > 0)
    {
        ANIM_CURVES_CHECK_AND_RETURN(start_keyframes_list.size() == end_keyframes_list.size());
        CommandInterface::execute("anim_engine_change_keyframes", CommandArgs().arg(start_keyframes_list).arg(end_keyframes_list));
    }
}

AnimEngine::CurveIdsList AnimEngine::key_attribute(UsdAttribute attr)
{
    return key_attributes(std::vector<UsdAttribute>(1, attr));
}

AnimEngine::CurveIdsList AnimEngine::key_attributes(const UsdAttributeVector& input_attrs)
{
    UsdAttributeVector attributes;
    std::vector<uint32_t> components;
    for (auto attr : input_attrs)
    {
        if (!is_attribute_may_animated(attr))
        {
            OPENDCC_ERROR("Attribute {} may not be animated", attr.GetPath().GetText());
            continue;
        }
        auto num_components = num_components_in_attribute(attr);
        for (uint32_t i = 0; i < num_components; ++i)
        {
            attributes.push_back(attr);
            components.push_back(i);
        }
    }
    return create_animation_curve_and_add_keys(attributes, components, CurveIdToKeyframesMap());
}

bool AnimEngine::remove_animation_curves(UsdAttribute attribute)
{
    return remove_animation_curves(UsdAttributeVector(1, attribute));
}

bool AnimEngine::remove_animation_curves(const UsdAttributeVector& attributes)
{
    CurveIdsList list;
    for (auto attribute : attributes)
    {
        auto prim_path = attribute.GetPrimPath();
        auto prim_it = m_prim_path_to_curves_map.find(prim_path);
        if (prim_it == m_prim_path_to_curves_map.end())
            return false;

        for (CurveId curve_id : prim_it->second)
        {
            auto current_curve = get_curve(curve_id);
            if (current_curve->attribute() == attribute)
            {
                list.push_back(curve_id);
            }
        }
    }

    if (!list.empty())
    {
        remove_curves(list);
        return true;
    }
    else
    {
        return false;
    }
}

bool AnimEngine::bake_all(SdfLayerRefPtr layer, double start_frame, double end_frame, const std::vector<double>& frame_samples, bool remove_origin)
{
    CurveIdsList curves_ids;

    for (auto& it : m_prim_path_to_curves_map)
    {
        for (CurveId id : it.second)
            curves_ids.push_back(id);
    }

    return bake(layer, curves_ids, start_frame, end_frame, frame_samples, remove_origin);
}

bool AnimEngine::bake(SdfLayerRefPtr layer, const SdfPathVector& prim_paths, const UsdAttributeVector& attrs, double start_frame, double end_frame,
                      const std::vector<double>& frame_samples, bool remove_origin)
{
    std::set<CurveId> unique_ids;
    for (auto prim_path : prim_paths)
    {
        auto prim_it = m_prim_path_to_curves_map.find(prim_path);
        if (prim_it == m_prim_path_to_curves_map.end())
            return false;

        for (CurveId curve_id : prim_it->second)
            unique_ids.insert(curve_id);
    }

    for (auto attr : attrs)
    {
        auto prim_path = attr.GetPrimPath();
        auto prim_it = m_prim_path_to_curves_map.find(prim_path);
        if (prim_it == m_prim_path_to_curves_map.end())
            return false;

        for (CurveId curve_id : prim_it->second)
        {
            auto current_curve = get_curve(curve_id);
            if (current_curve->attribute() == attr)
                unique_ids.insert(curve_id);
        }
    }
    CurveIdsList curves_ids;
    for (auto id : unique_ids)
        curves_ids.push_back(id);

    return bake(layer, curves_ids, start_frame, end_frame, frame_samples, remove_origin);
}

namespace
{
    struct ComponentsGroup
    {
        UsdAttribute attribute;
        std::vector<uint32_t> components;
        std::vector<double> values;
    };
}

bool AnimEngine::bake(SdfLayerRefPtr layer, const CurveIdsList& curves_ids, double start_frame, double end_frame,
                      const std::vector<double>& frame_samples, bool remove_origin)
{
    if (curves_ids.empty())
        return false;

    { // undo block
        commands::UsdEditsUndoBlock block;

        auto scope = m_stage_listener.create_mute_scope();
        auto stage = m_stage_listener.stage();
        UsdEditContext context(stage, layer);

        for (double frame = start_frame; frame < end_frame + 1e-3; frame += 1.0)
        {
            for (double sample : frame_samples)
            {
                double time = frame + sample;
                std::unordered_map<SdfPath, ComponentsGroup, SdfPath::Hash> components_groups;
                for (auto& id : curves_ids)
                {
                    auto curve = get_curve(id);
                    auto& group = components_groups[curve->attribute().GetPath()];
                    group.attribute = curve->attribute();
                    uint32_t component = curve->component_idx();
                    double value = curve->evaluate(time);
                    group.components.push_back(curve->component_idx());
                    group.values.push_back(value);
                }

                for (auto& group : components_groups)
                    set_usd_attribute_component(group.second.attribute, group.second.components, group.second.values, time);
            }
        }
        if (remove_origin)
        {
            UsdEditContext context(stage, stage->GetSessionLayer());
            for (auto& id : curves_ids)
            {
                auto curve = get_curve(id);
                curve->attribute().GetPrim().RemoveProperty(curve->attribute().GetName());
            }
        }

        if (remove_origin)
            remove_curves(curves_ids);

    } // end usd undo block
    // first push add_samples command, then remove_curves command
    return true;
}

AnimEngine::CurveIdsList AnimEngine::create_animation_curve_and_add_keys(std::vector<UsdAttribute> attrs, std::vector<uint32_t> components,
                                                                         CurveIdToKeyframesMap extra_keys)
{
    if (attrs.empty() && extra_keys.empty())
        return CurveIdsList();

    ANIM_CURVES_CHECK_AND_RETURN_VAL(attrs.size() == components.size(), CurveIdsList());
    std::vector<AnimEngineCurve> created_curves;
    for (size_t i = 0; i < attrs.size(); ++i)
    {
        auto attribute = attrs[i];
        auto component_idx = components[i];
        double value = 0;
        bool ok = get_usd_attribute_component(attribute, component_idx, value);
        if (!ok)
        {
            OPENDCC_WARN("Fail to get attribute value {}", attribute.GetPath().GetText());
            continue;
        }

        auto curve_id = get_curve_id(attribute, component_idx);
        adsk::Keyframe key;
        key.time = Application::instance().get_current_time();
        key.value = value;
        key.tanIn.type = adsk::TangentType::Auto;
        key.tanOut.type = adsk::TangentType::Auto;
        key.linearInterpolation = false;
        key.quaternionW = 1.0;

        if (!curve_id.valid())
        {
            AnimEngineCurve curve(attribute, component_idx);
            key.id = curve.generate_unique_key_id();
            curve.add_key(key);
            created_curves.push_back(curve);
        }
        else
        {
            auto curve_it = m_curves.find(curve_id);
            ANIM_CURVES_CHECK_AND_CONTINUE(curve_it != m_curves.cend());
            key.id = curve_it->second->generate_unique_key_id();
            extra_keys[curve_id].push_back(key);
        }
    }
    if (created_curves.size() > 0 || extra_keys.size() > 0)
    {
        auto curve_ids = add_curves_direct(created_curves);
        add_keys_direct(extra_keys, false, true);

        auto cmd = CommandRegistry::create_command<AddCurvesAndKeysCommand>("anim_engine_add_curves_and_keys");
        cmd->set_initial_state(curve_ids, created_curves, extra_keys);
        CommandInterface::finalize(cmd, CommandArgs().kwarg("curve_ids", curve_ids).kwarg("curves", created_curves).kwarg("keyframes", extra_keys));
        return curve_ids;
    }
    else
    {
        return CurveIdsList();
    }
}

bool AnimEngine::is_attribute_animated(UsdAttribute attr, uint32_t component) const
{
    auto it = m_curves.find(CurveId(attr.GetPath(), component));
    return it != m_curves.end();
}

bool AnimEngine::is_attribute_animated(UsdAttribute attr) const
{
    auto prim_path = attr.GetPrimPath();
    auto prim_it = m_prim_path_to_curves_map.find(prim_path);
    if (prim_it == m_prim_path_to_curves_map.end())
        return false;

    for (CurveId curve_id : prim_it->second)
    {
        auto current_curve = get_curve(curve_id);
        if (current_curve->attribute() == attr)
            return true;
    }
    return false;
}

AnimEngine::CurveIdsList AnimEngine::get_ids_for_attr(const SdfPath& attr_path) const
{
    CurveIdsList result;
    auto prim_it = m_prim_path_to_curves_map.find(attr_path.GetPrimPath());
    if (prim_it == m_prim_path_to_curves_map.end())
        return result;

    for (CurveId curve_id : prim_it->second)
    {
        auto current_curve = get_curve(curve_id);
        if (current_curve->attribute().GetPath() == attr_path)
        {
            result.push_back(curve_id);
        }
    }
    return result;
}

bool AnimEngine::is_prim_has_animated_attributes(SdfPath prim_path) const
{
    return m_prim_path_to_curves_map.find(prim_path) != m_prim_path_to_curves_map.end();
}

AnimEngine::CurveId AnimEngine::get_curve_id(UsdAttribute attr, uint32_t component) const
{
    auto prim_path = attr.GetPrimPath();
    auto prim_it = m_prim_path_to_curves_map.find(prim_path);
    if (prim_it == m_prim_path_to_curves_map.end())
        return CurveId();

    for (CurveId curve_id : prim_it->second)
    {
        auto current_curve = get_curve(curve_id);
        if (current_curve->attribute() == attr && current_curve->component_idx() == component)
            return curve_id;
    }
    return CurveId();
}

std::set<float> AnimEngine::selected_prims_keys_times() const
{
    std::set<float> result;
    auto prim_paths = Application::instance().get_prim_selection();
    for (auto prim_path : prim_paths)
    {
        auto curves_id = curves(prim_path);
        for (auto curve_id : curves_id)
        {
            auto& curve = m_curves.at(curve_id);
            for (size_t key_idx = 0; key_idx < curve->keyframeCount(); ++key_idx)
                result.insert(curve->at(key_idx).time);
        }
    }
    return result;
}

std::pair<AnimEngine::CurveId, AnimEngineCurveCPtr> AnimEngine::id_and_curve(UsdAttribute attr, uint32_t component)
{
    auto it = m_curves.find(CurveId(attr.GetPath(), component));
    if (it != m_curves.end())
    {
        return *it;
    }
    return std::make_pair<CurveId, AnimEngineCurveCPtr>(CurveId(), nullptr);
}

void AnimEngine::clear()
{
    m_curves.clear();
    m_prim_path_to_curves_map.clear();
}

void AnimEngine::on_changed() const
{
    if (m_curves.size() == 0)
        return;

    auto scope = m_stage_listener.create_mute_scope();
    auto stage = m_stage_listener.stage();

    auto layer = stage->GetSessionLayer();
    if (!layer)
        return;

    UsdEditContext context(stage, layer);

    double time = Application::instance().get_current_time();
    for (auto& it : m_curves)
    {
        auto usd_attr = it.second->attribute();
        uint32_t component = it.second->component_idx();
        double value = it.second->evaluate(time);
        set_usd_attribute_component(usd_attr, component, value);
    }
}

#ifdef UNUSED
SdfLayerRefPtr AnimEngine::get_or_create_anonymous_layer()
{
    if (m_fail_to_create_anonymous_layer)
    {
        return nullptr;
    }
    else if (m_anonymous_layer)
    {
        return m_anonymous_layer;
    }
    else
    {
        auto stage = m_stage_listener.stage();
        if (!stage)
        {
            OPENDCC_WARN("Fail to get current stage.");
            m_fail_to_create_anonymous_layer = true;
            return nullptr;
        }
        auto session_layer = stage->GetSessionLayer();
        if (session_layer)
        {
            m_anonymous_layer = SdfLayer::CreateAnonymous("amin_engine");
            session_layer->InsertSubLayerPath(m_anonymous_layer->GetIdentifier());
            return m_anonymous_layer;
        }
        else
        {
            OPENDCC_WARN("Fail to get session layer.");
            m_fail_to_create_anonymous_layer = true;
            return nullptr;
        }
    }
}
#endif
OPENDCC_NAMESPACE_CLOSE
