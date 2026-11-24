/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/app/viewport/viewport_panel.h"
#include "opendcc/app/viewport/viewport_widget.h"
#include "opendcc/app/viewport/viewport_gl_widget.h"
#include "opendcc/app/viewport/viewport_camera_controller.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/application_ui.h"
#include "opendcc/app/core/session.h"
#include <pxr/usd/usdGeom/metrics.h>
#include <QAction>
#include <QActionGroup>
#include <QFrame>
#include <QMenu>
#include <QMenuBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedValueRollback>
#include <QPainter>
#include <cmath>
#include <limits>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    constexpr int splitter_width = 2;
    QIcon layout_icon(ViewportPanel::LayoutMode mode)
    {
        QPixmap pixmap(20, 20);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setPen(QColor(180, 180, 180));
        painter.drawRect(2, 2, 15, 15);
        if (mode == ViewportPanel::LayoutMode::FOUR_PANES || mode == ViewportPanel::LayoutMode::TWO_STACKED)
        {
            painter.drawLine(2, 10, 17, 10);
        }
        if (mode == ViewportPanel::LayoutMode::FOUR_PANES || mode == ViewportPanel::LayoutMode::TWO_SIDE_BY_SIDE)
        {
            painter.drawLine(10, 2, 10, 17);
        }
        return QIcon(pixmap);
    }

    using Preset = ViewportCameraController::ViewPreset;
    const std::array<Preset, 4> presets { Preset::TOP, Preset::PERSPECTIVE, Preset::FRONT, Preset::SIDE };

    QJsonObject save_camera(const GfCamera& camera)
    {
        QJsonArray transform;
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                transform.append(camera.GetTransform()[row][column]);
            }
        }
        QJsonArray clipping_planes;
        for (const auto& plane : camera.GetClippingPlanes())
        {
            clipping_planes.append(QJsonArray { plane[0], plane[1], plane[2], plane[3] });
        }
        return { { "transform", transform },
                 { "perspective", camera.GetProjection() == GfCamera::Perspective },
                 { "horizontal_aperture", camera.GetHorizontalAperture() },
                 { "vertical_aperture", camera.GetVerticalAperture() },
                 { "horizontal_offset", camera.GetHorizontalApertureOffset() },
                 { "vertical_offset", camera.GetVerticalApertureOffset() },
                 { "focal_length", camera.GetFocalLength() },
                 { "near", camera.GetClippingRange().GetMin() },
                 { "far", camera.GetClippingRange().GetMax() },
                 { "focus", camera.GetFocusDistance() },
                 { "f_stop", camera.GetFStop() },
                 { "clipping_planes", clipping_planes } };
    }

    bool restore_camera(const QJsonObject& state, GfCamera& camera)
    {
        const auto transform = state.value("transform").toArray();
        if (transform.size() != 16 || !state.value("perspective").isBool())
        {
            return false;
        }
        GfMatrix4d matrix(1);
        for (int i = 0; i < 16; ++i)
        {
            if (!transform[i].isDouble() || !std::isfinite(transform[i].toDouble()))
            {
                return false;
            }
            matrix[i / 4][i % 4] = transform[i].toDouble();
        }
        const auto determinant = matrix.GetDeterminant();
        if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        {
            return false;
        }
        for (const auto key :
             { "horizontal_aperture", "vertical_aperture", "horizontal_offset", "vertical_offset", "focal_length", "near", "far", "focus" })
        {
            const auto value = state.value(key);
            if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > std::numeric_limits<float>::max())
            {
                return false;
            }
        }
        const auto horizontal = static_cast<float>(state.value("horizontal_aperture").toDouble());
        const auto vertical = static_cast<float>(state.value("vertical_aperture").toDouble());
        const auto near_clip = static_cast<float>(state.value("near").toDouble());
        const auto far_clip = static_cast<float>(state.value("far").toDouble());
        const auto focus = static_cast<float>(state.value("focus").toDouble());
        const auto focal_length = static_cast<float>(state.value("focal_length").toDouble());
        const bool perspective = state.value("perspective").toBool();
        if (horizontal <= 0 || vertical <= 0 || (perspective && near_clip <= 0) || far_clip <= near_clip || focus <= 0 || focal_length <= 0)
        {
            return false;
        }
        const auto f_stop = state.value("f_stop");
        if (!f_stop.isUndefined() && (!f_stop.isDouble() || !std::isfinite(f_stop.toDouble()) || f_stop.toDouble() < 0 ||
                                      f_stop.toDouble() > std::numeric_limits<float>::max()))
        {
            return false;
        }
        const auto planes = state.value("clipping_planes");
        if (!planes.isUndefined() && !planes.isArray())
        {
            return false;
        }
        std::vector<GfVec4f> clipping_planes;
        for (const auto& entry : planes.toArray())
        {
            const auto plane = entry.toArray();
            if (plane.size() != 4)
            {
                return false;
            }
            GfVec4f coefficients;
            for (int i = 0; i < 4; ++i)
            {
                if (!plane[i].isDouble() || !std::isfinite(plane[i].toDouble()) || std::abs(plane[i].toDouble()) > std::numeric_limits<float>::max())
                {
                    return false;
                }
                coefficients[i] = static_cast<float>(plane[i].toDouble());
            }
            if (coefficients[0] == 0 && coefficients[1] == 0 && coefficients[2] == 0)
            {
                return false;
            }
            clipping_planes.push_back(coefficients);
        }
        camera = GfCamera(matrix, perspective ? GfCamera::Perspective : GfCamera::Orthographic, horizontal, vertical,
                          state.value("horizontal_offset").toDouble(), state.value("vertical_offset").toDouble(), focal_length,
                          GfRange1f(near_clip, far_clip), clipping_planes, static_cast<float>(f_stop.toDouble()), focus);
        return true;
    }

    struct SavedPane
    {
        bool m_present = false;
        Preset m_preset = Preset::PERSPECTIVE;
        GfCamera m_camera;
        std::string m_follow_path;
    };
}

ViewportPanel::ViewportPanel(QWidget* parent)
    : QWidget(parent)
    , m_splitter(new QSplitter(Qt::Vertical, this))
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(splitter_width);
    for (int row = 0; row < s_row_count; ++row)
    {
        m_rows[row] = new QSplitter(Qt::Horizontal, m_splitter);
        m_rows[row]->setChildrenCollapsible(false);
        m_rows[row]->setHandleWidth(splitter_width);
        connect(m_rows[row], &QSplitter::splitterMoved, this, [this, row] {
            if (!m_syncing_splitters && m_mode == LayoutMode::FOUR_PANES && !m_maximized)
            {
                QScopedValueRollback<bool> syncing(m_syncing_splitters, true);
                m_rows[1 - row]->setSizes(m_rows[row]->sizes());
            }
        });
    }
    create_pane(s_perspective_pane);
    update_layout();
}

ViewportPanel::~ViewportPanel()
{
    for (const auto& pane : m_panes)
    {
        if (pane.m_widget)
        {
            disconnect(pane.m_widget.data(), nullptr, this, nullptr);
        }
    }
    delete m_splitter;
}

ViewportWidget* ViewportPanel::get_pane(int index) const
{
    return index >= 0 && index < s_pane_count ? m_panes[index].m_widget.data() : nullptr;
}

void ViewportPanel::create_pane(int index)
{
    if (m_panes[index].m_widget)
    {
        return;
    }
    delete m_panes[index].m_frame.data();
    m_panes[index] = Pane {};
    auto frame = new QFrame(m_rows[index / 2]);
    frame->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    frame->setObjectName(QString("viewport_pane_%1").arg(index));
    auto layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto pane = new ViewportWidget(frame);
    layout->addWidget(pane);
    m_panes[index].m_widget = pane;
    m_panes[index].m_frame = frame;
    auto camera = pane->get_gl_widget()->get_camera_controller();
    if (auto stage = Application::instance().get_session()->get_current_stage())
    {
        camera->set_up_axis(UsdGeomGetStageUpAxis(stage));
    }
    camera->set_view_preset(presets[index]);
    connect(pane, &ViewportWidget::activated, this, [this, index] { set_active_pane(index); });
    connect(pane, &ViewportWidget::scene_context_changed, this, [this, index](const TfToken& context) {
        if (context != TfToken("USD"))
        {
            m_active_pane = index;
            set_layout_mode(LayoutMode::SINGLE);
        }
        update_actions();
    });
    create_pane_actions(index);
    m_rows[index / 2]->insertWidget(index % 2, frame);
}

void ViewportPanel::create_pane_actions(int index)
{
    auto pane = get_pane(index);
    auto menu = pane->get_menubar()->addMenu(i18n("viewport.menu_bar", "Layout"));
    menu->setObjectName("viewport_layout_menu");
    auto group = new QActionGroup(menu);
    const std::array<QString, s_layout_count> names { i18n("viewport.layout", "Single"), i18n("viewport.layout", "Four Panes"),
                                                      i18n("viewport.layout", "Two Stacked"), i18n("viewport.layout", "Two Side by Side") };
    for (int layout = 0; layout < s_layout_count; ++layout)
    {
        const auto mode = static_cast<LayoutMode>(layout);
        auto action = menu->addAction(layout_icon(mode), names[layout]);
        action->setCheckable(true);
        group->addAction(action);
        m_panes[index].m_layout_actions[layout] = action;
        connect(action, &QAction::triggered, this, [this, index, mode] {
            set_active_pane(index);
            set_layout_mode(mode);
        });
    }
    auto maximize = menu->addAction(i18n("viewport.layout", "Maximize Pane"));
    maximize->setIcon(QIcon(":icons/layout"));
    m_panes[index].m_maximize_action = maximize;
    connect(maximize, &QAction::triggered, this, [this, index] {
        if (m_maximized)
        {
            restore_panes();
        }
        else
        {
            maximize_pane(index);
        }
    });
    auto toggle = new QAction(i18n("viewport.layout", "Toggle Pane Layout"), pane);
    toggle->setObjectName("viewport_toggle_pane_layout");
    toggle->setShortcut(QKeySequence(Qt::Key_Space));
    toggle->setShortcutContext(Qt::WidgetShortcut);
    pane->get_gl_widget()->addAction(toggle);
    connect(toggle, &QAction::triggered, this, [this, index] {
        if (m_mode == LayoutMode::SINGLE)
        {
            set_layout_mode(LayoutMode::FOUR_PANES);
        }
        else if (m_maximized)
        {
            restore_panes();
        }
        else
        {
            maximize_pane(index);
        }
    });
}

bool ViewportPanel::supports_multiple_views() const
{
    for (const auto& pane : m_panes)
    {
        if (pane.m_widget && pane.m_widget->get_scene_context()->get_context_name() != TfToken("USD"))
        {
            return false;
        }
    }
    return true;
}

void ViewportPanel::remember_splitters()
{
    auto& states = m_splitter_states[static_cast<int>(m_mode)];
    states[0] = m_splitter->saveState();
    for (int row = 0; row < s_row_count; ++row)
    {
        states[row + 1] = m_rows[row]->saveState();
    }
}

void ViewportPanel::set_layout_mode(LayoutMode mode)
{
    if (static_cast<int>(mode) < 0 || static_cast<int>(mode) >= s_layout_count || (mode != LayoutMode::SINGLE && !supports_multiple_views()))
    {
        return;
    }
    if (m_mode != LayoutMode::SINGLE && !m_maximized)
    {
        remember_splitters();
    }
    m_mode = mode;
    m_maximized = false;
    if (mode != LayoutMode::SINGLE)
    {
        create_pane(s_perspective_pane);
        const bool first_multiple = !m_panes[0].m_widget;
        const int pane_count = mode == LayoutMode::FOUR_PANES ? s_pane_count : 2;
        for (int index = 0; index < pane_count; ++index)
        {
            create_pane(index);
        }
        if (first_multiple)
        {
            auto camera = m_panes[s_perspective_pane].m_widget->get_gl_widget()->get_camera_controller();
            const auto path = camera->get_follow_prim_path();
            auto state = camera->get_navigation_camera();
            camera->set_view_preset(Preset::PERSPECTIVE);
            if (state.GetProjection() != GfCamera::Perspective)
            {
                const auto perspective = camera->get_navigation_camera();
                state.SetPerspectiveFromAspectRatioAndFieldOfView(perspective.GetAspectRatio(), perspective.GetFieldOfView(GfCamera::FOVHorizontal),
                                                                  GfCamera::FOVHorizontal, perspective.GetHorizontalAperture());
            }
            camera->set_navigation_camera(state);
            if (!path.IsEmpty())
            {
                camera->set_follow_prim(path);
            }
        }
    }
    if ((mode == LayoutMode::TWO_STACKED || mode == LayoutMode::TWO_SIDE_BY_SIDE) && m_active_pane >= 2)
    {
        m_active_pane = s_perspective_pane;
    }
    update_layout();
}

void ViewportPanel::set_active_pane(int index)
{
    auto pane = get_pane(index);
    if (m_updating_layout || !isVisible() || !pane || !m_panes[index].m_frame->isVisibleTo(this))
    {
        return;
    }
    m_active_pane = index;
    ApplicationUI::instance().set_active_view(pane);
}

void ViewportPanel::maximize_pane(int index)
{
    if (m_mode == LayoutMode::SINGLE || !get_pane(index) || !m_panes[index].m_frame->isVisibleTo(this))
    {
        return;
    }
    if (!m_maximized)
    {
        remember_splitters();
    }
    m_active_pane = index;
    m_maximized = true;
    update_layout();
}

void ViewportPanel::restore_panes()
{
    m_maximized = false;
    update_layout();
}

void ViewportPanel::update_layout()
{
    QScopedValueRollback<bool> updating(m_updating_layout, true);
    create_pane(m_active_pane);
    const bool multiple = m_mode != LayoutMode::SINGLE && !m_maximized;
    const bool quad = multiple && m_mode == LayoutMode::FOUR_PANES;
    m_rows[0]->setOrientation(multiple && m_mode == LayoutMode::TWO_STACKED ? Qt::Vertical : Qt::Horizontal);
    for (int row = 0; row < s_row_count; ++row)
    {
        m_rows[row]->setVisible(multiple ? (quad || row == 0) : row == m_active_pane / 2);
        for (int column = 0; column < 2; ++column)
        {
            const int index = row * 2 + column;
            if (m_panes[index].m_frame)
            {
                m_panes[index].m_frame->setVisible(multiple ? (quad || index < 2) : index == m_active_pane);
            }
        }
    }
    if (multiple)
    {
        const auto& states = m_splitter_states[static_cast<int>(m_mode)];
        if (!states[0].isEmpty())
        {
            m_splitter->restoreState(states[0]);
        }
        else
        {
            m_splitter->setSizes({ 1, 1 });
        }
        m_splitter->setHandleWidth(splitter_width);
        for (int row = 0; row < s_row_count; ++row)
        {
            if (!states[row + 1].isEmpty())
            {
                m_rows[row]->restoreState(states[row + 1]);
            }
            else
            {
                m_rows[row]->setSizes({ 1, 1 });
            }
            m_rows[row]->setHandleWidth(splitter_width);
        }
    }
    else
    {
        m_splitter->setSizes({ m_active_pane < 2 ? 1 : 0, m_active_pane >= 2 ? 1 : 0 });
        m_rows[m_active_pane / 2]->setSizes({ m_active_pane % 2 == 0 ? 1 : 0, m_active_pane % 2 == 1 ? 1 : 0 });
    }
    if (isVisible())
    {
        m_panes[m_active_pane].m_widget->get_gl_widget()->setFocus(Qt::OtherFocusReason);
        ApplicationUI::instance().set_active_view(get_pane(m_active_pane));
    }
    update_actions();
    m_panes[m_active_pane].m_widget->get_gl_widget()->update();
}

void ViewportPanel::update_actions()
{
    const bool multiple_supported = supports_multiple_views();
    for (const auto& pane : m_panes)
    {
        if (!pane.m_widget)
        {
            continue;
        }
        for (int layout = 0; layout < s_layout_count; ++layout)
        {
            pane.m_layout_actions[layout]->setEnabled(layout == 0 || multiple_supported);
            pane.m_layout_actions[layout]->setChecked(layout == static_cast<int>(m_mode));
        }
        pane.m_maximize_action->setEnabled(m_mode != LayoutMode::SINGLE);
        pane.m_maximize_action->setText(m_maximized ? i18n("viewport.layout", "Restore Panes") : i18n("viewport.layout", "Maximize Pane"));
    }
}

void ViewportPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    set_active_pane(m_active_pane);
}

QByteArray ViewportPanel::save_state() const
{
    QJsonArray panes;
    for (int index = 0; index < s_pane_count; ++index)
    {
        if (auto pane = get_pane(index))
        {
            auto widget = pane->get_gl_widget();
            auto controller = widget->get_camera_controller();
            const auto preset = controller->get_view_preset();
            QJsonObject state { { "index", index },
                                { "preset", static_cast<int>(preset) },
                                { "camera", save_camera(controller->get_navigation_camera()) },
                                { "follow_path", QString::fromStdString(widget->get_saved_follow_path().GetString()) } };
            if (preset != Preset::DEFAULT)
            {
                const auto& settings = controller->m_preset_camera_settings;
                const auto range = settings.GetClippingRange();
                state["def_cam_settings"] =
                    QJsonArray { settings.GetHorizontalAperture(), settings.GetFocalLength(), range.GetMin(), range.GetMax() };
            }
            panes.append(state);
        }
    }
    QJsonArray layouts;
    for (int layout = 0; layout < s_layout_count; ++layout)
    {
        QJsonArray splitters;
        const bool current = layout == static_cast<int>(m_mode) && m_mode != LayoutMode::SINGLE && !m_maximized;
        splitters.append(QString::fromLatin1((current ? m_splitter->saveState() : m_splitter_states[layout][0]).toBase64()));
        for (int row = 0; row < s_row_count; ++row)
        {
            splitters.append(QString::fromLatin1((current ? m_rows[row]->saveState() : m_splitter_states[layout][row + 1]).toBase64()));
        }
        layouts.append(splitters);
    }
    return QJsonDocument(QJsonObject { { "version", 2 },
                                       { "mode", static_cast<int>(m_mode) },
                                       { "maximized", m_maximized },
                                       { "active", m_active_pane },
                                       { "panes", panes },
                                       { "layout_splitters", layouts } })
        .toJson(QJsonDocument::Compact);
}

bool ViewportPanel::restore_state(const QByteArray& state)
{
    const auto object = QJsonDocument::fromJson(state).object();
    const auto entries = object.value("panes").toArray();
    const int active = object.value("active").toInt(-1);
    const auto version = object.value("version").toDouble();
    const int mode_value = version == 1 ? (object.value("quad").toBool() ? 1 : 0) : object.value("mode").toInt(-1);
    if ((version != 1 && version != 2) || active < 0 || active >= s_pane_count || object.value("active").toDouble() != active ||
        !object.value("maximized").isBool() || (version == 1 && !object.value("quad").isBool()) || mode_value < 0 || mode_value >= s_layout_count ||
        (version == 2 && object.value("mode").toDouble(-1) != mode_value))
    {
        return false;
    }
    const auto mode = static_cast<LayoutMode>(mode_value);
    const int required_panes = mode == LayoutMode::SINGLE ? 1 : mode == LayoutMode::FOUR_PANES ? s_pane_count : 2;
    if (entries.size() < required_panes || entries.size() > s_pane_count || (mode != LayoutMode::SINGLE && !supports_multiple_views()) ||
        (mode_value >= 2 && active >= 2))
    {
        return false;
    }
    std::array<SavedPane, s_pane_count> panes;
    for (const auto& entry : entries)
    {
        const auto pane = entry.toObject();
        const int index = pane.value("index").toInt(-1);
        const int preset = pane.value("preset").toInt(-1);
        if (index < 0 || index >= s_pane_count || pane.value("index").toDouble() != index || panes[index].m_present || preset < 0 ||
            preset > static_cast<int>(Preset::SIDE) || pane.value("preset").toDouble() != preset || !pane.value("follow_path").isString() ||
            !restore_camera(pane.value("camera").toObject(), panes[index].m_camera))
        {
            return false;
        }
        panes[index].m_present = true;
        panes[index].m_preset = static_cast<Preset>(preset);
        panes[index].m_follow_path = pane.value("follow_path").toString().toStdString();
        if (panes[index].m_preset != Preset::DEFAULT && pane.contains("def_cam_settings"))
        {
            const auto values = pane.value("def_cam_settings").toArray();
            if (values.size() != 4 || !values[0].isDouble() || !values[1].isDouble() || !values[2].isDouble() || !values[3].isDouble())
                return false;
            const auto aperture = static_cast<float>(values[0].toDouble());
            const auto focal = static_cast<float>(values[1].toDouble());
            const auto near_clip = static_cast<float>(values[2].toDouble());
            const auto far_clip = static_cast<float>(values[3].toDouble());
            if (!std::isfinite(aperture) || !std::isfinite(focal) || !std::isfinite(near_clip) || !std::isfinite(far_clip) || aperture <= 0 ||
                focal <= 0 || near_clip >= far_clip)
                return false;

            const auto settings = ViewportCameraController::get_preset_camera_settings(panes[index].m_preset);
            auto previous = settings;
            previous.SetHorizontalAperture(aperture);
            previous.SetFocalLength(focal);
            previous.SetClippingRange(GfRange1f(near_clip, far_clip));
            ViewportCameraController::apply_preset_camera_settings(panes[index].m_camera, previous, settings);
        }
    }
    if (!panes[active].m_present || !panes[s_perspective_pane].m_present || (mode != LayoutMode::SINGLE && !panes[0].m_present))
    {
        return false;
    }
    create_pane(s_perspective_pane);
    for (int index = 0; index < s_pane_count; ++index)
    {
        const auto& pane = panes[index];
        if (!pane.m_present)
        {
            continue;
        }
        create_pane(index);
        const auto path = SdfPath::IsValidPathString(pane.m_follow_path) ? SdfPath(pane.m_follow_path) : SdfPath();
        get_pane(index)->get_gl_widget()->restore_camera_state(pane.m_preset, pane.m_camera, path);
    }
    const auto layouts = object.value("layout_splitters").toArray();
    for (int layout = 0; layout < s_layout_count; ++layout)
    {
        const auto splitters =
            version == 1 && layout == 1 ? object.value("splitters").toArray() : (layout < layouts.size() ? layouts[layout].toArray() : QJsonArray());
        for (int i = 0; i < s_row_count + 1; ++i)
        {
            m_splitter_states[layout][i] = i < splitters.size() ? QByteArray::fromBase64(splitters[i].toString().toLatin1()) : QByteArray();
        }
    }
    m_mode = mode;
    m_maximized = mode != LayoutMode::SINGLE && object.value("maximized").toBool();
    m_active_pane = active;
    update_layout();
    return true;
}

OPENDCC_NAMESPACE_CLOSE
