// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/pxr.h>
#if PXR_VERSION < 2108
#include <GL/glew.h>
#else
#include <pxr/imaging/garch/gl.h>
#endif
#include "spline_widget.h"

#include <QColorDialog>
#include <QtGui/QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <memory>
#include <cmath>
#include <iomanip>
#include <chrono>

#include "opendcc/anim_engine/core/engine.h"
#include "opendcc/anim_engine/core/commands.h"
#include "opendcc/anim_engine/core/session.h"

#include "opendcc/anim_engine/ui/graph_editor/spline_widget_commands.h"
#include "opendcc/anim_engine/ui/graph_editor/utils.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/undo/stack.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/command_registry.h"

OPENDCC_NAMESPACE_OPEN

static const QColor background_color(0.377f * 255, 0.377f * 255, 0.377f * 255);
static const QColor grid_color(0.325f * 255, 0.325f * 255, 0.325f * 255);
static const QColor axis_color(0.477f * 255, 0.477f * 255, 0.477f * 255);
static const QColor selected_color(0.898f * 255, 0.898f * 255, 0.0f * 255);
static const QColor tangent_color(1.0f * 255, 0.0f * 255, 1.0f * 255);
static const QColor insert_line_color(1.0f * 255, 0.5f * 255, 0.5f * 255);

static const QColor grid_text_color(Qt::black);
static const QColor current_time_color(Qt::red);
static const QColor current_time_text_color(Qt::red);
static const QColor selected_area_color(Qt::white);
static const QColor selected_spline_color(Qt::white);

static const double eps_for_fit_to_window = 1e-3;
static const double default_window_size = 1;
static const double select_paint_size = 5;
static const double paint_size = 3;
static const float tangent_length = 50;

template <typename T>
static void norm(T& x, T& y, T norm_value = 1)
{
    double len = std::sqrt(x * x + y * y);
    if (len != 0)
    {
        const double alpha = norm_value / len;
        x = alpha * x;
        y = alpha * y;
    }
}

static double round_to(double x, double to)
{
    return to > 1e-4 ? round(x / to) * to : x;
}

static void debug_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* message, const void*)
{
    // ignore non-significant error/warning codes
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204 || id == 0x7fffffff)
    {
        return;
    }

    QString msg;

    switch (source)
    {
    case GL_DEBUG_SOURCE_API:
    {
        msg += "Source: API ";
        break;
    }
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
    {
        msg += "Source: Window System ";
        break;
    }
    case GL_DEBUG_SOURCE_SHADER_COMPILER:
    {
        msg += "Source: Shader Compiler ";
        break;
    }
    case GL_DEBUG_SOURCE_THIRD_PARTY:
    {
        msg += "Source: Third Party ";
        break;
    }
    case GL_DEBUG_SOURCE_APPLICATION:
    {
        msg += "Source: Application ";
        break;
    }
    case GL_DEBUG_SOURCE_OTHER:
    {
        msg += "Source: Other ";
        break;
    }
    }

    switch (type)
    {
    case GL_DEBUG_TYPE_ERROR:
    {
        msg += "Type: Error ";
        break;
    }
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    {
        msg += "Type: Deprecated Behaviour ";
        break;
    }
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    {
        msg += "Type: Undefined Behaviour ";
        break;
    }
    case GL_DEBUG_TYPE_PORTABILITY:
    {
        msg += "Type: Portability ";
        break;
    }
    case GL_DEBUG_TYPE_PERFORMANCE:
    {
        msg += "Type: Performance ";
        break;
    }
    case GL_DEBUG_TYPE_MARKER:
    {
        msg += "Type: Marker ";
        break;
    }
    case GL_DEBUG_TYPE_PUSH_GROUP:
    {
        msg += "Type: Push Group ";
        break;
    }
    case GL_DEBUG_TYPE_POP_GROUP:
    {
        msg += "Type: Pop Group ";
        break;
    }
    case GL_DEBUG_TYPE_OTHER:
    {
        msg += "Type: Other ";
        break;
    }
    }

    switch (severity)
    {
    case GL_DEBUG_SEVERITY_HIGH:
    {
        msg += "Severity: high";
        break;
    }
    case GL_DEBUG_SEVERITY_MEDIUM:
    {
        msg += "Severity: medium";
        break;
    }
    case GL_DEBUG_SEVERITY_LOW:
    {
        msg += "Severity: low";
        break;
    }
    case GL_DEBUG_SEVERITY_NOTIFICATION:
    {
        msg += "Severity: notification";
        break;
    }
    }

    msg += message;

    switch (severity)
    {
    case GL_DEBUG_SEVERITY_NOTIFICATION:
    {
        qDebug() << msg;
        break;
    }
    case GL_DEBUG_SEVERITY_LOW:
    {
        qInfo() << msg;
        break;
    }
    case GL_DEBUG_SEVERITY_MEDIUM:
    {
        qWarning() << msg;
        break;
    }
    case GL_DEBUG_SEVERITY_HIGH:
    {
        qCritical() << msg;
        break;
    }
    }
}

SplineWidget::SplineWidget(QWidget* parent /*= nullptr*/, Qt::WindowFlags f /*= Qt::WindowFlags()*/)
    : QOpenGLWidget(parent, f)
    , m_line_vbo(QOpenGLBuffer::Type::VertexBuffer)
{
    setFocusPolicy(Qt::StrongFocus);
    setProperty("unfocusedKeyEvent_enable", true);
    setFormat(QSurfaceFormat::defaultFormat());

    m_x_left = -10;
    m_x_right = 10;
    m_y_bottom = -10;
    m_y_top = 10;

    m_app_events_handles[Application::EventType::CURRENT_TIME_CHANGED] = Application::instance().register_event_callback(
        Application::EventType::CURRENT_TIME_CHANGED, [this]() { set_current_time(Application::instance().get_current_time()); });

    set_current_time(Application::instance().get_current_time());
}

SplineWidget::~SplineWidget()
{
    if (m_current_engine)
    {
        for (auto& it : m_events)
        {
            m_current_engine->unregister_event_callback(it.first, it.second);
        }
        for (auto& it : m_keys_events)
        {
            m_current_engine->unregister_event_callback(it.first, it.second);
        }
    }

    for (auto& it : m_app_events_handles)
    {
        Application::instance().unregister_event_callback(it.first, it.second);
    }

    global_selection_dispatcher().removeListener(SelectionEvent::SELECTION_CHANGED, m_selection_callback_handle);

    makeCurrent();

    m_line_vao.destroy();
    m_line_vbo.destroy();
}

void SplineWidget::set_displayed_curves(const std::set<AnimEngine::CurveId>& curves_ids)
{
    m_displayed_curves_map.clear();

    for (const auto& id : curves_ids)
    {
        auto curve = m_current_engine->get_curve(id);
        CurveData data;
        data.color = color_for_component(curve->component_idx());
        m_displayed_curves_map[id] = data;
    }

    update();
}

void SplineWidget::clear()
{
    m_displayed_curves_map.clear();

    update();
}

void SplineWidget::update_tangents_type(adsk::TangentType type, bool update_in, bool update_out)
{
    if (!have_selection())
    {
        return;
    }

    AnimEngine::CurveIdToKeyframesMap end_key_map;
    AnimEngine::CurveIdToKeyframesMap start_key_map;

    for (const auto& it : m_displayed_curves_map)
    {
        const CurveData& curve_data = it.second;
        if (curve_data.selected_keys.empty() && curve_data.selected_tangents.empty())
        {
            continue;
        }

        auto curve = m_current_engine->get_curve(it.first);
        ANIM_CURVES_CHECK_AND_CONTINUE(curve);
        auto& start_vector = start_key_map[it.first];
        auto& end_vector = end_key_map[it.first];
        auto map = curve->compute_id_to_idx_map();

        for (const auto& key_id : curve_data.selected_keys)
        {
            adsk::Keyframe keyframe = (*curve)[map[key_id]];
            start_vector.push_back(keyframe);
            if (update_in)
            {
                keyframe.tanIn.type = type;
            }
            if (update_out)
            {
                keyframe.tanOut.type = type;
            }
            end_vector.push_back(keyframe);
        }

        for (const SelectedTangent& tan : curve_data.selected_tangents)
        {
            adsk::Keyframe keyframe = (*curve)[map[tan.key_id]];
            start_vector.push_back(keyframe);
            if (update_in)
            {
                keyframe.tanIn.type = type;
            }
            if (update_out)
            {
                keyframe.tanOut.type = type;
            }
            end_vector.push_back(keyframe);
        }
    }

    CommandInterface::execute("anim_engine_change_keyframes", CommandArgs().arg(start_key_map).arg(end_key_map));
}

void SplineWidget::set_mode(Mode mode)
{
    m_is_insert_key = false;
    m_mode = mode;
    update();
}

void SplineWidget::get_selection_info(double& time, double& value, bool& is_time_define, bool& is_value_define,
                                      std::set<adsk::TangentType>& tangents_types)
{
    tangents_types.clear();
    AnimEngine::CurveIdToKeysIdsMap ids;
    is_time_define = false;
    is_value_define = false;

    bool is_some_time_define = false;
    bool is_some_value_define = false;

    auto process_keyframe = [&](const AnimEngineCurve& curve, const adsk::Keyframe& keyframe) {
        if (!is_some_time_define)
        {
            time = keyframe.time;
            is_some_time_define = true;
            is_time_define = true;
        }
        else if (is_time_define && std::abs(time - keyframe.time) > eps_for_fit_to_window)
        {
            is_time_define = false;
        }

        if (!is_some_value_define)
        {
            value = keyframe.value;
            is_some_value_define = true;
            is_value_define = true;
        }
        else if (is_value_define && std::abs(value - keyframe.value) > eps_for_fit_to_window)
        {
            is_value_define = false;
        }
    };

    for (const auto& it : m_displayed_curves_map)
    {
        const auto curve = m_current_engine->get_curve(it.first);
        const auto& keys_ids = it.second.selected_keys;
        const auto& tangents = it.second.selected_tangents;

        if (!keys_ids.empty() || !tangents.empty())
        {
            auto map = curve->compute_id_to_idx_map();
            for (auto id : keys_ids)
            {
                auto& keyframe = curve->at(map[id]);
                process_keyframe(*curve, keyframe);
                tangents_types.insert(keyframe.tanIn.type);
                tangents_types.insert(keyframe.tanOut.type);
            }

            for (auto tg : tangents)
            {
                auto& keyframe = curve->at(map[tg.key_id]);
                process_keyframe(*curve, keyframe);
                tangents_types.insert(tg.direction == SelectedTangent::TangentDirection::in ? keyframe.tanIn.type : keyframe.tanOut.type);
            }
        }
    }
}

bool SplineWidget::have_selection() const
{
    for (const auto& it : m_displayed_curves_map)
    {
        const CurveData& curve_data = it.second;
        if (!curve_data.selected_keys.empty() || !curve_data.selected_tangents.empty())
        {
            return true;
        }
    }

    return false;
}

void SplineWidget::set_time_to_selection(double time)
{
    set_attribute_to_selection(time, true);
}

void SplineWidget::set_value_to_selection(double value)
{
    set_attribute_to_selection(value, false);
}

void SplineWidget::set_is_tangents_break(bool is_tangents_break_)
{
    m_is_tangents_break = is_tangents_break_;
}

void SplineWidget::set_is_draw_infinity(bool is_draw_infinity_)
{
    m_is_draw_infinity = is_draw_infinity_;

    update();
}

void SplineWidget::set_auto_snap_time_interval(double spun_time_interval_)
{
    m_snap_x_interval = spun_time_interval_;
}

void SplineWidget::set_auto_snap_value_interval(double spun_y_interval_)
{
    m_snap_y_interval = spun_y_interval_;
}

void SplineWidget::set_is_auto_snap_time(bool is_auto_snap_time_)
{
    m_is_auto_snap_time = is_auto_snap_time_;
}

void SplineWidget::set_is_auto_snap_value(bool is_auto_snap_value_)
{
    m_is_auto_snap_value = is_auto_snap_value_;
}

void SplineWidget::set_pre_infinity(adsk::InfinityType infinity_type, ApplyGroup apply_group /*= ApplyGroup::Selected*/)
{
    set_infinity(infinity_type, true, apply_group);
}

void SplineWidget::set_post_infinity(adsk::InfinityType infinity_type, ApplyGroup apply_group /*= ApplyGroup::Selected*/)
{
    set_infinity(infinity_type, false, apply_group);
}

void SplineWidget::set_current_time(double current_time_)
{
    m_current_time = current_time_;

    update();
}

void SplineWidget::set_current_engine(AnimEnginePtr current_engine)
{
    if (m_current_engine.get() == current_engine.get())
    {
        return;
    }

    if (m_current_engine)
    {
        for (auto& it : m_events)
        {
            m_current_engine->unregister_event_callback(it.first, it.second);
        }

        for (auto& it : m_keys_events)
        {
            m_current_engine->unregister_event_callback(it.first, it.second);
        }

        m_events.clear();
        m_keys_events.clear();
    }

    m_current_engine = current_engine;

    if (current_engine)
    {
        m_events[AnimEngine::EventType::INFINITY_CHANGED] = m_current_engine->register_event_callback(
            AnimEngine::EventType::INFINITY_CHANGED, [&](const AnimEngine::CurveIdsList& ids_list) { update(); });

        m_keys_events[AnimEngine::EventType::KEYFRAMES_REMOVED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::KEYFRAMES_REMOVED, [&](const AnimEngine::CurveIdToKeysIdsMap& map) {
                for (auto it : map)
                {
                    if (!m_current_engine->get_curve(it.first))
                    {
                        m_displayed_curves_map.erase(it.first);
                    }
                }

                update();
                selection_changed();
            });

        m_keys_events[AnimEngine::EventType::KEYFRAMES_ADDED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::KEYFRAMES_ADDED, [&](const AnimEngine::CurveIdToKeysIdsMap& map) {
                for (auto it : map)
                {
                    if (m_current_engine->get_curve(it.first) && m_displayed_curves_map.find(it.first) != m_displayed_curves_map.end())
                    {
                        auto curve = m_current_engine->get_curve(it.first);
                        CurveData d;
                        d.color = color_for_component(curve->component_idx());
                        m_displayed_curves_map[it.first] = d;
                        m_displayed_curves_map[it.first].selected_keys = it.second;
                    }
                }

                update();
                selection_changed();
            });

        m_keys_events[AnimEngine::EventType::KEYFRAMES_CHANGED] =
            m_current_engine->register_event_callback(AnimEngine::EventType::KEYFRAMES_CHANGED, [&](const AnimEngine::CurveIdToKeysIdsMap& list) {
                update();
                selection_changed();
            });

        m_selection_callback_handle = global_selection_dispatcher().appendListener(SelectionEvent::SELECTION_CHANGED,
                                                                                   [&](const std::map<AnimEngine::CurveId, SelectionInfo>& map) {
                                                                                       set_selection_info(map, m_displayed_curves_map);
                                                                                       update();
                                                                                       selection_changed();
                                                                                   });
    }

    update();
}

void SplineWidget::add_keyframes(double time, adsk::TangentType in_tangent_type, adsk::TangentType out_tangent_type)
{
    AnimEngine::CurveIdToKeyframesMap map;
    bool is_selected = have_selection();

    for (auto& it : m_displayed_curves_map)
    {
        auto curve_id = it.first;
        CurveData& curve_data = it.second;
        auto curve = m_current_engine->get_curve(it.first);

        if (!curve_data.selected_keys.empty() || !curve_data.selected_tangents.empty() || !is_selected)
        {
            curve_data.selected_keys.clear();
            curve_data.selected_tangents.clear();
            adsk::Keyframe key;
            key.time = time;
            key.value = curve->evaluate(time);
            key.tanIn.type = adsk::TangentType::Auto;
            key.tanOut.type = adsk::TangentType::Auto;
            key.linearInterpolation = false;
            key.quaternionW = 1.0;
            key.id = curve->generate_unique_key_id();

            map[curve_id].push_back(key);
        }
    }

    m_current_engine->add_keys(map);
}

void SplineWidget::delete_selected_keyframes()
{
    m_current_engine->remove_keys(get_selected_keys_ids());
}

void SplineWidget::snap_selection(bool is_snap_time, bool is_snap_value)
{
    if (!have_selection())
    {
        return;
    }

    AnimEngine::CurveIdToKeyframesMap end_key_map;
    AnimEngine::CurveIdToKeyframesMap start_key_map;

    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;
        if (curve_data.selected_keys.empty())
        {
            continue;
        }
        auto curve = m_current_engine->get_curve(it.first);
        ANIM_CURVES_CHECK_AND_CONTINUE(curve);
        auto& start_vector = start_key_map[it.first];
        auto& end_vector = end_key_map[it.first];

        auto map = curve->compute_id_to_idx_map();
        for (const auto key_id : curve_data.selected_keys)
        {
            adsk::Keyframe keyframe = (*curve)[map[key_id]];
            start_vector.push_back(keyframe);
            if (is_snap_time)
            {
                keyframe.time = round_to(keyframe.time, m_snap_x_interval);
            }

            if (is_snap_value)
            {
                keyframe.value = round_to(keyframe.value, m_snap_y_interval);
            }
            end_vector.push_back(keyframe);
        }
    }

    CommandInterface::execute("anim_engine_change_keyframes", CommandArgs().arg(start_key_map).arg(end_key_map));
}

void SplineWidget::fit_to_widget(bool fit_all)
{
    uint64_t num_keyframes = 0;

    double x_min = 1e10;
    double x_max = -1e10;
    double y_min = 1e10;
    double y_max = -1e10;

    auto process_curve_if_keyframe_count_is_1 = [&](AnimEngineCurveCPtr curve) {
        if (curve->keyframeCount() == 1)
        {
            const double x = curve->at(0).time;
            const double y = curve->at(0).value;
            x_min = std::min(x, x_min);
            x_max = std::max(x, x_max);
            y_min = std::min(y, y_min);
            y_max = std::max(y, y_max);
        }
    };

    auto process_curve_interval = [&](AnimEngineCurveCPtr curve, size_t interval_idx) {
        const double x0 = curve->at(interval_idx).time;
        const double y0 = curve->at(interval_idx).value;

        const double x1 = curve->at(interval_idx + 1).time;
        const double y1 = curve->at(interval_idx + 1).value;

        x_min = std::min(x0, x_min);
        x_max = std::max(x1, x_max);

        const double d_t = (x1 - x0) * 0.1;
        for (double t = x0; t < x1 + d_t * 0.5; t += d_t)
        {
            const double y = curve->evaluate(t);
            y_min = std::min(y, y_min);
            y_max = std::max(y, y_max);
        }
    };

    if (fit_all || !have_selection())
    {
        for (auto it : m_displayed_curves_map)
        {
            auto curve = m_current_engine->get_curve(it.first);
            ANIM_CURVES_CHECK_AND_CONTINUE(curve);
            process_curve_if_keyframe_count_is_1(curve);
            for (size_t i = 0; i < curve->keyframeCount() - 1; ++i)
            {
                process_curve_interval(curve, i);
            }
        }
    }
    else
    {
        for (auto it : m_displayed_curves_map)
        {
            CurveData& curve_data = it.second;
            if (curve_data.selected_keys.empty() && curve_data.selected_tangents.empty())
            {
                continue;
            }

            auto curve = m_current_engine->get_curve(it.first);
            ANIM_CURVES_CHECK_AND_CONTINUE(curve);
            process_curve_if_keyframe_count_is_1(curve);
            std::set<size_t> keys_indices;

            auto map = curve->compute_id_to_idx_map();

            for (auto id : curve_data.selected_keys)
            {
                size_t idx = map[id];
                if (idx < curve->keyframeCount() - 1)
                {
                    keys_indices.insert(idx);
                }
                if (idx > 0)
                {
                    keys_indices.insert(idx - 1);
                }
            }

            for (auto tan : curve_data.selected_tangents)
            {
                size_t idx = map[tan.key_id];
                if (idx < curve->keyframeCount() - 1)
                {
                    keys_indices.insert(idx);
                }
                if (idx > 0)
                {
                    keys_indices.insert(idx - 1);
                }
            }

            for (size_t i : keys_indices)
            {
                process_curve_interval(curve, i);
            }
        }
    }

    if (x_max > x_min - eps_for_fit_to_window && y_max > y_min - eps_for_fit_to_window)
    {
        if (x_max > x_min + eps_for_fit_to_window)
        {
            m_x_left = x_min;
            m_x_right = x_max;
        }
        else
        {
            m_x_left = x_min - default_window_size / 2;
            m_x_right = x_max + default_window_size / 2;
        }
        if (y_max > y_min + eps_for_fit_to_window)
        {
            m_y_bottom = y_min;
            m_y_top = y_max;
        }
        else
        {
            m_y_bottom = y_min - default_window_size / 2;
            m_y_top = y_max + default_window_size / 2;
        }

        change_zoom(1.2f);
        update();
    }
}

void SplineWidget::initializeGL()
{
    QOpenGLFunctions* functions = context()->functions();

    functions->glClearColor(background_color.redF(), background_color.greenF(), background_color.blueF(), background_color.alphaF());
    functions->glEnable(GL_DEPTH_TEST);
    functions->glEnable(GL_CULL_FACE);

    enable_gl_debug();

    initialize_grid();
    initialize_screen_rectangle();

    initialize_line();
}

void SplineWidget::resizeGL(int width, int height)
{
    QOpenGLFunctions* functions = context()->functions();

    functions->glViewport(0, 0, width, height);
}

void SplineWidget::paintGL()
{
    QOpenGLFunctions* functions = context()->functions();

    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw();
}

void SplineWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();

    m_current_modifiers = event->modifiers();
    m_current_mouse_buttons = event->buttons();
    m_current_mouse_pos = event->pos();

    // current_pos = event->pos();
    m_last_pos_x = event->pos().x();
    m_last_pos_y = event->pos().y();

    m_x_left_selected = m_x_right_selected = screen_x_to_world_x(m_last_pos_x);
    m_y_bottom_selected = m_y_top_selected = screen_y_to_world_y(m_last_pos_y);

    bool is_move_command = false;

    switch (m_mode)
    {
    case SplineWidget::Mode::RegionTools:
    {
        bool is_cursor_on_some_key_or_tangent = false;
        for (const auto& it : m_displayed_curves_map)
        {
            if (is_cursor_on_key_or_tangent_pivot(it.first, m_last_pos_x, m_last_pos_y))
            {
                is_cursor_on_some_key_or_tangent = true;
                break;
            }
        }

        for (auto& it : m_displayed_curves_map)
        {
            if ((event->buttons() & Qt::MiddleButton) || ((event->buttons() & Qt::LeftButton) && is_cursor_on_some_key_or_tangent))
            {
                CurveData& curve_data = it.second;
                auto curve = m_current_engine->get_curve(it.first);
                update_pivots(curve, curve_data);
                is_move_command = true;
            }
        }
        if (is_move_command)
        {
            setCursor(Qt::SizeAllCursor);
            m_key_changed_command = CommandRegistry::create_command<ChangeKeyframesCommand>("anim_engine_change_keyframes");
            m_key_changed_command->set_start_keyframes(get_selected_keys());
        }
        else if (event->buttons() & Qt::LeftButton)
        {
            m_selected_state = SelectedState::start_selected;
            start_command("spline_widget_selection");
        }
        break;
    }
    case SplineWidget::Mode::InsertKeys:
        if (event->buttons() & Qt::LeftButton)
        {
            m_selected_state = SelectedState::start_selected;
            start_command("spline_widget_selection");
        }
        if (event->buttons() & Qt::MiddleButton)
        {
            m_is_insert_key = true;
            m_insert_key_position = m_x_left_selected;
        }
        break;
    default:
        break;
    }

    update();
}

void SplineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    setCursor(Qt::ArrowCursor);
    m_current_modifiers = event->modifiers();
    m_current_mouse_buttons = event->buttons();
    m_current_mouse_pos = event->pos();

    if (m_is_insert_key)
    {
        add_keyframes(m_x_right_selected, adsk::TangentType::Auto, adsk::TangentType::Auto);
        m_is_insert_key = false;
        emit keyframe_moved();
    }
    else
    {
        if (m_selected_state == SelectedState::start_selected)
        {
            update_selection(event);
            m_selected_state = SelectedState::no_selected_area;
        }
        else if (m_selected_state == SelectedState::start_moving)
        {
            m_selected_state = SelectedState::no_selected_area;
            ANIM_CURVES_CHECK_AND_RETURN(m_key_changed_command);
            CommandInterface::execute(m_key_changed_command,
                                      CommandArgs().arg(m_key_changed_command->get_start_keyframes()).arg(get_selected_keys()));
            emit keyframe_moved();
        }
        update();
    }
    end_command();
}

void SplineWidget::mouseMoveEvent(QMouseEvent* event)
{
    m_current_modifiers = event->modifiers();
    m_current_mouse_buttons = event->buttons();
    m_current_mouse_pos = event->pos();

    bool Alt = (m_current_modifiers & Qt::AltModifier);

    const float pos_x = m_current_mouse_pos.x();
    const float pos_y = m_current_mouse_pos.y();
    const float dx = -(pos_x - m_last_pos_x) / width() * (m_x_right - m_x_left);
    const float dy = -(pos_y - m_last_pos_y) / height() * (m_y_bottom - m_y_top);
    m_x_right_selected = screen_x_to_world_x(pos_x);
    m_y_top_selected = screen_y_to_world_y(pos_y);

    switch (m_mode)
    {
    case SplineWidget::Mode::RegionTools:
        if ((m_current_mouse_buttons == Qt::MiddleButton) && Alt)
        {
            m_x_left += dx;
            m_x_right += dx;
            m_y_bottom += dy;
            m_y_top += dy;
        }
        else if ((m_current_mouse_buttons == Qt::RightButton) && Alt)
        {
            float zoom_delta = 1 - 5 * (m_current_mouse_pos.x() - m_last_pos_x) / width();
            change_zoom(zoom_delta);
        }
        else if ((m_current_mouse_buttons == Qt::LeftButton))
        {
            switch (m_selected_state)
            {
            case SelectedState::no_selected_area:
                break;
            case SelectedState::start_selected:

                break;
            case SelectedState::start_moving:
                move_selected_keys();
                move_selected_tangents(pos_x, pos_y);
                break;
            default:
                break;
            }
        }
        else if (m_current_mouse_buttons == Qt::MiddleButton)
        {
            move_selected_keys();
            move_selected_tangents(pos_x, pos_y);
        }

        m_last_pos_x = m_current_mouse_pos.x();
        m_last_pos_y = m_current_mouse_pos.y();
        break;

    case SplineWidget::Mode::InsertKeys:
        m_insert_key_position = m_x_right_selected;
        break;
    default:
        break;
    }

    update();
}

void SplineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QPoint& p = event->pos();
    const double x = screen_x_to_world_x(p.x());
    const double y = screen_y_to_world_y(p.y());

    update();
}

void SplineWidget::wheelEvent(QWheelEvent* event)
{
    const float alpha = ((float)event->angleDelta().y()) / 400.0f;

    if (alpha > 0)
    {
        change_zoom(1 - alpha);
    }
    else
    {
        change_zoom(1 - alpha / (1 + alpha));
    }

    update();
}

void SplineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    emit context_menu_event_signal(event);
}

void SplineWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_F)
    {
        fit_to_widget(false);
    }
    else if (event->key() == Qt::Key_A)
    {
        fit_to_widget(true);
    }
    else if (event->key() == Qt::Key_Delete)
    {
        delete_selected_keyframes();
    }
    else
    {
        QOpenGLWidget::keyPressEvent(event);
    }
}

void SplineWidget::set_infinity(adsk::InfinityType infinity_type, bool is_pre_infinity, ApplyGroup apply_group /*= ApplyGroup::Selected*/)
{
    AnimEngine::CurveIdsList list;
    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;
        if (apply_group == ApplyGroup::Selected && curve_data.selected_keys.empty() && curve_data.selected_tangents.empty())
        {
            continue;
        }
        list.push_back(it.first);
    }

    m_current_engine->set_infinity_type(list, infinity_type, is_pre_infinity);
}

void SplineWidget::set_attribute_to_selection(double attr_value, bool attr_is_time)
{
    if (!have_selection())
    {
        return;
    }

    AnimEngine::CurveIdToKeyframesMap end_key_map;
    AnimEngine::CurveIdToKeyframesMap start_key_map;
    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;
        if (curve_data.selected_keys.empty() && curve_data.selected_tangents.empty())
        {
            continue;
        }

        auto curve = m_current_engine->get_curve(it.first);
        ANIM_CURVES_CHECK_AND_CONTINUE(curve);
        auto& start_vector = start_key_map[it.first];
        auto& end_vector = end_key_map[it.first];
        auto map = curve->compute_id_to_idx_map();
        for (const auto key_id : curve_data.selected_keys)
        {
            adsk::Keyframe keyframe = (*curve)[map[key_id]];
            start_vector.push_back(keyframe);
            if (attr_is_time)
            {
                keyframe.time = attr_value;
            }
            else
            {
                keyframe.value = attr_value;
            }
            end_vector.push_back(keyframe);
        }

        for (SelectedTangent tan : curve_data.selected_tangents)
        {
            adsk::Keyframe keyframe = (*curve)[map[tan.key_id]];
            start_vector.push_back(keyframe);
            if (attr_is_time)
            {
                keyframe.time = attr_value;
            }
            else
            {
                keyframe.value = attr_value;
            }
            end_vector.push_back(keyframe);
        }
    }

    CommandInterface::execute("anim_engine_change_keyframes", CommandArgs().arg(start_key_map).arg(end_key_map));
}

void SplineWidget::draw()
{
    draw_grid();
    draw_splines();
    draw_key_points();
    draw_tangents();
    draw_selected_area();
}

void SplineWidget::draw_splines()
{
    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;
        auto curve = m_current_engine->get_curve(it.first);
        ANIM_CURVES_CHECK_AND_RETURN(curve);
        if (curve->keyframeCount() == 0)
        {
            continue;
        }

        const double dx_optimal = 3 * (m_x_right - m_x_left) / width();
        std::vector<float> spline_points;
        for (size_t key_idx = 0; key_idx < (size_t)curve->keyframeCount() - 1; ++key_idx)
        {
            spline_points.clear();
            auto key = (*curve)[key_idx];
            auto next_key = (*curve)[key_idx + 1];

            const double x0 = std::max(key.time, (double)m_x_left);
            const double x1 = std::min(next_key.time, (double)m_x_right);

            const uint64_t num_samples = ceil((x1 - x0) / dx_optimal);
            const double d_x = (x1 - x0) / num_samples;
            if (d_x > 1e-6)
            {
                if (key.tanOut.type == adsk::TangentType::Step)
                {
                    spline_points.push_back(key.time);
                    spline_points.push_back(key.value);

                    spline_points.push_back(next_key.time);
                    spline_points.push_back(key.value);

                    spline_points.push_back(next_key.time);
                    spline_points.push_back(next_key.value);
                }
                else if (key.tanIn.type == adsk::TangentType::StepNext)
                {
                    spline_points.push_back(key.time);
                    spline_points.push_back(key.value);

                    spline_points.push_back(key.time);
                    spline_points.push_back(next_key.value);

                    spline_points.push_back(next_key.time);
                    spline_points.push_back(next_key.value);
                }
                else
                {
                    spline_points.push_back(key.time);
                    spline_points.push_back(key.value);
                    for (double x = x0 + d_x; x < x1 - d_x / 2; x += d_x)
                    {
                        const double y = curve->evaluate(x);
                        spline_points.push_back(x);
                        spline_points.push_back(y);
                    }
                    spline_points.push_back(next_key.time);
                    spline_points.push_back(next_key.value);
                }
            }
            else
            {
                spline_points.push_back(key.time);
                spline_points.push_back(key.value);

                spline_points.push_back(next_key.time);
                spline_points.push_back(next_key.value);
            }

            if ((curve_data.selected_keys.count(key.id) > 0) || (curve_data.selected_keys.count(next_key.id) > 0) ||
                (curve_data.selected_tangents.count({ key.id, SelectedTangent::out }) > 0))
            {
                draw_strip_line(spline_points, 0.11f, selected_spline_color);
            }
            else
            {
                draw_strip_line(spline_points, 0.11f, curve_data.color);
            }

            spline_points.clear();
        }

        if (m_is_draw_infinity)
        {
            std::vector<float> points_pre_inf;
            for (double x = std::min(curve->at(0).time, m_x_right); x > m_x_left; x -= dx_optimal)
            {
                const double y = curve->evaluate(x);
                points_pre_inf.push_back(x);
                points_pre_inf.push_back(y);
            }
            draw_strip_line(points_pre_inf, 0.11f, curve_data.color, true);

            std::vector<float> points_post_inf;
            for (double x = std::max(curve->at(curve->keyframeCount() - 1).time, m_x_left); x < m_x_right; x += dx_optimal)
            {
                const double y = curve->evaluate(x);
                points_post_inf.push_back(x);
                points_post_inf.push_back(y);
            }
            draw_strip_line(points_post_inf, 0.11f, curve_data.color, true);
        }
    }
}

void SplineWidget::draw_grid()
{
    // grid
    if (!m_grid_program.bind())
    {
        qWarning() << "can\'t bind shader program";
    }

    m_grid_program.setUniformValue(m_grid_z_location, -0.1f);
    m_grid_program.setUniformValue(m_grid_color_location, grid_color.redF(), grid_color.greenF(), grid_color.blueF(), grid_color.alphaF());
    m_grid_program.setUniformValue(m_grid_axis_color_location, axis_color.redF(), axis_color.greenF(), axis_color.blueF(), axis_color.alphaF());
    m_grid_program.setUniformValue(m_grid_current_time_color_location, current_time_color.redF(), current_time_color.greenF(),
                                   current_time_color.blueF(), current_time_color.alphaF());
    m_grid_program.setUniformValue(m_grid_insert_key_color_location, insert_line_color.redF(), insert_line_color.greenF(), insert_line_color.blueF(),
                                   insert_line_color.alphaF());

    const float world_grid_rectangle_width = compute_grid_step(m_x_left, m_x_right, width());
    const float world_grid_rectangle_height = compute_grid_step(m_y_bottom, m_y_top, height());

    const float screen_origin_x = world_x_to_screen_x(0);
    const float screen_origin_y = world_y_to_screen_y(0);

    const float screen_grid_rectangle_width = std::abs(screen_origin_x - world_x_to_screen_x(world_grid_rectangle_width));
    const float screen_grid_rectangle_height = std::abs(screen_origin_y - world_y_to_screen_y(world_grid_rectangle_height));

    m_grid_program.setUniformValue(m_grid_rectangle_size_location, screen_grid_rectangle_width, screen_grid_rectangle_height);

    m_grid_program.setUniformValue(m_grid_origin_location, screen_x_to_fragment_x(screen_origin_x), screen_y_to_fragment_y(screen_origin_y));

    m_grid_program.setUniformValue(m_grid_current_time_x_location, world_x_to_screen_x(m_current_time));

    m_grid_program.setUniformValue(m_grid_insert_key_x_location, world_x_to_screen_x(m_insert_key_position));
    m_grid_program.setUniformValue(m_grid_insert_key_location, m_is_insert_key);

    QOpenGLFunctions* functions = context()->functions();
    functions->glDrawArrays(GL_TRIANGLES, 0, m_grid_draw_arrays_count);

    m_grid_program.release();

    // grid numbers
    draw_text(world_x_to_screen_x(m_current_time) + 5, height() - 25, QString::number(m_current_time), current_time_text_color);

    float value;

    // vertical numbers
    int64_t y_min = trunc((float)m_y_bottom / world_grid_rectangle_height);
    int64_t y_max = trunc((float)m_y_top / world_grid_rectangle_height);

    for (int64_t i = y_min; i <= y_max; ++i)
    {
        value = i * world_grid_rectangle_height;

        draw_text(10, world_y_to_screen_y(value) - 3, QString::number(value), grid_text_color);
    }

    // horizontal numbers
    int64_t x_min = trunc((float)m_x_left / world_grid_rectangle_width);
    int64_t x_max = trunc((float)m_x_right / world_grid_rectangle_width);

    for (int64_t i = x_min; i <= x_max; ++i)
    {
        value = i * world_grid_rectangle_width;

        draw_text(world_x_to_screen_x(value) + 5, height() - 10, QString::number(value), grid_text_color);
    }
}

void SplineWidget::draw_key_points()
{
    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;
        auto curve = m_current_engine->get_curve(it.first);

        if (!curve)
        {
            continue;
        }

        for (uint32_t i = 0; i < curve->keyframeCount(); ++i)
        {
            adsk::Keyframe keyframe;
            curve->keyframeAtIndex(i, keyframe);
            const double x_pixel = world_x_to_screen_x(keyframe.time);
            const double y_pixel = world_y_to_screen_y(keyframe.value);
            math::vec2 pos = { x_pixel, y_pixel };

            if (curve_data.selected_keys.count(keyframe.id) > 0)
            {
                draw_point_on_screen(pos, 0.5f, selected_color);
                draw_point_on_screen(in_tangent_pos(keyframe), 0.3f, tangent_color);
                draw_point_on_screen(out_tangent_pos(keyframe), 0.3f, tangent_color);
                draw_line_in_pixels(pos, in_tangent_pos(keyframe), 0.29f, tangent_color);
                draw_line_in_pixels(pos, out_tangent_pos(keyframe), 0.29f, tangent_color);
            }
            else
            {
                draw_point_on_screen(pos, 0.41f, { 0, 0, 0 });
            }
        }
    }
}

void SplineWidget::draw_tangents()
{
    for (auto& it : m_displayed_curves_map)
    {
        const CurveData& curve_data = it.second;

        if (curve_data.selected_tangents.empty())
        {
            continue;
        }

        auto curve = m_current_engine->get_curve(it.first);
        auto id_to_idx = curve->compute_id_to_idx_map();

        for (SelectedTangent dir : curve_data.selected_tangents)
        {
            auto idx = id_to_idx[dir.key_id];
            const adsk::Keyframe& keyframe = (*curve)[idx];
            const math::vec2 pos = { world_x_to_screen_x(keyframe.time), world_y_to_screen_y(keyframe.value) };
            draw_point_on_screen(pos, 0.3f, { 1, 1, 0 });

            if (dir.direction == SelectedTangent::in)
            {
                draw_point_on_screen(in_tangent_pos(keyframe), 0.4f, selected_color);
                draw_line_in_pixels(pos, in_tangent_pos(keyframe), 0.4f, selected_color);
                draw_point_on_screen(out_tangent_pos(keyframe), 0.29f, tangent_color);
                draw_line_in_pixels(pos, out_tangent_pos(keyframe), 0.29f, tangent_color);
            }
            else
            {
                draw_point_on_screen(in_tangent_pos(keyframe), 0.29f, tangent_color);
                draw_line_in_pixels(pos, in_tangent_pos(keyframe), 0.29f, tangent_color);
                draw_point_on_screen(out_tangent_pos(keyframe), 0.4f, selected_color);
                draw_line_in_pixels(pos, out_tangent_pos(keyframe), 0.4f, selected_color);
            }
        }
    }
}

void SplineWidget::draw_line_in_pixels(math::vec2 pos0, math::vec2 pos1, float z, QColor color)
{
    math::vec2 coord0 = screen_to_world(pos0);
    math::vec2 coord1 = screen_to_world(pos1);

    std::vector<float> data;
    data.push_back(coord0.x);
    data.push_back(coord0.y);
    data.push_back(coord1.x);
    data.push_back(coord1.y);

    draw_strip_line(data, z, color);
}

void SplineWidget::draw_point_on_screen(math::vec2 screen_coordinate, float z, QColor color)
{
    math::vec2 right_up = screen_to_fragment({ screen_coordinate.x + paint_size, screen_coordinate.y + paint_size });
    math::vec2 left_down = screen_to_fragment({ screen_coordinate.x - paint_size, screen_coordinate.y - paint_size });

    draw_rectangle_on_screen(left_down, right_up, z, color);
}

void SplineWidget::draw_rectangle_on_screen(math::vec2 left_down, math::vec2 right_up, float z, QColor color, bool edge /*= false*/)
{
    if (!m_rectangle_program.bind())
    {
        qWarning() << "can\'t bind shader program";
    }

    m_rectangle_program.setUniformValue(m_rectangle_z_location, z);
    m_rectangle_program.setUniformValue(m_rectangle_color_location, color.redF(), color.greenF(), color.blueF(), color.alphaF());

    m_rectangle_program.setUniformValue(m_rectangle_left_down_coordinate_location, std::min(right_up.x, left_down.x),
                                        std::min(right_up.y, left_down.y));

    m_rectangle_program.setUniformValue(m_rectangle_right_up_coordinate_location, std::max(right_up.x, left_down.x),
                                        std::max(right_up.y, left_down.y));

    m_rectangle_program.setUniformValue(m_rectangle_need_draw_edge_location, edge);

    QOpenGLFunctions* functions = context()->functions();
    functions->glDrawArrays(GL_TRIANGLES, 0, m_rectangle_draw_arrays_count);

    m_rectangle_program.release();
}

void SplineWidget::draw_selected_area()
{
    if (m_selected_state == SelectedState::start_selected)
    {
        draw_rectangle_on_screen(screen_to_fragment(world_to_screen({ m_x_left_selected, m_y_bottom_selected })),
                                 screen_to_fragment(world_to_screen({ m_x_right_selected, m_y_top_selected })), 0.11f, selected_area_color, true);
    }
}

void SplineWidget::draw_text(double x, double y, const QString& text, QColor color)
{
    QPainter painter(this);
    painter.setPen(color);
    painter.drawText(x, y, text);
    painter.end();
}

void SplineWidget::draw_strip_line(const std::vector<float>& data, float z, QColor color, bool dotted /*= false*/)
{
    if (dotted)
    {
        glPushAttrib(GL_ENABLE_BIT);
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(1, 0x00FF);
    }

    draw_line(data, z, color, GL_LINE_STRIP);

    if (dotted)
    {
        glPopAttrib();
    }
}

void SplineWidget::draw_line(const std::vector<float>& data, float z, QColor color, GLenum mode)
{
    if (!m_line_program.bind())
    {
        qWarning() << "can\'t bind shader program";
        return;
    }

    m_line_program.setUniformValue(m_line_z_location, z);
    m_line_program.setUniformValue(m_line_in_color_location, color.redF(), color.greenF(), color.blueF(), color.alphaF());

    m_line_program.setUniformValue(m_line_x_left_location, static_cast<float>(m_x_left));
    m_line_program.setUniformValue(m_line_x_right_location, static_cast<float>(m_x_right));
    m_line_program.setUniformValue(m_line_y_bottom_location, static_cast<float>(m_y_bottom));
    m_line_program.setUniformValue(m_line_y_top_location, static_cast<float>(m_y_top));

    m_line_vao.bind();

    if (!m_line_vbo.bind())
    {
        qWarning() << "can\'t bind vbo";
    }

    m_line_vbo.bind();

    QOpenGLFunctions* functions = context()->functions();

    const int size = data.size();
    int render_size;
    int offset;

    int i = 0;

    for (int i = 0; size > i * m_line_vbo_capacity; ++i)
    {
        offset = i * m_line_vbo_capacity;
        render_size = size > (i + 1) * m_line_vbo_capacity ? m_line_vbo_capacity : (size - offset);

        m_line_vbo.write(0, data.data() + offset, render_size * sizeof(float));

        functions->glDrawArrays(mode, 0, render_size / 2);
    }

    m_line_vao.release();
    m_line_vbo.release();
    m_line_program.release();
}

math::vec2 SplineWidget::world_to_screen(math::vec2 coord)
{
    return { (coord.x - m_x_left) / (m_x_right - m_x_left) * width(), -(coord.y - m_y_top) / (m_y_top - m_y_bottom) * height() };
}

math::vec2 SplineWidget::screen_to_world(math::vec2 pos)
{
    return { m_x_left + (m_x_right - m_x_left) * pos.x / width(), m_y_top - (m_y_top - m_y_bottom) * pos.y / height() };
}

math::vec2 SplineWidget::screen_to_fragment(math::vec2 screen)
{
    return { screen.x, height() - screen.y };
}

float SplineWidget::world_x_to_screen_x(float x)
{
    return (x - m_x_left) / (m_x_right - m_x_left) * width();
}

float SplineWidget::world_y_to_screen_y(float y)
{
    return -(y - m_y_top) / (m_y_top - m_y_bottom) * height();
}

float SplineWidget::screen_x_to_world_x(float widget_x)
{
    return m_x_left + (m_x_right - m_x_left) * widget_x / width();
}

float SplineWidget::screen_y_to_world_y(float widget_y)
{
    return m_y_top - (m_y_top - m_y_bottom) * widget_y / height();
}

float SplineWidget::screen_x_to_fragment_x(float screen_x)
{
    return screen_x;
}

float SplineWidget::screen_y_to_fragment_y(float screen_y)
{
    return height() - screen_y;
}

void SplineWidget::move_selected_keys()
{
    const double dx = m_x_right_selected - m_x_left_selected;
    const double dy = m_y_top_selected - m_y_bottom_selected;
    AnimEngine::CurveIdToKeyframesMap key_map;
    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;

        if (curve_data.selected_keys.empty())
        {
            continue;
        }

        auto curve = m_current_engine->get_curve(it.first);
        ANIM_CURVES_CHECK_AND_CONTINUE(curve);
        auto& vector = key_map[it.first];
        auto map = curve->compute_id_to_idx_map();

        for (const auto key_id : curve_data.selected_keys)
        {
            adsk::Keyframe keyframe = (*curve)[map[key_id]];

            keyframe.time =
                m_is_auto_snap_time ? round_to(curve_data.key_pivots[key_id].x + dx, m_snap_x_interval) : curve_data.key_pivots[key_id].x + dx;

            keyframe.value =
                m_is_auto_snap_value ? round_to(curve_data.key_pivots[key_id].y + dy, m_snap_y_interval) : curve_data.key_pivots[key_id].y + dy;

            vector.push_back(keyframe);
        }
    }

    m_current_engine->set_keys_direct(key_map, false);
}

void SplineWidget::move_selected_tangents(float pos_x, float pos_y)
{
    AnimEngine::CurveIdToKeyframesMap key_map;
    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;
        if (curve_data.selected_tangents.empty())
        {
            continue;
        }

        auto curve = m_current_engine->get_curve(it.first);
        ANIM_CURVES_CHECK_AND_CONTINUE(curve);
        auto& vector = key_map[it.first];
        auto map = curve->compute_id_to_idx_map();

        for (SelectedTangent dir : curve_data.selected_tangents)
        {
            auto keyframe = (*curve)[map[dir.key_id]];
            math::vec2 pivot = curve_data.tangent_pivots[dir];
            double new_tan_x;
            double new_tan_y;

            if (dir.direction == dir.in)
            {
                new_tan_x = -(pos_x - pivot.x);
                new_tan_y = (pos_y - pivot.y);
            }
            else
            {
                new_tan_x = pos_x - pivot.x;
                new_tan_y = -(pos_y - pivot.y);
            }

            if (new_tan_x < 1)
            {
                new_tan_x = 1;
            }
            new_tan_y *= (m_y_top - m_y_bottom) / (m_x_right - m_x_left);
            norm(new_tan_x, new_tan_y);

            if (dir.direction == dir.in || !m_is_tangents_break)
            {
                keyframe.tanIn.type = adsk::TangentType::Fixed;
                keyframe.tanIn.x = new_tan_x;
                keyframe.tanIn.y = new_tan_y;
            }
            if (dir.direction == dir.out || !m_is_tangents_break)
            {
                keyframe.tanOut.type = adsk::TangentType::Fixed;
                keyframe.tanOut.x = new_tan_x;
                keyframe.tanOut.y = new_tan_y;
            }
            vector.push_back(keyframe);
        }
    }
    m_current_engine->set_keys_direct(key_map, false);
}

void SplineWidget::change_zoom(float zoom_delta)
{
    float length_x = m_x_right - m_x_left;
    float center_x = (m_x_right + m_x_left) / 2;
    float length_y = m_y_top - m_y_bottom;
    float center_y = (m_y_top + m_y_bottom) / 2;

    if ((length_x * zoom_delta > 1e-3) && ((length_y * zoom_delta > 1e-3)))
    {
        m_x_left = center_x - length_x * zoom_delta / 2;
        m_x_right = center_x + length_x * zoom_delta / 2;
        m_y_bottom = center_y - length_y * zoom_delta / 2;
        m_y_top = center_y + length_y * zoom_delta / 2;
    }
}

bool SplineWidget::curve_is_selected(AnimEngine::CurveId id, float x_left_selected, float x_right_selected, float y_bottom_selected,
                                     float y_top_selected, float dx)
{
    auto curve = m_current_engine->get_curve(id);

    if (curve->keyframeCount() == 0)
    {
        return false;
    }

    const double x0 = std::max(x_left_selected + dx / 2, (float)curve->at(0).time + dx / 2);
    const double x1 = std::min(x_right_selected + dx / 2, (float)curve->at(curve->keyframeCount() - 1).time);

    for (double x = x0; x < x1; x += dx)
    {
        double y = curve->evaluate(x);
        if ((y_bottom_selected <= y) && (y <= y_top_selected))
        {
            return true;
        }
    }
    return false;
}

void SplineWidget::update_selection(QMouseEvent* event)
{
    if ((m_selected_state == SelectedState::start_moving) || (m_selected_state == SelectedState::no_selected_area))
    {
        assert(!"SplineWidget::update_selection Error");
    }

    if (m_x_left_selected > m_x_right_selected)
    {
        std::swap(m_x_left_selected, m_x_right_selected);
    }
    if (m_y_bottom_selected > m_y_top_selected)
    {
        std::swap(m_y_bottom_selected, m_y_top_selected);
    }

    const bool is_point_selection = ((m_x_right_selected - m_x_left_selected) < 1e-6) && ((m_y_top_selected - m_y_bottom_selected) < 1e-6);

    if (is_point_selection)
    {
        const double dx = select_paint_size * (m_x_right - m_x_left) / height();
        const double dy = select_paint_size * (m_y_top - m_y_bottom) / width();
        m_x_right_selected += dx;
        m_x_left_selected -= dx;
        m_y_bottom_selected -= dy;
        m_y_top_selected += dy;
    }

    std::set<adsk::KeyId> new_selection_ids;
    std::set<adsk::KeyId> new_selection_indices;
    uint64_t num_selecting_points = 0;
    uint64_t num_selecting_tangents = 0;
    for (auto& it : m_displayed_curves_map)
    {
        CurveData& curve_data = it.second;

        auto curve = m_current_engine->get_curve(it.first);

        new_selection_ids.clear();
        // find selected keys
        for (uint32_t key_idx = 0; key_idx < curve->keyframeCount(); ++key_idx)
        {
            const auto& keyframe = (*curve)[key_idx];

            if ((m_x_left_selected <= keyframe.time) && (keyframe.time <= m_x_right_selected) && (m_y_bottom_selected <= keyframe.value) &&
                (keyframe.value <= m_y_top_selected))
            {
                if (!is_point_selection || (num_selecting_points == 0))
                {
                    new_selection_ids.insert(keyframe.id);
                    ++num_selecting_points;
                }
            }
        }

        for (adsk::KeyId key_id : new_selection_ids)
        {
            curve_data.selected_tangents.erase({ key_id, SelectedTangent::in });
            curve_data.selected_tangents.erase({ key_id, SelectedTangent::out });
        }

        if (curve_data.selected_keys.empty() && curve_data.selected_tangents.empty())
        {
            curve_data.selected_keys = new_selection_ids;
            if (!(event->modifiers() & Qt::ShiftModifier))
            {
                curve_data.selected_tangents.clear();
            }
        }
        else
        {
            if (!new_selection_ids.empty())
            {
                if (!(event->modifiers() & Qt::ShiftModifier))
                {
                    curve_data.selected_tangents.clear();
                }
            }
            else
            {
                auto& tangent_search_keys = curve_data.selected_keys;
                for (auto& tangent : curve_data.selected_tangents)
                {
                    tangent_search_keys.insert(tangent.key_id);
                }

                if (!(event->modifiers() & Qt::ShiftModifier))
                {
                    curve_data.selected_tangents.clear();
                }
                // find selected tangents
                auto id_to_idx = curve->compute_id_to_idx_map();
                for (auto& key_id : tangent_search_keys)
                {
                    auto key_idx = id_to_idx[key_id];
                    const math::vec2 in_pos = in_tangent_pos((*curve)[key_idx]);
                    if ((m_x_left_selected <= screen_x_to_world_x(in_pos.x)) && (screen_x_to_world_x(in_pos.x) <= m_x_right_selected) &&
                        (m_y_bottom_selected <= screen_y_to_world_y(in_pos.y)) && (screen_y_to_world_y(in_pos.y) <= m_y_top_selected))
                    {
                        if (curve_data.selected_tangents.count({ key_id, SelectedTangent::in }) == 0)
                        {
                            curve_data.selected_tangents.insert({ key_id, SelectedTangent::in });
                        }
                        else
                        {
                            curve_data.selected_tangents.erase({ key_id, SelectedTangent::in });
                        }
                    }

                    const math::vec2 out_pos = out_tangent_pos((*curve)[key_idx]);
                    if ((m_x_left_selected <= screen_x_to_world_x(out_pos.x)) && (screen_x_to_world_x(out_pos.x) <= m_x_right_selected) &&
                        (m_y_bottom_selected <= screen_y_to_world_y(out_pos.y)) && (screen_y_to_world_y(out_pos.y) <= m_y_top_selected))
                    {
                        if (curve_data.selected_tangents.count({ key_id, SelectedTangent::out }) == 0)
                        {
                            curve_data.selected_tangents.insert({ key_id, SelectedTangent::out });
                        }
                        else
                        {
                            curve_data.selected_tangents.erase({ key_id, SelectedTangent::out });
                        }
                    }
                }
            }

            for (auto tangent : curve_data.selected_tangents)
            {
                curve_data.selected_keys.erase(tangent.key_id);
            }

            if (event->modifiers() & Qt::ShiftModifier)
            {
                std::set<adsk::KeyId> xor_union;

                std::set_symmetric_difference(curve_data.selected_keys.begin(), curve_data.selected_keys.end(), new_selection_ids.begin(),
                                              new_selection_ids.end(), std::inserter(xor_union, xor_union.begin()));

                curve_data.selected_keys = xor_union;
            }
            else
            {
                curve_data.selected_keys = new_selection_ids;
            }
        }
        num_selecting_tangents += curve_data.selected_tangents.size();
    }

    if ((num_selecting_points == 0) && (num_selecting_tangents == 0))
    {
        for (auto& it : m_displayed_curves_map)
        {
            CurveData& curve_data = it.second;
            auto curve = m_current_engine->get_curve(it.first);

            if (curve_is_selected(it.first, m_x_left_selected, m_x_right_selected, m_y_bottom_selected, m_y_top_selected,
                                  (m_x_right - m_x_left) / width()))
            {
                for (uint32_t key_idx = 0; key_idx < curve->keyframeCount(); ++key_idx)
                {
                    curve_data.selected_keys.insert(curve->at(key_idx).id);
                }
            }
        }
    }
}

void SplineWidget::update_pivots(AnimEngineCurveCPtr curve, CurveData& curve_data)
{
    curve_data.key_pivots.clear();
    auto map = curve->compute_id_to_idx_map();
    for (auto key_id : curve_data.selected_keys)
    {
        const auto& keyframe = (*curve)[map[key_id]];
        curve_data.key_pivots[key_id] = { keyframe.time, keyframe.value };
    }

    curve_data.tangent_pivots.clear();
    for (auto t : curve_data.selected_tangents)
    {
        const auto keyframe = (*curve)[map[t.key_id]];
        const math::vec2 pos = { m_last_pos_x, m_last_pos_y };
        math::vec2 tan_pos;

        tan_pos = t.direction == SelectedTangent::in ? in_tangent_pos(keyframe) : tan_pos = out_tangent_pos(keyframe);

        const math::vec2 key_pos = world_to_screen({ keyframe.time, keyframe.value });
        curve_data.tangent_pivots[t].x = (pos.x + (key_pos.x - tan_pos.x));
        curve_data.tangent_pivots[t].y = (pos.y + (key_pos.y - tan_pos.y));
    }
    m_selected_state = SelectedState::start_moving;
}

AnimEngine::CurveIdToKeyframesMap SplineWidget::get_selected_keys()
{
    AnimEngine::CurveIdToKeyframesMap keys;
    for (auto& it : m_displayed_curves_map)
    {
        auto curve_id = it.first;
        CurveData& curve_data = it.second;
        auto curve = m_current_engine->get_curve(it.first);
        auto map = curve->compute_id_to_idx_map();
        for (auto id : it.second.selected_keys)
            keys[it.first].push_back(curve->at(map[id]));
    }
    return keys;
}

AnimEngine::CurveIdToKeysIdsMap SplineWidget::get_selected_keys_ids()
{
    AnimEngine::CurveIdToKeysIdsMap ids;
    for (auto& it : m_displayed_curves_map)
    {
        if (!it.second.selected_keys.empty())
        {
            ids[it.first] = it.second.selected_keys;
        }
    }
    return ids;
}

bool SplineWidget::is_cursor_on_key_or_tangent_pivot(AnimEngine::CurveId curve_id, float pos_x, float pos_y)
{
    CurveData& curve_data = m_displayed_curves_map[curve_id];
    auto curve = m_current_engine->get_curve(curve_id);

    for (uint32_t key_id = 0; key_id < curve->keyframeCount(); ++key_id)
    {
        const auto& keyframe = (*curve)[key_id];
        const float x_pixel = world_x_to_screen_x(keyframe.time);
        const float y_pixel = world_y_to_screen_y(keyframe.value);

        const math::vec2 pos = { x_pixel, y_pixel };
        const math::vec2 in_tan_pos = in_tangent_pos(keyframe);
        const math::vec2 out_tan_pos = out_tangent_pos(keyframe);

        if ((std::abs(pos_x - x_pixel) < select_paint_size) && (std::abs(pos_y - y_pixel) < select_paint_size) &&
            (curve_data.selected_keys.count(keyframe.id) > 0))
        {
            return true;
        }

        if ((std::abs(pos_x - in_tan_pos.x) < select_paint_size) && (std::abs(pos_y - in_tan_pos.y) < select_paint_size) &&
            (curve_data.selected_tangents.count({ keyframe.id, SelectedTangent::in }) > 0))
        {
            return true;
        }

        if ((std::abs(pos_x - out_tan_pos.x) < select_paint_size) && (std::abs(pos_y - out_tan_pos.y) < select_paint_size) &&
            (curve_data.selected_tangents.count({ keyframe.id, SelectedTangent::out }) > 0))
        {
            return true;
        }
    }
    return false;
}

math::vec2 SplineWidget::in_tangent_pos(const adsk::Keyframe& keyframe)
{
    const float x_pixel = world_x_to_screen_x(keyframe.time);
    const float y_pixel = world_y_to_screen_y(keyframe.value);
    float dx_in = -tangent_length * keyframe.tanIn.x;
    float dy_in = +tangent_length * keyframe.tanIn.y * height() / width() * ((m_x_right - m_x_left) / (m_y_top - m_y_bottom));
    norm<float>(dx_in, dy_in, tangent_length);
    return { x_pixel + dx_in, y_pixel + dy_in };
}

math::vec2 SplineWidget::out_tangent_pos(const adsk::Keyframe& keyframe)
{
    const float x_pixel = world_x_to_screen_x(keyframe.time);
    const float y_pixel = world_y_to_screen_y(keyframe.value);
    float dx_out = tangent_length * keyframe.tanOut.x;
    float dy_out = -tangent_length * keyframe.tanOut.y * height() / width() * ((m_x_right - m_x_left) / (m_y_top - m_y_bottom));
    norm<float>(dx_out, dy_out, tangent_length);
    return { x_pixel + dx_out, y_pixel + dy_out };
}

float SplineWidget::compute_grid_step(float t_min, float t_max, uint64_t num_pixels)
{
    const float dt_max = 20.0 * (t_max - t_min) / (float)num_pixels;
    const float n0 = round(std::log10(dt_max));
    const float n1 = round(std::log10(dt_max / 2));
    const float n2 = round(std::log10(dt_max / 5));

    const float dt0 = 1.0 * std::pow((float)10.0, (float)n0);
    const float dt1 = 2.0 * std::pow((float)10.0, (float)n1);
    const float dt2 = 5.0 * std::pow((float)10.0, (float)n2);

    return std::max(dt0, std::max(dt1, dt2));
}

void SplineWidget::start_command(const std::string& command_name)
{
    m_current_command = CommandRegistry::create_command<SplineWidgetCommand>(command_name);
    m_current_command->set_initial_state(m_displayed_curves_map);
}

void SplineWidget::end_command()
{
    if (!m_current_command)
    {
        return;
    }
    m_current_command->finalize();
    m_current_command->redo();
    CommandInterface::finalize(m_current_command);
    m_current_command = nullptr;
}

void SplineWidget::enable_gl_debug()
{
    QOpenGLContext* glcontext = context();
    QSurfaceFormat format = glcontext->format();

    if (format.majorVersion() <= 4 && format.minorVersion() < 3)
    {
        return;
    }

    QOpenGLExtraFunctions* functions = glcontext->extraFunctions();

    functions->glEnable(GL_DEBUG_OUTPUT);
    functions->glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    functions->glDebugMessageCallback(debug_message_callback, nullptr);
    functions->glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
}

void SplineWidget::initialize_grid()
{
    QOpenGLFunctions* functions = context()->functions();

    static const char* grid_vert = R"(
        #version 330 core

        uniform float z;

        uniform vec2 grid_planes[6] =
            vec2[]
            (
                vec2( 1,  1),
                vec2(-1,  1),
                vec2(-1, -1),

                vec2( 1,  1),
                vec2(-1, -1),
                vec2( 1, -1)
            );

        void main()
        {
            gl_Position = vec4(grid_planes[gl_VertexID], z, 1.0f);
        }
    )";

    static const char* grid_frag = R"(
        #version 330 core

        uniform vec4 grid_color;
        uniform vec4 axis_color;
        uniform vec4 current_time_color;
        uniform vec4 insert_key_color;

        uniform vec2 grid_rectangle_size;

        uniform vec2 origin;

        uniform float current_time_x;
        uniform float insert_key_x;

        uniform bool insert_key;

        out vec4 out_color;

        bool is_insert_key(vec2 frag_coord)
        {
            return floor(frag_coord.x) == floor(insert_key_x);
        }

        bool is_current_time(vec2 frag_coord)
        {
            return floor(frag_coord.x) == floor(current_time_x);
        }

        bool is_axis(vec2 frag_coord)
        {
            return floor(frag_coord.x) == floor(origin.x)
                || floor(frag_coord.y) == floor(origin.y);
        }

        bool is_grid_line(vec2 frag_coord)
        {
        	vec2 screen_pixel_coordinate = frag_coord - origin;
        	
        	vec2 grid_rectangle_coordinate = fract(screen_pixel_coordinate / grid_rectangle_size);
        	
        	vec2 grid_rectangle_pixel_coordinate = grid_rectangle_coordinate * grid_rectangle_size;
        
        	vec2 is_grid_line = step(grid_rectangle_pixel_coordinate, vec2(1.0));
        
        	return max(is_grid_line.x, is_grid_line.y) != 0.0;
        }
        
        void main()
        {
            vec2 frag_coord = gl_FragCoord.xy;

            if(insert_key && is_insert_key(frag_coord))
            {
	            out_color = insert_key_color;
            }
            else if(is_current_time(frag_coord))
            {
	            out_color = current_time_color;
            }
            else if(is_axis(frag_coord))
            {
	            out_color = axis_color;
            }
	        else if(is_grid_line(frag_coord))
            {
	            out_color = grid_color;
            }
            else
            {
                discard;
            }
        }
    )";

    if (!m_grid_program.create())
    {
        qWarning() << "can\'t create shader program";
    }
    if (!m_grid_program.addShaderFromSourceCode(QOpenGLShader::Vertex, grid_vert))
    {
        qWarning() << "can\'t add vertex shader";
    }
    if (!m_grid_program.addShaderFromSourceCode(QOpenGLShader::Fragment, grid_frag))
    {
        qWarning() << "can\'t add fragment shader";
    }
    if (!m_grid_program.link())
    {
        qWarning() << "can\'t link shader program";
    }
    if (!m_grid_program.bind())
    {
        qWarning() << "can\'t bind shader program";
    }

    m_grid_draw_arrays_count = 6;

    m_grid_z_location = m_grid_program.uniformLocation("z");

    m_grid_color_location = m_grid_program.uniformLocation("grid_color");
    m_grid_axis_color_location = m_grid_program.uniformLocation("axis_color");
    m_grid_current_time_color_location = m_grid_program.uniformLocation("current_time_color");

    m_grid_rectangle_size_location = m_grid_program.uniformLocation("grid_rectangle_size");

    m_grid_origin_location = m_grid_program.uniformLocation("origin");

    m_grid_current_time_x_location = m_grid_program.uniformLocation("current_time_x");

    m_grid_insert_key_color_location = m_grid_program.uniformLocation("insert_key_color");
    m_grid_insert_key_x_location = m_grid_program.uniformLocation("insert_key_x");
    m_grid_insert_key_location = m_grid_program.uniformLocation("insert_key");

    m_grid_program.release();
}

void SplineWidget::initialize_screen_rectangle()
{
    static const char* rectangle_vert = R"(
        #version 330 core

        uniform float z;

        uniform vec2 grid_planes[6] =
            vec2[]
            (
                vec2( 1,  1),
                vec2(-1,  1),
                vec2(-1, -1),

                vec2( 1,  1),
                vec2(-1, -1),
                vec2( 1, -1)
            );

        void main()
        {
            gl_Position = vec4(grid_planes[gl_VertexID], z, 1.0f);
        }
    )";

    static const char* rectangle_frag = R"(
        #version 330 core

        uniform vec4 rectangle_color;

        uniform vec2 left_down_coordinate;
        uniform vec2 right_up_coordinate;

        uniform bool need_draw_edge;

        out vec4 out_color;

        bool is_rectangle(vec2 frag_coord)
        {
            float x = frag_coord.x;
            float y = frag_coord.y;

            return (left_down_coordinate.x <= x && x <= right_up_coordinate.x)
                && (left_down_coordinate.y <= y && y <= right_up_coordinate.y);
        }

        bool is_rectangle_edge(vec2 frag_coord)
        {
            float x = frag_coord.x;
            float y = frag_coord.y;

            bool vertical = (floor(x) == floor(left_down_coordinate.x)
                          || floor(x) == floor(right_up_coordinate.x))
                          && (left_down_coordinate.y <= y && y <= right_up_coordinate.y);
            bool horizontal = (floor(y) == floor(left_down_coordinate.y)
                            || floor(y) == floor(right_up_coordinate.y ))
                           && (left_down_coordinate.x <= x && x <= right_up_coordinate.x);

            return vertical || horizontal;
        }

        void main()
        {
            vec2 frag_coord = gl_FragCoord.xy;

            if(need_draw_edge && is_rectangle_edge(frag_coord))
            {
	            out_color = rectangle_color;
            }
            else if(!need_draw_edge && is_rectangle(frag_coord))
            {
	            out_color = rectangle_color;
            }
            else
            {
                discard;
            }
        }
    )";

    if (!m_rectangle_program.create())
    {
        qWarning() << "can\'t create shader program";
    }
    if (!m_rectangle_program.addShaderFromSourceCode(QOpenGLShader::Vertex, rectangle_vert))
    {
        qWarning() << "can\'t add vertex shader";
    }
    if (!m_rectangle_program.addShaderFromSourceCode(QOpenGLShader::Fragment, rectangle_frag))
    {
        qWarning() << "can\'t add fragment shader";
    }
    if (!m_rectangle_program.link())
    {
        qWarning() << "can\'t link shader program";
    }
    if (!m_rectangle_program.bind())
    {
        qWarning() << "can\'t bind shader program";
    }

    m_rectangle_draw_arrays_count = 6;

    m_rectangle_z_location = m_rectangle_program.uniformLocation("z");

    m_rectangle_color_location = m_rectangle_program.uniformLocation("rectangle_color");

    m_rectangle_left_down_coordinate_location = m_rectangle_program.uniformLocation("left_down_coordinate");
    m_rectangle_right_up_coordinate_location = m_rectangle_program.uniformLocation("right_up_coordinate");

    m_rectangle_need_draw_edge_location = m_rectangle_program.uniformLocation("need_draw_edge");

    m_rectangle_program.release();
}

void SplineWidget::initialize_line()
{
    QOpenGLFunctions* functions = context()->functions();

    static const char* vec2_vert = R"(
        #version 330 core

        layout(location = 0) in vec2 coord;

        uniform float z;

        uniform float x_left;
        uniform float x_right;
        uniform float y_bottom;
        uniform float y_top;

        vec2 world_to_clip(vec2 vec)
        {
            return vec2(
                2 * (coord.x - x_left) / (x_right - x_left) - 1,
                2 * (coord.y - y_top)  / (y_top - y_bottom) + 1
            );
        }

        void main()
        {
            gl_Position = vec4(world_to_clip(coord), z, 1.0f);
        }
    )";

    static const char* vec2_frag = R"(
        #version 330 core

        uniform vec4 inColor;

        out vec4 outColor;

        void main()
        {
          outColor = inColor;
        }
    )";

    if (!m_line_program.create())
    {
        qWarning() << "can\'t create shader program";
    }
    if (!m_line_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vec2_vert))
    {
        qWarning() << "can\'t add vertex shader";
    }
    if (!m_line_program.addShaderFromSourceCode(QOpenGLShader::Fragment, vec2_frag))
    {
        qWarning() << "can\'t add fragment shader";
    }
    if (!m_line_program.link())
    {
        qWarning() << "can\'t link shader program";
    }
    if (!m_line_program.bind())
    {
        qWarning() << "can\'t bind shader program";
    }

    m_line_z_location = m_line_program.uniformLocation("z");
    m_line_in_color_location = m_line_program.uniformLocation("inColor");

    m_line_x_left_location = m_line_program.uniformLocation("x_left");
    m_line_x_right_location = m_line_program.uniformLocation("x_right");
    m_line_y_bottom_location = m_line_program.uniformLocation("y_bottom");
    m_line_y_top_location = m_line_program.uniformLocation("y_top");

    m_line_coord_location = 0;

    if (!m_line_vao.create())
    {
        qWarning() << "can\'t create vao";
    }

    if (!m_line_vbo.create())
    {
        qWarning() << "can\'t create vbo";
    }

    m_line_vao.bind();

    if (!m_line_vbo.bind())
    {
        qWarning() << "can\'t bind vbo";
    }

    m_line_vbo.setUsagePattern(QOpenGLBuffer::UsagePattern::DynamicDraw);
    m_line_vbo.allocate(m_line_vbo_capacity * sizeof(float));

    m_line_program.enableAttributeArray(m_line_coord_location);
    m_line_program.setAttributeBuffer(m_line_coord_location, GL_FLOAT, 0, 2, 0);

    m_line_vao.release();
    m_line_vbo.release();
}

OPENDCC_NAMESPACE_CLOSE
