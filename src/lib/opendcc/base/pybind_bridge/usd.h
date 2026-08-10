/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/pybind_bridge/boost.h"
#include <pxr/base/vt/value.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/stageCache.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix3d.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#if PXR_VERSION >= 2508
#include <pxr/usd/sdr/shaderNodeDiscoveryResult.h>
#else
#include <pxr/usd/ndr/nodeDiscoveryResult.h>
#endif

OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec2i, "pxr.Gf.Vec2i")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec2h, "pxr.Gf.Vec2h")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec2f, "pxr.Gf.Vec2f")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec2d, "pxr.Gf.Vec2f")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec3i, "pxr.Gf.Vec3i")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec3h, "pxr.Gf.Vec3h")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec3f, "pxr.Gf.Vec3f")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec3d, "pxr.Gf.Vec3d")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec4i, "pxr.Gf.Vec4i")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec4h, "pxr.Gf.Vec4h")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec4f, "pxr.Gf.Vec4f")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfVec4d, "pxr.Gf.Vec4d")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfRotation, "pxr.Gf.Rotation")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfMatrix3f, "pxr.Gf.Matrix3f")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfMatrix3d, "pxr.Gf.Matrix3d")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfMatrix4f, "pxr.Gf.Matrix4f")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::GfMatrix4d, "pxr.Gf.Matrix4d")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::TfToken, "str")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::TfTokenVector, "list(str)")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::TfType, "pxr.Tf.Type")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::TfHash, "pxr.Tf.Hash")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::VtValue, "pxr.Vt.Value")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::VtDictionary, "dict")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdPrim, "pxr.Usd.Prim")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdAttribute, "pxr.Usd.Attribute")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdRelationship, "pxr.Usd.Relationship")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdProperty, "pxr.Usd.Property")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdStageCache, "pxr.Usd.StageCache")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdStagePtr, "pxr.Usd.Stage")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdStageCache::Id, "pxr.Usd.StageCache.Id")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdStageRefPtr, "pxr.Usd.Stage")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::SdfLayerHandle, "pxr.Sdf.Layer")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::SdfLayerRefPtr, "pxr.Sdf.Layer")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::UsdTimeCode, "pxr.Usd.TimeCode")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::SdfPath, "pxr.Sdf.Path")
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::SdfSpecType, "pxr.Sdf.SpecType")
#if PXR_VERSION >= 2508
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::SdrShaderNodeDiscoveryResult, "pxr.Sdr.ShaderNodeDiscoveryResult");
#else
OPENDCC_PYBIND_BOOST_BRIDGE(PXR_NS::NdrNodeDiscoveryResult, "pxr.Ndr.NodeDiscoveryResult");
#endif

namespace pybind11
{
    namespace detail
    {
        // Fallback binding to any TfRefPtr/TfWeakPtr besides declared above
        // If your function uses some of these then consider declaring it using OPENDCC_PYBIND_BOOST_BRIDGE because
        // it allows you to set meaningful name for TfRefPtr<T> in Python
        template <class T>
        struct tf_ptr_caster
        {
            using value_type = T;
            PYBIND11_TYPE_CASTER(value_type, _<value_type>());
            bool load(pybind11::handle src, bool) { return OPENDCC_NAMESPACE::pybind_boost_bridge<value_type>::load(value, src); }

            static handle cast(value_type src, pybind11::return_value_policy, pybind11::handle)
            {
                return OPENDCC_NAMESPACE::pybind_boost_bridge<value_type>::cast(src);
            }
        };

        template <class T>
        struct type_caster<PXR_NS::TfRefPtr<T>> : tf_ptr_caster<PXR_NS::TfRefPtr<T>>
        {
        };

        template <class T>
        struct type_caster<PXR_NS::TfWeakPtr<T>> : tf_ptr_caster<PXR_NS::TfWeakPtr<T>>
        {
        };

        // Wrap all VtArray<U>
        template <class U>
        struct type_caster<PXR_NS::VtArray<U>>
        {
            PYBIND11_TYPE_CASTER(PXR_NS::VtArray<U>, _("pxr.Vt.Array"));
            bool load(pybind11::handle src, bool) { return OPENDCC_NAMESPACE::pybind_boost_bridge<PXR_NS::VtArray<U>>::load(value, src); }

            static handle cast(PXR_NS::VtArray<U> src, pybind11::return_value_policy, pybind11::handle)
            {
                return OPENDCC_NAMESPACE::pybind_boost_bridge<PXR_NS::VtArray<U>>::cast(src);
            }
        };
    }
}
