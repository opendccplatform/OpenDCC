// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_camera_menu_controller.h"
#include "opendcc/app/viewport/viewport_camera_labels.h"
#include <QMenu>
#include <QActionGroup>
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include <pxr/usd/usdGeom/camera.h>
#include "pxr/usd/usd/primRange.h"
#include "opendcc/app/viewport/viewport_overlay.h"
#include <algorithm>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

QString details::get_view_preset_label(ViewportCameraController::ViewPreset preset)
{
    using Preset = ViewportCameraController::ViewPreset;
    switch (preset)
    {
    case Preset::PERSPECTIVE:
        return i18n("viewport.camera", "persp");
    case Preset::TOP:
        return i18n("viewport.camera", "top");
    case Preset::FRONT:
        return i18n("viewport.camera", "front");
    case Preset::SIDE:
        return i18n("viewport.camera", "side");
    default:
        return i18n("viewport.camera", "Def Cam");
    }
}

ViewportUsdCameraMenuController::ViewportUsdCameraMenuController(ViewportCameraControllerPtr camera_controller, ViewportOverlayWidget* overlay,
                                                                 QObject* parent /*= nullptr*/)
    : ViewportCameraMenuController(camera_controller, parent)
{
    m_camera_menu = new QMenu(i18n("viewport.menu_bar.view", "Camera"));
    m_camera_menu->setToolTipsVisible(true);
    m_overlay = overlay;

    m_camera_select_group = new QActionGroup(this);
    m_camera_select_group->setExclusive(true);

    using Preset = ViewportCameraController::ViewPreset;
    for (const auto preset : { Preset::PERSPECTIVE, Preset::TOP, Preset::FRONT, Preset::SIDE })
    {
        const auto name = details::get_view_preset_label(preset);
        auto action = new QAction(name, this);
        action->setIconText(i18n("viewport.camera", "%1 (Def Cam)").arg(name));
        action->setToolTip(i18n("viewport.camera", "In-memory navigation view"));
        action->setCheckable(true);
        m_navigation_actions.emplace(preset, action);
        m_camera_select_group->addAction(action);
        m_overlay->add_camera(action);
        connect(action, &QAction::triggered, this, [this, preset] {
            if (preset == m_camera_controller->get_view_preset())
                m_camera_controller->set_default_camera();
            else
                m_camera_controller->set_view_preset(preset);
        });
    }

    m_look_through_action = new QAction(i18n("viewport.menu_bar.view", "Look Through Selected"), this);
    connect(m_look_through_action, &QAction::triggered, this, &ViewportUsdCameraMenuController::on_look_through);

    if (TF_VERIFY(m_camera_controller, "Failed to initialize viewport camera menu controller. GLWidget is null."))
    {
        connect(m_camera_controller.get(), &ViewportCameraController::camera_changed, this, &ViewportUsdCameraMenuController::on_camera_changed);
    }

    m_current_stage_object_changed_cid = Application::instance().get_session()->register_stage_changed_callback(
        Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED,
        std::bind(&ViewportUsdCameraMenuController::on_current_stage_object_changed, this, std::placeholders::_1));
    on_rebuild_ui();
}

ViewportUsdCameraMenuController::~ViewportUsdCameraMenuController()
{
    Application::instance().get_session()->unregister_stage_changed_callback(Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED,
                                                                             m_current_stage_object_changed_cid);
    delete m_camera_menu;
}

void ViewportUsdCameraMenuController::on_current_stage_object_changed(const UsdNotice::ObjectsChanged& notice)
{
    auto stage = Application::instance().get_session()->get_current_stage();

    auto has_camera_changes = false;
    for (const auto& camera : m_camera_actions)
    {
        if (notice.ResyncedObject(camera.first))
        {
            has_camera_changes = true;
            break;
        }
    }
    if (!has_camera_changes)
    {
        for (const auto& resync_path : notice.GetResyncedPaths())
        {
            auto prim = stage->GetPrimAtPath(resync_path.GetPrimPath());
            if (!prim)
                continue;

            if (UsdGeomCamera(prim))
            {
                has_camera_changes = true;
                break;
            }

            const auto descendants = prim.GetAllDescendants();
            auto camera_prim =
                std::find_if(descendants.begin(), descendants.end(), [](const UsdPrim& descendant) { return UsdGeomCamera(descendant); });

            if (camera_prim != descendants.end())
            {
                has_camera_changes = true;
                break;
            }
        }
    }
    if (has_camera_changes)
        on_rebuild_ui();
}

void ViewportUsdCameraMenuController::on_look_through(bool)
{
    if (auto stage = Application::instance().get_session()->get_current_stage())
    {
        auto selection = Application::instance().get_prim_selection();
        if (!selection.empty())
        {
            auto prim = stage->GetPrimAtPath(selection[0]);
            if (prim.IsValid() && prim.IsA<UsdGeomXformable>())
            {
                m_camera_controller->set_follow_prim(prim.GetPath());
                if (!prim.IsA<UsdGeomCamera>())
                {
                    m_look_through_prim = prim.GetPath();
                    on_rebuild_ui();
                }
            }
        }
    }
}

void ViewportUsdCameraMenuController::on_rebuild_ui()
{
    m_camera_menu->clear();
    for (const auto& camera : m_camera_actions)
    {
        m_camera_select_group->removeAction(camera.second);
        camera.second->deleteLater();
    }
    m_camera_actions.clear();
    m_camera_menu->addSection(i18n("viewport.camera", "Def Cams"));
    for (const auto& navigation : m_navigation_actions)
        m_camera_menu->addAction(navigation.second);

    auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
    {
        on_camera_changed(SdfPath::EmptyPath());
        return;
    }

    const auto add_scene_view = [this](const UsdPrim& prim, const QString& tooltip) {
        auto action = new QAction(prim.GetName().GetText(), this);
        action->setToolTip(tooltip);
        action->setCheckable(true);
        m_camera_actions.emplace(prim, action);
        m_camera_select_group->addAction(action);
        m_camera_menu->addAction(action);
        m_overlay->add_camera(action);
        connect(action, &QAction::triggered, this, [this, path = prim.GetPath()] { m_camera_controller->set_follow_prim(path); });
    };

    if (!m_look_through_prim.IsEmpty())
    {
        auto look_through_prim = stage->GetPrimAtPath(m_look_through_prim);
        if (look_through_prim && !look_through_prim.IsA<UsdGeomCamera>())
        {
            m_camera_menu->addSeparator();
            add_scene_view(look_through_prim, i18n("viewport.camera", "Look through a USD prim"));
        }
        else
        {
            m_look_through_prim = SdfPath::EmptyPath();
        }
    }
    bool have_scene_cameras = false;
    for (UsdPrim prim : stage->TraverseAll())
    {
        if (!prim.IsA<UsdGeomCamera>())
            continue;

        if (!have_scene_cameras)
        {
            have_scene_cameras = true;
            m_camera_menu->addSection(i18n("viewport.camera", "USD Cameras"));
        }

        add_scene_view(prim, i18n("viewport.camera", "USD scene camera"));
    }
    on_camera_changed(m_camera_controller->get_follow_prim_path());
}

void ViewportUsdCameraMenuController::on_camera_changed(PXR_NS::SdfPath follow_path)
{
    if (!m_look_through_prim.IsEmpty() && m_look_through_prim != follow_path)
    {
        m_look_through_prim = SdfPath::EmptyPath();
        on_rebuild_ui();
        return;
    }
    if (follow_path.IsEmpty())
    {
        using Preset = ViewportCameraController::ViewPreset;
        const auto preset = m_camera_controller->get_view_preset();
        m_navigation_actions.at(preset == Preset::DEFAULT ? Preset::PERSPECTIVE : preset)->setChecked(true);
        return;
    }

    if (auto stage = Application::instance().get_session()->get_current_stage())
    {
        const auto iter = m_camera_actions.find(stage->GetPrimAtPath(follow_path));
        if (iter != m_camera_actions.end())
        {
            iter->second->setChecked(true);
            return;
        }
    }
    if (auto checked_action = m_camera_select_group->checkedAction())
        checked_action->setChecked(false);
}

OPENDCC_NAMESPACE_CLOSE
