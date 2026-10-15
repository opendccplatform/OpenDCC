/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/render_view/image_view/api.h"
#include "opendcc/base/app_config/config.h"
#include "opendcc/base/ipc_commands_api/server_info.h"
#include "opendcc/render_view/image_view/preferences_window.hpp"

#include <QSettings>
#include <QMainWindow>
#include <QCoreApplication>

#include <OpenImageIO/imagebuf.h>
#include <OpenColorIO/OpenColorIO.h>

#include <map>
#include <mutex>

class QLabel;
class QThread;
class QTreeWidgetItem;
class QTreeWidget;
class QPushButton;

OPENDCC_NAMESPACE_OPEN

namespace OCIO = OCIO_NAMESPACE;

class RenderViewGLWidget;
class PixelInfoColorRect;
class RenderViewGLWidgetTool;
class RenderViewInternalImageCache;

namespace base::ipc
{
    class CommandServer;
}

inline QString i18n(const char* context, const char* key, const char* disambiguation = nullptr, int n = -1)
{
    return QCoreApplication::translate(context, key, disambiguation, n);
}

class OPENDCC_RENDER_VIEW_API RenderViewMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    enum struct ColorMode
    {
        RGB,
        SingleChannel,
        Lumiance
    };

    enum struct ImageDataType
    {
        Byte,
        UInt,
        Int,
        Float,
        HalfFloat
    };

    struct ImageROI
    {
        int xstart;
        int xend;
        int ystart;
        int yend;
    };

    struct ImageDesc
    {
        std::string image_name;
        int parent_image_id;
        int width;
        int height;
        int num_channels;
        ImageDataType image_type;

        std::map<std::string, std::string> extra_attributes;
    };

    RenderViewMainWindow(RenderViewPreferences&& preferences,
                         const base::app_config::ApplicationConfig& config = base::app_config::ApplicationConfig());
    ~RenderViewMainWindow();

    static void set_instance(RenderViewMainWindow* instance) { m_global_instance = instance; }
    static RenderViewMainWindow* instance() { return m_global_instance; }
    void update_pixel_info();
    void update_titlebar();
    int32_t load_image(const char* image_path);
    int32_t create_image(int image_id, const ImageDesc& image_desc);

    OIIO::ImageBuf* get_current_image() { return m_current_image_buf; };

    OIIO::ImageBuf* get_background_image() { return m_background_image_buf; };

    uint32_t get_current_image_id() { return m_current_image; }
    void set_current_image(unsigned int index, bool selection_change = false);

    uint32_t get_background_image_id() { return m_background_image; }

    bool is_toggle_background_mode() { return m_toggle_background; }

    int current_channel() { return m_current_channel; }
    ColorMode current_color_mode() { return m_color_mode; }
    void view_channel(int channel, ColorMode colormode);

    int current_input_colorspace(void) const { return m_current_input_colorspace; };
    void set_current_input_colorspace(int colorspace_index);

    float gamma() const { return m_gamma; };
    float exposure() const { return m_exposure; };

    int current_display_view(void) const { return m_current_display_view; };
    void set_current_display_view(int display_view_index);

#if OCIO_VERSION_HEX < 0x02000000
    OCIO::ConstDisplayTransformRcPtr get_color_transform();
#else
    OCIO::LegacyViewingPipelineRcPtr get_viewing_pipeline();
#endif
    void create_menus(QMenu* fillMenu = nullptr);

    void update_image(int image_id, const ImageROI& region, std::vector<char>& bucket_data);

    RenderViewInternalImageCache* get_image_cache() { return m_image_cache; };

    std::map<QAction*, QKeySequence>& get_defaults_map();

    bool inline is_ocio_enabled() { return m_ocio_enabled; }
    void init_python();
    void init_python_ui();

    RenderViewPreferences& get_prefs();
    const RenderViewPreferences& get_prefs() const;

public Q_SLOTS:
    void send_crop_update(bool display, const int region_min_x, const int region_max_x, const int region_min_y, const int region_max_y);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
Q_SIGNALS:
    void current_image_changed(int);
    void current_image_pixels_changed();
    void new_image_item_event(int, int, const QString&);

private Q_SLOTS:
    void show_preferences_window();
    void activate_window_slot();
    void catalog_item_selection_change();
    void view_channel_full();
    void view_channel_red();
    void view_channel_green();
    void view_channel_blue();
    void view_channel_alpha();
    void view_channel_lumiance();
    void set_current_input_colorspace_slot();
    void set_current_display_view_slot();
    void set_current_background_mode_slot();
    void set_gamma_slot(double value);
    void set_exposure_slot(double value);
    void open_file();
    void export_file();
    void delete_image();
    void render_again();
    void cancel_render();
    void change_current_image(int index);
    void new_image_item(int image_id, int parent_id, const QString& name);
    void next_main_image();
    void prev_main_image();
    void toggle_window_always_on_top(bool checked);
    void burn_in_mapping_on_save_slot(bool checked);
    void resize_window_to_image_slot();
    void reset_zoom_pan_slot();
    void go_between_main_images(bool move_down);
    void show_resolution_slot(bool value);

    void next_image();
    void prev_image();

    void toggle_background_slot();

    void show_about_dialog();
    void preferences_updated();

    void update_timesago();

private:
    void init_ui();
    void init_tools();
    int current_catalog_top_level_index();
    void create_actions();
    void init_ocio(std::vector<QString>& colorspace_values, std::vector<QString>& display_values);
    void init_background_mode();
    void toggle_background(int image_id);
    void read_settings();
    void write_settings();
    void load_shortcuts();
    void update_status_label();
    QTreeWidgetItem* find_image_item(uint32_t image_id);
    void clear_scratch_images();
    void setup_python_home();

    int32_t m_current_image;

    int m_current_channel; ///< Channel we're viewing.
    ColorMode m_color_mode; ///< How to show the current channel(s).

    bool m_toggle_background;
    bool m_ocio_enabled;
    int m_background_image;

    int m_current_input_colorspace;
    int m_current_display_view;
    QString current_export_path;

    OIIO::ImageBuf* m_current_image_buf;
    OIIO::ImageBuf* m_background_image_buf;

    float m_gamma;
    float m_exposure;

    QAction *view_channel_full_act, *view_channel_red_act, *view_channel_green_act;
    QAction *view_channel_blue_act, *view_channel_alpha_act, *view_channel_luminance_act;

    QAction* delete_image_act;
    QAction *open_file_act, *export_file_act;

    QAction *prev_image_act, *next_image_act, *prev_main_image_act, *next_main_image_act;

    QAction *toggle_background_act, *toggle_windows_always_on_top_act;
    QAction *reset_zoom_pan_act, *resize_window_to_image_act, *lock_pixel_readout_act;

    QAction *render_again_act, *cancel_render_act;
    QAction* burn_in_mapping_on_save_act;
    QAction* show_resolution_guides_act;
    QAction* about_act;
    QAction* show_preferences_window_act;
    std::vector<RenderViewGLWidgetTool*> m_image_tools;

    QLabel* m_color_mode_label;
    QLabel* m_status_label;
    QLabel* m_pixelinfo;
    QPalette m_palette;
    QTreeWidget* m_catalog_widget;
    RenderViewGLWidget* m_glwidget;
    QPushButton* m_input_colorspace_widget;
    QMenu* m_input_colorspace_menu;
    QMenu* m_display_view_menu;
    QMenu* m_background_mode_menu;
    PixelInfoColorRect* m_pixel_info_rect;
    QThread* m_listener_thread;

    RenderViewInternalImageCache* m_image_cache;
    RenderViewPreferencesWindow* m_prefs_window;

    void* m_zmq_ctx;
    RenderViewPreferences m_prefs;
    std::map<QAction*, QKeySequence> m_defaults_map;
    QTimer* m_timesago_timer;
    base::app_config::ApplicationConfig m_app_config;

    std::unique_ptr<base::ipc::CommandServer> m_server;

    base::ipc::ServerInfo m_main_server_info;

    static RenderViewMainWindow* m_global_instance;

    bool m_has_crop = false;
    int m_region_min_x;
    int m_region_max_x;
    int m_region_min_y;
    int m_region_max_y;
};

OPENDCC_NAMESPACE_CLOSE
