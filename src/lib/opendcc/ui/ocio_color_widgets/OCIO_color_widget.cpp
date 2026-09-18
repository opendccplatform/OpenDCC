// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/ui/ocio_color_widgets/OCIO_color_widget.h"
#include "opendcc/ui/common_widgets/qt_compat.h"

#include <QLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>

#include <array>

OPENDCC_NAMESPACE_OPEN

OCIOColorManager::OCIOColorManager()
{
    namespace OCIO = OCIO_NAMESPACE;
    m_config = OCIO::GetCurrentConfig();
    if (!m_config)
        return;

    m_processor = m_config->getProcessor(OCIO::ROLE_SCENE_LINEAR, OCIO::ROLE_COLOR_PICKING);
    m_reverse_processor = m_config->getProcessor(OCIO::ROLE_COLOR_PICKING, OCIO::ROLE_SCENE_LINEAR);
}

QColor OCIOColorManager::convert(const QColor& color, bool reverse /* = false*/)
{
    auto processor = reverse ? m_reverse_processor : m_processor;

    std::array<float, 4> rgba = {
        static_cast<float>(color.redF()),
        static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()),
        static_cast<float>(color.alphaF()),
    };

#if OCIO_VERSION_MAJOR < 2
    processor->applyRGBA(rgba.data());
#else
    auto cpuProcessor = processor->getDefaultCPUProcessor();
    cpuProcessor->applyRGBA(rgba.data());
#endif
    return QColor::fromRgbF(rgba[0], rgba[1], rgba[2], rgba[3]);
}

OCIOColorWidget::OCIOColorWidget(QWidget* parent /* = nullptr*/, bool enable_alpha /* = false*/)
    : ColorWidget(parent, enable_alpha)
    , m_color_manager(new OCIOColorManager)
{
    setMinimumSize(200, 110);

    setup_hue();
    setup_sat();
    setup_val();

    if (enable_alpha)
        setup_alpha();

    init_box();
    init_box_sat_val();
    if (enable_alpha)
        init_box_alpha();

    setup_preview();
    setup_palette();

    auto color_mgmt_layout = new QHBoxLayout();
    color_mgmt_layout->setAlignment(Qt::AlignLeft);

    color_mgmt_layout->addWidget(new QLabel("Color Management:", this));
    m_check_box = new QCheckBox(this);
    m_check_box->setChecked(false);
    color_mgmt_layout->addWidget(m_check_box);
    layout()->addItem(color_mgmt_layout);

    connect(m_check_box, &QCheckBox::stateChanged, this, [this](int state) { enabled(state == Qt::CheckState::Checked); });

    connect(this, &OCIOColorWidget::enabled, this, [this] { update(); });

    connect(this, &ColorWidget::changing_color, this, [this] {
        if (is_enabled())
            color_changed(color());
    });

    connect(this, &ColorWidget::color_changed, this, [this] { color_changed(color()); });

    connect(m_pick_variant, qOverload<int>(&ComboBoxNoFocus::currentIndexChanged), this, [this] {
        if (m_pick_variant->currentIndex() == RGB)
        {
            setup_R();
            setup_G();
            setup_B();
        }
        else // BOX || HSV
        {
            setup_hue();
            setup_sat();
            setup_val();
        }

        update();
    });
}

OCIOColorWidget::~OCIOColorWidget()
{
    delete m_color_manager;
}

bool OCIOColorWidget::is_enabled()
{
    return m_check_box->checkState() == Qt::CheckState::Checked;
}

QColor OCIOColorWidget::convert(const QColor& color, bool reverse /* = false*/, bool normalize /* = true*/)
{
    auto new_color = m_color_manager->convert(color, reverse);
    if (normalize)
    {
        if (new_color.hueF() == -1) // achromatic
            new_color.setHsvF(0, new_color.saturationF(), new_color.valueF());
    }
    return new_color;
}

void OCIOColorWidget::color_changed(const QColor& new_color)
{
    m_converted = convert(new_color);
}

void OCIOColorWidget::setup_hue()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_HUE_pair);
        auto control = std::get<1>(m_HUE_pair);

        float w = canvas->width();
        H = clamp(float(e->x()) / w, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_achromatic ? convert(m_prev_chromatic) : m_converted;
            converted.setHsvF(H, converted.saturationF(), converted.valueF(), converted.alphaF());
            auto reverse_converted = convert(converted, true);
            H = reverse_converted.hueF();
        }
        control->setValue(H * 360.f);
    };

    auto canvas = std::get<0>(m_HUE_pair);
    auto pressed = std::make_shared<bool>(false);

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
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_HUE_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, Qt::GlobalColor::red);
        grad.setColorAt(0.17, Qt::GlobalColor::yellow);
        grad.setColorAt(0.34, Qt::GlobalColor::green);
        grad.setColorAt(0.51, Qt::GlobalColor::cyan);
        grad.setColorAt(0.67, Qt::GlobalColor::blue);
        grad.setColorAt(0.84, Qt::GlobalColor::magenta);
        grad.setColorAt(1, Qt::GlobalColor::red);

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        qreal h_val = is_enabled() ? m_converted.hueF() : H;
        painter.drawRect(w * h_val - 2, 0, 4, h);
    };
}

void OCIOColorWidget::setup_sat()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_SAT_pair);
        auto control = std::get<1>(m_SAT_pair);

        float w = canvas->width();
        S = clamp(float(e->x()) / w, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_achromatic ? convert(m_prev_chromatic) : m_converted;
            converted.setHsvF(converted.hueF(), S, converted.valueF(), converted.alphaF());
            auto reverse_converted = convert(converted, true);
            S = reverse_converted.saturationF();
        }
        control->setValue(S);
        update();
    };

    auto canvas = std::get<0>(m_SAT_pair);
    auto pressed = std::make_shared<bool>(false);

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
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_SAT_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        qreal h_val = H;
        qreal s_val = S;
        qreal v_val = V;
        if (is_enabled())
            get_hsv_f(m_converted, &h_val, &s_val, &v_val);

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromHsvF(h_val, 0, v_val, 1));
        grad.setColorAt(1, QColor::fromHsvF(h_val, 1, v_val, 1));

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * s_val - 2, 0, 4, h);
    };
}

void OCIOColorWidget::setup_val()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_VAL_pair);
        auto control = std::get<1>(m_VAL_pair);

        float w = canvas->width();
        V = clamp(float(e->x()) / w, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_achromatic ? convert(m_prev_chromatic) : m_converted;
            converted.setHsvF(converted.hueF(), converted.saturationF(), V, converted.alphaF());
            auto reverse_converted = convert(converted, true);
            V = reverse_converted.valueF();
        }
        control->setValue(V);
    };

    auto canvas = std::get<0>(m_VAL_pair);
    auto pressed = std::make_shared<bool>(false);

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
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_VAL_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        qreal h_val = H;
        qreal s_val = S;
        qreal v_val = V;
        if (is_enabled())
            get_hsv_f(m_converted, &h_val, &s_val, &v_val);

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromHsvF(h_val, s_val, 0, 1));
        grad.setColorAt(1, QColor::fromHsvF(h_val, s_val, 1, 1));

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * v_val - 2, 0, 4, h);
    };
}

void OCIOColorWidget::setup_R()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_HUE_pair);
        auto control = std::get<1>(m_HUE_pair);

        float w = canvas->width();
        R = clamp(float(e->x()) / w, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_converted;
            converted.setRgbF(R, converted.greenF(), converted.blueF(), converted.alphaF());
            auto reverse_converted = convert(converted, true);
            R = reverse_converted.redF();
        }
        control->setValue(R);
    };

    auto canvas = std::get<0>(m_HUE_pair);
    auto pressed = std::make_shared<bool>(false);

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
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_HUE_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        qreal r_val = R;
        qreal g_val = G;
        qreal b_val = B;
        if (is_enabled())
            get_rgb_f(m_converted, &r_val, &g_val, &b_val);

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromRgbF(0, g_val, b_val));
        grad.setColorAt(1, QColor::fromRgbF(1, g_val, b_val));

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * r_val - 2, 0, 4, h);
    };
}

void OCIOColorWidget::setup_G()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_SAT_pair);
        auto control = std::get<1>(m_SAT_pair);

        float w = canvas->width();
        G = clamp(float(e->x()) / w, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_converted;
            converted.setRgbF(converted.redF(), G, converted.blueF(), converted.alphaF());
            auto reverse_converted = convert(converted, true);
            G = reverse_converted.greenF();
        }
        control->setValue(G);
    };

    auto canvas = std::get<0>(m_SAT_pair);
    auto pressed = std::make_shared<bool>(false);

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
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_SAT_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        qreal r_val = R;
        qreal g_val = G;
        qreal b_val = B;
        if (is_enabled())
            get_rgb_f(m_converted, &r_val, &g_val, &b_val);

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromRgbF(r_val, 0, b_val));
        grad.setColorAt(1, QColor::fromRgbF(r_val, 1, b_val));

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * g_val - 2, 0, 4, h);
    };
}

void OCIOColorWidget::setup_B()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_VAL_pair);
        auto control = std::get<1>(m_VAL_pair);

        float w = canvas->width();
        B = clamp(float(e->x()) / w, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_converted;
            converted.setRgbF(converted.redF(), converted.greenF(), B, converted.alphaF());
            auto reverse_converted = convert(converted, true);
            B = reverse_converted.blueF();
        }
        control->setValue(B);
    };

    auto canvas = std::get<0>(m_VAL_pair);
    auto pressed = std::make_shared<bool>(false);

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
    canvas->paint_event = [this](QPaintEvent*) {
        auto canvas = std::get<0>(m_VAL_pair);
        float w = canvas->width() - 1;
        float h = canvas->height() - 1;

        qreal r_val = R;
        qreal g_val = G;
        qreal b_val = B;
        if (is_enabled())
            if (is_enabled())
                get_rgb_f(m_converted, &r_val, &g_val, &b_val);

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(0, QColor::fromRgbF(r_val, g_val, 0));
        grad.setColorAt(1, QColor::fromRgbF(r_val, g_val, 1));

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(w * b_val - 2, 0, 4, h);
    };
}

void OCIOColorWidget::setup_alpha()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto canvas = std::get<0>(m_ALP_pair);
        auto control = std::get<1>(m_ALP_pair);

        float w = canvas->width();
        A = clamp(float(e->x()) / w, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_converted;
            converted.setAlphaF(A);
            auto reverse_converted = convert(converted, true);
            A = reverse_converted.alphaF();
        }
        control->setValue(A);
    };

    auto canvas = std::get<0>(m_ALP_pair);
    auto pressed = std::make_shared<bool>(false);

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
        brush.setColor({ 42, 42, 42 });
        brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
        QPainter painter(canvas);
        painter.setPen(pen);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);

        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        qreal a_val = is_enabled() ? m_converted.alphaF() : A;
        painter.drawRect(w * a_val - 2, 0, 4, h);
    };
}

void OCIOColorWidget::init_box()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto control = std::get<1>(m_HUE_pair);
        float h = m_box_hue->height();
        H = clamp(float(e->y()) / h, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_converted;
            converted.setHsvF(H, converted.saturationF(), converted.valueF(), converted.alphaF());
            auto reverse_converted = convert(converted, true);
            H = reverse_converted.hueF();
        }
        control->setValue(H * 360.f);
    };

    auto pressed = std::make_shared<bool>(false);
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
        grad.setColorAt(0, Qt::GlobalColor::red);
        grad.setColorAt(0.17, Qt::GlobalColor::yellow);
        grad.setColorAt(0.34, Qt::GlobalColor::green);
        grad.setColorAt(0.51, Qt::GlobalColor::cyan);
        grad.setColorAt(0.67, Qt::GlobalColor::blue);
        grad.setColorAt(0.84, Qt::GlobalColor::magenta);
        grad.setColorAt(1, Qt::GlobalColor::red);

        QPen pen;
        pen.setColor({ 58, 58, 58 });
        QPainter painter(m_box_hue);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QBrush brush;
        brush.setColor(Qt::GlobalColor::white);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        qreal h_val = is_enabled() ? m_converted.hueF() : H;
        painter.drawRect(0, h_val * h - 2, w, 4);
    };
}

void OCIOColorWidget::init_box_sat_val()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto controlS = std::get<1>(m_SAT_pair);
        auto controlV = std::get<1>(m_VAL_pair);

        float w = m_box_sat_val->width();
        float h = m_box_sat_val->height();

        S = clamp(float(e->x()) / w, 0.f, 1.f);
        V = clamp(float(e->y()) / h, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_converted;
            converted.setHsvF(converted.hueF(), S, V, converted.alphaF());
            auto reverse_converted = convert(converted, true);
            S = reverse_converted.saturationF();
            V = reverse_converted.valueF();
        }

        controlS->setValue(S);
        controlV->setValue(V);
        update();
    };

    auto pressed = std::make_shared<bool>(false);
    auto cursor_pos = std::make_shared<QPoint>();

    m_box_sat_val->mouse_press_event = [this, pressed, mouse_actions, cursor_pos](QMouseEvent* e) {
        *pressed = true;
        m_change_in_progress = true;
        mouse_actions(e);
        *cursor_pos = e->pos();
    };
    m_box_sat_val->mouse_move_event = [this, pressed, mouse_actions, cursor_pos](QMouseEvent* e) {
        if (!*pressed)
            return;
        mouse_actions(e);
        *cursor_pos = e->pos();
    };
    m_box_sat_val->paint_event = [this, cursor_pos](QPaintEvent*) {
        float w = m_box_sat_val->width() - 1;
        float h = m_box_sat_val->height() - 1;

        qreal h_val = H;
        qreal s_val = S;
        qreal v_val = V;
        if (is_enabled())
        {
            get_hsv_f(m_converted, &h_val, &s_val, &v_val);
            if (v_val == 0)
                s_val = clamp(float(cursor_pos->x()) / w, 0.f, 1.f); // just a visual fix so it doesn't "jump" to 0 (upper-left corner of the BOX)
        }

        QLinearGradient grad(0, 0, w, 0);
        grad.setColorAt(1, QColor::fromHsvF(h_val, 1, 1));
        grad.setColorAt(0, Qt::GlobalColor::white);

        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QPainter painter(m_box_sat_val);
        painter.setPen(pen);
        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        QLinearGradient grad2(0, 0, 0, h);
        grad2.setColorAt(1, { 255, 0, 0, 0 });
        grad2.setColorAt(0, { 0, 0, 0, 255 });
        painter.setBrush(grad2);
        painter.drawRect(0, 0, w, h);

        pen.setColor(Qt::GlobalColor::white);
        painter.setPen(pen);

        QBrush brush;
        brush.setStyle(Qt::BrushStyle::NoBrush);
        painter.setBrush(brush);
        painter.drawRect(w * s_val - 3, h * v_val - 3, 6, 6);
    };
}

void OCIOColorWidget::init_box_alpha()
{
    auto mouse_actions = [this](QMouseEvent* e) {
        auto control = std::get<1>(m_ALP_pair);
        float h = m_box_alpha->height();
        A = clamp(float(e->y()) / h, 0.f, 1.f);
        if (is_enabled())
        {
            auto converted = m_converted;
            converted.setAlphaF(A);
            auto reverse_converted = convert(converted, true);
            A = reverse_converted.alphaF();
        }
        control->setValue(A);
    };

    auto pressed = std::make_shared<bool>(false);

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
        if (is_enabled())
            color_val = convert(color_val);
        color_val.setAlphaF(0);
        grad.setColorAt(0, color_val);
        color_val.setAlphaF(1);
        grad.setColorAt(1, color_val);

        painter.setBrush(grad);
        painter.drawRect(0, 0, w, h);

        brush.setColor({ 255, 255, 255, 255 });
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        qreal a_val = is_enabled() ? m_converted.alphaF() : A;
        painter.drawRect(0, h * a_val - 2, w, 4);
    };
}

void OCIOColorWidget::setup_preview()
{
    m_color_box->paint_event = [this](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;
        QPainter painter(m_color_box);
        painter.setPen(pen);

        float w = m_color_box->width() - 1;
        float h = m_color_box->height() - 1;

        brush.setColor({ 42, 42, 42 });
        brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);

        QColor new_color = is_enabled() ? m_converted : color();
        brush.setColor(new_color);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);
    };

    m_prev_color_box->paint_event = [this](QPaintEvent*) {
        QPen pen;
        pen.setColor({ 58, 58, 58 });

        QBrush brush;
        QPainter painter(m_prev_color_box);
        painter.setPen(pen);

        float w = m_prev_color_box->width() - 1;
        float h = m_prev_color_box->height() - 1;

        brush.setColor({ 42, 42, 42 });
        brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);

        QColor new_color = is_enabled() ? convert(m_prev_color) : m_prev_color;
        brush.setColor(new_color);
        brush.setStyle(Qt::BrushStyle::SolidPattern);
        painter.setBrush(brush);
        painter.drawRect(0, 0, w, h);
    };
}

void OCIOColorWidget::setup_palette()
{
    for (int i = 0; i < m_palette_boxes.size(); i++)
    {
        auto palette_color = &m_palette[i];
        auto palette_box = m_palette_boxes[i];
        palette_box->setFixedSize(15, 15);
        palette_box->paint_event = [palette_box, this, palette_color](QPaintEvent*) {
            QPen pen;
            pen.setColor(QColor(42, 42, 42));
            if (palette_box == m_current_palette)
                pen.setWidthF(4);

            QPainter painter(palette_box);
            painter.setPen(pen);
            QBrush brush;

            float w = palette_box->width() - 2;
            float h = palette_box->height() - 2;

            brush.setColor({ 42, 42, 42 });
            brush.setStyle(Qt::BrushStyle::DiagCrossPattern);
            painter.setBrush(brush);
            painter.drawRect(1, 1, w, h);

            auto paint_color = is_enabled() ? convert(*palette_color) : *palette_color;
            brush.setColor(paint_color);
            brush.setStyle(Qt::BrushStyle::SolidPattern);
            painter.setBrush(brush);
            painter.drawRect(1, 1, w, h);
        };
    }
}

OCIOColorPickDialog::OCIOColorPickDialog(QWidget* parent /* = nullptr*/, bool enable_alpha /* = false*/)
    : ColorPickDialog(parent, enable_alpha, new OCIOColorWidget(parent, enable_alpha))
{
    setFixedSize(295, 110);
}

OCIOColorButton::OCIOColorButton(QWidget* parent /* = nullptr*/, bool enable_alpha /* = false*/)
    : ColorButton(parent, enable_alpha, new OCIOColorPickDialog(nullptr, enable_alpha))
{
}

OPENDCC_NAMESPACE_CLOSE
