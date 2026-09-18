// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/ui/logger/render_log.h"

#include "opendcc/app/ui/application_ui.h"

#include <QSplitter>
#include <QScrollBar>
#include <QListWidget>
#include <QPlainTextEdit>

OPENDCC_NAMESPACE_OPEN

void split(std::vector<std::string>& dest, const std::string& orig, char by)
{
    dest.clear();
    auto modif = orig.c_str();

    auto car = modif;
    for (; *car; ++car)
    {
        if (*car == by)
        {
            if (car == modif)
            {
                modif++;
                continue;
            }

            dest.push_back(std::string(modif, car));
            modif = car + 1;
        }
    }

    if (car != modif)
        dest.push_back(modif);
}

RenderLog::RenderLog(QWidget* parent /*= 0*/)
    : QWidget(parent)
{
    auto layout = new QHBoxLayout;
    layout->setContentsMargins(1, 1, 1, 1);
    QSplitter* splitter = new QSplitter;
    layout->addWidget(splitter);
    splitter->addWidget(m_catalog_list = new QListWidget);
    splitter->addWidget(m_output = new QPlainTextEdit);

    auto size_pol = m_catalog_list->sizePolicy();
    size_pol.setHorizontalStretch(20);
    m_catalog_list->setSizePolicy(size_pol);

    size_pol = m_output->sizePolicy();
    size_pol.setHorizontalStretch(80);
    m_output->setSizePolicy(size_pol);

    m_output->setReadOnly(true);
    m_output->setWordWrapMode(QTextOption::WrapMode::NoWrap);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLayout(layout);

    for (auto item : RenderCatalog::instance().catalogs())
        add_catalog(item.c_str());

    m_current_catalog = RenderCatalog::instance().current_catalog();
    m_output->appendPlainText(RenderCatalog::instance().get_log(m_current_catalog).c_str());

    setup_new_catalog();
    setup_active_catalog();
    setup_add_msg();
    setup_catalog_changed();
}

RenderLog::~RenderLog()
{
    RenderCatalog::instance().unregister_new_catalog(m_new_catalog_handle);
    RenderCatalog::instance().unregister_activate_catalog(m_active_catalog_handle);
    RenderCatalog::instance().unregister_add_msg(m_add_msg_handle);
}

void RenderLog::setup_new_catalog()
{
    m_new_catalog_handle = RenderCatalog::instance().at_new_catalg([this](std::string new_catalog_val) {
        m_current_catalog = new_catalog_val;
        clear_log();
        new_catalog(new_catalog_val.c_str());
    });
    connect(this, &RenderLog::clear_log, this, [this]() { m_output->clear(); });
    connect(this, &RenderLog::new_catalog, this, [this](QString catalog) {
        add_catalog(catalog);

        std::vector<std::string> val;
        split(val, RenderCatalog::instance().get_log(catalog.toLocal8Bit().data()), '\n');

        for (auto item : val)
        {
            auto iter = item.find('\n');
            if (iter != std::string::npos)
                item.erase(iter);
            iter = item.find('\r');
            if (iter != std::string::npos)
                item.erase(iter);

            if (item.empty())
                return;

            m_output->appendPlainText(item.c_str());
        }
        m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
        m_output->horizontalScrollBar()->setValue(m_output->horizontalScrollBar()->minimum());
    });
}

void RenderLog::setup_active_catalog()
{
    m_active_catalog_handle = RenderCatalog::instance().at_activate_catalg([this](std::string catalog) {
        m_current_catalog = catalog;
        set_log(RenderCatalog::instance().get_log(catalog).c_str());
    });
    connect(this, &RenderLog::set_log, this, [this](QString log) {
        m_output->clear();
        m_output->appendPlainText(log);
        m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
        m_output->horizontalScrollBar()->setValue(m_output->horizontalScrollBar()->minimum());
    });
}

void RenderLog::setup_add_msg()
{
    m_add_msg_handle = RenderCatalog::instance().at_add_msg([this](std::string catalog, std::string msg) {
        if (m_current_catalog == catalog)
        {
            std::vector<std::string> val;
            split(val, msg, '\n');

            for (auto item : val)
            {
                auto iter = item.find('\n');
                if (iter != std::string::npos)
                    item.erase(iter);
                iter = item.find('\r');
                if (iter != std::string::npos)
                    item.erase(iter);

                if (item.empty())
                    return;

                add_msg(item.c_str());
            }
        }
    });
    connect(this, &RenderLog::add_msg, this, [this](QString msg) {
        m_output->appendPlainText(msg);
        m_output->verticalScrollBar()->setValue(m_output->verticalScrollBar()->maximum());
        m_output->horizontalScrollBar()->setValue(m_output->horizontalScrollBar()->minimum());
    });
}

void RenderLog::setup_catalog_changed()
{
    connect(m_catalog_list, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { RenderCatalog::instance().activate_catalog(item->data(Qt::UserRole).toString().toLocal8Bit().data()); });
}

void RenderLog::add_catalog(QString catalog_name)
{
    auto catalog_info = RenderCatalog::instance().get_info(catalog_name.toLocal8Bit().data());

    auto widget = new QWidget(m_catalog_list);
    widget->setContentsMargins(0, 0, 0, 0);
    auto item_layout = new QVBoxLayout(widget);
    item_layout->setContentsMargins(1, 3, 1, 3);
    item_layout->setSpacing(1);
    item_layout->addWidget(new QLabel(i18n("logger.render_log", "Time: ") + catalog_name));
    item_layout->addWidget(new QLabel(i18n("logger.render_log", "Frame: ") + QString::number(catalog_info->frame_time)));
    item_layout->addWidget(new QLabel(i18n("logger.render_log", "Output: ") + catalog_info->terminal_node.c_str()));
    item_layout->setSizeConstraint(QLayout::SetFixedSize);
    widget->setLayout(item_layout);

    auto list_item = new QListWidgetItem();
    list_item->setData(Qt::UserRole, catalog_name);
    list_item->setSizeHint(widget->sizeHint());
    m_catalog_list->addItem(list_item);
    m_catalog_list->setItemWidget(list_item, widget);
    list_item->setSelected(true);
    m_catalog_list->verticalScrollBar()->setValue(m_catalog_list->verticalScrollBar()->maximum());
    m_catalog_list->horizontalScrollBar()->setValue(m_catalog_list->horizontalScrollBar()->minimum());
}

OPENDCC_NAMESPACE_CLOSE
