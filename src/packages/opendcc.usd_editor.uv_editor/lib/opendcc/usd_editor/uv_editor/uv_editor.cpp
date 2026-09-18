// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd_editor/uv_editor/uv_editor.h"
#include <QActionGroup>
#include <QRegularExpression>

#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/app/viewport/prim_material_override.h"
#include "opendcc/usd_editor/uv_editor/uv_editor_gl_widget.h"
#include "opendcc/app/ui/application_ui.h"

#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usdImaging/usdImaging/tokens.h>
#include <pxr/imaging/hd/material.h>

#include <OpenColorIO/OpenColorIO.h>

#include <QMenuBar>
#include <QMenu>
#include <QBoxLayout>
#include <QToolBar>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QCheckBox>
#include <QSpinBox>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    void for_each_texture(const HdMaterialNetworkMap& mat_resource, size_t mat_id, const std::function<void(const SdfAssetPath&, size_t)>& fn)
    {
        for (const auto& entry : mat_resource.map)
        {
            for (const auto& shader : entry.second.nodes)
            {
                if (shader.identifier != UsdImagingTokens->UsdUVTexture)
                    continue;
                auto path_it = shader.parameters.find(TfToken("file"));
                if (path_it == shader.parameters.end())
                    continue;

                if (path_it->second.IsHolding<SdfAssetPath>())
                    fn(path_it->second.UncheckedGet<SdfAssetPath>(), mat_id);
            }
        }
    };

    SdfPathVector get_prims_to_populate()
    {
        const auto prim_selection = OPENDCC_NAMESPACE::Application::instance().get_prim_selection();
        auto result = OPENDCC_NAMESPACE::Application::instance().get_highlighted_prims();
        result.insert(result.end(), prim_selection.begin(), prim_selection.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    QStringList get_tiling_modes()
    {
        static const QStringList tiling_modes = { "None", "UDIM" };
        return tiling_modes;
    }

    QString get_tiling_mode(uint32_t id)
    {
        if (id < get_tiling_modes().size())
            return get_tiling_modes()[id];
        return "";
    }

    static VtIntArray get_vertex_uv_indices(const UsdGeomMesh& mesh, const TfToken& uv_primvar, UsdTimeCode time)
    {
        UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
        auto st = primvars_api.GetPrimvar(uv_primvar);
        VtVec3fArray mesh_points;
        mesh.GetPointsAttr().Get(&mesh_points, time);

        VtVec2fArray uv_points;
        st.Get(&uv_points, time);
        VtIntArray uv_points_indices;

        if (st.IsIndexed())
        {
            if (!TF_VERIFY(st.GetIndicesAttr().Get(&uv_points_indices, time), "Failed to extract st indices from prim '%s'.",
                           mesh.GetPrim().GetPrimPath().GetText()))
            {
                return {};
            }
        }
        else
        {
            uv_points_indices.resize(uv_points.size());
            std::iota(uv_points_indices.begin(), uv_points_indices.end(), 0);
        }

        if (!TF_VERIFY(uv_points_indices.size() == mesh_points.size(),
                       "Failed to extract st indices from prim '%s', st indices mismatch: expected '%s', got '%s'.",
                       mesh.GetPrim().GetPrimPath().GetText(), std::to_string(uv_points.size()).c_str(), std::to_string(mesh_points.size()).c_str()))
        {
            return {};
        }

        VtIntArray face_indices;
        mesh.GetFaceVertexIndicesAttr().Get(&face_indices, time);
        VtIntArray result(face_indices.size());
        for (int i = 0; i < face_indices.size(); i++)
            result[i] = uv_points_indices[face_indices[i]];
        return result;
    }

    static VtIntArray get_varying_uv_indices(const UsdGeomMesh& mesh, const TfToken& uv_primvar, UsdTimeCode time)
    {
        UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
        auto st = primvars_api.GetPrimvar(uv_primvar);
        VtIntArray result;

        if (st.IsIndexed())
        {
            if (!TF_VERIFY(st.GetIndicesAttr().Get(&result, time), "Failed to extract st indices from prim '%s'.",
                           mesh.GetPrim().GetPrimPath().GetText()))
            {
                return {};
            }
        }
        else
        {
            VtVec2fArray uv_points;
            st.Get(&uv_points, time);
            result.resize(uv_points.size());
            std::iota(result.begin(), result.end(), 0);
        }

        return result;
    }
};

class TextureFileData
{
    QString base_path;
    QString udim_path;
    QString base_name;
    QString udim_name;

public:
    TextureFileData() = default;
    TextureFileData(QString base_path, QString udim_path)
        : base_path(base_path)
        , udim_path(udim_path)
    {
        base_name = QFileInfo(base_path).fileName();
        udim_name = QFileInfo(udim_path).fileName();
    }

    const QString& get_name(int tiling_mode) const { return get_tiling_mode(tiling_mode) == "UDIM" ? udim_name : base_name; }
    const QString& get_path(int tiling_mode) const { return get_tiling_mode(tiling_mode) == "UDIM" ? udim_path : base_path; }

    const QString& get_name(const QString& tiling_mode) const { return tiling_mode.toUpper() == "UDIM" ? udim_name : base_name; }
    const QString& get_path(const QString& tiling_mode) const { return tiling_mode.toUpper() == "UDIM" ? udim_path : base_path; }
};

class MaterialOverrideTexture
{
    QString path;
    QString name;
    size_t material_id;

public:
    MaterialOverrideTexture() = default;
    MaterialOverrideTexture(QString path, QString name, size_t material_id)
        : path(path)
        , name(name)
        , material_id(material_id)
    {
    }

    const QString& get_name() const { return name; }
    const QString& get_path() const { return path; }
    size_t get_mat_id() const { return material_id; }
};
Q_DECLARE_METATYPE(TextureFileData);
Q_DECLARE_METATYPE(MaterialOverrideTexture);

OPENDCC_NAMESPACE_OPEN

UVEditor::UVEditor(QWidget* parent /*= nullptr*/)
    : QWidget(parent)
{
    auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    auto toolbar = new QToolBar;
    toolbar->setContentsMargins(0, 0, 0, 0);
    // Tiling Mode
    auto tiling_mode_lbl = new QLabel(i18n("uveditor.toolbar", "Tiling mode: "));
    tiling_mode_lbl->setContentsMargins(5, 0, 0, 0);
    auto tiling_mode_cb = new QComboBox;
    tiling_mode_cb->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    tiling_mode_cb->addItems(get_tiling_modes());
    connect(tiling_mode_cb, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int id) {
        m_gl_widget->set_tiling_mode(get_tiling_modes()[id]);
        const auto cur_data = m_custom_texture_cb->currentData();
        if (cur_data.canConvert<TextureFileData>())
            m_gl_widget->set_background_texture(cur_data.value<TextureFileData>().get_path(id));
        else
            m_gl_widget->set_background_texture(cur_data.value<MaterialOverrideTexture>().get_path());
        update_texture_names(id);
    });
    tiling_mode_cb->setCurrentIndex(0);

    // Custom texture
    auto custom_texture_lbl = new QLabel(i18n("uveditor.toolbar", "Custom texture: "));
    custom_texture_lbl->setContentsMargins(5, 0, 0, 0);
    m_custom_texture_cb = new QComboBox;
    m_custom_texture_cb->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_custom_texture_cb->setCurrentText(i18n("uveditor.toolbar.custom_texture", "None"));
    m_custom_texture_cb->addItem(i18n("uveditor.toolbar.custom_texture", "None"));
    connect(m_custom_texture_cb, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, tiling_mode_cb](int id) {
        if (id == 0)
        {
            m_gl_widget->set_background_texture("");
            return;
        }

        const auto cur_data = m_custom_texture_cb->currentData();
        if (cur_data.canConvert<TextureFileData>())
            m_gl_widget->set_background_texture(cur_data.value<TextureFileData>().get_path(tiling_mode_cb->currentIndex()));
        else
            m_gl_widget->set_background_texture(cur_data.value<MaterialOverrideTexture>().get_path());
    });

    auto add_custom_texture_btn = new QPushButton(QIcon(":icons/plus"), "");
    add_custom_texture_btn->setFixedSize(16, 16);
    add_custom_texture_btn->setFlat(true);
    connect(add_custom_texture_btn, &QPushButton::clicked, this, [this] { load_texture(); });

    // Display texture
    auto display_texture_cb = new QCheckBox(i18n("uveditor.toolbar", "Display Texture:"));
    display_texture_cb->setLayoutDirection(Qt::RightToLeft);
    connect(display_texture_cb, &QCheckBox::stateChanged, this, [this, tiling_mode_cb, add_custom_texture_btn](int state) {
        const auto enable = static_cast<bool>(state);
        m_gl_widget->show_background_texture(enable);
        tiling_mode_cb->setEnabled(enable);
        m_custom_texture_cb->setEnabled(enable);
        add_custom_texture_btn->setEnabled(enable);
    });

    m_tool_changed_cid = Application::instance().register_event_callback(Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED,
                                                                         [this] { gather_textures_from_mat_overrides(); });
    // UV primvar
    m_selection_changed_cid =
        Application::instance().register_event_callback(Application::EventType::SELECTION_CHANGED, [this] { on_selection_changed(); });
    auto uv_primvar_lbl = new QLabel(i18n("uveditor.toolbar", "UV Primvar:"));
    uv_primvar_lbl->setContentsMargins(5, 0, 0, 0);
    m_uv_primvar_cb = new QComboBox;
    connect(m_uv_primvar_cb, qOverload<int>(&QComboBox::activated), this, [this] { m_gl_widget->set_uv_primvar(m_uv_primvar_cb->currentText()); });

    // Color Management
    auto settings = Application::instance().get_settings();
    auto default_view_transform = settings->get("colormanagement.ocio_view_transform", "sRGB");

    const double default_gamma = 1.0;
    const double default_exposure = 0.0;

    auto gamma_icon = new QLabel;
    gamma_icon->setScaledContents(true);
    gamma_icon->setFixedSize(QSize(16, 16));
    gamma_icon->setPixmap(QPixmap(":/icons/gamma"));
    gamma_icon->setToolTip(i18n("uveditor.toolbar", "Gamma"));
    // gamma_icon->setContentsMargins(5, 0, 0, 0);

    auto init_color_adjustment_widget = [this](double init_value, const QString& tooltip,
                                               std::function<void(double)> value_setter_fn) -> QDoubleSpinBox* {
        QDoubleSpinBox* widget = new QDoubleSpinBox;
        widget->setToolTip(tooltip);
        widget->setButtonSymbols(QAbstractSpinBox::NoButtons);
        widget->setFixedWidth(40);
        widget->setFixedHeight(20);
        widget->setMaximum(1e10);
        widget->setMinimum(-1e10);
        widget->setValue(init_value);
        widget->setLocale(QLocale(QLocale::Hawaiian, QLocale::UnitedStates));

        QObject::connect(widget, &QDoubleSpinBox::editingFinished, [this, widget, value_setter_fn] {
            double value = widget->value();
            widget->setValue(value);
            value_setter_fn(value);
        });
        return widget;
    };

    m_gamma_sb = init_color_adjustment_widget(default_gamma, i18n("uveditor.toolbar", "Gamma"), [&](double value) { m_gl_widget->set_gamma(value); });

    auto exposure_icon = new QLabel;
    exposure_icon->setScaledContents(true);
    exposure_icon->setFixedSize(QSize(16, 16));
    exposure_icon->setPixmap(QPixmap(":/icons/exposure"));
    exposure_icon->setToolTip(i18n("uveditor.toolbar", "Exposure"));
    // exposure_icon->setContentsMargins(5, 0, 0, 0);

    m_exposure_sb =
        init_color_adjustment_widget(default_exposure, i18n("uveditor.toolbar", "Exposure"), [&](double value) { m_gl_widget->set_exposure(value); });

    m_view_transform_cb = new QComboBox;
    m_view_transform_cb->setToolTip(i18n("uveditor.toolbar", "OCIO View Transform"));
    m_view_transform_cb->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    namespace OCIO = OCIO_NAMESPACE; // OpenColorIO;
    auto config = OCIO::GetCurrentConfig();
    auto default_display = config->getDefaultDisplay();
    for (int i = 0; i < config->getNumViews(default_display); i++)
    {
        m_view_transform_cb->addItem(config->getView(default_display, i));
    }
    m_view_transform_cb->setCurrentText(QString(default_view_transform.c_str()));
    m_view_transform_cb->connect(m_view_transform_cb, qOverload<int>(&QComboBox::activated), this,
                                 [&](int index) { m_gl_widget->set_view_transform(m_view_transform_cb->itemText(index).toStdString()); });

    toolbar->addWidget(display_texture_cb);
    toolbar->addWidget(tiling_mode_lbl);
    toolbar->addWidget(tiling_mode_cb);
    toolbar->addWidget(custom_texture_lbl);
    toolbar->addWidget(m_custom_texture_cb);
    toolbar->addWidget(add_custom_texture_btn);
    toolbar->addWidget(uv_primvar_lbl);
    toolbar->addWidget(m_uv_primvar_cb);
    toolbar->addWidget(gamma_icon);
    toolbar->addWidget(m_gamma_sb);
    toolbar->addWidget(exposure_icon);
    toolbar->addWidget(m_exposure_sb);
    toolbar->addSeparator();
    toolbar->addWidget(m_view_transform_cb);
    // toolbar->addStretch();

    m_gl_widget = new UVEditorGLWidget;
    layout->addWidget(toolbar);
    layout->addWidget(m_gl_widget);

    m_gl_widget->show_background_texture(false);
    tiling_mode_cb->setEnabled(false);
    m_custom_texture_cb->setEnabled(false);
    add_custom_texture_btn->setEnabled(false);

    // Color Management Menu
    auto menu_bar = new QMenuBar;
    menu_bar->setContentsMargins(0, 0, 0, 0);
    auto view_menu = new QMenu(i18n("uveditor.menu_bar", "View"));
    menu_bar->addMenu(view_menu);

    auto color_mode_menu = view_menu->addMenu(i18n("uveditor.menu_bar.view", "Color Management"));
    QActionGroup* color_mode_group = new QActionGroup(this);

    auto add_color_mode_action = [this, color_mode_menu, color_mode_group](const QString& name, const std::string value) -> QAction* {
        QAction* mode_action = new QAction(name, this);
        mode_action->setData(QString(value.c_str()));
        mode_action->setCheckable(true);

        connect(mode_action, &QAction::triggered, [this, value](bool) {
            m_view_transform_cb->setEnabled(value == "openColorIO");
            m_gl_widget->set_color_mode(value);
        });

        color_mode_menu->addAction(mode_action);
        color_mode_group->addAction(mode_action);
        return mode_action;
    };

    add_color_mode_action(i18n("uveditor.menu_bar.view", "Disabled"), "disabled");
    add_color_mode_action(i18n("uveditor.menu_bar.view", "sRGB"), "sRGB");
    color_mode_menu->addSeparator();
    add_color_mode_action(i18n("uveditor.menu_bar.view", "OpenColorIO"), "openColorIO");

    auto default_color_mode = Application::instance().get_settings()->get("colormanagement.color_management", "openColorIO");

    m_view_transform_cb->setEnabled(default_color_mode == "openColorIO");

    for (auto& action : color_mode_group->actions())
    {
        if (action->data().toString().toLocal8Bit().data() == default_color_mode)
        {
            action->setChecked(true);
            break;
        }
    }

    layout->setMenuBar(menu_bar);
    gather_textures_from_mat_overrides();
    setLayout(layout);

    on_selection_changed();
}

UVEditor::~UVEditor()
{
    Application::instance().unregister_event_callback(Application::EventType::SELECTION_CHANGED, m_selection_changed_cid);
    Application::instance().unregister_event_callback(Application::EventType::CURRENT_VIEWPORT_TOOL_CHANGED, m_tool_changed_cid);
    auto tool = ApplicationUI::instance().get_current_viewport_tool();
    if (tool && m_material_changed_cid)
    {
        if (auto over = tool->get_prim_material_override())
            over->unregister_callback(m_material_changed_cid);
    }
}

void UVEditor::load_texture()
{
    auto base_path =
        QFileDialog::getOpenFileName(this, i18n("uveditor.load_texture", "Select File"), QString(),
                                     "All files (*.*);;BMP (*.bmp);;JPEG (*.jpg *.jpeg);;TIFF (*.tiff *.tif *.tx);;PNG (*.png);;EXR (*.exr);;"
                                     "TGA (*.tga);;HDR (*.hdr)",
                                     nullptr);
    if (base_path.isEmpty())
        return;

    const auto tiling_mode = m_gl_widget->get_tiling_mode();
    // search for duplicates
    const auto max_idx = std::max(m_material_overrides_index, m_custom_texture_cb->count());
    for (int i = 1; i < max_idx; i++)
    {
        const auto tex_file_data = qvariant_cast<TextureFileData>(m_custom_texture_cb->itemData(i));
        if (tex_file_data.get_path(tiling_mode) == base_path)
            return;
    }

    QString udim_path;
    if (base_path.contains("<UDIM>", Qt::CaseInsensitive))
    {
        udim_path = base_path;
    }
    else
    {
        auto regex = QRegularExpression(R"#((\D?)(10\d{2})(\D?))#");
        udim_path = base_path;
        const auto last_occ = udim_path.lastIndexOf(regex);
        if (last_occ != -1)
        {
            if (udim_path.at(last_occ).isDigit())
                udim_path.replace(last_occ, 4, "<UDIM>");
            else
                udim_path.replace(last_occ + 1, 4, "<UDIM>");
        }
    }

    QVariant stored;
    TextureFileData file_data(base_path, udim_path);
    stored.setValue(file_data);

    if (m_material_overrides_index == -1)
        m_custom_texture_cb->addItem(file_data.get_name(tiling_mode), stored);
    else
    {
        m_custom_texture_cb->insertItem(m_material_overrides_index, file_data.get_name(tiling_mode), stored);
        m_material_overrides_index++;
    }
    m_custom_texture_cb->setCurrentText(file_data.get_name(tiling_mode));
    m_gl_widget->set_background_texture(file_data.get_path(tiling_mode));
}

void UVEditor::update_texture_names(int tiling_mode_id)
{
    // Start with 1 because 0 is None
    const auto max_idx = std::max(m_material_overrides_index, m_custom_texture_cb->count());
    for (int i = 1; i < max_idx; i++)
    {
        const auto tex_file_data = qvariant_cast<TextureFileData>(m_custom_texture_cb->itemData(i));
        m_custom_texture_cb->setItemText(i, tex_file_data.get_name(tiling_mode_id));
    }

    const auto cur_data = m_custom_texture_cb->currentData();
    if (cur_data.canConvert<TextureFileData>())
        m_custom_texture_cb->setCurrentText(cur_data.value<TextureFileData>().get_name(tiling_mode_id));
    else
        m_custom_texture_cb->setCurrentText(cur_data.value<MaterialOverrideTexture>().get_name());
}

void UVEditor::on_selection_changed()
{
    const auto selection = get_prims_to_populate();
    auto stage = Application::instance().get_session()->get_current_stage();

    std::set<QString> uv_primvars;
    SdfPathVector prims_to_populate;
    if (stage)
        for (const auto& path : selection)
        {
            auto mesh = UsdGeomMesh(stage->GetPrimAtPath(path));
            if (!mesh)
                continue;

            auto primvar_api = UsdGeomPrimvarsAPI(mesh);
            if (!primvar_api)
                continue;

            auto prim_added = false;
            for (const auto& primvar : primvar_api.GetPrimvars())
            {
                if ((primvar.GetTypeName() != SdfValueTypeNames->TexCoord2fArray &&
                     primvar.GetTypeName() != SdfValueTypeNames->Float2Array) || // legacy pipeline support where UV sets where defined as float2[]
                    primvar.GetInterpolation() == UsdGeomTokens->constant ||
                    primvar.GetInterpolation() == UsdGeomTokens->uniform)
                {
                    continue;
                }

                if (!prim_added)
                {
                    prims_to_populate.push_back(mesh.GetPrim().GetPrimPath());
                    prim_added = true;
                }
                uv_primvars.insert(primvar.GetPrimvarName().GetText());
            }
        }

    m_uv_primvar_cb->clear();
    for (const auto& uv_primvar : uv_primvars)
        m_uv_primvar_cb->addItem(uv_primvar);
    m_uv_primvar_cb->setCurrentText(m_uv_primvar_cb->count() == 0 ? "" : m_uv_primvar_cb->itemText(0));
    m_gl_widget->set_uv_primvar(m_uv_primvar_cb->currentText());
    m_gl_widget->set_prim_paths(prims_to_populate);
    collect_geometry(prims_to_populate);
    m_gl_widget->set_prims_info(m_prims_info);
}

void UVEditor::gather_textures_from_mat_overrides()
{
    if (m_material_overrides_index != -1)
    {
        const auto max_override_idx = m_custom_texture_cb->count();
        for (int i = m_material_overrides_index; i < max_override_idx; i++)
            m_custom_texture_cb->removeItem(m_material_overrides_index);
        m_material_overrides_index = -1;
    }
    auto tool = ApplicationUI::instance().get_current_viewport_tool();
    if (!tool)
        return;
    auto over = tool->get_prim_material_override();
    if (!over)
        return;

    for (const auto& mat : over->get_materials())
    {
        const auto& mat_resource = mat.second.get_material_resource().Get<HdMaterialNetworkMap>();
        insert_mat_over_texture(mat_resource, mat.first);
    }
    if (m_material_changed_cid)
        over->unregister_callback(m_material_changed_cid);
    m_material_changed_cid = over->register_callback<PrimMaterialOverride::EventType::MATERIAL>(
        [this](size_t mat_id, const PrimMaterialDescriptor& descr, PrimMaterialOverride::Status status) {
            if (status == PrimMaterialOverride::Status::REMOVED)
            {
                if (m_material_overrides_index == -1)
                    return;

                const auto& mat_resource = descr.get_material_resource().Get<HdMaterialNetworkMap>();
                remove_texture(mat_id, mat_resource);
            }
            else if (status == PrimMaterialOverride::Status::NEW)
            {
                const auto& mat_resource = descr.get_material_resource().Get<HdMaterialNetworkMap>();
                insert_mat_over_texture(mat_resource, mat_id);
            }
            else if (status == PrimMaterialOverride::Status::CHANGED)
            {
                const auto& mat_resource = descr.get_material_resource().Get<HdMaterialNetworkMap>();
                update_textures(mat_id, mat_resource);
            }
        });
}

void UVEditor::insert_material_override(const SdfAssetPath& texture_path, size_t mat_id)
{
    const auto& path = texture_path.GetAssetPath();
    MaterialOverrideTexture tex_data(QString::fromStdString(path), QString::fromStdString(path.substr(path.rfind('/') + 1)), mat_id);
    QVariant stored;
    stored.setValue(tex_data);
    m_custom_texture_cb->addItem(tex_data.get_name(), stored);
}

void UVEditor::insert_mat_over_texture(const HdMaterialNetworkMap& mat_resource, size_t mat_id)
{
    if (m_material_overrides_index == -1)
    {
        m_material_overrides_index = m_custom_texture_cb->count();
        m_custom_texture_cb->insertSeparator(m_material_overrides_index);
    }

    for_each_texture(mat_resource, mat_id,
                     [this](const SdfAssetPath& texture_path, size_t mat_id) { insert_material_override(texture_path, mat_id); });
}

void UVEditor::remove_texture(size_t mat_id, const HdMaterialNetworkMap& mat_resource)
{
    for_each_texture(mat_resource, mat_id, [this](const SdfAssetPath& texture_path, size_t mat_id) {
        const auto& tex_path = texture_path.GetAssetPath();
        for (int i = m_material_overrides_index + 1; i < m_custom_texture_cb->count();)
        {
            const auto mat_over_tex = m_custom_texture_cb->itemData(i).value<MaterialOverrideTexture>();
            if (mat_over_tex.get_path().toStdString() == tex_path)
            {
                m_custom_texture_cb->removeItem(i);
                if (mat_over_tex.get_path() == m_gl_widget->get_background_texture())
                    m_custom_texture_cb->setCurrentIndex(0);
                if (m_material_overrides_index == m_custom_texture_cb->count() - 1)
                {
                    m_custom_texture_cb->removeItem(m_material_overrides_index);
                    m_material_overrides_index = -1;
                    return;
                }
                break;
            }
            i++;
        }
    });
}

void UVEditor::update_textures(size_t mat_id, const PXR_NS::HdMaterialNetworkMap& mat_resource)
{
    if (m_material_overrides_index == -1)
    {
        insert_mat_over_texture(mat_resource, mat_id);
    }
    else
    {
        std::unordered_set<std::string> textures;
        for_each_texture(mat_resource, mat_id,
                         [&textures](const SdfAssetPath& texture_path, size_t mat_id) { textures.insert(texture_path.GetAssetPath()); });

        int max_id = m_custom_texture_cb->count();
        for (int i = m_material_overrides_index + 1; i < max_id;)
        {
            const auto mat_over_tex = m_custom_texture_cb->itemData(i).value<MaterialOverrideTexture>();
            if (mat_over_tex.get_mat_id() != mat_id)
                continue;
            const auto mat_over_tex_path = mat_over_tex.get_path().toStdString();
            auto iter = std::find_if(textures.begin(), textures.end(),
                                     [&mat_over_tex_path](const std::string& texture) { return texture == mat_over_tex_path; });

            // if texture exists in the material
            if (iter != textures.end())
            {
                textures.erase(iter);
                if (mat_over_tex.get_path() == m_gl_widget->get_background_texture())
                    m_gl_widget->reload_current_texture();
                i++;
                continue;
            }
            else // texture was removed
            {
                m_custom_texture_cb->removeItem(i);
                if (mat_over_tex.get_path() == m_gl_widget->get_background_texture())
                    m_custom_texture_cb->setCurrentIndex(0);

                max_id--;
                if (m_material_overrides_index == m_custom_texture_cb->count() - 1)
                {
                    m_custom_texture_cb->removeItem(m_material_overrides_index);
                    m_material_overrides_index = -1;
                    break;
                }
            }
        }

        for (const auto& tex : textures)
        {
            MaterialOverrideTexture tex_data(QString::fromStdString(tex), QString::fromStdString(tex.substr(tex.find('/', 0))), mat_id);
            QVariant stored;
            stored.setValue(tex_data);
            m_custom_texture_cb->addItem(tex_data.get_name(), stored);
        }
    }
}

void UVEditor::fill_prim_info(const PXR_NS::UsdGeomMesh& mesh)
{
    auto& app = Application::instance();
    auto stage = app.get_session()->get_current_stage();
    if (!stage)
    {
        return;
    }

    const auto time = app.get_current_time();
    const auto uv_primvar = TfToken(m_uv_primvar_cb->currentText().toLocal8Bit().toStdString());

    PrimInfo prim_info;
    UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
    auto st = primvars_api.GetPrimvar(uv_primvar);
    VtVec2fArray st_points;
    if (!st.Get<VtVec2fArray>(&st_points, time))
    {
        return;
    }

    prim_info.range = std::accumulate(st_points.begin(), st_points.end(), GfRange3d(), [](GfRange3d& range, const GfVec2f& st) {
        range.ExtendBy(GfVec3d(st[0], st[1], 0));
        return range;
    });

    VtIntArray vertex_indices;
    VtIntArray face_vertex_counts;
    VtVec3fArray points;
    if (!mesh.GetFaceVertexIndicesAttr().Get(&vertex_indices, time) || !mesh.GetFaceVertexCountsAttr().Get<VtIntArray>(&face_vertex_counts, time) ||
        !mesh.GetPointsAttr().Get(&points, time))
    {
        return;
    }

    VtIntArray st_indices;
    const auto interp = st.GetInterpolation();
    if (interp == UsdGeomTokens->varying || interp == UsdGeomTokens->faceVarying)
    {
        st_indices = get_varying_uv_indices(mesh, uv_primvar, time);
    }
    else
    {
        st_indices = get_vertex_uv_indices(mesh, uv_primvar, time);
    }

    if (st_indices.empty())
    {
        return;
    }

    if (!TF_VERIFY(st_indices.size() == vertex_indices.size(),
                   "Failed to extract uv data from prim '%s', st indices mismatch: expected '%s', got '%s'.", mesh.GetPrim().GetPrimPath().GetText(),
                   std::to_string(vertex_indices.size()).c_str(), std::to_string(st_indices.size()).c_str()))
    {
        return;
    }

    TfToken orientation;
    mesh.GetOrientationAttr().Get(&orientation, time);
    prim_info.topology = HdMeshTopology(HdTokens->linear, orientation, face_vertex_counts, st_indices);

    prim_info.mesh_vertices_to_uv_vertices.resize(points.size());
    prim_info.uv_vertices_to_mesh_vertices.resize(st_points.size());
    for (int i = 0; i < st_indices.size(); i++)
    {
        const auto vert_id = vertex_indices[i];
        const auto uv_id = st_indices[i];
        if (vert_id < prim_info.mesh_vertices_to_uv_vertices.size())
        {
            prim_info.mesh_vertices_to_uv_vertices[vert_id].push_back(uv_id);
        }
        if (uv_id < prim_info.uv_vertices_to_mesh_vertices.size())
        {
            prim_info.uv_vertices_to_mesh_vertices[uv_id] = vert_id;
        }
    }

    auto& topology_cache = app.get_session()->get_stage_topology_cache(app.get_session()->get_current_stage_id());
    auto topology = topology_cache.get_topology(mesh.GetPrim(), time);

    const auto& mesh_edge_map = topology->edge_map;
    const auto uv_edge_map = EdgeIndexTable(&prim_info.topology);
    prim_info.mesh_edges_to_uv_edges.resize(mesh_edge_map.get_edge_count());
    prim_info.uv_edges_to_mesh_edges.resize(uv_edge_map.get_edge_count());
    for (int i = 0; i < uv_edge_map.get_edge_count(); i++)
    {
        GfVec2i uv_vert_ids;
        bool result;
        std::tie(uv_vert_ids, result) = uv_edge_map.get_vertices_by_edge_id(i);
        const auto mesh_edge0 = prim_info.uv_vertices_to_mesh_vertices[uv_vert_ids[0]];
        const auto mesh_edge1 = prim_info.uv_vertices_to_mesh_vertices[uv_vert_ids[1]];

        std::vector<int> mesh_edges;
        std::tie(mesh_edges, result) = mesh_edge_map.get_edge_id_by_edge_vertices({ mesh_edge0, mesh_edge1 });
        prim_info.uv_edges_to_mesh_edges[i] = VtIntArray(mesh_edges.begin(), mesh_edges.end());
        for (int j = 0; j < mesh_edges.size(); j++)
        {
            prim_info.mesh_edges_to_uv_edges[mesh_edges[j]].push_back(i);
        }
    }

    m_prims_info[mesh.GetPath()] = std::move(prim_info);
}

void UVEditor::collect_geometry(const PXR_NS::SdfPathVector& paths)
{
    auto stage = Application::instance().get_session()->get_current_stage();
    m_prims_info.clear();
    if (!stage)
    {
        return;
    }

    const auto uv_primvar = TfToken(m_uv_primvar_cb->currentText().toLocal8Bit().toStdString());

    for (const auto& path : paths)
    {
        auto mesh = UsdGeomMesh(stage->GetPrimAtPath(path));
        if (!mesh)
        {
            continue;
        }

        UsdGeomPrimvarsAPI primvars_api(mesh.GetPrim());
        auto uv = primvars_api.GetPrimvar(uv_primvar);
        if (!uv)
        {
            continue;
        }

        const auto uv_interpolation = uv.GetInterpolation();
        if (uv_interpolation == UsdGeomTokens->constant || uv_interpolation == UsdGeomTokens->uniform)
        {
            TF_WARN("Unsupported interpolation type for primvar '%s' :"
                    " expected 'vertex', 'varying' or 'faceVarying', got '%s'.",
                    uv.GetAttr().GetPath().GetText(), uv_interpolation.GetText());
            continue;
        }

        fill_prim_info(mesh);
    }
}

OPENDCC_NAMESPACE_CLOSE
