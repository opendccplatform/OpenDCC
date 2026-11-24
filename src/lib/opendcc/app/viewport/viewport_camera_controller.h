/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/app/core/api.h"
#include <QObject>
#include <pxr/usd/sdf/path.h>
#include "opendcc/app/viewport/viewport_camera_mapper.h"
#include "opendcc/app/core/application.h"
#include "opendcc/app/core/settings.h"

OPENDCC_NAMESPACE_OPEN

class FollowModeStrategy;
class ViewportCameraParameters;

class OPENDCC_API ViewportCameraController : public QObject
{
    Q_OBJECT;

public:
    enum class FollowMode
    {
        Invalid,
        DefCam,
        StageCamPrimReadOnly,
        StageCamPrim,
        StageXformablePrim,
    };

    ViewportCameraController(ViewportCameraMapperPtr camera_mapper, QObject* parent = nullptr);
    ~ViewportCameraController();

    /** @brief Independent navigation presets; DEFAULT retains global camera preferences. */
    enum class ViewPreset
    {
        DEFAULT, ///< Use global camera preferences.
        PERSPECTIVE, ///< Independent perspective navigation.
        TOP, ///< Orthographic view along the stage up axis.
        FRONT, ///< Orthographic front view with rotation disabled.
        SIDE ///< Orthographic side view with rotation disabled.
    };

    void set_view_preset(ViewPreset preset);
    ViewPreset get_view_preset() const { return m_view_preset; }
    /** @brief Return the shared optics for a named built-in camera. */
    static PXR_NS::GfCamera get_preset_camera_settings(ViewPreset preset);
    /** @brief Apply an unsaved settings edit to views using the given preset. */
    static void preview_preset_camera_settings(ViewPreset preset, const PXR_NS::GfCamera& camera);
    /** @brief Save or discard the unsaved settings for a preset. */
    static void finish_preset_camera_settings_edit(ViewPreset preset, bool save);
    /** @brief Capture the local navigation camera, including while following a scene camera. */
    PXR_NS::GfCamera get_navigation_camera();
    /** @brief Restore a local camera without authoring a USD camera. */
    void set_navigation_camera(const PXR_NS::GfCamera& camera);

    void truck(double delta_right, double delta_up);
    void pan_tilt(double delta_pan, double delta_tilt);
    void tumble(double delta_theta, double delta_phi);
    void adjust_distance(double scale_factor);
    double compute_pixels_to_world_factor(int viewport_height);

    void frame_selection(const PXR_NS::GfBBox3d& selection_bbox, double frame_fit = 1.1);
    void set_display_size(int w, int h);
    PXR_NS::GfCamera get_gf_camera();
    PXR_NS::GfFrustum get_frustum();
    PXR_NS::SdfPath get_follow_prim_path() const;
    void set_up_axis(const PXR_NS::TfToken& up_axis);
    void set_default_camera();
    void set_follow_prim(const PXR_NS::SdfPath& prim_path);
    double get_fov() const;
    FollowMode get_follow_mode() const;

    void update_camera_mapper(ViewportCameraMapperPtr camera_mapper);
    void set_time(PXR_NS::UsdTimeCode time);
Q_SIGNALS:
    void camera_changed(PXR_NS::SdfPath);
    void camera_mapper_changed();

private:
    friend class ViewportPanel;
    static void apply_preset_camera_settings(PXR_NS::GfCamera& camera, const PXR_NS::GfCamera& previous, const PXR_NS::GfCamera& settings);
    void update_follow_mode_strategy();
    void update_preset_camera_settings();

    PXR_NS::TfToken m_up_axis = PXR_NS::TfToken("y");
    ViewPreset m_view_preset = ViewPreset::DEFAULT;
    PXR_NS::GfCamera m_navigation_camera;
    PXR_NS::GfCamera m_preset_camera_settings;
    Settings::SettingChangedHandle m_preset_settings_changed;
    ViewportCameraMapperPtr m_camera_mapper;
    std::unique_ptr<FollowModeStrategy> m_follow_mode_strategy;
};

using ViewportCameraControllerPtr = std::shared_ptr<ViewportCameraController>;

OPENDCC_NAMESPACE_CLOSE
