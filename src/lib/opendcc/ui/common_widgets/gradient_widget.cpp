// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/common_widgets/qt_compat.h"

#include <QScreen>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include "opendcc/ui/common_widgets/gradient_widget.h"
#include "opendcc/ui/common_widgets/ramp.h"
#include "opendcc/ui/common_widgets/ramp_widget.h"
#include "opendcc/ui/common_widgets/color_widget.h"
#include "opendcc/app/ui/application_ui.h"

#include <QMouseEvent>
#include <QPainter>
#include <QBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    QColor operator*(QColor right, float left)
    {
        return QColor::fromRgbF(right.redF() * left, right.greenF() * left, right.blueF() * left, right.alphaF() * left);
    }

    QColor operator+(QColor right, QColor left)
    {
        return QColor::fromRgbF(right.redF() + left.redF(), right.greenF() + left.greenF(), right.blueF() + left.blueF(),
                                right.alphaF() + left.alphaF());
    }

    template <class T>
    T lerp(const T& left, const T& right, double t)
    {
        return left * (1 - t) + right * t;
    }

    QColor to_qcolor(const PXR_NS::GfVec3f& color)
    {
        return QColor::fromRgbF(color[0], color[1], color[2]);
    }
    GfVec3f to_vec3(const QColor& color)
    {
        return GfVec3f(color.redF(), color.greenF(), color.blueF());
    }
};

QSizeF const GradientWidget::s_point_size(7, 7);
int const GradientWidget::s_point_border = 1;
int const GradientWidget::s_hovered_point_border = 2;
int const GradientWidget::s_point_active_zone = 3;

GradientWidget::GradientWidget()
    : GradientWidget(std::make_shared<Ramp<PXR_NS::GfVec3f>>())
{
}

GradientWidget::GradientWidget(std::shared_ptr<RampV3f> color_ramp)
{
    setMinimumSize(180, 60);
    setMouseTracking(true); // to track a point to highlight
    m_color_ramp = color_ramp ? color_ramp : std::make_shared<Ramp<PXR_NS::GfVec3f>>();

    if (m_color_ramp->cv().size() < 3)
    {
        m_color_ramp->add_point(0, GfVec3f(1, 0, 0), RampV3f::kLinear);
        m_color_ramp->add_point(1, GfVec3f(0, 0, 1), RampV3f::kLinear);
    }

    m_gradient_rect.setTopLeft({ s_point_size.width() / 2 + s_point_border, s_point_size.height() / 2 + s_point_border });
    update_gradient_rect();
}

void GradientWidget::paintEvent(QPaintEvent*)
{
    auto const x = m_gradient_rect.left();
    auto const y = m_gradient_rect.top();
    auto const w = m_gradient_rect.width();
    auto const h = m_gradient_rect.height();

    static QColor const widget_base_color = qRgb(42, 42, 42);
    static QColor const active_point_color = qRgb(128, 128, 128);
    static QColor const normal_point_color = qRgb(0, 0, 0);

    QPen pen;
    pen.setColor(widget_base_color);

    QBrush brush;
    brush.setColor(widget_base_color);
    brush.setStyle(Qt::BrushStyle::DiagCrossPattern);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.drawRect(x, y, w, h);

    QImage image;
    fill_image(image, w, h);
    painter.drawImage(x, y, image);

    auto painting = [&](RampV3f::CV const& val) {
        brush.setColor(to_qcolor(val.value));
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);

        if (val.id == m_hovered || val.id == m_active)
            pen.setColor(active_point_color);
        else
            pen.setColor(normal_point_color);

        if (val.id == m_hovered)
            pen.setWidth(s_hovered_point_border);
        else
            pen.setWidth(s_point_border);

        painter.setPen(pen);

        auto val_x = x + val.position * w;
        auto val_y = y - s_point_size.height() / 2;
        painter.drawEllipse(QRectF(QPoint(val_x - s_point_size.width() / 2, val_y), s_point_size));
        painter.drawRect(QRectF(QPoint(val_x - s_point_size.width() / 2, y + h), s_point_size));
    };

    for (size_t i = 1; i < m_color_ramp->cv().size() - 1; ++i)
    {
        auto const& val = m_color_ramp->cv()[i];
        if (val.id != m_active)
            painting(val);
    }
    if (m_active >= 0)
        painting(m_color_ramp->cv(m_active)); // active point is always the last one here to stay always on top (only visually)
}

void GradientWidget::mousePressEvent(QMouseEvent* e)
{
    start_changing();

    float const point_x = e->x();
    float const point_y = e->y();

    m_selected = find_point(point_x, point_y);
    if (m_selected >= 0)
    {
        m_hovered = m_active = m_selected;
        point_selected(m_selected);
    }
    else
    {
        int remove_id = find_point_to_remove(point_x, point_y);
        if (remove_id >= 0)
        {
            remove_point(remove_id);
            point_selected(m_active);
        }
        else
        {
            add_point(point_x);
            point_selected(m_selected);
        }
    }

    update();
}

void GradientWidget::mouseMoveEvent(QMouseEvent* e)
{
    float const point_x = e->x();
    float const point_y = e->y();

    if (m_selected >= 0)
    {
        auto at_pos = clamp((point_x - m_gradient_rect.left()) / m_gradient_rect.width(), 0.0, 1.0);
        m_color_ramp->cv(m_selected).position = at_pos;

        m_hovered = m_selected;
        changing();
    }
    else
    {
        m_hovered = find_point(point_x, point_y); // either a point itself...
        if (m_hovered < 0)
            m_hovered = find_point_to_remove(point_x, point_y); // or its' remove button
    }

    update();
}

void GradientWidget::mouseReleaseEvent(QMouseEvent*)
{
    m_selected = -1;
    changing();
    end_changing();

    update();
}

void GradientWidget::leaveEvent(QEvent* e)
{
    m_hovered = -1;
    update();
}

void GradientWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update_gradient_rect();
    update();
}

int GradientWidget::find_point(float point_x, float point_y)
{
    auto const x = m_gradient_rect.left();
    auto const y = m_gradient_rect.top();
    auto const w = m_gradient_rect.width();
    auto const h = m_gradient_rect.height();

    for (size_t i = m_color_ramp->cv().size() - 2; i > 0; --i) //! the reverse loop is to start looking from the top. Painting was in a normal loop
    {
        auto const& val = m_color_ramp->cv()[i];
        auto cv_point_x = x + val.position * w - s_point_border;
        auto cv_point_y = y - s_point_border;
        QPointF pos(cv_point_x, cv_point_y);
        QPointF vec = pos - QPointF { point_x, point_y };

        if (abs(vec.x()) < (s_point_size.width() / 2 + s_point_border + s_point_active_zone) &&
            point_y < (y + h - s_point_active_zone)) // -s_point_active_zone is for remove button
            return val.id;
    }

    return -1;
}

int GradientWidget::find_point_to_remove(float point_x, float point_y)
{
    auto const x = m_gradient_rect.left();
    auto const y = m_gradient_rect.top();
    auto const w = m_gradient_rect.width();
    auto const h = m_gradient_rect.height();

    for (size_t i = m_color_ramp->cv().size() - 2; i > 0; --i) //! the reverse loop is to start looking from the top. Painting was in a normal loop
    {
        auto const& val = m_color_ramp->cv()[i];
        auto cv_point_x = x + val.position * w - s_point_border;
        auto cv_point_y = y + h + s_point_size.height() / 2 - s_point_border;
        QPointF pos(cv_point_x, cv_point_y);
        QPointF vec = pos - QPointF { point_x, point_y };

        if (abs(vec.x()) < (s_point_size.width() / 2 + s_point_border + s_point_active_zone) &&
            abs(vec.y()) < (s_point_size.height() / 2 + s_point_border + s_point_active_zone))
            return val.id;
    }

    return -1;
}

int GradientWidget::find_point_helper(float point_x, float point_y, int point_h)
{
    auto const x = m_gradient_rect.left();
    auto const y = m_gradient_rect.top();
    auto const w = m_gradient_rect.width();
    auto const h = m_gradient_rect.height();

    for (int i = m_color_ramp->cv().size() - 2; i > 0; --i) //! the reverse loop is to start looking from the top. Painting was in a normal loop
    {
        const auto& val = m_color_ramp->cv()[i];
        QPointF pos = { x + val.position * w, static_cast<qreal>(point_h) };
        QPointF vec = pos - QPointF { point_x, point_y };

        if (abs(vec.x()) < (s_point_size.width() / 2 + s_point_border + s_point_active_zone) && point_y < (y + h - s_point_active_zone))
            return i;
    }

    return -1;
}

void GradientWidget::remove_point(int ind)
{
    if (ind < 0)
        return;

    m_hovered = 0;
    if (m_active == ind) // going to remove the active point
    {
        if (m_color_ramp->cv().size() > 3) // not the last one
        {
            size_t remove_index = 0; // need to find the index within cv()
            for (size_t i = 1; i < m_color_ramp->cv().size() - 1; ++i)
            {
                if (m_color_ramp->cv()[i].id == ind)
                {
                    remove_index = i;
                    break;
                }
            }
            m_active = m_color_ramp->cv()[remove_index % (m_color_ramp->cv().size() - 2) + 1].id; // take the next one as a new active point
        }
        else
            m_active = -1;
    }

    m_color_ramp->remove_point(ind);
}

const Ramp<GfVec3f>::CV& GradientWidget::get_point(int id) const
{
    return m_color_ramp->cv(id);
}

Ramp<GfVec3f>::CV& GradientWidget::get_point(int id)
{
    return m_color_ramp->cv(id);
}

void GradientWidget::fill_image(QImage& dest, int w, int h)
{
    m_color_ramp->prepare_points();

    auto data = new uchar[w * h * 4];

    for (int i = 0; i < w; i++)
    {
        auto color = (!m_color_ramp->cv().empty() ? m_color_ramp->value_at(float(i) / float(w)) : GfVec3f(0, 0, 0));
        data[i * 4 + 0] = clamp(color[2], 0.f, 1.f) * 255;
        data[i * 4 + 1] = clamp(color[1], 0.f, 1.f) * 255;
        data[i * 4 + 2] = clamp(color[0], 0.f, 1.f) * 255;
        data[i * 4 + 3] = 255;
    }

    for (int i = 1; i < h; i++)
        memcpy(data + i * w * 4, data, w * 4 * sizeof(char));

    dest = QImage((uchar*)data, w, h, QImage::Format::Format_ARGB32);
}

void GradientWidget::update_gradient_rect()
{
    auto const w = width() - (s_point_size.width() / 2 + s_point_border) * 2;
    auto const h = height() - (s_point_size.height() + s_point_border * 2) * 1.5; // 1.5 = half of a point and a whole remove point
    m_gradient_rect.setSize({ w, h });
}

void GradientWidget::add_point(double pos, const GfVec3f& color, RampV3f::InterpType interp)
{
    m_color_ramp->add_point(pos, color, interp);
}

void GradientWidget::add_point(double pos)
{
    auto const at_pos = clamp((pos - m_gradient_rect.left()) / m_gradient_rect.width(), 0.0, 1.0);
    auto down_ind = -1;
    auto up_ind = -1;

    for (int i = 0; i < m_color_ramp->cv().size(); i++)
    {
        const auto& val = m_color_ramp->cv()[i];
        if (val.position < at_pos)
        {
            if (down_ind < 0 || m_color_ramp->cv()[down_ind].position < val.position)
                down_ind = i;
        }
        else if (val.position > at_pos)
        {
            if (up_ind < 0 || m_color_ramp->cv()[up_ind].position > val.position)
                up_ind = i;
        }
    }

    auto const& up_ind_point = m_color_ramp->cv()[up_ind];
    auto const& down_ind_point = m_color_ramp->cv()[down_ind];
    const auto new_color = down_ind < 0 ? up_ind_point.value
                           : up_ind < 0 ? down_ind_point.value
                                        : lerp(down_ind_point.value, up_ind_point.value,
                                               (at_pos - down_ind_point.position) / (up_ind_point.position - down_ind_point.position));

    m_color_ramp->add_point(at_pos, new_color, RampV3f::kLinear);
    m_selected = m_color_ramp->cv().back().id;
    m_hovered = m_active = m_selected;
}

void GradientWidget::clear()
{
    m_color_ramp->clear();
}

//////////////////////////////////////////////////////////////////////////

GradientEditor::GradientEditor(std::shared_ptr<RampV3f> color_ramp, ColorPickDialog* color_dialog /* = nullptr*/)
{
    setContentsMargins(0, 0, 0, 0);
    setMinimumSize(200, 110);
    auto main_layout = new QVBoxLayout();
    main_layout->setContentsMargins(0, 0, 0, 0);
    setLayout(main_layout);
    m_gradient_widget = new GradientWidget(color_ramp);

    main_layout->addWidget(m_gradient_widget);
    connect(m_gradient_widget, SIGNAL(point_selected(int)), this, SLOT(slot_point_selected(int)));
    connect(m_gradient_widget, SIGNAL(start_changing()), this, SLOT(slot_start_changing()));
    connect(m_gradient_widget, SIGNAL(changing()), this, SLOT(slot_changing()));
    connect(m_gradient_widget, SIGNAL(end_changing()), this, SLOT(slot_end_changing()));

    connect(m_gradient_widget, SIGNAL(point_selected(int)), this, SLOT(point_selected(int)));
    connect(m_gradient_widget, SIGNAL(changing()), this, SLOT(point_update()));

    auto controls_layout = new QHBoxLayout();
    main_layout->addLayout(controls_layout);
    controls_layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding));

    auto layout = new QVBoxLayout();
    controls_layout->addLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto editors = new QHBoxLayout();
    editors->setContentsMargins(0, 0, 0, 0);
    editors->setSpacing(1);
    auto editor_layout = new QHBoxLayout();
    setup_position_editor(editor_layout);
    setup_value_editor(editor_layout, color_dialog);
    editors->addLayout(editor_layout);
    layout->addLayout(editors);

    editor_layout = new QHBoxLayout();
    setup_interpolation_widget(editor_layout);
    layout->addLayout(editor_layout);
}

GradientEditor::GradientEditor(ColorPickDialog* color_dialog /* = nullptr*/)
    : GradientEditor(std::make_shared<Ramp<PXR_NS::GfVec3f>>(), color_dialog)
{
}

void GradientEditor::setup_position_editor(QBoxLayout* layout)
{
    layout->addWidget(new QLabel(i18n("ui.gradient_widget", "Position:")));
    m_pos_editor = new FloatWidget(this);
    m_pos_editor->setRange(0, 1);
    m_pos_editor->setSingleStep(0.1);
    m_pos_editor->setDisabled(true);
    layout->addWidget(m_pos_editor);
    connect(m_pos_editor, SIGNAL(valueChanged(double)), this, SLOT(position_changed(double)));
}

void GradientEditor::setup_value_editor(QBoxLayout* layout, ColorPickDialog* color_dialog)
{
    layout->addWidget(new QLabel(i18n("ui.gradient_widget", " Value:")));
    m_value_editor = new CanvasWidget;
    m_value_editor->setFixedSize(50, 20);
    m_value_editor->setDisabled(true);
    layout->addWidget(m_value_editor);

    ColorPickDialog* dialog = color_dialog ? color_dialog : new ColorPickDialog();

    connect(dialog, &ColorPickDialog::changing_color, this, [this, dialog] {
        if (m_selected < 0)
            return;

        if (!m_changing)
            start_changing();
        auto color = dialog->color();
        m_gradient_widget->get_point(m_selected).value = to_vec3(color);
        m_select_color = to_vec3(color);
        changing();
        if (!m_changing)
            end_changing();
        update();
    });

    m_value_editor->paint_event = [this](QPaintEvent*) {
        QPen pen;
        QBrush brush;
        float w = m_value_editor->width();
        float h = m_value_editor->height();
        float border = 2;
        QPainter painter(m_value_editor);
        pen.setColor({ 42, 42, 42 });
        pen.setStyle(Qt::PenStyle::NoPen);
        brush.setColor(to_qcolor(m_select_color));
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(pen);
        painter.setBrush(brush);
        painter.drawRect(border, border, w - border * 2, h - border * 2);
    };

    m_value_editor->mouse_press_event = [this, dialog](QMouseEvent* e) mutable {
        QRect rec = primary_screen_geometry();
        auto height = rec.height();
        auto width = rec.width();

        dialog->color(to_qcolor(m_select_color));
        int x = QCursor::pos().x() - dialog->width();
        int y = QCursor::pos().y();

        if (x < 0)
            x = 0;
        if (y > height - dialog->height())
            y = height - dialog->height();

        dialog->move(x, y);
        dialog->show();
        dialog->setFocus(Qt::ActiveWindowFocusReason);
    };
}

void GradientEditor::setup_interpolation_widget(QBoxLayout* layout)
{
    layout->addWidget(new QLabel(i18n("ui.gradient_widget", "Interpolation type:")));
    m_combo_box = new QComboBox();
    m_combo_box->addItem(i18n("ui.gradient_widget.interpolation_type", "None"));
    m_combo_box->addItem(i18n("ui.gradient_widget.interpolation_type", "Linear"));
    m_combo_box->addItem(i18n("ui.gradient_widget.interpolation_type", "Smooth"));
    m_combo_box->addItem(i18n("ui.gradient_widget.interpolation_type", "Spline"));
    m_combo_box->setDisabled(true);
    layout->addWidget(m_combo_box);

    connect(m_combo_box, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_selected < 0)
            return;
        if (m_gradient_widget->selected() < 0)
            start_changing();
        m_gradient_widget->get_point(m_selected).interp_type = static_cast<RampV3f::InterpType>(index);
        changing();
        if (m_gradient_widget->selected() < 0)
            end_changing();
        update();
    });
}

void GradientEditor::point_selected(int ind)
{
    m_selected = ind;
    if (m_selected < 0)
        return;
    const auto& cp = m_gradient_widget->get_point(m_selected);
    m_pos_editor->blockSignals(true);
    m_pos_editor->setValue(cp.position);
    m_pos_editor->blockSignals(false);
    m_select_color = cp.value;
    m_combo_box->setCurrentIndex(static_cast<int>(cp.interp_type));
    update();
}

void GradientEditor::point_update()
{
    if (m_selected >= 0) // valid
    {
        m_pos_editor->blockSignals(true);
        m_pos_editor->setValue(m_gradient_widget->get_point(m_selected).position);
        m_pos_editor->blockSignals(false);
        m_select_color = m_gradient_widget->get_point(m_selected).value;
        m_value_editor->setEnabled(true);
        m_pos_editor->setEnabled(true);
        m_combo_box->setEnabled(true);
    }
    else
    {
        m_value_editor->setDisabled(true);
        m_pos_editor->setValue(0.0);
        m_pos_editor->setDisabled(true);
        m_combo_box->setCurrentIndex(static_cast<int>(RampV3f::InterpType::kNone));
        m_combo_box->setDisabled(true);
    }
    update();
}

void GradientEditor::position_changed(double val)
{
    if (m_selected < 0)
        return;

    if (m_gradient_widget->selected() < 0)
        start_changing();
    m_gradient_widget->get_point(m_selected).position = val;
    changing();
    if (m_gradient_widget->selected() < 0)
        end_changing();
    update();
}

void GradientEditor::add_point(double pos, const GfVec3f& color, RampV3f::InterpType interp)
{
    m_gradient_widget->add_point(pos, color, interp);
}

void GradientEditor::add_point(double pos, float r, float g, float b, int interp)
{
    add_point(pos, { r, g, b }, static_cast<RampV3f::InterpType>(interp));
}

const Ramp<PXR_NS::GfVec3f>& GradientEditor::get_color_ramp() const
{
    return m_gradient_widget->get_color_ramp();
}

int GradientEditor::points_count() const
{
    return m_gradient_widget->get_color_ramp().cv().size();
}

const Ramp<GfVec3f>::CV& GradientEditor::get_point(int i) const
{
    return m_gradient_widget->get_point(i);
}

void GradientEditor::clear()
{
    m_gradient_widget->clear();
    // m_gradient_widget->add_point(0, GfVec3f(1, 1, 1), Ramp<GfVec3f>::InterpType::kLinear);
}

OPENDCC_NAMESPACE_CLOSE
