// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/usd_node_editor/navigation_bar.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/ui/node_icon_registry.h"
#include <QPushButton>
#include <QLineEdit>
#include <QPainter>
#include <QEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QAction>
#include <QWidget>
#include <QMenu>
#include <QWidgetAction>
#include <QStylePainter>
#include <QCursor>
#include <QBitmap>
#include <QPainterPath>
PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

NavigationBar::NavigationBar(QWidget* parent /*= nullptr*/)
    : QWidget(parent)
{
    m_path_stack.reserve(50);
    auto layout = new QHBoxLayout(this);

    auto back_btn = new QPushButton(QIcon(":icons/small_left"), "");
    back_btn->setDisabled(true);
    auto forward_btn = new QPushButton(QIcon(":icons/small_right"), "");
    forward_btn->setDisabled(true);
    auto up_btn = new QPushButton(QIcon(":icons/small_up"), "");
    m_path_widget = new PathWidget(this);
    auto recent_btn = new QPushButton;
    auto pin_btn = new QPushButton(QIcon(":icons/small_lock"), "");
    m_recent_menu = new QMenu;

    back_btn->setFixedSize(20, 20);
    forward_btn->setFixedSize(20, 20);
    up_btn->setFixedSize(20, 20);
    recent_btn->setFixedSize(20, 20);
    pin_btn->setFixedSize(20, 20);
    back_btn->setFlat(true);
    forward_btn->setFlat(true);
    up_btn->setFlat(true);
    pin_btn->setFlat(true);
    recent_btn->setFlat(true);
    recent_btn->setMenu(m_recent_menu);
    recent_btn->setIconSize(QSize(16, 16));
    recent_btn->setStyleSheet("QPushButton:menu-indicator{ image: url(:icons/tabsMenu); }");
    connect(m_recent_menu, &QMenu::aboutToShow, this, [this] {
        QList<QAction*> recent_paths;
        for (auto iter = m_path_stack.rbegin(); iter != m_path_stack.rend(); iter++)
        {
            if (*iter == "/")
                continue;
            bool skip = false;
            for (const auto& recent_path : recent_paths)
            {
                if (recent_path->text() == *iter)
                {
                    skip = true;
                    break;
                }
            }
            if (skip)
                continue;

            auto stage = Application::instance().get_session()->get_current_stage();
            if (auto prim = stage->GetPrimAtPath(SdfPath(iter->toStdString())))
                recent_paths.push_back(new QAction(NodeIconRegistry::instance().get_icon(TfToken("USD"), prim.GetTypeName()), *iter));
        }
        m_recent_menu->clear();
        m_recent_menu->addActions(recent_paths);
    });
    connect(m_recent_menu, &QMenu::triggered, this, [this](QAction* action) { m_path_widget->update_path(action->text()); });

    layout->addWidget(back_btn);
    layout->addWidget(forward_btn);
    layout->addWidget(up_btn);
    layout->addWidget(m_path_widget);
    layout->addWidget(recent_btn);
    layout->addWidget(pin_btn);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    connect(m_path_widget, &PathWidget::path_changed, this, [this, back_btn, forward_btn, up_btn, recent_btn](const QString& path) {
        // workaround for double editingFinished event
        if (!m_path_stack.empty() && m_path_stack[m_stack_pointer] == path)
            return;
        if (m_stack_pointer == m_path_stack.size() - 1)
        {
            m_path_stack.push_back(path);
            m_stack_pointer++;
        }
        else
        {
            ++m_stack_pointer;
            m_path_stack.erase(m_path_stack.begin() + m_stack_pointer, m_path_stack.end());
            m_path_stack.push_back(path);
        }
        back_btn->setEnabled(m_stack_pointer > 0);
        forward_btn->setDisabled(true);
        up_btn->setDisabled(path == "/");
        const auto new_path = path.toStdString();
        m_path = new_path.empty() ? SdfPath::EmptyPath() : SdfPath(new_path);
        recent_btn->setEnabled(m_path_stack.size() > 1);
        this->path_changed(m_path);
    });
    connect(back_btn, &QPushButton::clicked, this, [this, back_btn, forward_btn, up_btn] {
        forward_btn->setEnabled(true);
        m_path_widget->blockSignals(true);
        m_path_widget->update_path(m_path_stack[--m_stack_pointer]);
        m_path_widget->blockSignals(false);
        m_path = SdfPath(m_path_stack[m_stack_pointer].toStdString());
        back_btn->setDisabled(m_stack_pointer == 0);
        up_btn->setDisabled(m_path_stack[m_stack_pointer] == "/");
        path_changed(m_path);
    });
    connect(forward_btn, &QPushButton::clicked, this, [this, forward_btn, back_btn, up_btn] {
        back_btn->setEnabled(true);
        m_path_widget->blockSignals(true);
        m_path_widget->update_path(m_path_stack[++m_stack_pointer]);
        m_path_widget->blockSignals(false);
        m_path = SdfPath(m_path_stack[m_stack_pointer].toStdString());
        forward_btn->setDisabled(m_stack_pointer == m_path_stack.size() - 1);
        up_btn->setDisabled(m_path_stack[m_stack_pointer] == "/");
        path_changed(m_path);
    });
    connect(up_btn, &QPushButton::clicked, this, [this] { m_path_widget->update_path(m_path.GetParentPath().GetText()); });
    m_path_widget->update_path(SdfPath::AbsoluteRootPath().GetText());
}

NavigationBar::~NavigationBar() {}

void NavigationBar::set_path(PXR_NS::SdfPath path)
{
    if (m_path == path)
        return;
    m_path = path;
    m_path_widget->update_path(path.GetText());
}

SdfPath NavigationBar::get_path() const
{
    return m_path;
}

PathWidget::PathWidget(QWidget* parent /*= nullptr*/)
    : QLineEdit(parent)
{
    m_layout = new QHBoxLayout;
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    setLayout(m_layout);
    setAlignment(Qt::AlignmentFlag::AlignLeft | Qt::AlignmentFlag::AlignCenter);

    m_address_group = new QButtonGroup(this);
    m_address_group->setExclusive(true);
    m_last_check_btn = nullptr;
    m_current_path = "/";

    connect(m_address_group, qOverload<QAbstractButton*>(&QButtonGroup::buttonClicked), this, &PathWidget::on_group_btn_clicked);
    connect(this, &QLineEdit::editingFinished, this, [this] {
        update_path(this->text());
        setReadOnly(true);
    });
    clearFocus();
    setReadOnly(true);
    setMouseTracking(true);
}
#define HOME_ICON_WIDTH 34
void PathWidget::update_path(const QString& path)
{
    clearFocus();
    clear_address_token_widget();

    auto old_path = m_current_path;
    auto path_std_str = path.toStdString();
    auto stage = Application::instance().get_session()->get_current_stage();
    if (stage)
    {
        if (SdfPath::IsValidPathString(path_std_str))
        {
            auto prim = stage->GetPrimAtPath(SdfPath(path_std_str));
            if (prim)
                m_current_path = path;
        }
        else
            m_current_path = QString("/");
    }
    else
    {
        m_current_path = QString("/");
    }
    setText("");

    int total_width = 0;
    int content_width = this->width();

    QStringList token_list = m_current_path.split('/', Qt::SkipEmptyParts);

    auto root_icon = new QPushButton(QIcon(":/icons/home"), "", this);
    root_icon->setFixedSize(20, 20);
    root_icon->setFlat(true);
    connect(root_icon, &QPushButton::clicked, this, [this] { update_path(QString("/")); });
    m_layout->addWidget(root_icon);

    auto root = new PathTokenWidget(QString(), QString("/"), !token_list.empty(), QPixmap(), this);
    connect(root, &PathTokenWidget::click_path, this, [this](const QString& path) { update_path(path); });
    m_layout->addWidget(root);
    m_address_group->addButton(root, 0);

    content_width = content_width - HOME_ICON_WIDTH;
    auto sdf_path = SdfPath(m_current_path.toStdString());
    for (int i = token_list.count() - 1; i >= 0; i--, sdf_path = sdf_path.GetParentPath())
    {
        auto icon = NodeIconRegistry::instance().get_icon(TfToken("USD"), stage->GetPrimAtPath(sdf_path).GetTypeName());
        if (!icon.isNull())
            icon = icon.scaled(20, 20, Qt::AspectRatioMode::KeepAspectRatio);
        // icon = icon.scaled(QSize(16, 16), Qt::AspectRatioMode::KeepAspectRatio);
        auto token_widget = new PathTokenWidget(token_list[i], sdf_path.GetText(), i != (token_list.count() - 1), icon, this);

        total_width += token_widget->width();
        m_layout->insertWidget(2, token_widget);
        connect(token_widget, &PathTokenWidget::click_path, this, [this](const QString& path) { update_path(path); });
        if (total_width < content_width)
        {
            token_widget->show();
            m_address_group->addButton(token_widget, i);
        }
        else
        {
            token_widget->hide();
        }
    }

    if (total_width > content_width)
        root->set_back_icon(true);
    m_layout->addStretch();
    update();
    if (old_path != path)
        path_changed(path);
}

void PathWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isReadOnly() && rect().contains(event->pos()))
    {
        setReadOnly(false);
        clear_address_token_widget();
        setText(m_current_path);
        selectAll();
        return;
    }
    QLineEdit::mouseReleaseEvent(event);
}

void PathWidget::mouseMoveEvent(QMouseEvent* event)
{

    if (m_address_group->checkedButton() != nullptr)
    {
        for (const auto& btn : m_address_group->buttons())
        {
            if (btn->geometry().contains(event->pos()) && m_address_group->checkedButton() != btn)
            {
                auto token_widget = qobject_cast<PathTokenWidget*>(btn);
                token_widget->setChecked(true);
                m_last_check_btn = token_widget;
            }
        }
    }
    QLineEdit::mouseMoveEvent(event);
}

void PathWidget::resizeEvent(QResizeEvent* event)
{
    if (m_layout->count() >= 2)
    {
        int content_width = event->size().width() - HOME_ICON_WIDTH;
        int items_width = 0;

        for (const auto& button : m_address_group->buttons())
            m_address_group->removeButton(button);
        m_last_check_btn = nullptr;

        for (int i = m_layout->count() - 2; i >= 2; i--)
        {
            auto item = m_layout->itemAt(i);
            auto path_token = qobject_cast<PathTokenWidget*>(item->widget());
            if (path_token == nullptr)
                continue;

            path_token->hide();
            items_width += path_token->width();
            if (items_width < content_width)
            {
                path_token->show();
                m_address_group->addButton(path_token, i);
            }
        }

        auto root = m_layout->itemAt(1);
        auto root_item = qobject_cast<PathTokenWidget*>(root->widget());
        root_item->set_back_icon(items_width > content_width);
    }

    clearFocus();
    QLineEdit::resizeEvent(event);
}

void PathWidget::on_group_btn_clicked(QAbstractButton* button)
{
    if (button == m_last_check_btn)
    {
        m_address_group->setExclusive(false);
        button->setChecked(false);
        m_address_group->setExclusive(true);
        m_last_check_btn = nullptr;
    }
    else
    {
        m_last_check_btn = button;
    }
}

void PathWidget::clear_address_token_widget()
{
    for (const auto& btn : m_address_group->buttons())
        m_address_group->removeButton(btn);
    m_last_check_btn = nullptr;
    QLayoutItem* child;
    while ((child = m_layout->takeAt(0)) != nullptr)
    {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }
}

#define ARROW_WIDTH 17

PathTokenWidget::PathTokenWidget(const QString& text, const QString& path, bool show_next /*= true*/, const QPixmap& icon /*= QIcon()*/,
                                 QWidget* parent /*= nullptr*/)
    : QPushButton(parent)
    , m_text(text)
    , m_path(path)
    , m_show_next(show_next)
    , m_icon(icon)
{
    m_font = parent->font();
    if (m_text.isEmpty())
    {
        m_text_width = 0;
        setFixedSize(m_icon.width() + show_next * ARROW_WIDTH, 21);
    }
    else
    {
        const int dist_between_text_and_menu_indicator = 8;
        QFontMetrics fm(m_font);
        m_text_width = fontMetrics().horizontalAdvance(m_text) + dist_between_text_and_menu_indicator;
        setFixedSize(m_icon.width() + m_text_width + show_next * ARROW_WIDTH, 21);
    }
    setIconSize(QSize(17, 21));
    m_normal_icon = QPixmap(":/icons/path_right");
    m_checked_icon = QPixmap(":/icons/path_down");
    setStyleSheet(R""""(
QPushButton:menu-indicator
{
    image: url(:/icons/path_right);
}

QPushButton:menu-indicator:open
{
    image: url(:icons/path_down);
}
)"""");
    setMouseTracking(true);
    setCheckable(true);
}

PathTokenWidget::~PathTokenWidget() {}

void PathTokenWidget::set_back_icon(bool flag)
{
    m_normal_icon = flag ? QPixmap(":icons/path_back") : QPixmap(":icons/path_right");
}

void PathTokenWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.save();
    if (underMouse())
    {
        painter.setPen(QColor(90, 90, 90));
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, rect().width(), rect().height() - 2), 2, 2);
        painter.fillPath(path, QBrush(QColor(58, 58, 58)));
        painter.drawRoundedRect(QRectF(0, 0, rect().width() - 1, rect().height() - 2), 2, 2);
        if (m_show_next)
            painter.drawLine(m_icon.width() + m_text_width, 0, m_icon.width() + m_text_width, rect().height() - 2);
    }

    if (isDown() || m_menu)
    {
        painter.setPen(QColor(28, 28, 28));
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, rect().width() - 1, rect().height() - 2), 2, 2);
        painter.fillPath(path, QBrush(QColor(33, 33, 33)));
        painter.drawRoundedRect(QRectF(0, 0, rect().width() - 1, rect().height() - 2), 2, 2);
        if (m_show_next)
            painter.drawLine(m_icon.width() + m_text_width, 0, m_icon.width() + m_text_width, rect().height() - 2);

        painter.restore();
        painter.setFont(m_font);
        painter.drawText(QRectF(m_icon.width(), 1, m_text_width, rect().height() - 2), Qt::AlignCenter, m_text);
        if (!m_icon.isNull())
            painter.drawPixmap(0, 0, m_icon);
        if (m_show_next)
            painter.drawPixmap(QRectF(m_icon.width() + m_text_width, 0, ARROW_WIDTH, rect().height()), m_checked_icon, m_checked_icon.rect());
    }
    else
    {
        painter.restore();
        painter.setFont(m_font);
        painter.drawText(QRectF(m_icon.width(), 0, m_text_width, rect().height() - 3), Qt::AlignCenter, m_text);
        if (!m_icon.isNull())
            painter.drawPixmap(0, 0, m_icon);
        if (m_show_next)
            painter.drawPixmap(QRectF(m_icon.width() + m_text_width, 0, ARROW_WIDTH, rect().height()), m_normal_icon, m_normal_icon.rect());
    }
}

void PathTokenWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (QRectF(m_icon.width(), 0, m_text_width, rect().height()).contains(event->pos()))
        {
            click_path(m_path);
        }
        else if (QRectF(m_icon.width() + m_text_width, 0, ARROW_WIDTH, rect().height()).contains(event->pos()))
        {
            auto stage = Application::instance().get_session()->get_current_stage();
            if (!stage)
                return;
            auto prim = stage->GetPrimAtPath(SdfPath(m_path.toStdString()));
            if (!prim)
                return;

            m_menu = new QMenu(this);
            m_menu->setAttribute(Qt::WA_DeleteOnClose);

            QPoint global_point(mapToGlobal(QPoint(0, 0)));
            global_point.setX(global_point.x() + m_icon.width() + ARROW_WIDTH + m_text_width - 30);
            global_point.setY(global_point.y() + rect().height());
            m_menu->move(global_point);
            connect(m_menu, &QMenu::triggered, this,
                    [this](QAction* action) { click_path(m_path + (m_path.endsWith('/') ? "" : "/") + action->text()); });
            connect(m_menu, &QMenu::aboutToHide, this, [this] { m_menu = nullptr; });
            for (const auto& child : prim.GetChildren())
            {
                m_menu->addAction(NodeIconRegistry::instance().get_icon(TfToken("USD"), child.GetTypeName()), child.GetName().GetText());
            }
            m_menu->popup(global_point);
        }
        update();
    }
    QPushButton::mouseReleaseEvent(event);
}

OPENDCC_NAMESPACE_CLOSE
