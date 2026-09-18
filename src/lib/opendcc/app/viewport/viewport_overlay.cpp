// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

// workaround
#include "opendcc/base/qt_python.h"

#include "viewport_overlay.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"

#include <pxr/usd/sdf/layer.h>
#include <pxr/base/vt/value.h>

#include <QWidget>
#include <QHBoxLayout>
#include <QEvent>
#include <QDebug>
#include <QAction>
#include <QLabel>
#include <QSvgWidget>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

NoStageMessageFrame::NoStageMessageFrame(QWidget* parent)
    : QFrame(parent)
{
    auto main_layout = new QVBoxLayout;
    main_layout->setContentsMargins(0, 0, 0, 0);

    setLayout(main_layout);

    setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum));

    auto title_frame = new QFrame;
    title_frame->setObjectName("no_stage_message_frame_title");

    auto title_layout = new QHBoxLayout;
    title_frame->setLayout(title_layout);

    auto no_stage_label = new QSvgWidget(":/icons/svg/usd_small");

    title_layout->addStretch();
    title_layout->addWidget(no_stage_label);
    title_layout->addWidget(new QLabel("No Stage"));
    title_layout->addStretch();

    main_layout->addWidget(title_frame);

    auto message_layout = new QHBoxLayout;
    message_layout->setContentsMargins(12, 4, 12, 12);

    message_layout->addWidget(new QLabel("Please create a new stage or open an existing one."));

    main_layout->addLayout(message_layout);

    setAutoFillBackground(true);

    setObjectName("no_stage_message_frame");

    setStyleSheet(R"(
QFrame#no_stage_message_frame {
    background: #3e3e3e;
    border: 1px solid #1b1b1b;
    border-radius: 2px;
}

QFrame#no_stage_message_frame_title {
    background: #2b2b2b;
}
)");
}

NoStageMessageFrame::~NoStageMessageFrame() {}

ViewportOverlay::ViewportOverlay(QWidget* parent /*= nullptr*/)
    : QObject(parent)
{
    m_effect = new QGraphicsOpacityEffect(this);
    m_effect->setOpacity(0.7);

    m_overlay_widget = new ViewportOverlayWidget(this, parent);
    m_overlay_widget->setGraphicsEffect(m_effect);

    fit();
}

ViewportOverlay::~ViewportOverlay() {}

void ViewportOverlay::fit()
{
    auto viewport = qobject_cast<QWidget*>(m_overlay_widget->parent());
    m_overlay_widget->setGeometry(viewport->rect());
}

ViewportOverlayWidget::ViewportOverlayWidget(ViewportOverlay* overlay, QWidget* parent /*= nullptr*/)
    : QWidget(parent)
{
    setMouseTracking(true); // without it there will be no hover events for viewport

    auto main_layout = new QVBoxLayout;
    setLayout(main_layout);

    auto row_layout = new QHBoxLayout;
    row_layout->setContentsMargins(0, 0, 0, 0);

    main_layout->addLayout(row_layout);
    main_layout->addStretch();

    m_overlay = overlay;
    auto center_layout = new QHBoxLayout;
    center_layout->addStretch();
    m_no_stage = new NoStageMessageFrame;
    center_layout->addWidget(m_no_stage);
    center_layout->addStretch();

    main_layout->addLayout(center_layout);
    main_layout->addStretch();

    m_camera = new ActionComboBox(":/icons/camera", i18n("viewport.overlay", "Camera"));
    row_layout->addWidget(m_camera);
    m_renderer = new ActionComboBox("", i18n("viewport.overlay", "Hydra Renderer"));
    row_layout->addWidget(m_renderer);
    m_scene_context = new ActionComboBox("", i18n("viewport.overlay", "Scene Context"));
    row_layout->addWidget(m_scene_context);

    row_layout->addStretch();

    m_edit_target = new ActionComboBox(":/icons/svg/layers", i18n("viewport.overlay", "Edit Target"), false);
    m_edit_target->addItem(QIcon(":/icons/svg/layers"), i18n("viewport.overlay", "None"));
    row_layout->addWidget(m_edit_target);

    m_edit_target_changed_cid = Application::instance().register_event_callback(Application::EventType::EDIT_TARGET_CHANGED,
                                                                                std::bind(&ViewportOverlayWidget::update_edit_target_display, this));

    m_edit_target_dirtiness_changed_cid = Application::instance().register_event_callback(
        Application::EventType::EDIT_TARGET_DIRTINESS_CHANGED, std::bind(&ViewportOverlayWidget::update_edit_target_display, this));

    m_current_stage_changed_cid = Application::instance().register_event_callback(
        Application::EventType::CURRENT_STAGE_CHANGED, std::bind(&ViewportOverlayWidget::update_edit_target_display, this));

    m_camera_cid = Application::instance().get_settings()->register_setting_changed(
        "viewport.overlay.camera", [this](const std::string&, const Settings::Value&, Settings::ChangeType) { update_visibility(); });

    m_renderer_cid = Application::instance().get_settings()->register_setting_changed(
        "viewport.overlay.renderer", [this](const std::string&, const Settings::Value&, Settings::ChangeType) { update_visibility(); });

    m_scene_context_cid = Application::instance().get_settings()->register_setting_changed(
        "viewport.overlay.scene_context", [this](const std::string&, const Settings::Value&, Settings::ChangeType) { update_visibility(); });

    m_edit_target_cid = Application::instance().get_settings()->register_setting_changed(
        "viewport.overlay.edit_target", [this](const std::string&, const Settings::Value&, Settings::ChangeType) { update_visibility(); });

    update_visibility();

    auto stage = Application::instance().get_session()->get_current_stage();
    hide_no_stage_message((bool)stage);
}

ViewportOverlayWidget::~ViewportOverlayWidget()
{
    Application::instance().unregister_event_callback(Application::EventType::EDIT_TARGET_CHANGED, m_edit_target_changed_cid);
    Application::instance().unregister_event_callback(Application::EventType::EDIT_TARGET_DIRTINESS_CHANGED, m_edit_target_dirtiness_changed_cid);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_STAGE_CHANGED, m_current_stage_changed_cid);
    Application::instance().get_settings()->unregister_setting_changed("viewport.overlay.camera", m_camera_cid);
    Application::instance().get_settings()->unregister_setting_changed("viewport.overlay.renderer", m_renderer_cid);
    Application::instance().get_settings()->unregister_setting_changed("viewport.overlay.scene_context", m_scene_context_cid);
    Application::instance().get_settings()->unregister_setting_changed("viewport.overlay.edit_target", m_edit_target_cid);
}

void ViewportOverlayWidget::add_camera(QAction* action)
{
    m_camera->add_action(action);
}

void ViewportOverlayWidget::add_renderer(QAction* action)
{
    m_renderer->add_action(action);
}

void ViewportOverlayWidget::add_scene_context(QAction* action)
{
    m_scene_context->add_action(action);
}

void ViewportOverlayWidget::set_edit_target(const QString& name)
{
    m_edit_target->clear();
    m_edit_target->addItem(QIcon(":/icons/svg/layers"), name);
}

void ViewportOverlayWidget::update_visibility()
{
    const auto camera = Application::instance().get_settings()->get("viewport.overlay.camera", true);
    const auto renderer = Application::instance().get_settings()->get("viewport.overlay.renderer", true);
    const auto scene_context = Application::instance().get_settings()->get("viewport.overlay.scene_context", false);
    const auto edit_target = Application::instance().get_settings()->get("viewport.overlay.edit_target", true);

    m_camera->setHidden(!camera);
    m_renderer->setHidden(!renderer);
    m_scene_context->setHidden(!scene_context);
    m_edit_target->setHidden(!edit_target);
}

void ViewportOverlayWidget::hide_no_stage_message(bool hide)
{
    if (hide)
    {
        if (!m_no_stage->isHidden())
        {
            setAutoFillBackground(false);
            m_overlay->set_opacity(0.7);
            m_no_stage->setHidden(true);
        }
    }
    else
    {
        setAutoFillBackground(true);
        m_overlay->set_opacity(0.97);
        m_no_stage->setHidden(false);
    }
}

void ViewportOverlayWidget::update_edit_target_display()
{
    auto stage = Application::instance().get_session()->get_current_stage();
    if (stage)
    {
        auto edit_target_layer = stage->GetEditTarget().GetLayer();
        auto edit_target_label = edit_target_layer->GetDisplayName();
        if (edit_target_layer->IsDirty())
        {
            edit_target_label = edit_target_label + "*";
        }
        const QString result = edit_target_label.c_str();
        set_edit_target(result);

        hide_no_stage_message(true);
    }
    else
    {
        set_edit_target(i18n("viewport.overlay", "None"));
        hide_no_stage_message(false);
    }
}

ActionComboBox::ActionComboBox(const QString& icon, const QString& tooltip, bool arrow, QWidget* parent /*= nullptr*/)
    : QComboBox(parent)
{
    m_has_arrow = arrow;
    if (!arrow)
    {
        setEnabled(false);
        setStyleSheet(R"(
QComboBox::down-arrow
{
    image: none;
}

QComboBox::drop-down
{
    width: 7px;
}

QComboBox:disabled
{
    color: palette(window-text);
}
)");
    }
    setFixedHeight(22); // meh
    setToolTip(tooltip);
    if (icon != "")
    {
        m_has_icon = true;
        m_icon = icon;
    }
    // setSizeAdjustPolicy(QComboBox::AdjustToContents); // doesn't look pretty

    connect(this, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        blockSignals(true);
        auto data = currentData();
        if (data.type() != QVariant::Invalid)
        {
            auto action = data.value<QAction*>();
            action->trigger();
        }
        blockSignals(false);
    });

    connect(this, &QComboBox::currentTextChanged, this, &ActionComboBox::update_width);
}

ActionComboBox::~ActionComboBox() {}

void ActionComboBox::add_action(QAction* action)
{
    QVariant data = QVariant::fromValue<QAction*>(action);

    blockSignals(true);
    if (m_has_icon)
        addItem(QIcon(m_icon), action->text(), data);
    else
        addItem(action->text(), data);
    update_width();
    blockSignals(false);

    connect(action, &QObject::destroyed, this, [this, data]() {
        blockSignals(true);
        removeItem(findData(data));
        update_width();
        blockSignals(false);
    });

    connect(action, &QAction::changed, this, [this, data] {
        auto action = data.value<QAction*>();
        if (action->isChecked())
        {
            blockSignals(true);
            setCurrentIndex(findData(data));
            update_width();
            blockSignals(false);
            emit currentTextChanged(currentText());
        }
    });
}

void ActionComboBox::showEvent(QShowEvent* event)
{
    QComboBox::showEvent(event);
    update_width();
}

void ActionComboBox::update_width()
{
    QFontMetrics metrics(font());
    int width = metrics.horizontalAdvance(currentText());

    if (m_has_arrow)
        width += 20;
    if (m_has_icon)
        width += 22;
    width += 14; // margins

    setFixedWidth(width);
}

OPENDCC_NAMESPACE_CLOSE
