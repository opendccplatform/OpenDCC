// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <QPainter>
#include <QRegularExpression>
#include <QLabel>
#include <QComboBox>
#include <QMouseEvent>
#include <cmath>
#include "opendcc/ui/common_widgets/ramp_widget.h"
#include "opendcc/app/ui/application_ui.h"

OPENDCC_NAMESPACE_OPEN

QSizeF const RampWidget::s_point_size(7, 7);
int const RampWidget::s_point_border_width = 1;
int const RampWidget::s_point_active_zone = 3;

RampWidget::RampWidget()
{
    setMinimumSize(180, 60);
    setContentsMargins(0, 0, 0, 0);
    setMouseTracking(true); // to track a point to highlight

    m_back_color = QColor::fromRgb(58, 58, 58);
    m_ramp_color = QColor::fromRgb(189, 189, 189);
    m_ramp_line = QColor::fromRgb(58, 58, 58);

    m_ramp_rect.setTopLeft({ s_point_size.width() / 2 + s_point_border_width, s_point_size.height() / 2 + s_point_border_width });
    update_ramp_rect();
}

void RampWidget::paintEvent(QPaintEvent*)
{
    auto const x = m_ramp_rect.left();
    auto const y = m_ramp_rect.top();
    auto const w = m_ramp_rect.width();
    auto const h = m_ramp_rect.height();
    static int const ramp_border_width = 1;

    static QColor const ramp_border_color = qRgb(42, 42, 42);
    static QColor const active_point_color = qRgb(255, 255, 255);
    static QColor const normal_point_color = qRgb(120, 120, 120);
    static QColor const hover_point_border_color = qRgb(128, 128, 128);
    static QColor const normal_point_border_color = qRgb(22, 22, 22);

    QPen pen;
    QBrush brush;
    pen.setColor(ramp_border_color);
    pen.setWidth(ramp_border_width);
    brush.setColor(m_back_color);
    brush.setStyle(Qt::BrushStyle::SolidPattern);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.drawRect(x, y, w, h);

    if (!m_ramp)
        return;

    const int max_points = 100;
    QPointF points[max_points + 3] = { { x, y + h } };
    for (int i = 0; i <= max_points; i++)
    {
        auto t = i / float(max_points);
        points[i + 1] = { x + t * w, y + clamp(h - m_ramp->value_at(t) * h, 0.0, h) };
    }
    points[max_points + 2] = { x + w, y + h };

    pen.setColor(m_ramp_line);
    pen.setWidth(2);
    brush.setColor(m_ramp_color);
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.drawPolygon(points, max_points + 3);

    pen.setWidth(s_point_border_width);

    auto painting = [&](Ramp<float>::CV const& val) {
        if (m_selected == val.id || m_active == val.id)
            brush.setColor(active_point_color);
        else
            brush.setColor(normal_point_color);

        if (m_hovered == val.id)
            pen.setColor(hover_point_border_color);
        else
            pen.setColor(normal_point_border_color);

        painter.setBrush(brush);
        painter.setPen(pen);

        auto val_x = x + val.position * w - s_point_size.width() / 2;
        auto val_y = y + clamp(h - val.value * h, 0.0, h) - s_point_size.height() / 2;

        painter.drawEllipse(QRectF(QPoint(val_x, val_y), s_point_size));
        painter.drawRect(QRectF(QPoint(val_x, y + h + ramp_border_width), s_point_size));
    };

    for (size_t i = 1; i < m_ramp->cv().size() - 1; ++i)
    {
        auto const& val = m_ramp->cv()[i];
        if (val.id != m_active)
            painting(val);
    }
    if (m_active != 0)
        painting(m_ramp->cv(m_active)); // active point is always the last one here to stay always on top (only visually)
}

void RampWidget::mousePressEvent(QMouseEvent* e)
{
    if (!m_ramp)
        return;

    float const point_x = e->x();
    float const point_y = e->y();

    m_selected = find_point(point_x, point_y);
    if (m_selected != 0)
    {
        m_hovered = m_active = m_selected;
        point_selected(m_selected);
    }
    else
    {
        int remove_index = find_point_to_remove(point_x, point_y);
        if (remove_index != 0)
        {
            remove_point(remove_index);
            point_selected(m_active);
        }
        else
        {
            add_point(point_x, point_y);
            point_selected(m_selected);
        }
    }

    start_changing();
    update();
}

void RampWidget::mouseReleaseEvent(QMouseEvent* e)
{
    end_changing();
    m_selected = 0;
    update();
}

void RampWidget::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_ramp)
        return;

    float const point_x = e->x();
    float const point_y = e->y();

    if (m_selected)
    {
        m_ramp->cv(m_selected).position = clamp((point_x - m_ramp_rect.left()) / m_ramp_rect.width(), 0.0, 1.0);
        m_ramp->cv(m_selected).value = clamp(1.0 - (point_y - s_point_size.height() / 2) / m_ramp_rect.height(), 0.0, 1.0);

        m_ramp->prepare_points();
        m_hovered = m_selected;
        changing();
    }
    else
    {
        m_hovered = find_point(point_x, point_y); // either a point itself...
        if (m_hovered == 0)
            m_hovered = m_ramp->cv()[find_point_to_remove(point_x, point_y)].id; // or its' remove button
    }

    update();
}

void RampWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    m_hovered = 0;
    update();
}

void RampWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update_ramp_rect();
    update();
}

int RampWidget::find_point(float point_x, float point_y)
{
    if (!m_ramp)
        return 0;

    auto const x = m_ramp_rect.left();
    auto const y = m_ramp_rect.top();
    auto const w = m_ramp_rect.width();
    auto const h = m_ramp_rect.height();

    for (size_t i = m_ramp->cv().size() - 2; i > 0; --i) //! the reverse loop is to start looking from the top. Painting was in a normal loop
    {
        auto const& val = m_ramp->cv()[i];
        auto cv_point_x = x + val.position * w - s_point_border_width;
        auto cv_point_y = y + clamp(h - val.value * h, 0.0, h) - s_point_border_width;
        QPointF pos(cv_point_x, cv_point_y);
        QPointF vec = pos - QPointF { point_x, point_y };
        auto dist = sqrt(pow(vec.x(), 2) + pow(vec.y(), 2));

        if (dist < s_point_size.width() / 2 + s_point_border_width + s_point_active_zone)
            return val.id;
    }

    return 0;
}

int RampWidget::find_point_to_remove(float point_x, float point_y)
{
    if (!m_ramp)
        return 0;

    auto const x = m_ramp_rect.left();
    auto const y = m_ramp_rect.top();
    auto const w = m_ramp_rect.width();
    auto const h = m_ramp_rect.height();

    for (size_t i = m_ramp->cv().size() - 2; i > 0; --i) //! the reverse loop is to start looking from the top. Painting was in a normal loop
    {
        auto const& val = m_ramp->cv()[i];
        auto cv_point_x = x + val.position * w - s_point_border_width;
        auto cv_point_y = y + h + s_point_size.height() / 2; // + ramp_border_width - s_point_border_width
        QPointF pos(cv_point_x, cv_point_y);
        QPointF vec = pos - QPointF { point_x, point_y };

        if (std::abs(vec.x()) < (s_point_size.width() / 2 + s_point_border_width + s_point_active_zone) &&
            std::abs(vec.y()) < (s_point_size.height() / 2 + s_point_border_width + s_point_active_zone))
            return i;
    }

    return 0;
}

void RampWidget::remove_point(int point)
{
    if (point == 0)
        return;

    if (m_ramp->cv().size() <= 3) // not the last one
        return;

    m_hovered = 0;
    if (m_active == m_ramp->cv()[point].id) // going to delete the active point
        m_active = m_ramp->cv()[point % (m_ramp->cv().size() - 2) + 1].id; // take the next one as a new active point

    m_ramp->cv().erase(m_ramp->cv().begin() + point);
    m_ramp->prepare_points();
    update();
}

void RampWidget::add_point(float point_x, float point_y)
{
    auto const position = clamp((point_x - m_ramp_rect.left()) / m_ramp_rect.width(), 0.0, 1.0);
    auto const value = clamp(1.0 - point_y / m_ramp_rect.height(), 0.0, 1.0);
    m_ramp->add_point(position, value, Ramp<float>::kSmooth);

    m_ramp->prepare_points();
    m_selected = find_point(point_x, point_y);
    m_hovered = m_active = m_selected;
}

void RampWidget::update_ramp_rect()
{
    auto const w = width() - (s_point_size.width() / 2 + s_point_border_width) * 2;
    auto const h = height() - (s_point_size.height() + s_point_border_width * 2) * 1.5; // 1.5 = half of a point and a whole remove point
    m_ramp_rect.setSize({ w, h });
}

//////////////////////////////////////////////////////////////////////////
void FloatWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_change)
    {
        setValue(value() + (event->localPos().x() - m_from) / 100.0f);
        m_from = event->localPos().x();
    }
    else
        QDoubleSpinBox::mouseMoveEvent(event);
}

void FloatWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_change)
        m_change = false;
    else
        QDoubleSpinBox::mouseReleaseEvent(event);
    mouseReleaseSignal(event);
}

void FloatWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton)
    {
        m_from = event->localPos().x();
        m_change = true;
    }
    else
        QDoubleSpinBox::mousePressEvent(event);
}

FloatWidget::FloatWidget(QWidget* parent)
    : QDoubleSpinBox(parent)
{
    setLocale(QLocale(QLocale::C));
    setMouseTracking(true);
    setRange(-1.e10, 1.e10);
    setDecimals(3);
    // setMinimumWidth(20);
    // setMinimumHeight(10);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
}

void FloatWidget::focusInEvent(QFocusEvent* e)
{
    focus_in(e);
    QDoubleSpinBox::focusInEvent(e);
}

void FloatWidget::focusOutEvent(QFocusEvent* e)
{
    focus_out(e);
    QDoubleSpinBox::focusOutEvent(e);
}

QValidator::State FloatWidget::validate(QString& input, int& pos) const
{
    if (input.indexOf(QRegularExpression("[^0-9.]"), 0) == -1)
    {
        QList<QString> list = input.split(".");
        if (list.size() == 2)
        {
            if (list[1].size() > decimals())
                return QValidator::Intermediate;
        }
        else if (list.size() > 2)
            return QValidator::Invalid;

        return QValidator::Acceptable;
    }
    return QValidator::Invalid;
}

void FloatWidget::fixup(QString& input) const
{
    QList<QString> list = input.split(".");
    if (list.size() > 1)
    {
        if (list[1].size() > decimals())
            list[1] = list[1].remove(decimals(), list[1].size() - decimals());
        input = list[0] + "." + list[1];
    }
}

//////////////////////////////////////////////////////////////////////////

RampEditor::RampEditor(std::shared_ptr<Ramp<float>> ramp)
    : m_ramp(ramp)
    , m_ramp_widget(new RampWidget())
{
    if (!ramp)
        ramp = std::make_shared<Ramp<float>>();
    setMinimumSize(200, 110);
    auto main_layout = new QVBoxLayout();
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(2);
    setLayout(main_layout);
    m_ramp_widget->solver(m_ramp);
    main_layout->addWidget(m_ramp_widget);

    auto control_layout = new QHBoxLayout();
    control_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addLayout(control_layout);
    control_layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding));
    auto layout = new QVBoxLayout();
    control_layout->addLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto editors = new QHBoxLayout();
    editors->setContentsMargins(0, 0, 0, 0);
    auto editor_layout = new QHBoxLayout();
    editor_layout->setContentsMargins(0, 0, 0, 0);
    setup_position_editor(editor_layout);
    setup_value_editor(editor_layout);
    editors->addLayout(editor_layout);
    layout->addLayout(editors);

    editor_layout = new QHBoxLayout();
    setup_interpolation_widget(editor_layout);
    layout->addLayout(editor_layout);

    connect(m_ramp_widget, &RampWidget::point_selected, this, &RampEditor::point_selected);
    connect(m_ramp_widget, &RampWidget::changing, this, &RampEditor::point_update);
    connect(m_ramp_widget, &RampWidget::end_changing, this, qOverload<>(&RampEditor::value_changed));
}

void RampEditor::setup_position_editor(QBoxLayout* layout)
{
    layout->addWidget(new QLabel(i18n("ui.ramp_widget", "Position:")));
    m_pos_editor = new FloatWidget(this);
    m_pos_editor->setRange(0, 1);
    m_pos_editor->setSingleStep(0.1);
    m_pos_editor->setDisabled(true);
    layout->addWidget(m_pos_editor);

    connect(m_pos_editor, qOverload<double>(&FloatWidget::valueChanged), this, &RampEditor::position_changed);
    connect(m_pos_editor, &FloatWidget::editingFinished, this, qOverload<>(&RampEditor::value_changed));
}

void RampEditor::setup_value_editor(QBoxLayout* layout)
{
    layout->addWidget(new QLabel(i18n("ui.ramp_widget", " Value:")));
    m_value_editor = new FloatWidget(this);
    m_value_editor->setRange(0, 1);
    m_value_editor->setSingleStep(0.1);
    m_value_editor->setDisabled(true);
    layout->addWidget(m_value_editor);

    connect(m_value_editor, qOverload<double>(&FloatWidget::valueChanged), this, qOverload<double>(&RampEditor::value_changed));
    connect(m_value_editor, &FloatWidget::editingFinished, this, qOverload<>(&RampEditor::value_changed));
}

void RampEditor::setup_interpolation_widget(QBoxLayout* layout)
{
    layout->addWidget(new QLabel(i18n("ui.ramp_widget", "Interpolation type:")));
    m_combo_box = new QComboBox();
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "None"));
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "Linear"));
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "Smooth"));
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "Spline"));
    m_combo_box->setDisabled(true);
    layout->addWidget(m_combo_box);

    connect(m_combo_box, qOverload<int>(&QComboBox::currentIndexChanged), this, &RampEditor::type_changed);
    connect(m_combo_box, qOverload<int>(&QComboBox::activated), this, qOverload<>(&RampEditor::value_changed));
}

void RampEditor::point_selected(int point)
{
    m_selected_point = point;
    point_update();
}

void RampEditor::point_update()
{
    if (m_selected_point != 0) // valid
    {
        auto& cv = m_ramp->cv(m_selected_point);
        m_combo_box->setCurrentIndex(cv.interp_type);
        m_value_editor->setValue(cv.value);
        m_pos_editor->setValue(cv.position);
        m_value_editor->setEnabled(true);
        m_pos_editor->setEnabled(true);
        m_combo_box->setEnabled(true);
    }
    else
    {
        m_value_editor->setValue(0.0);
        m_value_editor->setDisabled(true);
        m_pos_editor->setValue(0.0);
        m_pos_editor->setDisabled(true);
        m_combo_box->setCurrentIndex(static_cast<int>(Ramp<float>::InterpType::kNone));
        m_combo_box->setDisabled(true);
    }
    update();
}

void RampEditor::type_changed(int index)
{
    if (!m_selected_point)
        return;

    m_ramp->cv(m_selected_point).interp_type = (Ramp<float>::InterpType)m_combo_box->currentIndex();
    m_ramp->prepare_points();
    m_ramp_widget->update();
}

void RampEditor::value_changed(double val)
{
    if (!m_selected_point)
        return;

    auto& cv = m_ramp->cv(m_selected_point);
    cv.value = val;
    m_ramp->prepare_points();
    m_ramp_widget->update();
}

void RampEditor::position_changed(double val)
{
    if (!m_selected_point)
        return;

    auto& cv = m_ramp->cv(m_selected_point);
    cv.position = val;
    m_ramp->prepare_points();
    m_ramp_widget->update();
}

//////////////////////////////////////////////////////////////////////////

RampEdit::RampEdit()
    : m_ramp_widget(new RampWidget())
{
    setMinimumSize(200, 100);
    auto main_layout = new QVBoxLayout();
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(2);
    setLayout(main_layout);
    m_ramp = std::make_shared<Ramp<float>>();
    m_ramp_widget->solver(m_ramp);
    main_layout->addWidget(m_ramp_widget);

    auto control_layout = new QHBoxLayout();
    control_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addLayout(control_layout);
    control_layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Expanding));
    auto layout = new QVBoxLayout();
    control_layout->addLayout(layout);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    QHBoxLayout* editor_layout;

    auto editors = new QHBoxLayout();
    editor_layout = new QHBoxLayout();
    editor_layout->addWidget(new QLabel(i18n("ui.ramp_widget", "Position:")));
    m_pos_editor = new FloatWidget(this);
    m_pos_editor->setRange(0, 1);
    m_pos_editor->setSingleStep(0.1);
    editor_layout->addWidget(m_pos_editor);
    editors->addLayout(editor_layout);

    editor_layout = new QHBoxLayout();
    editor_layout->addWidget(new QLabel(i18n("ui.ramp_widget", " Value:")));
    m_value_editor = new FloatWidget(this);
    m_value_editor->setRange(0, 1);
    m_value_editor->setSingleStep(0.1);
    editor_layout->addWidget(m_value_editor);
    editors->addLayout(editor_layout);

    layout->addLayout(editors);

    editor_layout = new QHBoxLayout();
    editor_layout->addWidget(new QLabel(i18n("ui.ramp_widget", "Interpolation type:")));
    m_combo_box = new QComboBox();
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "None"));
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "Linear"));
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "Smooth"));
    m_combo_box->addItem(i18n("ui.ramp_widget.interpolation_type", "Spline"));
    editor_layout->addWidget(m_combo_box);
    layout->addLayout(editor_layout);

    connect(m_ramp_widget, SIGNAL(point_selected(int)), this, SLOT(point_select(int)));
    connect(m_ramp_widget, SIGNAL(changing()), this, SLOT(point_update()));
    connect(m_ramp_widget, SIGNAL(end_changing()), this, SIGNAL(changed()));
    connect(m_combo_box, SIGNAL(currentIndexChanged(int)), this, SLOT(type_changed(int)));
    connect(m_value_editor, SIGNAL(valueChanged(double)), this, SLOT(value_changed(double)));
    connect(m_pos_editor, SIGNAL(valueChanged(double)), this, SLOT(position_changed(double)));
}

RampWidget* RampEdit::get_ramp()
{
    return m_ramp_widget;
}

void RampEdit::point_select(int point)
{
    m_selected_point = point;
    point_update();
    point_selected(point);
}

void RampEdit::point_update()
{
    if (!m_selected_point)
        return;

    auto& cv = m_ramp->cv(m_selected_point);

    m_combo_box->blockSignals(true);
    m_combo_box->setCurrentIndex(cv.interp_type);
    m_combo_box->blockSignals(false);

    m_value_editor->blockSignals(true);
    m_value_editor->setValue(cv.value);
    m_value_editor->blockSignals(false);

    m_pos_editor->blockSignals(true);
    m_pos_editor->setValue(cv.position);
    m_pos_editor->blockSignals(false);
}

void RampEdit::type_changed(int index)
{
    if (!m_selected_point)
        return;

    m_ramp->cv(m_selected_point).interp_type = (Ramp<float>::InterpType)m_combo_box->currentIndex();
    m_ramp->prepare_points();
    m_ramp_widget->update();
    changed();
}

void RampEdit::value_changed(double val)
{
    if (!m_selected_point)
        return;

    auto& cv = m_ramp->cv(m_selected_point);
    cv.value = val;
    m_ramp->prepare_points();
    m_ramp_widget->update();
    changed();
}

void RampEdit::position_changed(double val)
{
    if (!m_selected_point)
        return;

    auto& cv = m_ramp->cv(m_selected_point);
    cv.position = val;
    m_ramp->prepare_points();
    m_ramp_widget->update();
    changed();
}

void RampEdit::clear()
{
    m_ramp->clear();
    m_ramp->prepare_points();
    m_ramp_widget->update();
    changed();
}

void RampEdit::add_point(RampPoint point)
{
    m_ramp->add_point(point.pos, point.val, (Ramp<float>::InterpType)point.inter);
    m_ramp->prepare_points();
    m_ramp_widget->update();
    changed();
}

int RampEdit::points_count()
{
    return m_ramp->cv().size();
}

RampPoint RampEdit::point(int i)
{
    Ramp<float>::CV point = m_ramp->cv()[i];
    return RampPoint { (float)point.position, point.value, point.interp_type };
}

OPENDCC_NAMESPACE_CLOSE
