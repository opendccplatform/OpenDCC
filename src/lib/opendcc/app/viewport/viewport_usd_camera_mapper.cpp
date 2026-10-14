// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/viewport/viewport_usd_camera_mapper.h"
#include "opendcc/app/core/session.h"
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/transform.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include "opendcc/app/core/application.h"
#include <pxr/usd/usdGeom/xformable.h>
#include "viewport_manipulator_utils.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.camera");

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    bool is_prim_transform_varying(const UsdGeomXformable xformable)
    {
        // we need to check all xform hierarchy, animation could be only on parent
        auto prim = xformable.GetPrim();
        do
        {
            UsdGeomXformable xform(prim);
            if (xform)
            {

                if (xform.TransformMightBeTimeVarying())
                {
                    // early out if found any animated transform
                    return true;
                }
                // If the xformable prim resets the transform stack, then
                // we dont have to check parents
                if (xform.GetResetXformStack())
                {
                    return false;
                }
            }
            prim = prim.GetParent();

        } while (prim.GetPath() != SdfPath::AbsoluteRootPath());

        return false;
    }
}

ViewportUsdCameraMapper::ViewportUsdCameraMapper(const SdfPath& path /*= PXR_NS::SdfPath()*/)
{
    set_path(path);
    m_follow_camera_changed_cid = Application::instance().get_session()->register_stage_changed_callback(
        Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED, [this](const UsdNotice::ObjectsChanged& notice) {
            if (!m_prim && !m_path.IsEmpty())
            {
                if (m_prim_changed_callback)
                    m_prim_changed_callback();
                return;
            }

            for (const auto& prim : notice.GetResyncedPaths())
            {
                if (prim.GetPrimPath() == m_prim.GetPath())
                {
                    if (m_prim_changed_callback)
                        m_prim_changed_callback();
                    return;
                }
            }
            for (const auto& prim : notice.GetChangedInfoOnlyPaths())
            {
                if (prim.GetPrimPath() == m_prim.GetPath())
                {
                    if (m_prim_changed_callback)
                        m_prim_changed_callback();
                    return;
                }
            }
        });
}

ViewportUsdCameraMapper::~ViewportUsdCameraMapper()
{
    Application::instance().get_session()->unregister_stage_changed_callback(Session::StageChangedEventType::CURRENT_STAGE_OBJECT_CHANGED,
                                                                             m_follow_camera_changed_cid);
}

void ViewportUsdCameraMapper::push(const GfCamera& camera, UsdTimeCode time /*= PXR_NS::UsdTimeCode::Default()*/)
{
    using namespace manipulator_utils;
    if (!m_prim)
        return;

    auto xformable_prim = UsdGeomXformable(m_prim);
    if (!xformable_prim)
        return;

    SdfChangeBlock block;
    const auto transform = camera.GetTransform();
    if (auto xform_common_api = UsdGeomXformCommonAPI(xformable_prim))
    {
        const auto parent_to_world_inverse = xformable_prim.ComputeParentToWorldTransform(time).GetInverse();
        const auto local_transform_mat = transform * parent_to_world_inverse;
        bool dummy;
        const auto ops = xformable_prim.GetOrderedXformOps(&dummy);
        auto translate_time = UsdTimeCode::Default();
        auto rotate_time = UsdTimeCode::Default();
        for (const auto& op : ops)
        {
            if (op.GetOpType() == UsdGeomXformOp::Type::TypeTranslate && !op.HasSuffix(TfToken("pivot")))
                translate_time = get_non_varying_time(op.GetAttr());
            if (UsdGeomXformOp::Type::TypeRotateX <= op.GetOpType() && op.GetOpType() <= UsdGeomXformOp::Type::TypeOrient)
                rotate_time = get_non_varying_time(op.GetAttr());
        }

        const auto rotation = local_transform_mat.ExtractRotation();
        const auto hint = get_euler_angles(xformable_prim, rotate_time);
        const auto angles = decompose_to_euler(GfMatrix4d(rotation, GfVec3d(0)), UsdGeomXformCommonAPI::RotationOrder::RotationOrderXYZ, hint);
        if (!GfIsClose(angles, GfVec3f(0), 0.00001))
            xform_common_api.SetRotate(angles, UsdGeomXformCommonAPI::RotationOrderXYZ, rotate_time);
        if (!GfIsClose(local_transform_mat.ExtractTranslation(), GfVec3f(0), 0.00001))
            xform_common_api.SetTranslate(local_transform_mat.ExtractTranslation(), translate_time);
    }
    else
    {
        bool resetsXformStack = false;
        GfMatrix4d current_transform;
        xformable_prim.GetLocalTransformation(&current_transform, &resetsXformStack, time);

        GfMatrix4d rot_mat(1.0);
        GfVec3d double_scale(1.0);
        GfMatrix4d scaleOrientMatUnused, perspMatUnused;
        GfVec3d translation_unused;
        current_transform.Factor(&scaleOrientMatUnused, &double_scale, &rot_mat, &translation_unused, &perspMatUnused);

        const auto parent_to_world_inverse = xformable_prim.ComputeParentToWorldTransform(time).GetInverse();
        const auto local_transform_mat = GfMatrix4d().SetScale(double_scale) * transform * parent_to_world_inverse;
        const auto local_transform = GfTransform(local_transform_mat);
        if (GfIsClose(local_transform.GetPivotOrientation().GetAngle(), 0, 0.0001))
        {
            xformable_prim.ClearXformOpOrder();
            decompose_to_common_api(xformable_prim, local_transform);
        }
        else if (auto matrix_op = xformable_prim.MakeMatrixXform())
        {
            matrix_op.Set(local_transform.GetMatrix(), get_non_varying_time(matrix_op));
        }
    }

    if (auto usd_camera = UsdGeomCamera(m_prim))
    {
        auto set_if_different = [](UsdAttribute attr, float value) {
            float current;
            if (attr.Get(&current, get_non_varying_time(attr)) && !GfIsClose(current, value, 0.000001))
            {
                attr.Set(value);
            }
        };
        set_if_different(usd_camera.GetHorizontalApertureAttr(), camera.GetHorizontalAperture());
        set_if_different(usd_camera.GetVerticalApertureAttr(), camera.GetVerticalAperture());
        // set_if_different(usd_camera.GetFocusDistanceAttr(), camera.GetFocusDistance());
    }
}

GfCamera ViewportUsdCameraMapper::pull(UsdTimeCode time /*= PXR_NS::UsdTimeCode::Default()*/)
{
    if (!m_prim)
        return GfCamera();

    if (auto camera_prim = UsdGeomCamera(m_prim))
    {
        return camera_prim.GetCamera(time);
    }

    if (auto xformable_prim = UsdGeomXformable(m_prim))
    {
        bool resetsXformStack = false;
        GfMatrix4d transform;
        xformable_prim.GetLocalTransformation(&transform, &resetsXformStack, time);
        return GfCamera(transform);
    }
    return GfCamera();
}

void ViewportUsdCameraMapper::set_path(const SdfPath& path)
{
    if (path.IsEmpty())
    {
        m_path = path;
        m_prim = UsdPrim();
        return;
    }

    if (auto stage = Application::instance().get_session()->get_current_stage())
    {
        if (auto prim = stage->GetPrimAtPath(path))
        {
            if (UsdGeomXformable(prim))
            {
                m_prim = prim;
                m_path = path;
            }
            else
            {
                OPENDCC_ERROR("Failed to assign new camera path. The specified path '{}' is not UsdGeomXformable prim.", path.GetText());
            }
        }
        else
        {
            OPENDCC_ERROR("Failed to assign new camera path. The prim at path '{}' doesn't exist.", path.GetText());
        }
    }
}

SdfPath ViewportUsdCameraMapper::get_path()
{
    return m_path;
}

bool ViewportUsdCameraMapper::is_camera_prim() const
{
    return static_cast<bool>(UsdGeomCamera(m_prim));
}

bool ViewportUsdCameraMapper::is_read_only() const
{
    if (!m_prim)
        return false;

    if (auto xformable_prim = UsdGeomXformable(m_prim))
        return xformable_prim.TransformMightBeTimeVarying();
    return false;
}

bool ViewportUsdCameraMapper::is_valid() const
{
    return m_prim.IsValid();
}

OPENDCC_NAMESPACE_CLOSE
