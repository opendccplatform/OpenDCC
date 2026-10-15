/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/package_entry_point.h"
#include "opendcc/app/core/application.h"

#include "opendcc/anim_engine/core/engine.h"
#include "opendcc/anim_engine/core/session.h"
#include "opendcc/anim_engine/core/commands.h"

OPENDCC_NAMESPACE_OPEN

class AnimEngineEntryPoint : public base::packaging::PackageEntryPoint
{
public:
    void initialize(const base::packaging::Package& package) override;
    void uninitialize(const base::packaging::Package& package) override;

private:
    void timeline_widget_update();
    Application::CallbackHandle m_timeline_selection_changed_callback_id;
    AnimEngineSession::BasicEventDispatcherHandle m_anim_changed_id;
    double m_time_selection_begin_start;
    double m_time_selection_begin_end;
    double m_time_selection_end_start;
    double m_time_selection_end_end;

    AnimEngine::CurveIdToKeyframesMap m_end_key_list;
    AnimEngine::CurveIdToKeyframesMap m_start_key_list;

    bool m_transform_keys_change;

    void transform_keys_begin();
    void transform_keys_update();
    void transform_keys_move();
    void transform_keys_command();
};
OPENDCC_NAMESPACE_CLOSE
