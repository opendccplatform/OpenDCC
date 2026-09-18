// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <QGuiApplication>
#include <memory>
#include <iostream>

#include <QPainter>
#include <QBoxLayout>
#include <QComboBox>
#include <QLabel>
#include <QMouseEvent>
#include <QTimer>
#include <QScreen>
#include <QApplication>
#include <QToolButton>

#include "opendcc/ui/common_widgets/color_widget.h"
#include "opendcc/ui/common_widgets/qt_compat.h"
#include "opendcc/ui/common_widgets/ramp_widget.h"
#include "opendcc/ui/common_widgets/tiny_slider.h"
#include <array>

OPENDCC_NAMESPACE_OPEN

ColorWidget::ColorPickingEventFilter::ColorPickingEventFilter(ColorWidget* color_widget)
    : QObject(color_widget)
    , m_color_widget(color_widget)
{
}

bool ColorWidget::ColorPickingEventFilter::eventFilter(QObject* obj, QEvent* event)
{
    switch (event->type())
    {
    case QEvent::MouseMove:
        return m_color_widget->on_color_picking_mouse_move(static_cast<QMouseEvent*>(event));
    case QEvent::MouseButtonRelease:
        return m_color_widget->on_color_picking_mouse_button_release(static_cast<QMouseEvent*>(event));
    case QEvent::KeyPress:
    case QEvent::WindowDeactivate:
        return m_color_widget->on_color_picking_key_press(static_cast<QKeyEvent*>(event));
    default:
        break;
    }
    return false;
}

void ComboBoxNoFocus::showPopup()
{
    focus_in();
    QComboBox::showPopup();
}

void ComboBoxNoFocus::hidePopup()
{
    focus_out();
    QComboBox::hidePopup();
}

ColorWidget::ColorWidget(QWidget* parent, bool enable_alpha)
    : QWidget(parent)
    , m_enable_alpha(enable_alpha)
{

    setMinimumSize(200, 100);

    QPen pen;
    QBrush brush;

    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    setLayout(main_layout);

    auto layout = new QHBoxLayout();
    main_layout->addLayout(layout);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    setup_preview(layout);
    init_box(layout);

    auto hsv_reg_lo = new QVBoxLayout();
    hsv_reg_lo->setSpacing(0);

    m_HUE_pair = create_color_bar(hsv_reg_lo, " H");
    m_SAT_pair = create_color_bar(hsv_reg_lo, " S");
    m_VAL_pair = create_color_bar(hsv_reg_lo, " V");

    if (m_enable_alpha)
        m_ALP_pair = create_color_bar(hsv_reg_lo, " A");

    setup_hue();
    setup_sat();
    setup_val();

    if (m_enable_alpha)
        setup_alpha();

    show_line_controls();

    connect(std::get<1>(m_HUE_pair), SIGNAL(valueChanged(double)), this, SLOT(HUE_changed(double)));
    connect(std::get<1>(m_SAT_pair), SIGNAL(valueChanged(double)), this, SLOT(SAT_changed(double)));
    connect(std::get<1>(m_VAL_pair), SIGNAL(valueChanged(double)), this, SLOT(VAL_changed(double)));

    connect(std::get<1>(m_HUE_pair), SIGNAL(valueChanged(double)), this, SLOT(change_color()));
    connect(std::get<1>(m_SAT_pair), SIGNAL(valueChanged(double)), this, SLOT(change_color()));
    connect(std::get<1>(m_VAL_pair), SIGNAL(valueChanged(double)), this, SLOT(change_color()));

    connect(std::get<1>(m_HUE_pair), &FloatWidget::mouseReleaseSignal, this, [this] { change_color(); });
    connect(std::get<1>(m_SAT_pair), &FloatWidget::mouseReleaseSignal, this, [this] { change_color(); });
    connect(std::get<1>(m_VAL_pair), &FloatWidget::mouseReleaseSignal, this, [this] { change_color(); });

    if (m_enable_alpha)
    {
        connect(std::get<1>(m_ALP_pair), SIGNAL(valueChanged(double)), this, SLOT(ALP_changed(double)));
        connect(std::get<1>(m_ALP_pair), SIGNAL(valueChanged(double)), this, SLOT(change_color()));
    }

    layout->setSpacing(0);
    layout->addLayout(hsv_reg_lo);
    layout->setContentsMargins(0, 0, 0, 0);

    setup_palette(main_layout);
}

std::tuple<CanvasWidget*, FloatWidget*, QLabel*> ColorWidget::create_color_bar(QVBoxLayout* layout, const QString& label_text)
{
    auto max_h = m_enable_alpha ? 15 : 20;
    CanvasWidget* custom_canvas = new CanvasWidget(this);
    custom_canvas->setFixedHeight(max_h);

    auto float_edit = new FloatWidget(this);
    float_edit->setRange(0, 1);
    float_edit->setSingleStep(0.1);
    float_edit->setFixedHeight(max_h);
    float_edit->setFixedWidth(50);

    connect(float_edit, &FloatWidget::focus_in, this, [this] { focus_in(); });
    connect(float_edit, &FloatWidget::focus_out, this, [this] { focus_out(); });

    auto label = new QLabel(label_text);
    label->setFixedWidth(15);

    auto HUE_layout = new QHBoxLayout();
    HUE_layout->addWidget(custom_canvas);
    HUE_layout->addWidget(label);
    HUE_layout->addWidget(float_edit);
    HUE_layout->setSpacing(0);
    layout->addLayout(HUE_layout);

    return std::make_tuple(custom_canvas, float_edit, label);
}

void ColorWidget::setup_hue()
{
    auto canvas = std::get<0>(m_HUE_pair);
    auto control = std::get<1>(m_HUE_pair);
    std::get<2>(m_HUE_pair)->setText(" H");
    control->setRange(0, 360);
    control->setValue(H * 360.f);
    control->setSingleStep(3.6);
    m_HUE_changed = [this](double val) {
        H = val / 360.f;
    };

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_HUE_pair);
        auto control = std::get<1>(m_HUE_pair);

        float w = canvas->width();
        H = clamp(float(e->x()) / w, 0.f, 1.f);
        control->setValue(H * 360.f);
    };

    canvas->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    canvas->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
    };
    canvas->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_HUE_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, { 255, 0, 0 });
        grad.setColorAt(0.17, { 255, 255, 0 });
        grad.setColorAt(0.34, { 0, 255, 0 });
        grad.setColorAt(0.51, { 0, 255, 255 });
        grad.setColorAt(0.67, { 0, 0, 255 });
        grad.setColorAt(0.84, { 255, 0, 255 });
        grad.setColorAt(1, { 255, 0, 0 });

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * H - 2, 0, 4, h);
    };
}

void ColorWidget::setup_sat()
{
    auto canvas = std::get<0>(m_SAT_pair);
    auto control = std::get<1>(m_SAT_pair);
    std::get<2>(m_SAT_pair)->setText(" S");
    control->setRange(0, 1);
    control->setValue(S);
    m_SAT_changed = [this](double val) {
        S = val;
    };

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_SAT_pair);
        auto control = std::get<1>(m_SAT_pair);

        float w = canvas->width();
        S = clamp(float(e->x()) / w, 0.f, 1.f);
        control->setValue(S);
    };

    canvas->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    canvas->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
    };
    canvas->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_SAT_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromHsvF(H, 0, V, 1));
        grad.setColorAt(1, QColor::fromHsvF(H, 1, V, 1));

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * S - 2, 0, 4, h);
    };
}

void ColorWidget::setup_val()
{
    auto canvas = std::get<0>(m_VAL_pair);
    auto control = std::get<1>(m_VAL_pair);
    std::get<2>(m_VAL_pair)->setText(" V");
    control->setRange(0, 1);
    control->setValue(V);
    m_VAL_changed = [this](double val) {
        V = val;
    };

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_VAL_pair);
        auto control = std::get<1>(m_VAL_pair);

        float w = canvas->width();
        V = clamp(float(e->x()) / w, 0.f, 1.f);
        control->setValue(V);
    };

    canvas->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    canvas->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };
    canvas->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
    };

    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_VAL_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromHsvF(H, S, 0, 1));
        grad.setColorAt(1, QColor::fromHsvF(H, S, 1, 1));

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * V - 2, 0, 4, h);
    };
}

void ColorWidget::setup_alpha()
{
    auto canvas = std::get<0>(m_ALP_pair);
    auto control = std::get<1>(m_ALP_pair);
    control->setRange(0, 1);
    control->setValue(A);

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_ALP_pair);
        auto control = std::get<1>(m_ALP_pair);

        float w = canvas->width();
        A = clamp(float(e->x()) / w, 0.f, 1.f);
        control->setValue(A);
    };

    canvas->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    canvas->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };
    canvas->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
    };

    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_ALP_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QLinearGradient grad(0, 0, w, 0);
        auto color_val = color();
        color_val.setAlphaF(0);
        grad.setColorAt(0, color_val);
        color_val.setAlphaF(1);
        grad.setColorAt(1, color_val);

        QPen pen;
        pen.setColor({ 58, 58, 58 });
        QBrush brush;
        QPainter painter(canvas);
        painter.setPen(pen);
        brush.setColor({ 42, 42, 42 });
        brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);

        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * A - 2, 0, 4, h);
    };
}

void ColorWidget::setup_R()
{
    auto canvas = std::get<0>(m_HUE_pair);
    auto control = std::get<1>(m_HUE_pair);
    std::get<2>(m_HUE_pair)->setText(" R");
    control->setRange(0, 1);
    control->setSingleStep(0.1);
    control->setValue(R);
    m_HUE_changed = [this](double val) {
        R = val;
    };

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_HUE_pair);
        auto control = std::get<1>(m_HUE_pair);

        float w = canvas->width();
        R = clamp(float(e->x()) / w, 0.f, 1.f);
        control->setValue(R);
    };

    canvas->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    canvas->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;

        mouse_actions(e);
    };
    canvas->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };

    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_HUE_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromRgbF(0, G, B));
        grad.setColorAt(1, QColor::fromRgbF(1, G, B));

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * R - 2, 0, 4, h);
    };
}

void ColorWidget::setup_G()
{
    auto canvas = std::get<0>(m_SAT_pair);
    auto control = std::get<1>(m_SAT_pair);
    std::get<2>(m_SAT_pair)->setText(" G");
    control->setRange(0, 1);
    control->setValue(G);
    m_SAT_changed = [this](double val) {
        G = val;
    };

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_SAT_pair);
        auto control = std::get<1>(m_SAT_pair);

        float w = canvas->width();
        G = clamp(float(e->x()) / w, 0.f, 1.f);
        control->setValue(G);
    };

    canvas->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    canvas->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;

        mouse_actions(e);
    };
    canvas->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_SAT_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromRgbF(R, 0, B));
        grad.setColorAt(1, QColor::fromRgbF(R, 1, B));

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * G - 2, 0, 4, h);
    };
}

void ColorWidget::setup_B()
{
    auto canvas = std::get<0>(m_VAL_pair);
    auto control = std::get<1>(m_VAL_pair);
    std::get<2>(m_VAL_pair)->setText(" B");
    control->setRange(0, 1);
    control->setValue(B);
    m_VAL_changed = [this](double val) {
        B = val;
    };

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_VAL_pair);
        auto control = std::get<1>(m_VAL_pair);

        float w = canvas->width();
        B = clamp(float(e->x()) / w, 0.f, 1.f);
        control->setValue(B);
    };

    canvas->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    canvas->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;

        mouse_actions(e);
    };
    canvas->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_VAL_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromRgbF(R, G, 0));
        grad.setColorAt(1, QColor::fromRgbF(R, G, 1));

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * B - 2, 0, 4, h);
    };
}

void ColorWidget::update_color()
{
    QColor color;
    if (m_pick_variant->currentIndex() != RGB)
    {
        color = QColor::fromHsvF(H, S, V);
        get_rgb_f(color, &R, &G, &B);
    }
    else
    {
        color = QColor::fromRgbF(R, G, B);
        get_hsv_f(color, &H, &S, &V);
    }

    color.setAlphaF(A);
    if (m_current_palette)
        if (auto ptr = m_palette_color_ptr.lock())
            *ptr = color;

    if (!is_achromatic(color))
        m_prev_chromatic = color;

    update();
    changing_color();
}

QColor ColorWidget::color()
{
    return m_pick_variant->currentIndex() != RGB ? QColor::fromHsvF(H, S, V, A) : QColor::fromRgbF(R, G, B, A);
}

void ColorWidget::color(const QColor& val, bool update_prev /* = true*/)
{
    m_current_palette = 0;

    if (update_prev)
        m_prev_color = val;

    R = clamp(static_cast<double>(val.redF()), 0., 1.);
    G = clamp(static_cast<double>(val.greenF()), 0., 1.);
    B = clamp(static_cast<double>(val.blueF()), 0., 1.);
    A = clamp(static_cast<double>(val.alphaF()), 0., 1.);

    get_hsv_f(val, &H, &S, &V, &A);
    if (H == -1) // we don't need to work with this
    {
        m_achromatic = true;
        H = 0;
        S = 0;
    }

    if (m_pick_variant->currentIndex() != RGB)
    {
        std::get<1>(m_HUE_pair)->setValue(H * 360);
        std::get<1>(m_SAT_pair)->setValue(S);
        std::get<1>(m_VAL_pair)->setValue(V);
    }
    else
    {
        std::get<1>(m_HUE_pair)->setValue(R);
        std::get<1>(m_SAT_pair)->setValue(G);
        std::get<1>(m_VAL_pair)->setValue(B);
    }

    if (m_enable_alpha)
        std::get<1>(m_ALP_pair)->setValue(A);

    update();
    changing_color();
}

void ColorWidget::HUE_changed(double val)
{
    if (m_pick_variant->currentIndex() != RGB)
    {
        auto val_norm = val / 360.;
        if (val > 0 && m_achromatic) // not achromatic anymore
        {
            m_achromatic = false;
            color(QColor::fromHsvF(val_norm, m_prev_chromatic.saturationF(), m_prev_chromatic.valueF()), false);
        }
        else if (is_achromatic(QColor::fromHsvF(val_norm, S, V)))
        {
            val = 0;
            S = 0;
            m_achromatic = true;
            color(QColor::fromHsvF(0, 0, V), false);
        }
    }

    if (m_HUE_changed)
        m_HUE_changed(val);
    update_color();
}

void ColorWidget::SAT_changed(double val)
{
    if (m_pick_variant->currentIndex() != RGB)
    {
        if (val > 0 && m_achromatic) // not achromatic anymore
        {
            m_achromatic = false;
            color(QColor::fromHsvF(m_prev_chromatic.hueF(), val, m_prev_chromatic.valueF()), false);
        }
        else if (is_achromatic(QColor::fromHsvF(H, val, V)))
        {
            H = 0;
            val = 0;
            m_achromatic = true;
            color(QColor::fromHsvF(0, 0, V), false);
        }
    }

    if (m_SAT_changed)
        m_SAT_changed(val);
    update_color();
}

void ColorWidget::VAL_changed(double val)
{
    if (m_pick_variant->currentIndex() != RGB)
    {
        if (val == 0)
        {
            H = 0;
            S = 0;
            m_achromatic = true;
            color(QColor::fromHsvF(0, 0, val), false);
        }
    }

    if (m_VAL_changed)
        m_VAL_changed(val);
    update_color();
}

void ColorWidget::ALP_changed(double val)
{
    A = val;
    update_color();
}

void ColorWidget::change_color()
{
    if (!m_change_in_progress)
        color_changed();
}

void ColorWidget::pick_screen_color()
{
    if (!m_color_picking_event_filter)
        m_color_picking_event_filter = new ColorPickingEventFilter(this);
    installEventFilter(m_color_picking_event_filter);
    m_before_screen_color_picking = color();

    grabMouse(Qt::CrossCursor);
    setMouseTracking(true);
    m_eye_dropper_timer->start(30);
    m_dummy_transparent_window.show();
    m_color_picking_widget.show();
    m_color_picking_widget.set_previous_color(m_prev_color);
}

void ColorWidget::init_box(QLayout* layout)
{
    if (m_enable_alpha)
        init_box_alpha(layout);

    init_box_sat_val(layout);

    m_box_hue = new CanvasWidget(this);
    m_box_hue->setFixedSize(m_enable_alpha ? 15 : 20, 60);

    auto pressed = std::make_shared<bool>(false);
    auto mouse_actions = [this](QMouseEvent* e) {
        auto control = std::get<1>(m_HUE_pair);
        float h = m_box_hue->height();
        H = clamp(float(e->y()) / h, 0.f, 1.f);
        control->setValue(H * 360.f);
    };

    m_box_hue->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };
    m_box_hue->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    m_box_hue->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
    };

    m_box_hue->paint_event = [this](QPaintEvent*) {
        float w = m_box_hue->width() - 1;
        float h = m_box_hue->height() - 1;

        QLinearGradient grad(0, 0, 0, h);
        grad.setColorAt(0, { 255, 0, 0 });
        grad.setColorAt(0.17, { 255, 255, 0 });
        grad.setColorAt(0.34, { 0, 255, 0 });
        grad.setColorAt(0.51, { 0, 255, 255 });
        grad.setColorAt(0.67, { 0, 0, 255 });
        grad.setColorAt(0.84, { 255, 0, 255 });
        grad.setColorAt(1, { 255, 0, 0 });

        QPen pen;
        pen.setColor({ 58, 58, 58 });
        QPainter painter(m_box_hue);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor({ 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, H * h - 2, w, 4);
    };
    layout->addWidget(m_box_hue);
}

void ColorWidget::init_box_alpha(QLayout* layout)
{
    m_box_alpha = new CanvasWidget(this);
    auto pressed = std::make_shared<bool>(false);
    m_box_alpha->setFixedSize(15, 60);
    m_box_alpha->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };

    auto mouse_actions = [this](QMouseEvent* e) {
        auto control = std::get<1>(m_ALP_pair);
        float h = m_box_alpha->height();
        A = clamp(float(e->y()) / h, 0.f, 1.f);
        control->setValue(A);
    };

    m_box_alpha->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    m_box_alpha->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
    };

    m_box_alpha->paint_event = [this](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;

        float w = m_box_alpha->width() - 1;
        float h = m_box_alpha->height() - 1;

        QPainter painter(m_box_alpha);
        painter.setPen(pen);
        brush.setColor({ 42, 42, 42 });
        brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);

        QLinearGradient grad(0, 0, 0, h);
        auto color_val = color();
        color_val.setAlphaF(0);
        grad.setColorAt(0, color_val);
        color_val.setAlphaF(1);
        grad.setColorAt(1, color_val);

        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        brush.setColor({ 255, 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, h * A - 2, w, 4);
    };
    layout->addWidget(m_box_alpha);
}

void ColorWidget::init_box_sat_val(QLayout* layout)
{
    m_box_sat_val = new CanvasWidget(this);
    auto pressed = std::make_shared<bool>(false);
    m_box_sat_val->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };

    auto mouse_actions = [this](QMouseEvent* e) {
        auto controlS = std::get<1>(m_SAT_pair);
        auto controlV = std::get<1>(m_VAL_pair);

        float w = m_box_sat_val->width();
        float h = m_box_sat_val->height();
        S = clamp(float(e->x()) / w, 0.f, 1.f);
        V = clamp(float(e->y()) / h, 0.f, 1.f);
        controlS->setValue(S);
        controlV->setValue(V);
    };

    m_box_sat_val->mouse_press_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
    };
    m_box_sat_val->mouse_move_event = [this, pressed, mouse_actions](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
    };

    m_box_sat_val->setMinimumSize(60, 60);
    m_box_sat_val->paint_event = [this](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;

        float w = m_box_sat_val->width() - 1;
        float h = m_box_sat_val->height() - 1;

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(1, QColor::fromHsvF(H, 1, 1));
        grad.setColorAt(0, { 255, 255, 255 });

        QPainter painter(m_box_sat_val);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QLinearGradient grad2(0, 0, 0, h);
        grad2.setColorAt(1, { 255, 0, 0, 0 });
        grad2.setColorAt(0, { 0, 0, 0, 255 });
        painter.setBrush(grad2);
        painter.drawRect(0, 0, w, h);

        pen.setColor({ 255, 255, 255 });
        painter.setPen(pen);
        brush.setStyle(Qt::BrushStyle::NoBrush);
        painter.setBrush(brush);
        painter.drawRect(w * S - 3, h * V - 3, 6, 6);
    };
    layout->addWidget(m_box_sat_val);
}

void ColorWidget::show_box()
{
    setup_hue();
    setup_sat();
    setup_val();

    std::get<0>(m_HUE_pair)->hide();
    std::get<0>(m_SAT_pair)->hide();
    std::get<0>(m_VAL_pair)->hide();
    if (m_enable_alpha)
        std::get<0>(m_ALP_pair)->hide();

    if (m_enable_alpha)
        m_box_alpha->show();
    m_box_hue->show();
    m_box_sat_val->show();
}

void ColorWidget::show_line_controls()
{
    if (m_enable_alpha)
        m_box_alpha->hide();
    m_box_hue->hide();
    m_box_sat_val->hide();

    std::get<0>(m_HUE_pair)->show();
    std::get<0>(m_SAT_pair)->show();
    std::get<0>(m_VAL_pair)->show();
    if (m_enable_alpha)
        std::get<0>(m_ALP_pair)->show();
}

void ColorWidget::setup_preview(QBoxLayout* layout)
{
    auto color_box_lo = new QVBoxLayout();
    m_color_box = new CanvasWidget(this);
    m_color_box->setFixedSize(45, 30);
    m_color_box->paint_event = [this](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;

        float w = m_color_box->width() - 1;
        float h = m_color_box->height() - 1;

        QPainter painter(m_color_box);
        painter.setPen(pen);
        brush.setColor({ 42, 42, 42 });
        brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);

        brush.setColor(color());
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);
    };

    m_prev_color_box = new CanvasWidget(this);
    m_prev_color_box->setFixedSize(45, 15);
    m_prev_color_box->paint_event = [this](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;

        float w = m_prev_color_box->width() - 1;
        float h = m_prev_color_box->height() - 1;

        QPainter painter(m_prev_color_box);
        painter.setPen(pen);
        brush.setColor({ 42, 42, 42 });
        brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);

        brush.setColor(m_prev_color);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);
    };
    m_prev_color_box->mouse_press_event = [this](QMouseEvent*) {
        m_change_in_progress = true;
        auto tmp_pal = m_current_palette;
        color(m_prev_color);
        m_current_palette = tmp_pal;
        if (m_current_palette)
            *m_palette_color_ptr.lock() = m_prev_color;
    };
    m_prev_color_box->mouse_release_event = [this](QMouseEvent* e) {
        m_change_in_progress = false;
        change_color();
    };

    m_pick_variant = new ComboBoxNoFocus(this);
    m_pick_variant->addItems(QString("HSV|RGB|BOX").split('|'));
    m_pick_variant->setFixedWidth(45);
    m_pick_variant->setFixedHeight(15);

    connect(m_pick_variant, &ComboBoxNoFocus::focus_in, this, [this] { focus_in(); });
    connect(m_pick_variant, &ComboBoxNoFocus::focus_out, this, [this] { focus_out(); });
    connect(m_pick_variant, qOverload<int>(&ComboBoxNoFocus::currentIndexChanged), this, [this](int index) {
        if (index == HSV)
        {
            show_line_controls();

            setup_hue();
            setup_sat();
            setup_val();
        }
        else if (index == RGB)
        {
            show_line_controls();

            setup_R();
            setup_G();
            setup_B();
        }
        else if (index == BOX)
        {
            show_box();
        }

        update();
    });

    color_box_lo->addWidget(m_color_box);
    color_box_lo->addWidget(m_prev_color_box);
    color_box_lo->addWidget(m_pick_variant);
    color_box_lo->setSpacing(0);

    layout->addLayout(color_box_lo);
}

std::vector<QColor> ColorWidget::m_palette = {};

void ColorWidget::setup_palette(QBoxLayout* main_layout)
{
    auto palette_layout = new QHBoxLayout();
    palette_layout->setAlignment(Qt::AlignLeft);
    main_layout->addLayout(palette_layout);

    m_color_picking_widget.setWindowFlag(Qt::WindowStaysOnTopHint);
    m_dummy_transparent_window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    auto eye_dropper_btn = new QToolButton;
    eye_dropper_btn->setAutoRaise(true);
    eye_dropper_btn->setIcon(QIcon(":/icons/color_picker"));
    eye_dropper_btn->setToolTip("Eye Dropper");
    eye_dropper_btn->setIconSize(QSize(16, 16));
    eye_dropper_btn->setFixedSize(20, 20);
    connect(eye_dropper_btn, &QToolButton::clicked, this, &ColorWidget::pick_screen_color);
    palette_layout->addWidget(eye_dropper_btn);

    m_eye_dropper_timer = new QTimer(this);
    QObject::connect(m_eye_dropper_timer, &QTimer::timeout, this, &ColorWidget::update_color_picking);

    auto palette_colors_layout = new QHBoxLayout;
    palette_colors_layout->setAlignment(Qt::AlignLeft);
    palette_colors_layout->setContentsMargins(0, 0, 0, 0);
    palette_colors_layout->setSpacing(0);
    palette_layout->addLayout(palette_colors_layout);

    static const int palette_size = 19;
    static bool once = true;

    if (once)
    {
        m_palette.resize(palette_size);
        for (int i = 0; i < palette_size; i++)
            m_palette[i] = QColor::fromHsvF(float(i) / float(palette_size), 1, 1, 1);
        once = false;
    }

    m_palette_boxes.resize(palette_size);
    for (int i = 0; i < palette_size; i++)
    {
        auto palette_color = &m_palette[i];

        m_palette_boxes[i] = new CanvasWidget(this);
        auto palette_box = m_palette_boxes[i];
        palette_box->setFixedSize(15, 15);
        palette_box->paint_event = [palette_box, this, palette_color](QPaintEvent*) {
            QPen pen;
            pen.setColor({ 58, 58, 58 });

            QBrush brush;

            float w = palette_box->width() - 2;
            float h = palette_box->height() - 2;

            pen.setColor(QColor(42, 42, 42));
            if (palette_box == m_current_palette)
                pen.setWidthF(4);

            QPainter painter(palette_box);
            painter.setPen(pen);
            brush.setColor({ 42, 42, 42 });
            brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
            painter.setBrush(brush);
            painter.drawRect(1, 1, w, h);

            brush.setColor(*palette_color);
            brush.setStyle(Qt::BrushStyle::SolidPattern);
            painter.setBrush(brush);
            painter.drawRect(1, 1, w, h);
        };

        palette_box->mouse_press_event = [palette_box, this, palette_color](QMouseEvent* e) {
            if (e->button() == Qt::RightButton)
            {
                *palette_color = QColor::fromRgbF(R, G, B, A);
            }
            else
            {
                auto prev_color = m_prev_color;

                // Since the new values ​​after calling color() depend on the value of m_prev_chromatic, in order for SAT & VAL to be == 1,
                //  we assign palette_color to m_prev_chromatic, before calling color()
                m_prev_chromatic = *palette_color;
                color(*palette_color);
                m_prev_color = prev_color;
            }

            update();
        };
        palette_box->mouse_release_event = [this](QMouseEvent* e) {
            change_color();
        };

        palette_colors_layout->addWidget(palette_box);
    }
}

void ColorWidget::mousePressEvent(QMouseEvent* e)
{
    e->accept();
}

void ColorWidget::mouseReleaseEvent(QMouseEvent* e)
{
    e->accept();
}

bool ColorWidget::on_color_picking_mouse_move(QMouseEvent* event)
{
    const auto screen_color = grab_screen_color(event->globalPos());
    auto prev_color = m_prev_color;
    color(screen_color);
    m_prev_color = prev_color;
    return true;
}

bool ColorWidget::on_color_picking_mouse_button_release(QMouseEvent* event)
{
    removeEventFilter(m_color_picking_event_filter);
    releaseMouse();
    m_dummy_transparent_window.setVisible(false);
    m_color_picking_widget.setVisible(false);
    setMouseTracking(false);
    m_eye_dropper_timer->stop();
    if (event->button() & Qt::MouseButton::RightButton)
    {
        auto prev_color = m_prev_color;
        color(m_before_screen_color_picking);
        m_prev_color = prev_color;
    }
    event->accept();
    return true;
}

bool ColorWidget::on_color_picking_key_press(QKeyEvent* event)
{
    auto prev_color = m_prev_color;
    color(m_before_screen_color_picking);
    m_prev_color = prev_color;
    removeEventFilter(m_color_picking_event_filter);
    releaseMouse();
    m_dummy_transparent_window.setVisible(false);
    m_color_picking_widget.setVisible(false);
    setMouseTracking(false);
    m_eye_dropper_timer->stop();
    event->accept();
    return true;
}

void ColorWidget::update_color_picking()
{
    static QPoint last_pos;
    auto global_pos = QCursor::pos();
    if (last_pos == global_pos)
        return;
    last_pos = global_pos;

    const auto screen_color = grab_screen_color(global_pos);
    auto prev_color = m_prev_color;
    color(screen_color);
    m_prev_color = prev_color;

    m_color_picking_widget.move(global_pos + QPoint(15, 15));
    m_color_picking_widget.update();
    m_color_picking_widget.set_current_color(screen_color);
    auto screen = QApplication::screenAt(global_pos);
    if (!screen)
        screen = QApplication::primaryScreen();
    if (screen != m_prev_screen)
    {
        m_prev_screen = screen;
        m_dummy_transparent_window.setGeometry(screen->availableVirtualGeometry());
    }
}

QColor ColorWidget::grab_screen_color(const QPoint& point)
{
    auto screen = QApplication::screenAt(point);
    if (!screen)
        screen = QApplication::primaryScreen();

    // global to screen pos
    auto screen_pos = point - screen->geometry().topLeft();

    const auto pixmap = screen->grabWindow(0, screen_pos.x(), screen_pos.y(), 1, 1);
    const auto image = pixmap.toImage();
    return image.pixel(0, 0);
}

bool ColorWidget::is_achromatic(const QColor& color)
{
    if (m_pick_variant->currentIndex() != RGB)
        return color.hue() == -1 || color.saturation() == 0;
    else
        return color.red() == color.green() && color.green() == color.blue();
}

ColorPickDialog::ColorPickDialog(QWidget* parent /*= 0*/, bool enable_alpha /*= false*/, ColorWidget* color_editor /* = nullptr*/)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint | Qt::SubWindow | Qt::CustomizeWindowHint)
{
    setFixedSize(295, 100);
    m_close_timer = new QTimer(this);
    m_close_timer->setInterval(100);
    connect(m_close_timer, &QTimer::timeout, this, [this] {
        if (!rect().marginsAdded({ 25, 25, 25, 25 }).contains(mapFromGlobal(QCursor::pos())) && !m_color_editor->hasMouseTracking())
        {
            hide();
            m_close_timer->stop();
        }
    });
    m_close_timer->start();

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(5, 0, 5, 5);
    setLayout(layout);

    m_color_editor = color_editor ? color_editor : new ColorWidget(this, enable_alpha);
    layout->addWidget(m_color_editor);

    connect(m_color_editor, &ColorWidget::changing_color, this, [this] { changing_color(); });
    connect(m_color_editor, &ColorWidget::color_changed, this, [this] { color_changed(); });
}

void ColorPickDialog::paintEvent(QPaintEvent*)
{
    QPen pen;
    pen.setColor({ 58, 58, 58 });

    QBrush brush;
    brush.setColor({ 42, 42, 42 });

    QPainter painter(this);
    painter.setPen(pen);
    painter.setBrush(brush);
    painter.drawRect(0, 0, width() - 1, height() - 1);
}

QColor ColorPickDialog::color()
{
    return m_color_editor->color();
}

void ColorPickDialog::color(const QColor& val)
{
    m_color_editor->color(val);
}

void ColorPickDialog::mousePressEvent(QMouseEvent*)
{
    setFocus(Qt::MouseFocusReason);
}

void ColorPickDialog::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_close_timer->start();
}

ColorButton::ColorButton(QWidget* parent, bool enable_alpha /*= false*/, ColorPickDialog* pick_dialog /* = nullptr*/)
    : QWidget(parent)
{
    setMinimumSize(10, 10);
    setContentsMargins(0, 0, 0, 0);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    m_value_editor = new CanvasWidget(this);
    layout->addWidget(m_value_editor);

    ColorPickDialog* dialog = pick_dialog ? pick_dialog : new ColorPickDialog(nullptr, enable_alpha);

    connect(dialog, &ColorPickDialog::changing_color, this, [this, dialog] {
        m_select_color = dialog->color();
        changing();
        update();
    });
    connect(dialog, &ColorPickDialog::color_changed, this, [this, dialog] {
        m_select_color = dialog->color();
        color_changed();
        update();
    });

    m_value_editor->paint_event = [this](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;
        float w = m_value_editor->width();
        float h = m_value_editor->height();
        QPainter painter(m_value_editor);
        pen.setColor({ 58, 58, 58 });
        brush.setColor(m_select_color);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        // painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(pen);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w - 1, h - 1);
    };

    m_value_editor->mouse_press_event = [this, dialog, enable_alpha](QMouseEvent* e) mutable {
        QRect rec = screen_geometry_at(mapToGlobal(QPoint(0, 0)));
        auto height = rec.height();
        auto width = rec.width();

        dialog->color(m_select_color);
        auto dialog_pos = QCursor::pos() - QPoint(dialog->width(), 0);
        const auto current_screen = QApplication::screenAt(QCursor::pos());

        if (QApplication::screenAt(dialog_pos) != current_screen)
            dialog_pos.setX(current_screen->availableGeometry().left());
        if (QApplication::screenAt(dialog_pos + QPoint(0, dialog->height())) != current_screen)
            dialog_pos.setY(current_screen->availableGeometry().bottom() - dialog->height());

        dialog->move(dialog_pos);
        dialog->show();
        dialog->setFocus(Qt::ActiveWindowFocusReason);
    };
}

QColor ColorButton::color() const
{
    return m_select_color;
}

void ColorButton::color(QColor val)
{
    m_select_color = val;
    update();
}

void ColorButton::blue(double val)
{
    m_select_color.setBlueF(val);
    update();
}

double ColorButton::blue()
{
    return m_select_color.blueF();
}

void ColorButton::green(double val)
{
    m_select_color.setGreenF(val);
    update();
}

double ColorButton::green()
{
    return m_select_color.greenF();
}

void ColorButton::red(double val)
{
    m_select_color.setRedF(val);
    update();
}

double ColorButton::red()
{
    return m_select_color.redF();
}

double ColorButton::alpha()
{
    return m_select_color.alphaF();
}

void ColorButton::alpha(double val)
{
    m_select_color.setAlphaF(val);
    update();
}

TinyColorWidget::TinyColorWidget(QWidget* parent /*= 0*/, bool enable_alpha /*= false*/)
    : QWidget(parent)
{
    auto pick_layout = new QHBoxLayout;
    pick_layout->setContentsMargins(0, 0, 0, 0);
    setLayout(pick_layout);

    color_button = new ColorButton(this, enable_alpha);
    color_button->setFixedSize(50, 20);
    pick_layout->addWidget(color_button);

    auto r_lay = new QVBoxLayout;
    r_lay->setContentsMargins(0, 0, 0, 0);
    r_lay->setSpacing(0);

    auto flw = new FloatWidget(this);
    auto flw_r = flw;
    flw->setFixedHeight(15);
    flw->setSingleStep(0.1f);
    auto pal = flw->palette();
    pal.setColor(QPalette::Base, { 42, 32, 32 });
    flw->setPalette(pal);
    r_lay->addWidget(flw);

    auto slider = new HTinySlider(this);
    auto slider_r = slider;
    slider->slider_color({ 128, 32, 32 });
    r_lay->addWidget(slider);

    slider->connect(slider, SIGNAL(changing(double)), color_button, SLOT(red(double)));
    slider->connect(slider, SIGNAL(changing(double)), flw, SLOT(setValue(double)));
    slider->connect(flw, SIGNAL(valueChanged(double)), slider, SLOT(value(double)));
    slider->connect(flw, SIGNAL(valueChanged(double)), color_button, SLOT(red(double)));
    slider->value(0);

    pick_layout->addLayout(r_lay);

    auto g_lay = new QVBoxLayout;
    g_lay->setContentsMargins(0, 0, 0, 0);
    g_lay->setSpacing(0);

    flw = new FloatWidget(this);
    auto flw_g = flw;
    flw->setFixedHeight(15);
    flw->setSingleStep(0.1f);
    pal = flw->palette();
    pal.setColor(QPalette::Base, { 32, 42, 32 });
    flw->setPalette(pal);
    g_lay->addWidget(flw);

    slider = new HTinySlider(this);
    auto slider_g = slider;
    slider->slider_color({ 32, 128, 32 });
    g_lay->addWidget(slider);

    slider->connect(slider, SIGNAL(changing(double)), color_button, SLOT(green(double)));
    slider->connect(slider, SIGNAL(changing(double)), flw, SLOT(setValue(double)));
    slider->connect(flw, SIGNAL(valueChanged(double)), slider, SLOT(value(double)));
    slider->connect(flw, SIGNAL(valueChanged(double)), color_button, SLOT(green(double)));
    slider->value(0);

    pick_layout->addLayout(g_lay);

    auto b_lay = new QVBoxLayout;
    b_lay->setContentsMargins(0, 0, 0, 0);
    b_lay->setSpacing(0);

    flw = new FloatWidget(this);
    auto flw_b = flw;
    flw->setFixedHeight(15);
    flw->setSingleStep(0.1f);
    pal = flw->palette();
    pal.setColor(QPalette::Base, { 32, 32, 42 });
    flw->setPalette(pal);
    b_lay->addWidget(flw);

    slider = new HTinySlider(this);
    auto slider_b = slider;
    slider->slider_color({ 32, 32, 128 });
    b_lay->addWidget(slider);

    slider->connect(slider, SIGNAL(changing(double)), color_button, SLOT(blue(double)));
    slider->connect(slider, SIGNAL(changing(double)), flw, SLOT(setValue(double)));
    slider->connect(flw, SIGNAL(valueChanged(double)), slider, SLOT(value(double)));
    slider->connect(flw, SIGNAL(valueChanged(double)), color_button, SLOT(blue(double)));
    slider->value(0);

    pick_layout->addLayout(b_lay);

    FloatWidget* flw_a = 0;
    HTinySlider* slider_a = 0;
    if (enable_alpha)
    {
        auto a_lay = new QVBoxLayout;
        a_lay->setContentsMargins(0, 0, 0, 0);
        a_lay->setSpacing(0);
        flw = new FloatWidget(this);
        flw_a = flw;
        flw->setFixedHeight(15);
        flw->setSingleStep(0.1f);
        a_lay->addWidget(flw);

        slider = new HTinySlider(this);
        slider_a = slider;
        slider->slider_color({ 128, 128, 128 });
        a_lay->addWidget(slider);

        slider->connect(slider, SIGNAL(changing(double)), color_button, SLOT(alpha(double)));
        slider->connect(slider, SIGNAL(changing(double)), flw, SLOT(setValue(double)));
        slider->connect(flw, SIGNAL(valueChanged(double)), slider, SLOT(value(double)));
        slider->connect(flw, SIGNAL(valueChanged(double)), color_button, SLOT(alpha(double)));
        slider->value(0);

        pick_layout->addLayout(a_lay);
    }

    connect(color_button, &ColorButton::changing, this, [=] {
        slider_r->value(color_button->red());
        slider_g->value(color_button->green());
        slider_b->value(color_button->blue());
        if (enable_alpha)
            slider_a->value(color_button->alpha());

        flw_r->setValue(color_button->red());
        flw_g->setValue(color_button->green());
        flw_b->setValue(color_button->blue());
        if (enable_alpha)
            flw_a->setValue(color_button->alpha());
    });
}

ColorWidget::ScreenColorPickingWidget::ScreenColorPickingWidget(QWidget* parent /*= nullptr*/)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint)
{
    static const auto background_color = QColor(52, 52, 52);
    QPalette background_palette;
    background_palette.setColor(QPalette::Window, background_color);
    setPalette(background_palette);
    auto current_color_widget = new CanvasWidget(this);
    current_color_widget->setFixedWidth(50);
    current_color_widget->paint_event = [this, current_color_widget](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;

        float w = current_color_widget->width();
        float h = current_color_widget->height();

        pen.setColor(background_color);
        pen.setWidth(2);

        QPainter painter(current_color_widget);
        painter.setPen(pen);

        brush.setColor(m_current_color);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);
    };

    auto previous_color_widget = new CanvasWidget(this);
    previous_color_widget->setFixedWidth(50);
    previous_color_widget->paint_event = [this, previous_color_widget](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;

        float w = previous_color_widget->width();
        float h = previous_color_widget->height();

        pen.setColor(background_color);
        pen.setWidth(2);

        QPainter painter(previous_color_widget);
        painter.setPen(pen);

        brush.setColor(m_previous_color);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);
    };

    QPalette r_palette;
    r_palette.setColor(QPalette::WindowText, QColor(250, 0, 0));
    QPalette g_palette;
    g_palette.setColor(QPalette::WindowText, QColor(0, 250, 0));
    QPalette b_palette;
    b_palette.setColor(QPalette::WindowText, QColor(0, 128, 255));

    m_current_r = new QLabel(QString::number(m_current_color.redF(), 'g', 4), this);
    m_current_g = new QLabel(QString::number(m_current_color.greenF(), 'g', 4), this);
    m_current_b = new QLabel(QString::number(m_current_color.blueF(), 'g', 4), this);
    m_previous_r = new QLabel(QString::number(m_previous_color.redF(), 'g', 4), this);
    m_previous_g = new QLabel(QString::number(m_previous_color.greenF(), 'g', 4), this);
    m_previous_b = new QLabel(QString::number(m_previous_color.blueF(), 'g', 4), this);
    const std::array<QPalette*, 3> palettes = { &r_palette, &g_palette, &b_palette };
    const std::array<QLabel*, 6> color_widgets = { m_current_r, m_current_g, m_current_b, m_previous_r, m_previous_g, m_previous_b };
    for (int i = 0; i < color_widgets.size(); i++)
    {
        color_widgets[i]->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        color_widgets[i]->setAlignment(Qt::AlignmentFlag::AlignVCenter | Qt::AlignmentFlag::AlignRight);
        color_widgets[i]->setPalette(*palettes[i % 3]);
    }

    auto current_color_rgb_layout = new QVBoxLayout;
    current_color_rgb_layout->addWidget(m_current_r);
    current_color_rgb_layout->addWidget(m_current_g);
    current_color_rgb_layout->addWidget(m_current_b);

    auto current_color_layout = new QHBoxLayout;
    current_color_layout->addWidget(current_color_widget);
    current_color_layout->addLayout(current_color_rgb_layout);

    auto previous_color_rgb_layout = new QVBoxLayout;
    previous_color_rgb_layout->addWidget(m_previous_r);
    previous_color_rgb_layout->addWidget(m_previous_g);
    previous_color_rgb_layout->addWidget(m_previous_b);

    auto previous_color_layout = new QHBoxLayout;
    previous_color_layout->addWidget(previous_color_widget);
    previous_color_layout->addLayout(previous_color_rgb_layout);

    auto vlayout = new QVBoxLayout;
    vlayout->setSpacing(0);
    vlayout->setContentsMargins(4, 4, 4, 4);
    vlayout->addLayout(current_color_layout);
    vlayout->addLayout(previous_color_layout);
    setLayout(vlayout);
}

void ColorWidget::ScreenColorPickingWidget::set_current_color(const QColor& current_color)
{
    m_current_color = current_color;
    m_current_r->setText(QString::number(m_current_color.redF(), 'g', 4));
    m_current_g->setText(QString::number(m_current_color.greenF(), 'g', 4));
    m_current_b->setText(QString::number(m_current_color.blueF(), 'g', 4));
}

void ColorWidget::ScreenColorPickingWidget::set_previous_color(const QColor& previous_color)
{
    m_previous_color = previous_color;
    m_previous_r->setText(QString::number(m_previous_color.redF(), 'g', 4));
    m_previous_g->setText(QString::number(m_previous_color.greenF(), 'g', 4));
    m_previous_b->setText(QString::number(m_previous_color.blueF(), 'g', 4));
}

OPENDCC_NAMESPACE_CLOSE
