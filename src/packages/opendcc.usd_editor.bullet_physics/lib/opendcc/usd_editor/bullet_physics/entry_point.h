/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "api.h"
#include "opendcc/app/core/application.h"
#include "opendcc/base/packaging/package_entry_point.h"
#include "opendcc/app/viewport/iviewport_draw_extension.h"
#include "opendcc/app/viewport/iviewport_ui_extension.h"
#include <pxr/base/gf/frustum.h>

OPENDCC_NAMESPACE_OPEN

class ViewportWidget;
class ViewportUiDrawManager;
class DebugDrawer;

class BULLET_PHYSICS_API BulletPhysicsViewportUIExtension : public IViewportUIExtension
{
public:
    BulletPhysicsViewportUIExtension(ViewportWidget* viewport_widget);
    virtual ~BulletPhysicsViewportUIExtension();
    std::vector<IViewportDrawExtensionPtr> create_draw_extensions() override;
    static void set_enabled(bool enable);
    static void update_gl(bool force = false);
    static bool is_enabled() { return m_enabled; }

private:
    std::shared_ptr<DebugDrawer> m_debug_drawer;
    static std::set<BulletPhysicsViewportUIExtension*> m_all_instances;
    static bool m_enabled;
};

class BULLET_PHYSICS_API BulletPhysicsEntryPoint : public base::packaging::PackageEntryPoint
{
public:
    void initialize(const base::packaging::Package& package) override;
    void uninitialize(const base::packaging::Package& package) override;
};

OPENDCC_NAMESPACE_CLOSE
