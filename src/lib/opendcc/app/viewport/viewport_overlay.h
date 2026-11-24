/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "viewport_widget.h"

#include <QWidget>
#include <QComboBox>
#include <QGraphicsOpacityEffect>

#include "opendcc/app/core/settings.h"

OPENDCC_NAMESPACE_OPEN

class NoStageMessageFrame : public QFrame
{
    Q_OBJECT

public:
    NoStageMessageFrame(QWidget* parent = nullptr);
    virtual ~NoStageMessageFrame();
};

class ActionComboBox : public QComboBox
{
    Q_OBJECT

public:
    ActionComboBox(const QString& icon, const QString& tooltip, bool arrow = true, QWidget* parent = nullptr);
    virtual ~ActionComboBox();

    void add_action(QAction* action, const QString& label = {});

protected:
    void showEvent(QShowEvent* event) override;

private Q_SLOTS:
    void update_width();

private:
    bool m_has_icon = false;
    bool m_has_arrow = true;
    QString m_icon;
};

class OPENDCC_API ViewportOverlayWidget : public QWidget
{
    Q_OBJECT

public:
    ViewportOverlayWidget(ViewportOverlay* overlay, QWidget* parent = nullptr);
    virtual ~ViewportOverlayWidget();

    void add_camera(QAction* action);
    void add_renderer(QAction* action);
    void add_scene_context(QAction* action);

private:
    void update_edit_target_display();
    void set_edit_target(const QString& name);
    void update_visibility();

    void hide_no_stage_message(bool hide);
    ViewportOverlay* m_overlay;

    Application::CallbackHandle m_edit_target_changed_cid;
    Application::CallbackHandle m_edit_target_dirtiness_changed_cid;
    Application::CallbackHandle m_current_stage_changed_cid;

    Settings::SettingChangedHandle m_camera_cid;
    Settings::SettingChangedHandle m_renderer_cid;
    Settings::SettingChangedHandle m_scene_context_cid;
    Settings::SettingChangedHandle m_edit_target_cid;

    ActionComboBox* m_camera;
    ActionComboBox* m_renderer;
    ActionComboBox* m_scene_context;
    ActionComboBox* m_edit_target;

    NoStageMessageFrame* m_no_stage;
};

class ViewportOverlay : public QObject
{
    Q_OBJECT

public:
    ViewportOverlay(QWidget* parent = nullptr);
    virtual ~ViewportOverlay();

    void fit();
    ViewportOverlayWidget* widget() { return m_overlay_widget; };

    void set_opacity(double opacity) { m_effect->setOpacity(opacity); };

private:
    ViewportOverlayWidget* m_overlay_widget;
    QGraphicsOpacityEffect* m_effect;
};

OPENDCC_NAMESPACE_CLOSE
