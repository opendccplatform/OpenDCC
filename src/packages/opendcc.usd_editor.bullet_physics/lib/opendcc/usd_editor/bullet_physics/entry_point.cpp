// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/qt_python.h"
#include <pxr/base/tf/type.h>

#include "opendcc/app/core/application.h"
#include "opendcc/usd_editor/bullet_physics/entry_point.h"
#include "opendcc/usd_editor/bullet_physics/session.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"

PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);
using base::packaging::Package;

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(BulletPhysicsEntryPoint);

std::set<BulletPhysicsViewportUIExtension*> BulletPhysicsViewportUIExtension::m_all_instances;
bool BulletPhysicsViewportUIExtension::m_enabled = false;

void BulletPhysicsEntryPoint::initialize(const Package& package)
{
    BulletPhysicsSession::instance(); // to create a session
    ViewportUIExtensionRegistry::instance().register_ui_extension(
        TfToken("BulletPhysics"), [](auto widget) { return std::make_shared<BulletPhysicsViewportUIExtension>(widget); });
}

void BulletPhysicsEntryPoint::uninitialize(const Package& package)
{
    ViewportUIExtensionRegistry::instance().unregister_ui_extension(TfToken("BulletPhysics"));
}

BulletPhysicsViewportUIExtension::BulletPhysicsViewportUIExtension(ViewportWidget* viewport_widget)
    : IViewportUIExtension(viewport_widget)
{
    m_all_instances.insert(this);
    m_debug_drawer = std::make_shared<DebugDrawer>();
}

BulletPhysicsViewportUIExtension::~BulletPhysicsViewportUIExtension()
{
    m_all_instances.erase(this);
}

std::vector<IViewportDrawExtensionPtr> BulletPhysicsViewportUIExtension::create_draw_extensions()
{
    std::vector<IViewportDrawExtensionPtr> result;
    result.push_back(m_debug_drawer);
    return result;
}

void BulletPhysicsViewportUIExtension::set_enabled(bool enable)
{
    if (enable != m_enabled)
    {
        m_enabled = enable;
        for (auto* instance : m_all_instances)
            instance->m_debug_drawer->set_enabled(enable);
        update_gl(true);
    }
}

void BulletPhysicsViewportUIExtension::update_gl(bool force)
{
    if (m_enabled || force)
    {
        for (auto live_widgets : ViewportWidget::get_live_widgets())
        {
            live_widgets->get_gl_widget()->update();
        }
    }
}

OPENDCC_NAMESPACE_CLOSE
