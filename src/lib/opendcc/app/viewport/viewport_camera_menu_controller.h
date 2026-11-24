/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include "opendcc/app/core/session.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include <map>

class QAction;
class QMenu;
class QActionGroup;

OPENDCC_NAMESPACE_OPEN

class ViewportGLWidget;
class ViewportOverlayWidget;

class OPENDCC_API ViewportCameraMenuController : public QObject
{
    Q_OBJECT;

public:
    ViewportCameraMenuController(ViewportCameraControllerPtr camera_controller, QObject* parent)
        : QObject(parent)
        , m_camera_controller(camera_controller)
    {
    }
    virtual QMenu* get_camera_menu() const = 0;
    virtual QAction* get_look_through_action() const = 0;

protected:
    ViewportCameraControllerPtr m_camera_controller;
};

class OPENDCC_API ViewportUsdCameraMenuController : public ViewportCameraMenuController
{
    Q_OBJECT;

public:
    ViewportUsdCameraMenuController(ViewportCameraControllerPtr camera_controller, ViewportOverlayWidget* overlay, QObject* parent = nullptr);
    ~ViewportUsdCameraMenuController();

    QMenu* get_camera_menu() const override { return m_camera_menu; }
    QAction* get_look_through_action() const override { return m_look_through_action; }

private:
    void on_current_stage_object_changed(const PXR_NS::UsdNotice::ObjectsChanged& notice);
    void on_look_through(bool);
    void on_rebuild_ui();
    void on_camera_changed(PXR_NS::SdfPath follow_path);

    QMenu* m_camera_menu = nullptr;
    ViewportOverlayWidget* m_overlay = nullptr;
    QActionGroup* m_camera_select_group = nullptr;
    QAction* m_look_through_action = nullptr;
    std::map<ViewportCameraController::ViewPreset, QAction*> m_navigation_actions;
    std::map<PXR_NS::UsdPrim, QAction*> m_camera_actions;
    PXR_NS::SdfPath m_look_through_prim;

    Session::StageChangedCallbackHandle m_current_stage_object_changed_cid;
};

OPENDCC_NAMESPACE_CLOSE
