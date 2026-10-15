/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/command.h"
#include "opendcc/anim_engine/core/engine.h"

#include <pxr/usd/usd/stageCache.h>

OPENDCC_NAMESPACE_OPEN

void update_timebar();

class ANIM_ENGINE_API AddCurvesAndKeysCommand : public base::commands::UndoCommand
{
public:
    virtual ~AddCurvesAndKeysCommand() {}
    void set_initial_state(const AnimEngine::CurveIdsList& curve_ids, const std::vector<AnimEngineCurve>& curves,
                           const AnimEngine::CurveIdToKeyframesMap& keyframes);
    virtual void undo() override;
    virtual void redo() override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

private:
    PXR_NS::UsdStageCache::Id m_stage_id;
    AnimEngine::CurveIdsList m_curves_ids;
    std::vector<AnimEngineCurve> m_curves;
    AnimEngine::CurveIdToKeyframesMap m_keyframes_to_add;
};

class ANIM_ENGINE_API RemoveCurvesAndKeysCommand : public base::commands::UndoCommand
{
public:
    virtual ~RemoveCurvesAndKeysCommand() {}
    virtual void undo() override;
    virtual void redo() override;
    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

private:
    PXR_NS::UsdStageCache::Id m_stage_id;
    AnimEngine::CurveIdsList m_curves_ids;
    std::vector<AnimEngineCurve> m_curves;
    AnimEngine::CurveIdToKeysIdsMap m_keys_ids;
    AnimEngine::CurveIdToKeyframesMap m_keyframes;
};

class ANIM_ENGINE_API ChangeKeyframesCommand : public base::commands::UndoCommand
{
public:
    void set_start_keyframes(const AnimEngine::CurveIdToKeyframesMap& start_key_frames);
    void set_end_keyframes(const AnimEngine::CurveIdToKeyframesMap& end_key_frames);
    const AnimEngine::CurveIdToKeyframesMap& get_start_keyframes() const { return m_start_keyframes_list; }
    virtual ~ChangeKeyframesCommand() {}
    virtual void undo() override;
    virtual void redo() override;

    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

private:
    PXR_NS::UsdStageCache::Id m_stage_id;
    AnimEngine::CurveIdToKeyframesMap m_start_keyframes_list;
    AnimEngine::CurveIdToKeyframesMap m_end_keyframes_list;
};

class ANIM_ENGINE_API ChangeInfinityTypeCommand : public base::commands::UndoCommand
{
public:
    virtual ~ChangeInfinityTypeCommand() {}
    virtual void undo() override;
    virtual void redo() override;

    virtual base::commands::CommandResult execute(const base::commands::CommandArgs& args) override;

private:
    PXR_NS::UsdStageCache::Id m_stage_id;
    AnimEngine::CurveIdsList m_curve_ids;
    std::map<AnimEngine::CurveId, adsk::InfinityType> m_start_infinity_values;
    adsk::InfinityType m_end_infinity;
    bool m_is_pre_infinity;
};

OPENDCC_NAMESPACE_CLOSE
