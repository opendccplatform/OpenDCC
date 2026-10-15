// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/bezier_tool/bezier_curve.h"

#include "opendcc/app/core/application.h"
#include "opendcc/app/core/command_utils.h"
#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/app/viewport/viewport_refine_manager.h"
#include "opendcc/usd_editor/bezier_tool/utils.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using base::commands::CommandInterface;
using base::commands::CommandArgs;

static const float s_point_rect_size = 0.01f;
static int s_bezier_curve_refine_level = 2;

static const GfVec3f s_select_point_rect_color = GfVec3f(1.0f, 0.0f, 0.0f);
static const GfVec3f s_intersect_point_rect_color = GfVec3f(0.0f, 1.0f, 0.0f);
static const GfVec3f s_point_rect_color = GfVec3f(0.0f, 0.0f, 1.0f);
static const GfVec3f s_first_point_rect_color = GfVec3f(0.0f, 0.0f, 0.0f);

static const GfVec3f s_outside_tangent_line_color = GfVec3f(0.5f, 0.5f, 0.5f);

static const GfVec3f s_selected_tangent_line_color = GfVec3f(0.0f, 1.0f, 1.0f);
static const GfVec3f s_intersect_tangent_line_color = GfVec3f(1.0f, 0.0f, 1.0f);

//////////////////////////////////////////////////////////////////////////
// BezierCurve::Point
//////////////////////////////////////////////////////////////////////////

BezierCurve::Point::Point(const PXR_NS::GfVec3f& p)
    : ltangent(p)
    , point(p)
    , rtangent(p)
{
}

BezierCurve::Point::Point(const PXR_NS::GfVec3f& l, const PXR_NS::GfVec3f& p, const PXR_NS::GfVec3f& r)
    : ltangent(l)
    , point(p)
    , rtangent(r)
{
}

//////////////////////////////////////////////////////////////////////////
// BezierCurve::Tangent
//////////////////////////////////////////////////////////////////////////

bool operator==(const BezierCurve::Tangent& l, const BezierCurve::Tangent& r)
{
    return r.point_index == l.point_index && l.type == r.type;
}

bool operator!=(const BezierCurve::Tangent& l, const BezierCurve::Tangent& r)
{
    return !(l == r);
}

//////////////////////////////////////////////////////////////////////////
// BezierCurve
//////////////////////////////////////////////////////////////////////////

BezierCurve::BezierCurve() {}

BezierCurve::BezierCurve(const PXR_NS::UsdGeomBasisCurves& curve)
    : m_usd_curve(curve)
{
    m_usd_path = m_usd_curve.GetPath();

    const auto wrap = m_usd_curve.GetWrapAttr();
    TfToken wrap_token;
    if (wrap && wrap.Get(&wrap_token))
    {
        m_periodic = wrap_token == HdTokens->periodic;
    }

    auto points_attr = m_usd_curve.GetPointsAttr();
    if (!points_attr)
    {
        points_attr = m_usd_curve.CreatePointsAttr();
    }
    points_attr.Get(&m_points);
}

void BezierCurve::remove_point(const size_t point_index)
{
    const auto index = point_index * vstep;
    auto begin = m_points.begin() + index - 1;
    auto end = m_points.begin() + index - 1 + vstep;

    const auto first = 0;
    const auto last = size() - 1;
    const auto size = m_points.size();

    if (point_index == first)
    {
        begin += 1;
        if (size == segment_points_count)
        {
            std::swap(m_points[2], m_points[3]);
        }
        else if (size > segment_points_count)
        {
            end += 1;
        }
    }
    else if (point_index == last && !m_periodic)
    {
        end -= 1;
        if (size != segment_points_count)
        {
            begin -= 1;
        }
    }

#if PXR_VERSION > 1911
    m_points.erase(begin, end);
#else
    VtVec3fArray new_points;
    new_points.reserve(m_points.size());

    for (auto it = m_points.begin(); it < m_points.end(); ++it)
    {
        if (it >= begin && it < end)
        {
            continue;
        }

        new_points.push_back(*it);
    }

    m_points = new_points;
#endif

    if (m_periodic)
    {
        periodic_remove(point_index);
    }

    update_usd();
}

void BezierCurve::insert_point(const size_t point_index, const BezierCurve::Point& point)
{
    const auto first = 0;
    const auto points_count = m_points.size();
    if (point_index == size() && !m_periodic)
    {
        push_back(point);
    }
    else if (point_index == first && points_count < segment_points_count)
    {
        const auto addition_points = vstep - 1;
        const auto size = m_points.size();
        m_points.resize(size + addition_points);

        m_points[2] = m_points[1];
        m_points[3] = m_points[0];

        set_point(point_index, point);
    }
    else if (point_index == first)
    {
        const auto addition_points = vstep;
        const auto size = m_points.size();
        const auto tangent = compute_first_tangent();
        m_points.resize(size + addition_points);
        std::memcpy(m_points.data() + addition_points, m_points.data(), size * sizeof(GfVec3f));
        m_points[2] = tangent;
        set_point(point_index, point);
    }
    else
    {
        const auto addition_points = vstep;
        const auto size = m_points.size();
        m_points.resize(size + addition_points);
        const auto index = point_index * vstep;
        std::memcpy(m_points.data() + index + addition_points - 1, m_points.data() + index - 1, (size - index + 1) * sizeof(GfVec3f));
        set_point(point_index, point);
    }
}

void BezierCurve::push_back(const BezierCurve::Point& point)
{
    if (m_points.size() >= segment_points_count)
    {
        m_points.push_back(compute_last_tangent());
    }

    if (is_empty())
    {
        m_points.push_back(point.point);
        m_points.push_back(point.rtangent);
    }
    else
    {
        m_points.push_back(point.ltangent);
        m_points.push_back(point.point);
    }

    update_usd();
}

void BezierCurve::update_tangents(const size_t point_index, const PXR_NS::GfVec3f& ltangent, const PXR_NS::GfVec3f& rtangent)
{
    const auto first = 0;
    const auto last = size() - 1;
    const auto index = point_index * vstep;

    if (point_index == first)
    {
        m_points[index + 1] = rtangent;
    }
    else if (point_index == last)
    {
        m_points[index - 1] = ltangent;
    }
    else
    {
        m_points[index - 1] = ltangent;
        m_points[index + 1] = rtangent;
    }

    if (m_periodic)
    {
        periodic_update(point_index, { ltangent, m_points[index], rtangent });
    }

    update_usd();
}

void BezierCurve::set_point(const size_t point_index, const BezierCurve::Point& point)
{
    const auto first = 0;
    const auto last = size() - 1;
    const auto index = point_index * vstep;

    m_points[index + 0] = point.point;

    if (point_index == first)
    {
        m_points[index + 1] = point.rtangent;
    }
    else if (point_index == last)
    {
        m_points[index - 1] = point.ltangent;
    }
    else
    {
        m_points[index - 1] = point.ltangent;
        m_points[index + 1] = point.rtangent;
    }

    if (m_periodic)
    {
        periodic_update(point_index, point);
    }

    update_usd();
}

void BezierCurve::update_point(const BezierCurve::Tangent& tangent, const PXR_NS::GfVec3f& new_tangent, TangentMode mode)
{
    const auto point = this->get_point(tangent.point_index);
    GfVec3f other_side_tangent = {};

    switch (mode)
    {
    case OPENDCC_NAMESPACE::BezierCurve::TangentMode::Normal:
    {
        const auto t = -1.0f;
        other_side_tangent = point.point * (1 - t) + new_tangent * t;
        break;
    }
    case OPENDCC_NAMESPACE::BezierCurve::TangentMode::Weighted:
    {
        GfVec3f old_other_side_tangent = tangent.type == BezierCurve::Tangent::Type::Right ? point.ltangent : point.rtangent;
        const auto length = (point.point - old_other_side_tangent).GetLength();
        const auto direction = (point.point - new_tangent).GetNormalized();
        other_side_tangent = point.point + direction * length;
        break;
    }
    case OPENDCC_NAMESPACE::BezierCurve::TangentMode::Tangent:
    {
        other_side_tangent = tangent.type == BezierCurve::Tangent::Type::Right ? point.ltangent : point.rtangent;
        break;
    }
    }

    GfVec3f ltangent = new_tangent;
    GfVec3f rtangent = other_side_tangent;

    if (tangent.type == BezierCurve::Tangent::Type::Right)
    {
        std::swap(ltangent, rtangent);
    }

    update_tangents(tangent.point_index, ltangent, rtangent);
}

size_t BezierCurve::size()
{
    if (m_periodic)
    {
        return (m_points.size() - 1) / vstep;
    }
    else
    {
        return (m_points.size() + 2) / vstep;
    }
}

void BezierCurve::clear()
{
    m_points.clear();
    m_usd_curve = UsdGeomBasisCurves();
    m_usd_path = SdfPath();
}

void BezierCurve::open()
{
    if (m_periodic)
    {
        auto wrap = m_usd_curve.GetWrapAttr();
        if (!wrap)
        {
            wrap = m_usd_curve.CreateWrapAttr();
        }
        wrap.Set(HdTokens->nonperiodic);
        m_points.pop_back();
        m_points.pop_back();
        m_points.pop_back();
        m_periodic = false;

        update_usd();
    }
}

void BezierCurve::close()
{
    if (!m_periodic)
    {
        auto wrap = m_usd_curve.GetWrapAttr();
        if (!wrap)
        {
            wrap = m_usd_curve.CreateWrapAttr();
        }
        wrap.Set(HdTokens->periodic);
        m_points.push_back(compute_last_tangent());
        m_points.push_back(compute_first_tangent());
        m_points.push_back(m_points.front());
        m_periodic = true;

        update_usd();
    }
}

bool BezierCurve::is_close()
{
    return m_periodic;
}

bool BezierCurve::is_empty()
{
    return m_points.empty();
}

void BezierCurve::pop_back()
{
    if (m_points.size() <= segment_points_count)
    {
        m_points.pop_back();
        m_points.pop_back();
    }
    else
    {
        m_points.pop_back();
        m_points.pop_back();
        m_points.pop_back();
    }

    update_usd();
}

BezierCurve::Point BezierCurve::get_point(const size_t point_index)
{
    const auto first = 0;
    const auto last = size() - 1;

    const auto index = point_index * vstep;
    if (point_index == first)
    {
        return BezierCurve::Point(compute_first_tangent(), m_points[index + 0], m_points[index + 1]);
    }
    if (point_index == last)
    {
        return BezierCurve::Point(m_points[index - 1], m_points[index + 0], compute_last_tangent());
    }
    else
    {
        return BezierCurve::Point(m_points[index - 1], m_points[index + 0], m_points[index + 1]);
    }
}

BezierCurve::Point BezierCurve::last()
{
    if (is_empty())
    {
        return BezierCurve::Point();
    }
    if (m_points.size() < segment_points_count)
    {
        auto begin = m_points.begin();
        return BezierCurve::Point(compute_first_tangent(), *(begin), *(begin + 1));
    }
    else
    {
        auto end = m_points.end();
        return BezierCurve::Point(*(end - 2), *(end - 1), compute_last_tangent());
    }
}

BezierCurve::Point BezierCurve::first()
{
    if (is_empty())
    {
        return BezierCurve::Point();
    }

    auto begin = m_points.begin();
    return BezierCurve::Point(compute_first_tangent(), *(begin), *(begin + 1));
}

bool BezierCurve::intersect_curve_point(const PXR_NS::GfRay& ray, const ViewportViewPtr& viewport_view, size_t* point_index)
{
    const auto model = compute_model_matrix();

    const auto size = m_points.size();
    for (size_t i = 0; i < size; i += vstep)
    {
        const auto point = GfVec3f(model.Transform(m_points[i]));

        const auto closest = ray.FindClosestPoint(point);

        if (is_intersect(point, PXR_NS::GfVec3f(closest), viewport_view))
        {
            if (point_index)
            {
                *point_index = i / vstep;
            }

            return true;
        }
    }

    if (point_index)
    {
        *point_index = s_invalid_index;
    }

    return false;
}

bool BezierCurve::intersect_curve_tangent(const PXR_NS::GfRay& ray, const ViewportViewPtr& viewport_view, BezierCurve::Tangent* tangent_info)
{
    const auto model = compute_model_matrix();

    const auto points_count = m_points.size();
    for (size_t i = 0; i < points_count; ++i)
    {
        if (i % vstep == 0)
        {
            continue;
        }

        const auto point = GfVec3f(model.Transform(m_points[i]));

        const auto closest = ray.FindClosestPoint(point);

        if (is_intersect(point, PXR_NS::GfVec3f(closest), viewport_view))
        {
            if (tangent_info)
            {
                tangent_info->point_index = (i + 1) / vstep;
                tangent_info->type = (i + 1) % vstep ? Tangent::Type::Right : Tangent::Type::Left;

                if (m_periodic && tangent_info->point_index == size())
                {
                    tangent_info->point_index = 0;
                    tangent_info->type = Tangent::Type::Left;
                }
            }

            return true;
        }
    }

    if (tangent_info)
    {
        *tangent_info = Tangent();
    }

    return false;
}

const PXR_NS::SdfPath& BezierCurve::get_path()
{
    return m_usd_path;
}

void BezierCurve::draw(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager)
{
    const auto model = compute_model_matrix();
    const GfMatrix4d mvp = model * compute_view_projection(viewport_view);

    const auto w = viewport_view->get_viewport_dimensions().width;
    const auto h = viewport_view->get_viewport_dimensions().height;
    const auto x = s_point_rect_size;
    const auto y = (s_point_rect_size * w) / h;

    const auto offset = GfVec2f(x, y);

    const auto points_size = m_periodic ? m_points.size() - 3 : m_points.size();

    const auto intersect_point_index = m_intersect_point * vstep;
    const auto select_point_index = m_select_point * vstep;

    for (int i = 0; i < points_size; i += vstep)
    {
        if (i == intersect_point_index || i == select_point_index)
        {
            continue;
        }

        draw_screen_rect(m_points[i], mvp, draw_manager, offset, i == 0 && !m_periodic ? s_first_point_rect_color : s_point_rect_color);
    }

    const auto first = 0;
    const auto last = size() - 1;

    if (m_intersect_point != BezierCurve::s_invalid_index)
    {
        if (m_intersect_point != m_intersect_tangent.point_index)
        {
            auto ltangent_color = m_intersect_point || m_periodic ? s_intersect_point_rect_color : s_outside_tangent_line_color;
            auto rtangent_color =
                m_intersect_point && m_intersect_point == last && !m_periodic ? s_outside_tangent_line_color : s_intersect_point_rect_color;

            draw_tangents(viewport_view, draw_manager, m_intersect_point, ltangent_color, rtangent_color);
        }

        draw_screen_rect(m_points[intersect_point_index], mvp, draw_manager, offset,
                         !intersect_point_index && !m_periodic ? s_first_point_rect_color : s_intersect_point_rect_color);
    }

    if (m_select_point != BezierCurve::s_invalid_index && m_select_point != m_intersect_point)
    {
        if (m_select_point != m_intersect_tangent.point_index)
        {
            auto ltangent_color = m_select_point || m_periodic ? s_select_point_rect_color : s_outside_tangent_line_color;
            auto rtangent_color = m_select_point && m_select_point == last && !m_periodic ? s_outside_tangent_line_color : s_select_point_rect_color;

            draw_tangents(viewport_view, draw_manager, m_select_point, ltangent_color, rtangent_color);
        }

        draw_screen_rect(m_points[select_point_index], mvp, draw_manager, offset,
                         !select_point_index && !m_periodic ? s_first_point_rect_color : s_select_point_rect_color);
    }

    if (m_intersect_tangent.point_index != BezierCurve::s_invalid_index && m_intersect_tangent.point_index == m_select_point &&
        m_intersect_tangent != m_select_tangent)
    {
        auto ltangent_color =
            m_intersect_tangent.type == BezierCurve::Tangent::Type::Left ? s_intersect_tangent_line_color : s_select_point_rect_color;
        auto rtangent_color =
            m_intersect_tangent.type == BezierCurve::Tangent::Type::Right ? s_intersect_tangent_line_color : s_select_point_rect_color;

        if (m_intersect_tangent.point_index == first && !m_periodic)
        {
            ltangent_color = s_outside_tangent_line_color;
        }
        else if (m_intersect_tangent.point_index == last && !m_periodic)
        {
            rtangent_color = s_outside_tangent_line_color;
        }

        draw_tangents(viewport_view, draw_manager, m_select_point, ltangent_color, rtangent_color);
    }

    if (m_select_tangent.point_index != BezierCurve::s_invalid_index && m_select_tangent.point_index == m_select_point)
    {
        auto ltangent_color =
            m_intersect_tangent.type == BezierCurve::Tangent::Type::Left ? s_selected_tangent_line_color : s_select_point_rect_color;
        auto rtangent_color =
            m_intersect_tangent.type == BezierCurve::Tangent::Type::Right ? s_selected_tangent_line_color : s_select_point_rect_color;

        if (m_intersect_tangent.point_index == first && !m_periodic)
        {
            ltangent_color = s_outside_tangent_line_color;
        }
        else if (m_intersect_tangent.point_index == last && !m_periodic)
        {
            rtangent_color = s_outside_tangent_line_color;
        }

        draw_tangents(viewport_view, draw_manager, m_select_point, ltangent_color, rtangent_color);
    }
}

void BezierCurve::set_select_point(size_t index)
{
    m_select_point = index;
}

void BezierCurve::set_intersect_point(size_t index)
{
    m_intersect_point = index;
}

void BezierCurve::set_intersect_tangent(const Tangent& tangent)
{
    m_intersect_tangent = tangent;
}

void BezierCurve::set_select_tangent(const Tangent& tangent)
{
    m_select_tangent = tangent;
}

PXR_NS::GfMatrix4d BezierCurve::compute_model_matrix() const
{
    if (!m_usd_curve)
    {
        return PXR_NS::GfMatrix4d();
    }

    auto& app = Application::instance();
    const auto time = app.get_current_time();
    return m_usd_curve.ComputeLocalToWorldTransform(time);
}

void BezierCurve::draw_tangents(const ViewportViewPtr& viewport_view, ViewportUiDrawManager* draw_manager, const int point_index,
                                const PXR_NS::GfVec3f& ltangent_color, const PXR_NS::GfVec3f& rtangent_color)
{
    if (vstep * point_index >= m_points.size())
    {
        return;
    }

    const auto w = viewport_view->get_viewport_dimensions().width;
    const auto h = viewport_view->get_viewport_dimensions().height;
    const auto x = s_point_rect_size;
    const auto y = (s_point_rect_size * w) / h;

    const auto offset = GfVec2f(x, y);

    const auto index = point_index * vstep;
    const auto first = 0;
    const auto last = size() - 1;

    GfVec3f ltangent;
    auto point = m_points[index + 0];
    GfVec3f rtangent;

    if (point_index == first)
    {
        ltangent = compute_first_tangent();
        rtangent = m_points[index + 1];
    }
    else if (point_index == last)
    {
        ltangent = m_points[index - 1];
        rtangent = compute_last_tangent();
    }
    else
    {
        ltangent = m_points[index - 1];
        rtangent = m_points[index + 1];
    }

    const auto model = compute_model_matrix();
    const GfMatrix4d mvp = model * compute_view_projection(viewport_view);

    draw_screen_rect(ltangent, mvp, draw_manager, offset, ltangent_color);
    draw_screen_rect(rtangent, mvp, draw_manager, offset, rtangent_color);

    draw_screen_line(ltangent, point, mvp, draw_manager, ltangent_color);
    draw_screen_line(rtangent, point, mvp, draw_manager, rtangent_color);
}

void BezierCurve::draw_screen_rect(const PXR_NS::GfVec3f& world_rect_center, const PXR_NS::GfMatrix4d view_projection,
                                   ViewportUiDrawManager* draw_manager, const GfVec2f& offset, const PXR_NS::GfVec3f& color)
{
    auto screen = GfVec4d(world_rect_center[0], world_rect_center[1], world_rect_center[2], 1.0) * view_projection;
    if (!screen[3])
    {
        return;
    }

    GfVec2f clip(screen[0] / screen[3], screen[1] / screen[3]);

    draw_manager->begin_drawable();
    draw_manager->set_color(color);
    draw_manager->rect2d(clip + offset, clip - offset);
    draw_manager->end_drawable();
}

void BezierCurve::draw_screen_line(const PXR_NS::GfVec3f& world_line_begin, const PXR_NS::GfVec3f& world_line_end,
                                   const PXR_NS::GfMatrix4d view_projection, ViewportUiDrawManager* draw_manager, const PXR_NS::GfVec3f& color)
{
    const auto screen_begin = GfVec4d(world_line_begin[0], world_line_begin[1], world_line_begin[2], 1.0) * view_projection;
    const auto screen_end = GfVec4d(world_line_end[0], world_line_end[1], world_line_end[2], 1.0) * view_projection;
    if (!screen_begin[3] || !screen_end[3])
    {
        return;
    }

    const auto clip_begin = GfVec2f(screen_begin[0] / screen_begin[3], screen_begin[1] / screen_begin[3]);
    const auto clip_end = GfVec2f(screen_end[0] / screen_end[3], screen_end[1] / screen_end[3]);

    draw_manager->begin_drawable();
    draw_manager->set_prim_type(ViewportUiDrawManager::PrimitiveTypeLines);
    draw_manager->set_color(color);
    draw_manager->line(GfVec3f(clip_begin[0], clip_begin[1], 0), GfVec3f(clip_end[0], clip_end[1], 0));
    draw_manager->end_drawable();
}

bool BezierCurve::is_intersect(const PXR_NS::GfVec3f& curve_point, const PXR_NS::GfVec3f& point, const ViewportViewPtr& viewport_view)
{
    const auto w = viewport_view->get_viewport_dimensions().width;
    const auto h = viewport_view->get_viewport_dimensions().height;
    const auto x = s_point_rect_size;
    const auto y = (s_point_rect_size * w) / h;

    const GfMatrix4d view_projection = compute_view_projection(viewport_view);

    const auto curve_point_screen = GfVec4d(curve_point[0], curve_point[1], curve_point[2], 1.0) * view_projection;
    if (!curve_point_screen[3])
    {
        return false;
    }

    const GfVec2f curve_point_clip(curve_point_screen[0] / curve_point_screen[3], curve_point_screen[1] / curve_point_screen[3]);

    const auto screen = GfVec4d(point[0], point[1], point[2], 1.0) * view_projection;
    if (!screen[3])
    {
        return false;
    }

    const GfVec2f clip(screen[0] / screen[3], screen[1] / screen[3]);

    return std::abs(clip[0] - curve_point_clip[0]) < x && std::abs(clip[1] - curve_point_clip[1]) < y;
}

void BezierCurve::update_usd()
{
    const auto stage = Application::instance().get_session()->get_current_stage();

    if (!stage)
    {
        return;
    }

    if (!m_points.size())
    {
        return;
    }

    if (!m_usd_curve)
    {
        if (!m_usd_path.IsEmpty())
        {
            m_usd_curve = UsdGeomBasisCurves(stage->GetPrimAtPath(m_usd_path));
        }

        if (!m_usd_curve)
        {
            m_usd_path =
                *CommandInterface::execute("create_prim", CommandArgs().arg(TfToken("Curve")).arg(TfToken("BasisCurves"))).get_result<SdfPath>();
            m_usd_curve = UsdGeomBasisCurves(stage->GetPrimAtPath(m_usd_path));
            m_usd_curve.ClearXformOpOrder();
        }

        auto basis = m_usd_curve.GetBasisAttr();
        if (!basis)
        {
            basis = m_usd_curve.CreateBasisAttr();
        }
        basis.Set(TfToken("bezier"));

        auto type = m_usd_curve.GetTypeAttr();
        if (!type)
        {
            type = m_usd_curve.CreateTypeAttr();
        }
        type.Set(TfToken("cubic"));

        auto purpose = m_usd_curve.GetPurposeAttr();
        if (!purpose)
        {
            purpose = m_usd_curve.CreatePurposeAttr();
        }
        purpose.Set(TfToken("default"));
    }

    if (UsdViewportRefineManager::instance().get_refine_level(stage, m_usd_path) != s_bezier_curve_refine_level)
    {
        // Sometimes, the use of set_refine_level after creating UsdGeomBasisCurves may not work as intended.
        // This is due to the fact that ViewportUsdDelegate::m_usd_refine_handle relies on HdRenderIndex,
        // and UsdGeomBasisCurves may not be included in the index yet.
        UsdViewportRefineManager::instance().set_refine_level(stage, m_usd_path, s_bezier_curve_refine_level);
    }

    auto points_attr = m_usd_curve.GetPointsAttr();
    if (!points_attr)
    {
        points_attr = m_usd_curve.CreatePointsAttr();
    }
    auto vertex_counts = m_usd_curve.GetCurveVertexCountsAttr();
    if (!vertex_counts)
    {
        vertex_counts = m_usd_curve.CreateCurveVertexCountsAttr();
    }

    if (size() == 1)
    {
        VtIntArray count = { 1 };
        vertex_counts.Set(VtValue(count));
        points_attr.Set(VtValue(VtVec3fArray({ m_points.front() })));
    }
    else
    {
        VtIntArray count = { static_cast<int>(m_points.size()) };
        vertex_counts.Set(VtValue(count));
        points_attr.Set(VtValue(m_points));
    }

    auto widths_attr = m_usd_curve.GetWidthsAttr();
    VtFloatArray widths;
    widths_attr.Get(&widths);

    auto extend = m_usd_curve.GetExtentAttr();
    if (!extend)
    {
        extend = m_usd_curve.CreateExtentAttr();
    }
    VtVec3fArray extent;
    if (UsdGeomCurves::ComputeExtent(m_points, widths, &extent))
    {
        extend.Set(extent);
    }
}

void BezierCurve::periodic_update(const size_t point_index, const BezierCurve::Point& point)
{
    const auto first = 0;
    const auto last = size() - 1;

    auto end = m_points.end();

    if (point_index == first)
    {
        *(end - 1) = point.point;
        *(end - 2) = point.ltangent;
    }
    else if (point_index == last)
    {
        *(end - 3) = point.rtangent;
    }
}

void BezierCurve::periodic_remove(const size_t point_index)
{
    const auto first = 0;
    const auto was_last = size();
    if (point_index == first)
    {
        auto wrap = m_usd_curve.GetWrapAttr();
        if (!wrap)
        {
            wrap = m_usd_curve.CreateWrapAttr();
        }
        wrap.Set(HdTokens->nonperiodic);
        m_periodic = false;
    }
    else if (point_index == was_last && was_last == 1)
    {
        auto wrap = m_usd_curve.GetWrapAttr();
        if (!wrap)
        {
            wrap = m_usd_curve.CreateWrapAttr();
        }
        wrap.Set(HdTokens->nonperiodic);
        m_points.pop_back();
        m_points.pop_back();
        m_periodic = false;
    }
}

PXR_NS::GfVec3f BezierCurve::compute_first_tangent()
{
    if (!size())
    {
        return PXR_NS::GfVec3f();
    }

    if (m_periodic)
    {
        return *(m_points.end() - 2);
    }

    auto begin = m_points.begin();
    const auto point = *(begin);
    const auto rtangent = *(begin + 1);

    const auto t = -1.0f;
    return point * (1 - t) + rtangent * t;
}

PXR_NS::GfVec3f BezierCurve::compute_last_tangent()
{
    if (!size())
    {
        return PXR_NS::GfVec3f();
    }

    if (m_periodic)
    {
        return *(m_points.end() - 3);
    }

    auto end = m_points.end();
    const auto point = *(end - 1);
    const auto ltangent = *(end - 2);

    const auto t = -1.0f;
    return point * (1 - t) + ltangent * t;
}

OPENDCC_NAMESPACE_CLOSE
