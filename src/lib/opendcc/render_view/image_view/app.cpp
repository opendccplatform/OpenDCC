// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/render_view/image_view/app.h"

#include "opendcc/render_view/image_view/listener_thread.hpp"
#include "opendcc/render_view/image_view/image_cache.h"
#include "opendcc/render_view/image_view/gl_widget.h"
#include "opendcc/render_view/image_view/qt_utils.h"
#include "opendcc/base/utils/process.h"
#include "opendcc/base/defines.h"
#include "opendcc/base/ipc_commands_api/server.h"

#include <QApplication>
#include <QLabel>

#include <QMessageBox>
#include <QThreadPool>
#include <QPushButton>
#include <QTreeWidget>
#include <QFileInfo>
#include <QRunnable>
#include <QDateTime>
#include <QTimer>
#include <QMenu>
#include <QDir>
#include <QUrl>

#include <filesystem>

#include <deque>
#include <locale>
#include <codecvt>
#include <regex>
#include <zmq.h>
#ifdef OPENDCC_OS_LINUX
#include <signal.h>
#endif

#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")

OPENDCC_NAMESPACE_OPEN

namespace ipc = base::ipc;

static int clamp(int lower, int n, int upper)
{
    return std::max(lower, std::min(n, upper));
}

RenderViewMainWindow* RenderViewMainWindow::m_global_instance = nullptr;

class RegionUploadTask : public QRunnable
{
public:
    RegionUploadTask(RenderViewMainWindow* app, RenderViewInternalImageCache* image_cache, int image_id, RenderViewMainWindow::ImageROI region,
                     std::shared_ptr<const std::vector<char>> bucket_data)
    {
        m_app = app;
        m_region = region;
        m_image_id = image_id;
        m_image_cache = image_cache;
        m_bucket_data = bucket_data;
    }

    void run()
    {
        auto image = m_image_cache->acquire_image(m_image_id);
        if (image)
        {
            auto image_spec = image->spec();
            OIIO::ROI roi(m_region.xstart, m_region.xend, m_region.ystart, m_region.yend, 0, 1, 0, image_spec.nchannels);
            image->set_pixels(roi, image_spec.format, m_bucket_data->data());
            m_image_cache->release_image(m_image_id);

            if (m_app->get_current_image_id() == m_image_id)
            {
                emit m_app->current_image_pixels_changed();
            }
        }
    }

private:
    std::shared_ptr<const std::vector<char>> m_bucket_data;
    RenderViewMainWindow::ImageROI m_region;
    int m_image_id;
    RenderViewInternalImageCache* m_image_cache;
    RenderViewMainWindow* m_app;
};

RenderViewMainWindow::RenderViewMainWindow(RenderViewPreferences&& preferences,
                                           const base::app_config::ApplicationConfig& config /* = ApplicationConfig() */)
    : m_prefs(std::move(preferences))
    , m_app_config(config)
{
    RenderViewPreferencesWindowOptions prefs_window_options;

    m_current_image = -1;
    m_current_channel = 0;
    m_color_mode = ColorMode::RGB;
    m_current_input_colorspace = -1;
    m_gamma = 1.0;
    m_exposure = 0.0;
    size_t max_size = 100;
    m_image_cache = new RenderViewInternalImageCache(max_size);
    m_current_image_buf = nullptr;
    m_background_image_buf = nullptr;
    m_toggle_background = false;
    m_background_image = -1;

    m_ocio_enabled = false;
    setObjectName("main");
    // common zeromq context for bucket driver and command port
    m_zmq_ctx = zmq_ctx_new();
    init_ui();
    init_ocio(prefs_window_options.color_space_values, prefs_window_options.display_values);
    init_background_mode();
    create_actions();
    create_menus();
    read_settings();
    load_shortcuts();

    m_prefs_window = new RenderViewPreferencesWindow(this, prefs_window_options);
    m_prefs_window->update_pref_windows();
    preferences_updated();

    m_listener_thread = new RenderViewListenerThread(this, m_zmq_ctx);
    m_listener_thread->start();

    connect(m_listener_thread, SIGNAL(new_image()), this, SLOT(activate_window_slot()));

    // ui
    connect(this, SIGNAL(current_image_changed(int)), this, SLOT(change_current_image(int)));
    connect(this, SIGNAL(new_image_item_event(int, int, const QString&)), this, SLOT(new_image_item(int, int, const QString&)));
    connect(m_prefs_window, SIGNAL(preferences_updated()), this, SLOT(preferences_updated()));
    clear_scratch_images();

    m_timesago_timer = new QTimer();
    m_timesago_timer->setInterval(1000 * 5);
    connect(m_timesago_timer, SIGNAL(timeout()), this, SLOT(update_timesago()));
    m_timesago_timer->start();
    QTimer* timer = new QTimer();
    timer->callOnTimeout([this]() { update_status_label(); });
    timer->start(500);

    ipc::ServerInfo info;
    info.hostname = "127.0.0.1";
    m_server = std::make_unique<ipc::CommandServer>(info);
    info = m_server->get_info();

    m_main_server_info.hostname = "127.0.0.1";
    m_main_server_info.input_port = m_app_config.get<uint32_t>("ipc.command_server.port", 8000);

    ipc::CommandServer::set_server_timeout(m_app_config.get<int>("ipc.command_server.server_timeout", 1000));

    ipc::Command command;
    command.name = "ServerCreated";
    command.args["pid"] = base::utils::get_pid_string();
    command.args["hostname"] = info.hostname;
    command.args["input_port"] = std::to_string(info.input_port);
    m_server->send_command(m_main_server_info, command);
}

void RenderViewMainWindow::activate_window_slot()
{
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
}

void RenderViewMainWindow::init_ocio(std::vector<QString>& colorspace_values, std::vector<QString>& display_values)
{
    colorspace_values.clear();
    display_values.clear();

    // there is must be better way for this common case that you see in any DCC that use OCIO
    // OCIO var should override embedded config into app installation
    // TODO investigate OCIO API further, it could be simpler way to do this
    auto ocio_var = getenv("OCIO");
    OCIO::ConstConfigRcPtr config;

    if (ocio_var && QFileInfo::exists(ocio_var))
    {
        config = OCIO::Config::CreateFromEnv();
    }
    else
    {
        QString config_path = QFileInfo(QApplication::applicationFilePath()).absolutePath();
        config_path += "/../ocio/config.ocio";
        if (QFileInfo::exists(config_path))
            config = OCIO::Config::CreateFromFile(config_path.toLocal8Bit().constData());
        else
            config = OCIO::Config::CreateFromEnv(); // create a fall-back config.
    }

    if (config)
    {
        m_ocio_enabled = true;
        OCIO::ConstColorSpaceRcPtr defaultcs = config->getColorSpace(OCIO::ROLE_SCENE_LINEAR);
        if (!defaultcs)
            return;

        std::string current_color_space = m_prefs.default_image_color_space.toLocal8Bit().data();
        if (current_color_space == "")
        {
            current_color_space = defaultcs->getName();
            m_prefs.image_color_space = current_color_space.c_str();
        }

        bool id_current_color_space_in_config = false;
        for (int i = 0; i < config->getNumColorSpaces(); i++)
        {
            if (config->getColorSpaceNameByIndex(i) == current_color_space)
            {
                id_current_color_space_in_config = true;
                break;
            }
        }

        if (!id_current_color_space_in_config)
        {
            current_color_space = defaultcs->getName();
            m_prefs.image_color_space = current_color_space.c_str();
        }

        OCIO::SetCurrentConfig(config);
        m_input_colorspace_menu = new QMenu(i18n("render_view.menu_bar.view", "Image Color Space"));

        for (int i = 0; i < config->getNumColorSpaces(); i++)
        {
            std::string csname = config->getColorSpaceNameByIndex(i);
            colorspace_values.push_back(csname.c_str());
            QAction* action = new QAction(csname.c_str(), this);
            action->setData(QVariant(i));
            action->setCheckable(true);
            action->setChecked(false);

            if (csname == current_color_space)
            {
                action->setChecked(true);
                m_current_input_colorspace = i;
                m_input_colorspace_widget->setText(action->text());
            }

            connect(action, SIGNAL(triggered()), this, SLOT(set_current_input_colorspace_slot()));
            m_input_colorspace_menu->addAction(action);
        }

        m_input_colorspace_widget->setMenu(m_input_colorspace_menu);
        m_display_view_menu = new QMenu(i18n("render_view.menu_bar.view", "Display View"));
        auto default_display = config->getDefaultDisplay();

        std::string current_display = m_prefs.default_display_view.toLocal8Bit().data();
        if (current_display == "")
        {
            current_display = config->getDefaultView(default_display);
            m_prefs.default_display_view = current_display.c_str();
        }

        bool is_current_display_in_config = false;
        for (int i = 0; i < config->getNumViews(default_display); i++)
        {
            if (config->getView(default_display, i) == current_display)
            {
                is_current_display_in_config = true;
                break;
            }
        }

        if (!is_current_display_in_config)
        {
            current_display = config->getDefaultView(default_display);
            m_prefs.default_display_view = current_display.c_str();
        }

        for (int i = 0; i < config->getNumViews(default_display); i++)
        {
            std::string view_name = config->getView(default_display, i);
            display_values.push_back(view_name.c_str());
            QAction* action = new QAction(view_name.c_str(), this);
            action->setData(QVariant(i));
            action->setCheckable(true);
            action->setChecked(false);

            if (view_name == current_display)
            {
                action->setChecked(true);
                m_current_display_view = i;
            }

            connect(action, SIGNAL(triggered()), this, SLOT(set_current_display_view_slot()));
            m_display_view_menu->addAction(action);
        }
    }
}

void RenderViewMainWindow::set_current_background_mode_slot()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action && m_glwidget)
    {
        m_glwidget->background_mode_idx = m_prefs.background_mode = action->data().toInt();
        auto actions = m_background_mode_menu->actions();
        for (auto action : actions)
        {
            if (action->data().toInt() == m_glwidget->background_mode_idx)
            {
                action->setChecked(true);
            }
            else
                action->setChecked(false);
        }
        m_glwidget->update();
    }
}

void RenderViewMainWindow::set_current_input_colorspace(int colorspace_index)
{
    auto config = OCIO::GetCurrentConfig();
    m_current_input_colorspace = colorspace_index;
    auto actions = m_input_colorspace_menu->actions();
    for (auto action : actions)
    {
        if (action->data().toInt() == m_current_input_colorspace)
        {
            action->setChecked(true);
            m_input_colorspace_widget->setText(action->text());
        }
        else
            action->setChecked(false);
    }
    m_glwidget->update_lut();
    m_glwidget->update();
}

void RenderViewMainWindow::set_current_display_view(int display_view_index)
{
    auto config = OCIO::GetCurrentConfig();
    m_current_display_view = display_view_index;
    auto actions = m_display_view_menu->actions();
    for (auto action : actions)
    {
        if (action->data().toInt() == m_current_display_view)
        {
            action->setChecked(true);
        }
        else
            action->setChecked(false);
    }
    m_glwidget->update_lut();
    m_glwidget->update();
}

void RenderViewMainWindow::set_current_input_colorspace_slot()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action)
    {
        set_current_input_colorspace(action->data().toInt());
    }
}

void RenderViewMainWindow::set_current_display_view_slot()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action)
    {
        set_current_display_view(action->data().toInt());
    }
}

void RenderViewMainWindow::init_background_mode()
{
    m_background_mode_menu = new QMenu(i18n("render_view.menu_bar.view", "Background"));

    for (int i = 0; i < RenderViewGLWidget::background_mode_names.size(); ++i)
    {
        QAction* action = new QAction(RenderViewGLWidget::background_mode_names[i], this);
        action->setData(QVariant(i));
        action->setCheckable(true);
        action->setChecked(false);
        if (i == m_prefs.background_mode)
        {
            action->setChecked(true);
            m_glwidget->background_mode_idx = i;
        }
        connect(action, SIGNAL(triggered()), this, SLOT(set_current_background_mode_slot()));
        m_background_mode_menu->addAction(action);
    }
}

#if OCIO_VERSION_HEX < 0x02000000
OCIO::ConstDisplayTransformRcPtr RenderViewMainWindow::get_color_transform()
{
    OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();

    int colorspace = current_input_colorspace();
    auto display = config->getDefaultDisplay();
    int view = current_display_view();

    OCIO::DisplayTransformRcPtr transform = OCIO::DisplayTransform::Create();
    transform->setInputColorSpaceName(config->getColorSpaceNameByIndex(colorspace));
    transform->setDisplay(display);
    transform->setView(config->getView(display, view));

    // exposure
    float gain = powf(2.0f, exposure());
    const float slope4f[] = { gain, gain, gain, 0.0 };
    float m44[16];
    float offset4[4];
    OCIO::MatrixTransform::Scale(m44, offset4, slope4f);
    OCIO::MatrixTransformRcPtr mtx = OCIO::MatrixTransform::Create();
    mtx->setValue(m44, offset4);
    transform->setLinearCC(mtx);

    // gamma
    float exponent = 1.0f / std::max(1e-6f, gamma());
    const float exponent4f[] = { exponent, exponent, exponent, 0.0 };
    OCIO::ExponentTransformRcPtr cc = OCIO::ExponentTransform::Create();
    cc->setValue(exponent4f);
    transform->setDisplayCC(cc);

    return transform;
}

#else
OCIO::LegacyViewingPipelineRcPtr RenderViewMainWindow::get_viewing_pipeline()
{
    OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();

    int colorspace = current_input_colorspace();
    auto display = config->getDefaultDisplay();
    int view = current_display_view();

    auto transform = OCIO::DisplayViewTransform::Create();
    transform->setSrc(config->getColorSpaceNameByIndex(colorspace));
    transform->setDisplay(display);
    transform->setView(config->getView(display, view));

    auto vpt = OCIO::LegacyViewingPipeline::Create();
    vpt->setDisplayViewTransform(transform);

    // exposure
    double gain = powf(2.0, static_cast<double>(exposure()));
    const double slope4d[] = { gain, gain, gain, 0.0 };
    double m44[16];
    double offset4[4];
    OCIO::MatrixTransform::Scale(m44, offset4, slope4d);
    OCIO::MatrixTransformRcPtr mtx = OCIO::MatrixTransform::Create();
    mtx->setMatrix(m44);
    mtx->setOffset(offset4);
    vpt->setLinearCC(mtx);

    // gamma
    double exponent = qBound(0.01, 1.0 / static_cast<double>(gamma()), 100.0);
    const double exponent4d[] = { exponent, exponent, exponent, exponent };
    OCIO::ExponentTransformRcPtr cc = OCIO::ExponentTransform::Create();
    cc->setValue(exponent4d);
    vpt->setDisplayCC(cc);

    return vpt;
}
#endif

int32_t RenderViewMainWindow::load_image(const char* image_path)
{
    uint32_t new_idx = 0;
    const bool status = m_image_cache->put_external(image_path, new_idx);

    if (status)
    {
        QTreeWidgetItem* item = new QTreeWidgetItem(m_catalog_widget);

        item->setData(0, Qt::UserRole, QVariant(new_idx));
        item->setIcon(0, QIcon(":icons/render_view/eye_icon.png"));
        item->setText(0, QFileInfo(image_path).fileName());
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        return new_idx;
    }
    return -1;
}

int32_t RenderViewMainWindow::create_image(int image_id, const ImageDesc& image_desc)
{
    OIIO::TypeDesc type_desc = OIIO::TypeDesc::FLOAT;

    switch (image_desc.image_type)
    {
    case RenderViewMainWindow::ImageDataType::Byte:
    {
        type_desc = OIIO::TypeDesc::INT8;
        break;
    }
    case RenderViewMainWindow::ImageDataType::UInt:
    {
        type_desc = OIIO::TypeDesc::UINT32;
        break;
    }
    case RenderViewMainWindow::ImageDataType::Int:
    {
        type_desc = OIIO::TypeDesc::INT32;
        break;
    }
    case RenderViewMainWindow::ImageDataType::Float:
    {
        type_desc = OIIO::TypeDesc::FLOAT;
        break;
    }
    case RenderViewMainWindow::ImageDataType::HalfFloat:
    {
        type_desc = OIIO::TypeDesc::HALF;
        break;
    }
    default:
        break;
    }

    OIIO::ImageSpec spec(image_desc.width, image_desc.height, image_desc.num_channels, type_desc);
    for (auto& it : image_desc.extra_attributes)
    {
        spec.attribute(it.first, it.second);
    }

    int32_t new_idx = -1;
    if (!m_image_cache->exist(image_id) || image_id == -1)
    {
        OIIO::ImageBuf* new_image = new OIIO::ImageBuf(image_desc.image_name, spec);
        uint32_t new_put_idx = 0;
        const bool put_status = m_image_cache->put(new_image, new_put_idx);
        if (put_status)
        {
            new_idx = new_put_idx;
            emit new_image_item_event(new_idx, image_desc.parent_image_id, QString::fromLocal8Bit(image_desc.image_name.c_str()));
            if (image_desc.parent_image_id == -1)
                emit current_image_changed(new_idx);
        }
    }
    else
    {
        OIIO::ImageBuf* new_image = m_image_cache->acquire_image(image_id);
        if (new_image)
        {
            m_image_cache->update_spec(image_id, spec);
            m_image_cache->release_image(image_id);
            new_idx = image_id;
        }
    }
    return new_idx;
}

void RenderViewMainWindow::delete_image()
{
    auto selected = m_catalog_widget->selectedItems();
    QTreeWidgetItem* next_selected_item = nullptr;
    if (selected.length() > 0)
    {
        auto delete_item_image = [&](QTreeWidgetItem* item) {
            const int image_id = item->data(0, Qt::UserRole).toInt();

            m_image_cache->delete_image(image_id);
            m_image_cache->release_image(image_id);

            if (m_toggle_background && image_id == m_background_image)
            {
                toggle_background(-1);
            }

            if (m_current_image == image_id)
            {
                m_current_image_buf = nullptr;
                m_current_image = -1;
                m_glwidget->update_image();
            }
        };

        // Select next selected_item
        bool all_selectid_is_aov = true;
        int max_top_level_index = -1;
        int min_top_level_index = m_catalog_widget->topLevelItemCount();

        QTreeWidgetItem* above_item = nullptr;
        for (auto* item : selected)
        {
            if (item->parent() != nullptr)
            {
                next_selected_item = item->parent();
            }
            else
            {
                all_selectid_is_aov = false;
                int top_level_index = m_catalog_widget->indexOfTopLevelItem(item);
                if (top_level_index >= 0)
                {
                    min_top_level_index = std::min(min_top_level_index, top_level_index);
                    max_top_level_index = std::max(max_top_level_index, top_level_index);
                }
            }
        }

        if (!all_selectid_is_aov)
        {
            if (max_top_level_index < m_catalog_widget->topLevelItemCount() - 1)
            {
                next_selected_item = m_catalog_widget->topLevelItem(max_top_level_index + 1);
            }
            else if (min_top_level_index > 0)
            {
                next_selected_item = m_catalog_widget->topLevelItem(min_top_level_index - 1);
            }
        }

        // delete items
        for (auto& item : selected)
        {
            std::deque<QTreeWidgetItem*> traverse_stack { item };

            while (!traverse_stack.empty())
            {
                QTreeWidgetItem* iter_item = traverse_stack.back();
                traverse_stack.pop_back();
                delete_item_image(iter_item);
                for (int i = iter_item->childCount() - 1; i >= 0; i--)
                {
                    traverse_stack.push_back(iter_item->child(i));
                }
            }
            delete item;
        }
    }

    if (next_selected_item)
    {
        m_catalog_widget->setCurrentItem(next_selected_item);
    }
}

void RenderViewMainWindow::update_image(int image_id, const ImageROI& region, std::vector<char>& bucket_data)
{
    RenderViewGLWidget::ROI gl_region;
    gl_region.xstart = region.xstart;
    gl_region.xend = region.xend;
    gl_region.ystart = region.ystart;
    gl_region.yend = region.yend;
    auto shared_buffer = std::make_shared<const std::vector<char>>(bucket_data);
    m_glwidget->update_image_region(image_id, gl_region, shared_buffer);
    RegionUploadTask* task = new RegionUploadTask(this, m_image_cache, image_id, region, shared_buffer);
    task->setAutoDelete(true);
    QThreadPool::globalInstance()->start(task);
}

void RenderViewMainWindow::set_current_image(unsigned int index, bool selection_change)
{
    m_image_cache->release_image(m_current_image);

    m_current_image_buf = m_image_cache->acquire_image(index);

    m_current_image = index;

    m_glwidget->update_image();

    m_glwidget->update();

    OIIO::ImageSpec spec;

    int nchannels = 0;
    if (m_image_cache->get_spec(get_current_image_id(), spec))
    {
        nchannels = spec.nchannels;
    }

    m_color_mode_label->setText(colormode_label_text(nchannels, m_color_mode, m_current_channel));
    update_titlebar();

    if (selection_change)
    {
        QTreeWidgetItemIterator it(m_catalog_widget);
        while (*it)
        {
            if ((*it)->data(0, Qt::UserRole).toInt() == index)
            {
                (*it)->setSelected(true);
            }
            else
            {
                (*it)->setSelected(false);
            }
            ++it;
        }
    }
    m_glwidget->update_pixel_info();

    send_crop_update(m_has_crop, m_region_min_x, m_region_max_x, m_region_min_y, m_region_max_y);
}

void RenderViewMainWindow::toggle_background(int image_id)
{
    if (m_toggle_background || image_id == -1)
    {
        m_image_cache->release_image(m_background_image);

        m_background_image_buf = nullptr;
        m_background_image = -1;
        m_toggle_background = false;
        toggle_background_act->setChecked(false);

        QTreeWidgetItemIterator it(m_catalog_widget);
        while (*it)
        {
            (*it)->setIcon(0, QIcon(":icons/render_view/eye_icon"));

            ++it;
        }
    }
    else
    {
        m_toggle_background = true;
        toggle_background_act->setChecked(true);
        m_background_image = image_id;
        m_background_image_buf = m_image_cache->acquire_image(image_id);
        m_glwidget->update_image();

        m_glwidget->update();

        QTreeWidgetItemIterator it(m_catalog_widget);
        while (*it)
        {
            if ((*it)->data(0, Qt::UserRole).toInt() == image_id)
            {
                (*it)->setIcon(0, QIcon(":icons/render_view/eye_icon_highlight"));
            }
            else
            {
                (*it)->setIcon(0, QIcon(":icons/render_view/eye_icon"));
            }
            ++it;
        }
    }
}

RenderViewMainWindow::~RenderViewMainWindow()
{
    if (m_image_cache)
    {
        delete m_image_cache;
        m_image_cache = nullptr;
    }

    m_listener_thread->requestInterruption();

    if (m_zmq_ctx)
    {
        zmq_ctx_destroy(m_zmq_ctx);
        m_zmq_ctx = nullptr;
    }
}

void RenderViewMainWindow::send_crop_update(bool display, const int region_min_x, const int region_max_x, const int region_min_y,
                                            const int region_max_y)
{
    const auto image = get_current_image();
    if (image == nullptr)
    {
        return;
    }

    ipc::Command command;
    command.name = "CropRender";

    const auto& spec = image->spec();

    const auto pid = spec.extra_attribs.find("opendcc/dcc/pid");
    if (pid != spec.extra_attribs.end())
    {
        command.args["destination_pid"] = pid->get_string();
    }

    if (m_has_crop = display)
    {
        m_region_min_x = clamp(0, region_min_x, spec.width - 1);
        m_region_min_y = clamp(0, region_min_y, spec.height - 1);
        m_region_max_x = clamp(0, region_max_x, spec.width - 1);
        m_region_max_y = clamp(0, region_max_y, spec.height - 1);

        if (m_region_min_x == m_region_max_x || m_region_min_y == m_region_max_y)
        {
            m_region_min_x = 0;
            m_region_min_y = 0;
            m_region_max_x = 0;
            m_region_max_y = 0;
        }

        command.args["min_x"] = std::to_string(m_region_min_x);
        command.args["max_x"] = std::to_string(m_region_max_x);
        command.args["min_y"] = std::to_string(m_region_min_y);
        command.args["max_y"] = std::to_string(m_region_max_y);
    }

    m_server->send_command(m_main_server_info, command);
}

void RenderViewMainWindow::render_again()
{
    ipc::Command command;
    command.name = "RenderAgain";
    m_server->send_command(m_main_server_info, command);
}

void RenderViewMainWindow::cancel_render()
{
    ipc::Command command;
    command.name = "CancelRender";
    m_server->send_command(m_main_server_info, command);
}

void RenderViewMainWindow::show_resolution_slot(bool value)
{
    m_prefs.show_resolution_guides = value;
    m_glwidget->show_resolution_guides = value;
    m_glwidget->update();
}

void RenderViewMainWindow::preferences_updated()
{
    if (QDir(m_prefs.scratch_image_location).exists())
    {
        m_image_cache->set_scratch_image_location(m_prefs.scratch_image_location.toStdString());
    }
    else
    {
        QMessageBox::warning(this, i18n("render_view.preferences_updated.message_box", "Warning"),
                             i18n("render_view.preferences_updated.message_box", "Scratch image directory ") + m_prefs.scratch_image_location +
                                 i18n("render_view.preferences_updated.message_box", " doesn't exist.\n") +
                                 QDir::tempPath().toLocal8Bit().constData() +
                                 i18n("render_view.preferences_updated.message_box", " is used instead."));
    }
    m_image_cache->set_max_size(m_prefs.image_cache_size);
    burn_in_mapping_on_save_act->setChecked(m_prefs.burn_in_mapping_on_save);
    write_settings();
}

void RenderViewMainWindow::show_preferences_window()
{
    m_prefs_window->update_pref_windows();
    m_prefs_window->show();
}

void RenderViewMainWindow::update_status_label()
{
    uint64_t allocated, disk;
    if (m_image_cache)
    {
        m_image_cache->used_memory(allocated, disk);
        if (m_status_label)
        {
            m_status_label->setText(i18n("render_view.status_label", "Mem: %1mb Disk: %2mb ").arg(allocated / 1024 / 1024).arg(disk / 1024 / 1024));
        }
    }
}

void RenderViewMainWindow::clear_scratch_images()
{
    namespace fs = std::filesystem;
    auto full_path = fs::u8path(m_prefs.scratch_image_location.toStdString());

    if (!fs::exists(full_path) || !fs::is_directory(full_path))
    {
        return;
    }

    fs::directory_iterator end_iter;
    for (fs::directory_iterator it(full_path); it != end_iter; ++it)
    {
        if (fs::is_directory(it->status()) || !fs::is_regular_file(it->status()))
        {
            continue;
        }
        std::string filename = it->path().filename().u8string();
        std::regex expr("render_view.(\\d+).(\\d+).tif");
        if (std::regex_match(filename, expr))
        {
            fs::remove(it->path());
        }
    }
}

class PyLock
{
public:
    PyLock() { m_state = PyGILState_Ensure(); }
    ~PyLock() { PyGILState_Release(m_state); }

private:
    PyGILState_STATE m_state;
};

void RenderViewMainWindow::setup_python_home()
{
    const auto application_dir = QCoreApplication::applicationDirPath();

    QDir dir(application_dir);
#ifdef OPENDCC_OS_MAC
    dir.cdUp();
    dir.cd("Resources");
#else
    dir.cdUp();
#endif

#ifdef OPENDCC_OS_WINDOWS
    static auto python_home_utf8 = dir.path().toStdString() + "/python/";
#else
    static auto python_home_utf8 = dir.path().toStdString();
#endif

#if PY_MAJOR_VERSION >= 3
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    static auto python_home_str = converter.from_bytes(python_home_utf8.c_str());
    Py_SetPythonHome(const_cast<wchar_t*>(python_home_str.c_str()));
#else
    Py_SetPythonHome(const_cast<char*>(python_home_utf8.c_str()));
#endif
}

void RenderViewMainWindow::init_python()
{
#ifdef OPENDCC_EMBEDDED_PYTHON_HOME
    setup_python_home();
#endif

    if (!Py_IsInitialized())
    {
        // We're here when this is a C++ program initializing python (i.e. this
        // is a case of "embedding" a python interpreter, as opposed to
        // "extending" python with extension modules).
        //
        // In this case we don't want python to change the sigint handler.  Save
        // it before calling Py_Initialize and restore it after.
#ifdef OPENDCC_OS_LINUX
        struct sigaction origSigintHandler;
        sigaction(SIGINT, NULL, &origSigintHandler);
#endif
        Py_Initialize();

#ifdef OPENDCC_OS_LINUX
        // Restore original sigint handler.
        sigaction(SIGINT, &origSigintHandler, NULL);
#endif
    }
}

void RenderViewMainWindow::init_python_ui() {}

OPENDCC_NAMESPACE_CLOSE
