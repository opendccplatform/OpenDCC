// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "app.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QLabel>
#include "gl_widget.h"

#include <QtCore/QDateTime>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragMoveEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QKeyEvent>
#include <QtCore/QUrl>
#include <QtCore/QFileInfo>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QTreeWidget>
#include <QAction>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QFileDialog>
#include <QtGui/QColor>
#include <QtWidgets/QMessageBox>
#include <QActionGroup>
#include <QtWidgets/QHeaderView>
#include <QMimeData>
#include <QtCore/QDebug>
#include "qt_utils.h"
#include "listener_thread.hpp"
#include "image_cache.h"
#include "gl_widget_tools.hpp"
#include "color_convert.hpp"
#include "metadata_view.h"
#include "stylesheet.h"

#include "opendcc/base/app_version.h"
#include <codecvt>

OPENDCC_NAMESPACE_OPEN

OIIO_NAMESPACE_USING;

static const char* s_file_filters = ""
                                    "Image Files (*.tif *.tiff *.tx "
                                    "*.jpg *.jpeg *.exr *.png "
                                    "*.tga "
                                    "*.hdr );;"
                                    "TIFF (*.tif *.tiff *.tx *.env);;"
                                    "HDR (*.hdr);;"
                                    "JPEG (*.jpg *.jpe *.jpeg *.jif *.jfif *.jfi);;"
                                    "OpenEXR (*.exr);;"
                                    "Portable Network Graphics (*.png);;"
                                    "Targa (*.tga *.tpic);;"
                                    "All Files (*)";

void RenderViewMainWindow::init_tools()
{
    RenderViewGLWidgetTool* tool = new GLWidgetPanZoomTool(m_glwidget);
    tool->init_action();
    m_image_tools.push_back(tool);
    tool->set_tool();

    tool = new GLWidgetCropRegionTool(m_glwidget);
    tool->init_action();

    connect(tool, SIGNAL(region_update(bool, int, int, int, int)), this, SLOT(send_crop_update(bool, int, int, int, int)));

    m_image_tools.push_back(tool);
}

void RenderViewMainWindow::init_ui()
{
    setWindowTitle(i18n("render_view", "Render View"));
    setStyleSheet(render_view_stylesheet);
    setAcceptDrops(true);

    m_glwidget = new RenderViewGLWidget(this, this);
    m_glwidget->setPalette(m_palette);
    resize(640, 480);
    setCentralWidget(m_glwidget);

    // after creating glwidget
    init_tools();

    QToolBar* image_tools_toolbar = new QToolBar(i18n("render_view.tools", "Image Tools"));
    image_tools_toolbar->setObjectName("Image Tools");
    QActionGroup* tool_group = new QActionGroup(this);
    for (int i = 0; i < m_image_tools.size(); i++)
    {
        QAction* action = m_image_tools[i]->tool_action();
        tool_group->addAction(action);
        image_tools_toolbar->addAction(action);
    }

    QDockWidget* dockWidget = new QDockWidget;
    dockWidget->setObjectName("Catalog");
    dockWidget->setWindowTitle(i18n("render_view.catalog", "Catalog"));
    addDockWidget(Qt::DockWidgetArea::LeftDockWidgetArea, dockWidget);
    dockWidget->hide();
    m_catalog_widget = new QTreeWidget;
    m_catalog_widget->setIconSize(QSize(20, 20));
    QStringList headers;
    headers << i18n("render_view.catalog", "name");
    headers << i18n("render_view.catalog", "timeago");
    m_catalog_widget->setHeaderLabels(headers);

    m_catalog_widget->header()->setStretchLastSection(false);
    m_catalog_widget->setHeaderHidden(true);
    m_catalog_widget->header()->setSectionResizeMode(0, QHeaderView::ResizeMode::Stretch);
    m_catalog_widget->header()->setSectionResizeMode(1, QHeaderView::ResizeMode::Fixed);
    m_catalog_widget->header()->resizeSection(1, 30);

    m_catalog_widget->setDragDropMode(QAbstractItemView::DragDropMode::InternalMove);
    m_catalog_widget->setSelectionMode(QAbstractItemView::SelectionMode::ExtendedSelection);
    m_catalog_widget->setAlternatingRowColors(true);
    dockWidget->setWidget(m_catalog_widget);
    QAction* doc_toggle = dockWidget->toggleViewAction();
    doc_toggle->setShortcut(QKeySequence("C"));

    this->connect(m_catalog_widget, SIGNAL(itemSelectionChanged()), SLOT(catalog_item_selection_change()));

    dockWidget = new QDockWidget;
    dockWidget->setObjectName("image_metadata");
    dockWidget->setWindowTitle(i18n("render_view.image_metadata", "Image Metadata"));
    addDockWidget(Qt::DockWidgetArea::RightDockWidgetArea, dockWidget);
    dockWidget->hide();
    auto metadata_view = new RenderViewMetadataView(this);
    dockWidget->setWidget(metadata_view);

    connect(m_catalog_widget, SIGNAL(itemSelectionChanged()), metadata_view, SLOT(update_metadata()));

    QToolBar* low_toolbar = new QToolBar(i18n("render_view.menu_bar.window", "Pixel Value"));
    low_toolbar->setObjectName("Pixel Value");
    low_toolbar->setStyleSheet("spacing:10px;");
    m_color_mode_label = new QLabel;
    m_color_mode_label->setText(colormode_label_text(0, m_color_mode, m_current_channel));
    m_status_label = new QLabel;

    m_pixelinfo = new QLabel;

    m_input_colorspace_widget = new QPushButton;
    m_input_colorspace_widget->setFlat(true);
    m_input_colorspace_widget->setStyleSheet("padding-top:0; padding-bottom:0");

    m_pixel_info_rect = new PixelInfoColorRect;

    low_toolbar->addWidget(m_color_mode_label);
    low_toolbar->addWidget(m_input_colorspace_widget);
    low_toolbar->addWidget(m_pixelinfo);
    low_toolbar->addWidget(m_pixel_info_rect);
    QWidget* spacer_widget = new QWidget;
    spacer_widget->setLayout(new QHBoxLayout);
    spacer_widget->layout()->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding));
    low_toolbar->addWidget(spacer_widget);
    low_toolbar->addWidget(m_status_label);
    addToolBar(Qt::ToolBarArea::BottomToolBarArea, low_toolbar);

    QToolBar* upper_toolbar = new QToolBar(i18n("render_view.menu_bar.window", "Monitor Controls"));
    upper_toolbar->setObjectName("Monitor Controls");
    upper_toolbar->setStyleSheet("spacing:5px;");

    const QString gamma_tooltip = i18n("render_view.tool_bar", "Gamma");
    QLabel* gamma_label = new QLabel;
    gamma_label->setScaledContents(true);
    gamma_label->setFixedSize(QSize(24, 24));
    gamma_label->setToolTip(gamma_tooltip);
    gamma_label->setPixmap(QPixmap(":/icons/render_view/gamma"));
    DoubleSpinBox* gamma_spinbox = new DoubleSpinBox;
    gamma_spinbox->setToolTip(gamma_tooltip);
    gamma_spinbox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    gamma_spinbox->setFixedWidth(70);
    gamma_spinbox->setLocale(QLocale(QLocale::Hawaiian, QLocale::UnitedStates));
    gamma_spinbox->setValue(1.0);
    gamma_spinbox->setMinimum(0.0);
    gamma_spinbox->setMaximum(10.0);
    DoubleSlider* gamma_slider = new DoubleSlider(Qt::Orientation::Horizontal);
    gamma_slider->setToolTip(gamma_tooltip);
    int gamma_num_steps = 10 / 0.01f;
    gamma_slider->setMinimum(0);
    gamma_slider->setValue(1.0 / 0.01f);
    gamma_slider->setMaximum(gamma_num_steps);
    gamma_slider->setMaximumWidth(250);
    connect(gamma_slider, SIGNAL(valueChangedDouble(double)), gamma_spinbox, SLOT(setValueSilent(double)));
    connect(gamma_spinbox, SIGNAL(valueChanged(double)), gamma_slider, SLOT(setValueSilent(double)));
    connect(gamma_spinbox, SIGNAL(valueChanged(double)), this, SLOT(set_gamma_slot(double)));
    connect(gamma_slider, SIGNAL(valueChangedDouble(double)), this, SLOT(set_gamma_slot(double)));

    const QString exposure_tooltip = i18n("render_view.tool_bar", "Exposure");
    QLabel* exposure_label = new QLabel;
    exposure_label->setScaledContents(true);
    exposure_label->setFixedSize(QSize(24, 24));
    exposure_label->setToolTip(exposure_tooltip);
    exposure_label->setPixmap(QPixmap(":/icons/render_view/exposure"));
    DoubleSpinBox* exposure_spinbox = new DoubleSpinBox;
    exposure_spinbox->setToolTip(exposure_tooltip);
    exposure_spinbox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    exposure_spinbox->setFixedWidth(70);
    exposure_spinbox->setLocale(QLocale(QLocale::Hawaiian, QLocale::UnitedStates));
    exposure_spinbox->setValue(0.0);
    exposure_spinbox->setMinimum(-10.0);
    exposure_spinbox->setMaximum(10.0);
    auto exposure_slider = new DoubleSlider(Qt::Orientation::Horizontal);
    exposure_slider->setToolTip(exposure_tooltip);
    int exposure_num_steps = 10 / 0.01f;
    exposure_slider->setMinimum(-exposure_num_steps);
    exposure_slider->setMaximum(exposure_num_steps);
    exposure_slider->setMaximumWidth(250);
    connect(exposure_slider, SIGNAL(valueChangedDouble(double)), exposure_spinbox, SLOT(setValueSilent(double)));
    connect(exposure_spinbox, SIGNAL(valueChanged(double)), exposure_slider, SLOT(setValueSilent(double)));
    connect(exposure_spinbox, SIGNAL(valueChanged(double)), this, SLOT(set_exposure_slot(double)));
    connect(exposure_slider, SIGNAL(valueChangedDouble(double)), this, SLOT(set_exposure_slot(double)));

    upper_toolbar->addWidget(gamma_label);
    upper_toolbar->addWidget(gamma_spinbox);
    upper_toolbar->addWidget(gamma_slider);

    upper_toolbar->addWidget(exposure_label);
    upper_toolbar->addWidget(exposure_spinbox);
    upper_toolbar->addWidget(exposure_slider);

    addToolBar(Qt::ToolBarArea::TopToolBarArea, image_tools_toolbar);
    addToolBar(Qt::ToolBarArea::TopToolBarArea, upper_toolbar);
}

void RenderViewMainWindow::create_actions()
{
    // File Menu
    open_file_act = new QAction(i18n("render_view.menu_bar.file", "&Open File..."), this);
    open_file_act->setObjectName("open_file");
    open_file_act->setShortcut(i18n("render_view.menu_bar.file.shortcut", "Ctrl+O"));
    m_defaults_map[open_file_act] = open_file_act->shortcut();
    connect(open_file_act, SIGNAL(triggered()), this, SLOT(open_file()));

    export_file_act = new QAction(i18n("render_view.menu_bar.file", "Export File..."), this);
    export_file_act->setObjectName("export_file");
    export_file_act->setShortcut(i18n("render_view.menu_bar.file.shortcut", "Ctrl+S"));
    m_defaults_map[export_file_act] = export_file_act->shortcut();
    connect(export_file_act, SIGNAL(triggered()), this, SLOT(export_file()));

    // Channel Menu
    view_channel_full_act = new QAction(i18n("render_view.menu_bar.view.channels", "Full Color"), this);
    view_channel_full_act->setObjectName("full_color");
    view_channel_full_act->setShortcut(i18n("render_view.menu_bar.view.channels.shortcut", "A"));
    view_channel_full_act->setCheckable(true);
    view_channel_full_act->setChecked(true);
    m_defaults_map[view_channel_full_act] = view_channel_full_act->shortcut();
    connect(view_channel_full_act, SIGNAL(triggered()), this, SLOT(view_channel_full()));

    view_channel_red_act = new QAction(i18n("render_view.menu_bar.view.channels", "Red"), this);
    view_channel_red_act->setObjectName("red");
    view_channel_red_act->setShortcut(i18n("render_view.menu_bar.view.channels.shortcut", "S"));
    view_channel_red_act->setCheckable(true);
    m_defaults_map[view_channel_red_act] = view_channel_red_act->shortcut();
    connect(view_channel_red_act, SIGNAL(triggered()), this, SLOT(view_channel_red()));

    view_channel_green_act = new QAction(i18n("render_view.menu_bar.view.channels", "Green"), this);
    view_channel_green_act->setObjectName("green");
    view_channel_green_act->setShortcut(i18n("render_view.menu_bar.view.channels.shortcut", "D"));
    view_channel_green_act->setCheckable(true);
    m_defaults_map[view_channel_green_act] = view_channel_green_act->shortcut();
    connect(view_channel_green_act, SIGNAL(triggered()), this, SLOT(view_channel_green()));

    view_channel_blue_act = new QAction(i18n("render_view.menu_bar.view.channels", "Blue"), this);
    view_channel_blue_act->setObjectName("blue");
    view_channel_blue_act->setShortcut(i18n("render_view.menu_bar.view.channels.shortcut", "F"));
    view_channel_blue_act->setCheckable(true);
    m_defaults_map[view_channel_blue_act] = view_channel_blue_act->shortcut();
    connect(view_channel_blue_act, SIGNAL(triggered()), this, SLOT(view_channel_blue()));

    view_channel_alpha_act = new QAction(i18n("render_view.menu_bar.view.channels", "Alpha"), this);
    view_channel_alpha_act->setObjectName("alpha");
    view_channel_alpha_act->setShortcut(i18n("render_view.menu_bar.view.channels.shortcut", "G"));
    view_channel_alpha_act->setCheckable(true);
    m_defaults_map[view_channel_alpha_act] = view_channel_alpha_act->shortcut();
    connect(view_channel_alpha_act, SIGNAL(triggered()), this, SLOT(view_channel_alpha()));

    view_channel_luminance_act = new QAction(i18n("render_view.menu_bar.view.channels", "Lumiance"), this);
    view_channel_luminance_act->setObjectName("lumiance");
    view_channel_luminance_act->setShortcut(i18n("render_view.menu_bar.view.channels.shortcut", "H"));
    view_channel_luminance_act->setCheckable(true);
    m_defaults_map[view_channel_luminance_act] = view_channel_luminance_act->shortcut();
    connect(view_channel_luminance_act, SIGNAL(triggered()), this, SLOT(view_channel_lumiance()));

    lock_pixel_readout_act = new QAction(i18n("render_view.menu_bar.view", "Lock Pixel Readout"), this);
    lock_pixel_readout_act->setObjectName("lock_pixel_readout");
    lock_pixel_readout_act->setShortcut(i18n("render_view.menu_bar.view.shortcut", "M"));
    m_defaults_map[lock_pixel_readout_act] = lock_pixel_readout_act->shortcut();
    connect(lock_pixel_readout_act, SIGNAL(triggered()), m_glwidget, SLOT(lock_pixel_readout()));

    // Image Menu
    render_again_act = new QAction(i18n("render_view.menu_bar.image", "Render Again"), this);
    render_again_act->setObjectName("render_again");
    render_again_act->setShortcut(Qt::Key_R);
    m_defaults_map[render_again_act] = render_again_act->shortcut();
    connect(render_again_act, SIGNAL(triggered()), this, SLOT(render_again()));

    cancel_render_act = new QAction(i18n("render_view.menu_bar.image", "Cancel Render"), this);
    cancel_render_act->setObjectName("cancel_render");
    cancel_render_act->setShortcut(Qt::Key_Escape);
    m_defaults_map[cancel_render_act] = cancel_render_act->shortcut();
    connect(cancel_render_act, SIGNAL(triggered()), this, SLOT(cancel_render()));

    // Image Menu
    delete_image_act = new QAction(i18n("render_view.menu_bar.image", "Delete Image"), this);
    delete_image_act->setObjectName("delete_image");
    delete_image_act->setShortcut(Qt::Key_Backspace);
    m_defaults_map[delete_image_act] = delete_image_act->shortcut();
    connect(delete_image_act, SIGNAL(triggered()), this, SLOT(delete_image()));

    next_image_act = new QAction(i18n("render_view.menu_bar.catalog", "Next Image"), this);
    next_image_act->setObjectName("next_image");
    next_image_act->setShortcut(Qt::Key_Down);
    m_defaults_map[next_image_act] = next_image_act->shortcut();
    connect(next_image_act, SIGNAL(triggered()), this, SLOT(next_image()));

    prev_image_act = new QAction(i18n("render_view.menu_bar.catalog", "Prev Image"), this);
    prev_image_act->setObjectName("prev_image");
    prev_image_act->setShortcut(Qt::Key_Up);
    m_defaults_map[prev_image_act] = prev_image_act->shortcut();
    connect(prev_image_act, SIGNAL(triggered()), this, SLOT(prev_image()));

    next_main_image_act = new QAction(i18n("render_view.menu_bar.catalog", "Next Main Image"), this);
    next_main_image_act->setObjectName("next_main_image");
    next_main_image_act->setShortcut(Qt::Key_PageDown);
    m_defaults_map[next_main_image_act] = next_main_image_act->shortcut();
    connect(next_main_image_act, SIGNAL(triggered()), this, SLOT(next_main_image()));

    prev_main_image_act = new QAction(i18n("render_view.menu_bar.catalog", "Prev Main Image"), this);
    prev_main_image_act->setObjectName("prev_main_image");
    prev_main_image_act->setShortcut(Qt::Key_PageUp);
    m_defaults_map[prev_main_image_act] = prev_main_image_act->shortcut();
    connect(prev_main_image_act, SIGNAL(triggered()), this, SLOT(prev_main_image()));

    toggle_background_act = new QAction(i18n("render_view.menu_bar.image", "Toggle Background"), this);
    toggle_background_act->setObjectName("toggle_background");
    toggle_background_act->setShortcut(QKeySequence());
    m_defaults_map[toggle_background_act] = toggle_background_act->shortcut();
    toggle_background_act->setCheckable(true);
    toggle_background_act->setChecked(false);
    connect(toggle_background_act, SIGNAL(triggered()), this, SLOT(toggle_background_slot()));

    burn_in_mapping_on_save_act = new QAction(i18n("render_view.menu_bar.catalog", "Burn In Mapping On Save"), this);
    burn_in_mapping_on_save_act->setCheckable(true);
    connect(burn_in_mapping_on_save_act, SIGNAL(triggered(bool)), this, SLOT(burn_in_mapping_on_save_slot(bool)));

    // View menu
    show_resolution_guides_act = new QAction(i18n("render_view.menu_bar.view", "Show Resolution Guides"), this);
    show_resolution_guides_act->setCheckable(true);
    show_resolution_guides_act->setChecked(m_prefs.show_resolution_guides);
    m_glwidget->show_resolution_guides = m_prefs.show_resolution_guides;
    connect(show_resolution_guides_act, SIGNAL(triggered(bool)), this, SLOT(show_resolution_slot(bool)));

    // Windows Menu
    toggle_windows_always_on_top_act = new QAction(i18n("render_view.menu_bar.window", "Window Always On Top"), this);
    toggle_windows_always_on_top_act->setObjectName("window_always_on_top");
    toggle_windows_always_on_top_act->setCheckable(true);
    toggle_windows_always_on_top_act->setShortcut(Qt::Key_T);
    m_defaults_map[toggle_windows_always_on_top_act] = toggle_windows_always_on_top_act->shortcut();
    connect(toggle_windows_always_on_top_act, SIGNAL(triggered(bool)), this, SLOT(toggle_window_always_on_top(bool)));

    resize_window_to_image_act = new QAction(i18n("render_view.menu_bar.view", "Resize Window To Image"), this);
    resize_window_to_image_act->setObjectName("resize_window_to_image");
    resize_window_to_image_act->setShortcut(i18n("render_view.menu_bar.view.shortcut", "Ctrl+F"));
    m_defaults_map[resize_window_to_image_act] = resize_window_to_image_act->shortcut();
    connect(resize_window_to_image_act, SIGNAL(triggered()), this, SLOT(resize_window_to_image_slot()));

    reset_zoom_pan_act = new QAction(i18n("render_view.menu_bar.view", "Reset Zoom/Pan"), this);
    reset_zoom_pan_act->setObjectName("reset_zoom_pan");
    reset_zoom_pan_act->setShortcut(Qt::Key_Home);
    m_defaults_map[reset_zoom_pan_act] = reset_zoom_pan_act->shortcut();
    connect(reset_zoom_pan_act, SIGNAL(triggered()), this, SLOT(reset_zoom_pan_slot()));

    show_preferences_window_act = new QAction(i18n("render_view.menu_bar.window", "Preferences"), this);
    connect(show_preferences_window_act, SIGNAL(triggered()), this, SLOT(show_preferences_window()));

    // Help menu
    about_act = new QAction(i18n("render_view.menu_bar.help", "About..."), this);
    connect(about_act, SIGNAL(triggered()), this, SLOT(show_about_dialog()));
}

void RenderViewMainWindow::create_menus(QMenu* fillMenu)
{
    QMenu* file_menu = new QMenu(i18n("render_view.menu_bar", "&File"), this);
    file_menu->addAction(open_file_act);
    file_menu->addAction(export_file_act);
    file_menu->addSeparator();
    file_menu->addAction(i18n("render_view.menu_bar", "Exit"), QApplication::instance(), SLOT(quit()),
                         i18n("render_view.menu_bar.shortcut", "Ctrl+Q"));

    QMenu* catalog_menu = new QMenu(i18n("render_view.menu_bar", "&Catalog"), this);
    catalog_menu->addAction(next_image_act);
    catalog_menu->addAction(prev_image_act);
    catalog_menu->addAction(next_main_image_act);
    catalog_menu->addAction(prev_main_image_act);
    catalog_menu->addAction(burn_in_mapping_on_save_act);

    QMenu* image_menu = new QMenu(i18n("render_view.menu_bar", "&Image"), this);
    image_menu->addAction(render_again_act);
    image_menu->addAction(cancel_render_act);
    image_menu->addAction(toggle_background_act);
    image_menu->addAction(delete_image_act);

    QMenu* channel_menu = new QMenu(i18n("render_view.menu_bar", "Channels"));
    channel_menu->addAction(view_channel_full_act);
    channel_menu->addAction(view_channel_red_act);
    channel_menu->addAction(view_channel_green_act);
    channel_menu->addAction(view_channel_blue_act);
    channel_menu->addAction(view_channel_alpha_act);
    channel_menu->addAction(view_channel_luminance_act);

    QMenu* view_menu = new QMenu(i18n("render_view.menu_bar", "&View"), this);
    view_menu->addAction(reset_zoom_pan_act);
    view_menu->addAction(resize_window_to_image_act);
    view_menu->addSeparator();
    view_menu->addAction(lock_pixel_readout_act);
    view_menu->addSeparator();
    view_menu->addMenu(channel_menu);
    view_menu->addMenu(m_background_mode_menu);
    view_menu->addAction(show_resolution_guides_act);
    view_menu->addSeparator();
    view_menu->addMenu(m_input_colorspace_menu);
    view_menu->addMenu(m_display_view_menu);

    QMenu* windows_menu = createPopupMenu();
    windows_menu->setTitle(i18n("render_view.menu_bar", "Window"));

    windows_menu->addSeparator();
    windows_menu->addAction(toggle_windows_always_on_top_act);
    windows_menu->addSeparator();
    windows_menu->addAction(show_preferences_window_act);

    QMenu* help_menu = new QMenu(i18n("render_view.menu_bar", "Help"), this);

    help_menu->addAction(about_act);

    if (fillMenu == nullptr)
    {
        auto menu_bar = menuBar();
        menu_bar->addMenu(file_menu);
        menu_bar->addMenu(catalog_menu);
        menu_bar->addMenu(image_menu);
        menu_bar->addMenu(view_menu);
        menu_bar->addMenu(windows_menu);
        menu_bar->addMenu(help_menu);
    }
    else
    {
        fillMenu->addMenu(file_menu);
        fillMenu->addMenu(catalog_menu);
        fillMenu->addMenu(image_menu);
        fillMenu->addMenu(view_menu);
        fillMenu->addMenu(windows_menu);
        fillMenu->addMenu(help_menu);
    }
}

void RenderViewMainWindow::read_settings()
{
    restoreGeometry(m_prefs.settings->value("main/geometry", saveGeometry()).toByteArray());
    restoreState(m_prefs.settings->value("main/windowState", saveState()).toByteArray());
    current_export_path = m_prefs.settings->value("main/current_export_path", QDir::currentPath()).toString();
    toggle_windows_always_on_top_act->setChecked(m_prefs.settings->value("main/toggle_windows_always_on_top", false).toBool());
    if (toggle_windows_always_on_top_act->isChecked()) // other variants do not work
    {
        toggle_window_always_on_top(true);
    }
}

void RenderViewMainWindow::write_settings()
{
    for (auto map_item : m_defaults_map)
    {
        if (map_item.first->shortcut() != map_item.second)
        {
            m_prefs.settings->setValue("shortcuts/" + map_item.first->objectName(), map_item.first->shortcut().toString());
        }
        else
        {
            m_prefs.settings->remove("shortcuts/" + map_item.first->objectName());
        }
    }
    m_prefs.settings->setValue("main/geometry", saveGeometry());
    m_prefs.settings->setValue("main/windowState", saveState());
    m_prefs.settings->setValue("main/current_export_path", current_export_path);
    m_prefs.settings->setValue("main/toggle_windows_always_on_top", toggle_windows_always_on_top_act->isChecked());
    m_prefs.save();
}

void RenderViewMainWindow::closeEvent(QCloseEvent* event)
{
    write_settings();
    QMainWindow::closeEvent(event);
    QApplication::quit();
}

void RenderViewMainWindow::open_file()
{
    QString openPath = QDir::currentPath();
    QStringList result = QFileDialog::getOpenFileNames(this, i18n("render_view.open", "Open Files"), openPath, s_file_filters);
    for (auto filepath : result)
    {
        int32_t image_id = load_image(filepath.toLocal8Bit().data());
        if (image_id != -1)
            set_current_image(image_id);
    }
}

void RenderViewMainWindow::export_file()
{
    auto selected = m_catalog_widget->selectedItems();
    if (selected.length() > 0)
    {
        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();
#if OCIO_VERSION_HEX < 0x02000000
        auto transform = get_color_transform();
        auto processor = ColorProcessor_OCIO(config->getProcessor(transform));
#else
        auto vpt = get_viewing_pipeline();
        auto processor = ColorProcessor_OCIO(vpt->getProcessor(config));
#endif
        if (selected.length() == 1)
        {
            // Build full dialog so we can fill filename with selected item
            QFileDialog diag(this);
            diag.setFileMode(QFileDialog::AnyFile);
            diag.setAcceptMode(QFileDialog::AcceptSave);
            diag.setDirectory(current_export_path);

            QString default_file_name = selected[0]->text(0);

            if (!default_file_name.contains("."))
            {
                default_file_name = default_file_name + ".exr";
            }
            diag.selectFile(default_file_name);
            int ret = diag.exec();

            if (ret == QFileDialog::DialogCode::Rejected)
            {
                return;
            }

            QString result = diag.selectedFiles()[0];

            current_export_path = QFileInfo(result).absolutePath();

            const uint32_t image_id = selected[0]->data(0, Qt::UserRole).toInt();
            auto image = m_image_cache->acquire_image(image_id);
            if (image)
            {
                if (burn_in_mapping_on_save_act->isChecked())
                {
                    OIIO::ImageBuf* temp_buff = new OIIO::ImageBuf;
                    temp_buff->copy(*image);
                    OCIO_apply(*temp_buff, *temp_buff, &processor, image->roi_full(), 2);
                    temp_buff->write(result.toLocal8Bit().data());
                    delete temp_buff;
                }
                else
                {
                    image->write(result.toLocal8Bit().data());
                }
                m_image_cache->release_image(image_id);
            }
        }
        else if (selected.length() > 1)
        {
            QString dir_name = QFileDialog::getExistingDirectory(this, i18n("render_view.export", "Export Directory"), current_export_path) + "/";
            QString file_extension = ".exr";
            if (dir_name.isEmpty())
            {
                return;
            }
            current_export_path = dir_name;
            QSet<QTreeWidgetItem*> parent_set;
            for (auto* item : selected)
            {
                if (item->parent() == nullptr)
                {
                    parent_set.insert(item);
                }
                else
                {
                    parent_set.insert(item->parent());
                }
            }
            bool is_one_beauty = (parent_set.size() == 1);

            for (auto* item : selected)
            {
                const int image_id = item->data(0, Qt::UserRole).toInt();
                auto image = m_image_cache->acquire_image(image_id);
                if (!image)
                {
                    continue;
                }

                QString current_dir_name = dir_name;

                if (!is_one_beauty)
                {
                    if (item->parent() == nullptr)
                    {
                        current_dir_name +=
                            item->text(0) + "_" + QString("%1").arg(m_catalog_widget->indexOfTopLevelItem(item), 2, 10, QChar('0')) + "/";
                    }
                    else
                    {
                        current_dir_name += item->parent()->text(0) + "_" +
                                            QString("%1").arg(m_catalog_widget->indexOfTopLevelItem(item->parent()), 2, 10, QChar('0')) + "/";
                    }
                }

                QDir().mkdir(current_dir_name);

                QString full_name = current_dir_name + item->text(0) + file_extension;

                if (burn_in_mapping_on_save_act->isChecked())
                {
                    OIIO::ImageBuf* temp_buff = new OIIO::ImageBuf;
                    temp_buff->copy(*image);
                    OCIO_apply(*temp_buff, *temp_buff, &processor, image->roi_full(), 2);
                    temp_buff->write(full_name.toLocal8Bit().data());
                    delete temp_buff;
                }
                else
                {
                    image->write(full_name.toLocal8Bit().data());
                }
                m_image_cache->release_image(image_id);
            }
        }
    }
    else
    {
        QMessageBox::warning(this, i18n("render_view.export.error.title", "Export File"),
                             i18n("render_view.export.error.text", "Nothing is selected!"));
    }
}

void RenderViewMainWindow::set_gamma_slot(double value)
{
    m_gamma = value;
    m_glwidget->update_lut();
    m_glwidget->update();
}

void RenderViewMainWindow::set_exposure_slot(double value)
{
    m_exposure = value;
    m_glwidget->update_lut();
    m_glwidget->update();
}

void RenderViewMainWindow::change_current_image(int index)
{
    set_current_image(index, true);
}

void RenderViewMainWindow::burn_in_mapping_on_save_slot(bool checked)
{
    m_prefs.burn_in_mapping_on_save = checked;
    m_prefs_window->update_pref_windows();
}

void RenderViewMainWindow::toggle_window_always_on_top(bool checked)
{
    if (checked)
    {
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    }
    else
    {
        setWindowFlags(windowFlags() ^ Qt::WindowStaysOnTopHint);
    }

    show();
}

void RenderViewMainWindow::resize_window_to_image_slot()
{
    const uint32_t image_id = get_current_image_id();
    OIIO::ImageBuf* image = nullptr;
    if (m_image_cache->exist(image_id))
    {
        image = m_image_cache->acquire_image(image_id);
        if (!image)
        {
            return;
        }
    }
    else
    {
        return;
    }
    OIIO::ImageSpec image_spec = image->spec();

    int new_window_width = this->width() - m_glwidget->width() + round((float)image_spec.full_width * m_glwidget->m_zoom);
    int new_window_height = this->height() - m_glwidget->height() + round((float)image_spec.full_height * m_glwidget->m_zoom);
    this->resize(new_window_width, new_window_height);
    m_glwidget->m_centerx = m_glwidget->width() * (1 - m_glwidget->m_zoom) / 2 / m_glwidget->m_zoom;
    m_glwidget->m_centery = m_glwidget->height() * (1 - m_glwidget->m_zoom) / 2 / m_glwidget->m_zoom;
    ;
    m_glwidget->update();
}

void RenderViewMainWindow::reset_zoom_pan_slot()
{
    m_glwidget->m_centerx = 0;
    m_glwidget->m_centery = 0;
    m_glwidget->m_zoom = 1;
    m_glwidget->update();
}

void RenderViewMainWindow::catalog_item_selection_change()
{
    auto selected = m_catalog_widget->selectedItems();
    if (selected.length() > 0)
    {
        int index = selected[0]->data(0, Qt::UserRole).toInt();
        set_current_image(index, false);
    }
}

void RenderViewMainWindow::update_pixel_info()
{
    const uint32_t image_id = get_current_image_id();

    if (m_image_cache->exist(image_id))
    {
        QString s;
        s = QString::asprintf("x= %d y= %d   %0.5f %0.5f %0.5f %0.5f", m_glwidget->m_mouse_image_x, m_glwidget->m_mouse_image_y,
                  m_glwidget->m_mouse_image_color[0], m_glwidget->m_mouse_image_color[1], m_glwidget->m_mouse_image_color[2],
                  m_glwidget->m_mouse_image_color[3]);
        m_pixelinfo->setText(s);
        float pixel[4] = { m_glwidget->m_mouse_image_color[0], m_glwidget->m_mouse_image_color[1], m_glwidget->m_mouse_image_color[2],
                           m_glwidget->m_mouse_image_color[3] };

        OCIO::ConstConfigRcPtr config = OCIO::GetCurrentConfig();

#if OCIO_VERSION_HEX < 0x02000000
        OCIO::ConstProcessorRcPtr processor = config->getProcessor(get_color_transform());
        processor->applyRGBA(pixel);
#else
        OCIO::ConstProcessorRcPtr processor = get_viewing_pipeline()->getProcessor(config);
        processor->getDefaultCPUProcessor()->applyRGBA(pixel);
#endif
        if (m_color_mode == ColorMode::SingleChannel)
            switch (m_current_channel)
            {
            case 0:
                pixel[1] = pixel[0];
                pixel[2] = pixel[0];
                break;
            case 1:
                pixel[0] = pixel[1];
                pixel[2] = pixel[1];
                break;
            case 2:
                pixel[0] = pixel[2];
                pixel[1] = pixel[2];
                break;
            default:
                break;
            }
        m_pixel_info_rect->setColor(pixel[0], pixel[1], pixel[2]);
    }
}

void RenderViewMainWindow::update_titlebar()
{
    QString s = i18n("render_view.title", "Render View ");

    const uint32_t image_id = get_current_image_id();
    OIIO::ImageBuf* image = nullptr;
    if (m_image_cache->exist(image_id))
    {
        image = m_image_cache->acquire_image(image_id);
        if (image)
        {
            QString imageName = QFileInfo(image->name().c_str()).fileName();
            s.append(imageName);
            s.append(" ");

            QString imageSize;
            auto spec(image->spec());
            imageSize = QString::asprintf("%dx%d", spec.full_width, spec.full_height);
            s.append(imageSize);
            s.append(" ");
            m_image_cache->release_image(image_id);
        }
    }

    QString zoom;
    zoom = QString::asprintf("%0.1f%%", m_glwidget->m_zoom * 100);
    s.append(zoom);

    setWindowTitle(s);
}

void RenderViewMainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->accept();
    else
        event->ignore();
}

void RenderViewMainWindow::dropEvent(QDropEvent* event)
{
    QList<QUrl> urls = event->mimeData()->urls();

    if (urls.length() > 1)
    {
        int idx = 0;
        for (auto url : urls)
        {
            QString filepath = url.toLocalFile();
            idx = load_image(filepath.toLocal8Bit().data());
            if (idx == -1)
                break;
        }
        if (idx != -1)
            set_current_image(idx, true);
    }
    else
    {
        QString filepath = urls[0].toLocalFile();
        int idx = load_image(filepath.toLocal8Bit().data());
        if (idx != -1)
            set_current_image(idx, true);
    }
}

void RenderViewMainWindow::view_channel(int channel, ColorMode colormode)
{
    if (m_current_channel != channel || colormode != m_color_mode)
    {
        m_current_channel = channel;
        m_color_mode = colormode;
        m_glwidget->update();

        view_channel_full_act->setChecked(channel == 0 && m_color_mode == ColorMode::RGB);
        view_channel_red_act->setChecked(channel == 0 && m_color_mode == ColorMode::SingleChannel);
        view_channel_green_act->setChecked(channel == 1 && m_color_mode == ColorMode::SingleChannel);
        view_channel_blue_act->setChecked(channel == 2 && m_color_mode == ColorMode::SingleChannel);
        view_channel_alpha_act->setChecked(channel == 3 && m_color_mode == ColorMode::SingleChannel);
        view_channel_luminance_act->setChecked(channel == 0 && m_color_mode == ColorMode::Lumiance);

        const uint32_t image_id = get_current_image_id();
        OIIO::ImageSpec image_spec;
        int nchannels = 0;
        if (m_image_cache->get_spec(image_id, image_spec))
            nchannels = image_spec.nchannels;

        m_color_mode_label->setText(colormode_label_text(nchannels, m_color_mode, m_current_channel));
    }
}

void RenderViewMainWindow::view_channel_full()
{
    view_channel(0, ColorMode::RGB);
}

void RenderViewMainWindow::view_channel_red()
{
    view_channel(0, ColorMode::SingleChannel);
}

void RenderViewMainWindow::view_channel_green()
{
    view_channel(1, ColorMode::SingleChannel);
}

void RenderViewMainWindow::view_channel_blue()
{
    view_channel(2, ColorMode::SingleChannel);
}

void RenderViewMainWindow::view_channel_alpha()
{
    view_channel(3, ColorMode::SingleChannel);
}

void RenderViewMainWindow::view_channel_lumiance()
{
    view_channel(0, ColorMode::Lumiance);
}

QTreeWidgetItem* RenderViewMainWindow::find_image_item(uint32_t image_id)
{
    QTreeWidgetItem* result = nullptr;
    QTreeWidgetItemIterator it(m_catalog_widget);
    while (*it)
    {
        if ((*it)->data(0, Qt::UserRole).toInt() == image_id)
        {
            result = (*it);
            break;
        }
        ++it;
    }
    return result;
}

void RenderViewMainWindow::new_image_item(int image_id, int parent_id, const QString& name)
{
    QTreeWidgetItem* item = nullptr;

    QTreeWidgetItem* parent_item = find_image_item(parent_id);

    if (parent_item != nullptr)
    {
        item = new QTreeWidgetItem(parent_item);
    }
    else
    {
        item = new QTreeWidgetItem(m_catalog_widget);
    }

    item->setData(0, Qt::UserRole, QVariant(image_id));
    item->setIcon(0, QIcon(":icons/render_view/eye_icon"));
    item->setText(0, name);

    item->setData(0, Qt::UserRole + 1, QDateTime::currentDateTime());

    item->setFlags(item->flags() | Qt::ItemIsEditable);
}

int RenderViewMainWindow::current_catalog_top_level_index()
{
    int current_index = -1;
    auto selected = m_catalog_widget->selectedItems();
    if (selected.length() > 0)
    {
        auto item = selected[0];
        current_index = -1;
        if (item)
        {
            auto parent_item = item->parent();
            if (item->parent() == nullptr)
            {
                current_index = m_catalog_widget->indexOfTopLevelItem(item);
            }
            else
            {
                QTreeWidgetItem* current_item = item;
                while (current_item->parent() != nullptr)
                {
                    current_item = current_item->parent();
                }

                current_index = m_catalog_widget->indexOfTopLevelItem(current_item);
            }
        }
    }
    return current_index;
}

void RenderViewMainWindow::prev_image()
{
    auto selected = m_catalog_widget->selectedItems();
    if (selected.length() > 0)
    {
        auto item = m_catalog_widget->itemAbove(selected[0]);
        if (item)
        {
            m_catalog_widget->setCurrentItem(item);
        }
    }
}

void RenderViewMainWindow::next_image()
{
    auto selected = m_catalog_widget->selectedItems();
    if (selected.length() > 0)
    {
        auto item = m_catalog_widget->itemBelow(selected[0]);
        if (item)
        {
            m_catalog_widget->setCurrentItem(item);
        }
    }
}

void RenderViewMainWindow::prev_main_image()
{
    go_between_main_images(false);
}

void RenderViewMainWindow::next_main_image()
{
    go_between_main_images(true);
}

void RenderViewMainWindow::go_between_main_images(bool move_down)
{
    auto selected = m_catalog_widget->selectedItems();
    if (selected.length() > 0)
    {
        QTreeWidgetItem* current_item = selected[0];
        int current_top_level_index = current_catalog_top_level_index();

        const int item_count = m_catalog_widget->topLevelItemCount();
        const bool condition = move_down ? (current_top_level_index >= 0 && current_top_level_index < (item_count - 1))
                                         : (current_top_level_index > 0 && current_top_level_index <= item_count);

        if (condition)
        {
            QTreeWidgetItem* item = m_catalog_widget->topLevelItem(current_top_level_index + (move_down ? 1 : -1));
            bool children_match = false;
            for (int i = 0; i < item->childCount(); i++)
            {
                QTreeWidgetItem* child = item->child(i);
                if (child->text(0) == current_item->text(0))
                {
                    m_catalog_widget->setCurrentItem(child);
                    children_match = true;
                    break;
                }
            }
            if (!children_match)
                m_catalog_widget->setCurrentItem(item);
        }
        else if (current_top_level_index >= 0 && current_top_level_index < item_count)
        {
            m_catalog_widget->setCurrentItem(m_catalog_widget->topLevelItem(current_top_level_index));
        }
    }
}

void RenderViewMainWindow::toggle_background_slot()
{
    auto selected = m_catalog_widget->selectedItems();
    if (selected.length() > 0)
    {

        toggle_background(selected[0]->data(0, Qt::UserRole).toInt());
    }
    else
    {
        toggle_background(-1);
    }
}

void RenderViewMainWindow::show_about_dialog()
{
    char render_view_build_year[5];
    auto build_date = platform::get_build_date_str();
    render_view_build_year[0] = build_date[7];
    render_view_build_year[1] = build_date[8];
    render_view_build_year[2] = build_date[9];
    render_view_build_year[3] = build_date[10];
    render_view_build_year[4] = '\0';

    auto company_name = m_app_config.get<std::string>("settings.app.window.about.company");
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    const auto unicode_company_name = converter.from_bytes(company_name);
    const auto company_name_str = QString::fromStdWString(unicode_company_name);

    QString text;
    text = QString::asprintf(i18n("render_view.about", "Render View (git_commit: %s build date: %s )").toStdString().c_str(),
                             platform::get_git_commit_hash_str(), platform::get_build_date_str());
    text += "\n";
    text += render_view_build_year;
    text += " " + company_name_str + ".";

    QMessageBox::about(this, i18n("render_view.about", "Render View"), text);
}

void RenderViewMainWindow::load_shortcuts()
{
    m_prefs.settings->beginGroup("shortcuts");
    for (auto key : m_prefs.settings->childKeys())
    {
        QString read_shortcut = m_prefs.settings->value(key).toString();
        QAction* got_action = findChild<QAction*>(key);
        got_action->setShortcut(read_shortcut);
    }
    m_prefs.settings->endGroup();
}

std::map<QAction*, QKeySequence>& RenderViewMainWindow::get_defaults_map()
{
    return m_defaults_map;
}

void RenderViewMainWindow::update_timesago()
{
    QTreeWidgetItemIterator it(m_catalog_widget);
    auto now = QDateTime::currentDateTime();
    while (*it)
    {
        QDateTime image_time = (*it)->data(0, Qt::UserRole + 1).toDateTime();
        if (image_time.isValid())
        {
            auto elapsed_time = image_time.secsTo(now);
            const int minute = 60;
            const int hour = minute * 60;
            const int day = hour * 24;
            QString timeago_str;
            if (elapsed_time > day)
            {
                timeago_str = QString::asprintf("%dd", int(elapsed_time / day));
            }
            else if (elapsed_time > hour)
            {
                timeago_str = QString::asprintf("%dh", int(elapsed_time / hour));
            }
            else if (elapsed_time > minute)
            {
                timeago_str = QString::asprintf("%dm", int(elapsed_time / minute));
            }
            else
            {
                timeago_str = QString::asprintf("%ds", int(elapsed_time));
            }
            (*it)->setText(1, timeago_str);
        }
        ++it;
    }
}

RenderViewPreferences& RenderViewMainWindow::get_prefs()
{
    return m_prefs;
}

const RenderViewPreferences& RenderViewMainWindow::get_prefs() const
{
    return m_prefs;
}

OPENDCC_NAMESPACE_CLOSE
