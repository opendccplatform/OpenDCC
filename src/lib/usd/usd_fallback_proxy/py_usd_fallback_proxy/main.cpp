// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/opendcc.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include <pxr/base/tf/pyContainerConversions.h>
#include <pxr/base/tf/pyResultConversions.h>
#include <pxr/base/tf/pyUtils.h>
#include <pxr/usd/usd/pyConversions.h>
#include "opendcc/base/pybind_bridge/boost_python.h"
#include <pxr/base/tf/pyContainerConversions.h>
#include <pxr/base/tf/pyError.h>
#include <pxr/usd/ar/pyResolverContext.h>
#include <usd_fallback_proxy/core/usd_property_proxy.h>
#include <usd_fallback_proxy/core/source_registry.h>
#include <usd_fallback_proxy/core/usd_prim_fallback_proxy.h>
#include <usd_fallback_proxy/core/usd_fallback_proxy_watcher.h>
#include <sstream>
#include <usd_fallback_proxy/core/usd_property_source.h>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

namespace bp = PXR_BOOST_PYTHON_NAMESPACE;

static VtValue get(UsdPropertyProxy* self, const UsdTimeCode& time = UsdTimeCode::Default())
{
    VtValue val;
    self->get(&val, time);
    TfPyLock lock;
    return val;
}

static TfPyObjWrapper get_default(UsdPropertyProxy* self)
{
    VtValue val;
    self->get_default(&val);
    TfPyLock lock;
    return TfPyObjWrapper(TfPyObject(val));
}

static bool set(UsdPropertyProxy* self, pybind11::object val, const UsdTimeCode time = UsdTimeCode::Default())
{
    auto bp_object = bp::object(bp::handle<>(bp::borrowed(val.ptr())));
    auto wrapper = TfPyObjWrapper(bp_object);
    VtValue vt_value;
    {
        TfPyLock lock;
        vt_value = bp::extract<VtValue>(wrapper.Get())();
    }

    VtValue def_value = self->get_type_name().GetDefaultValue();

    VtValue cast = VtValue::CastToTypeOf(vt_value, def_value);
    if (!cast.IsEmpty())
        cast.Swap(vt_value);

    return self->set(vt_value, time);
}

static VtValue get_metadata(UsdPropertyProxy* self, const TfToken& key)
{
    VtValue result;
    self->get_metadata(key, result);
    return result;
}

static bool set_metadata(UsdPropertyProxy* self, const TfToken& key, pybind11::object obj)
{
    VtValue value;
    return UsdPythonToMetadataValue(key, TfToken(), bp::object(bp::handle<>(bp::borrowed(obj.ptr()))), &value) &&
           self->set_metadata(key, value);
}

static std::string property_proxy_repr(UsdPropertyProxy* self)
{
    std::ostringstream oss;
    oss << "UsdPropertyProxy(\"" << self->get_name_token().GetString() << "\")";
    return oss.str();
}

PYBIND11_MODULE(MODULE_NAME, m)
{
    using namespace pybind11;

    class_<SourceRegistry, std::unique_ptr<SourceRegistry, nodelete>>(m, "SourceRegistry")
        .def_static("get_property_proxies", &SourceRegistry::get_property_proxies)
        .def_static("get_property_proxy", &SourceRegistry::get_property_proxy);

    class_<UsdPrimFallbackProxy>(m, "UsdPrimFallbackProxy")
        .def(init<>())
        .def(init<UsdPrim>())
        .def("get_all_property_proxies", &UsdPrimFallbackProxy::get_all_property_proxies)
        .def("get_property_proxy", &UsdPrimFallbackProxy::get_property_proxy)
        .def("get_usd_prim", &UsdPrimFallbackProxy::get_usd_prim);

    class_<UsdFallbackProxyWatcher::InvalidProxyDispatcherHandle, std::unique_ptr<UsdFallbackProxyWatcher::InvalidProxyDispatcherHandle, nodelete>>(
        m, "InvalidProxyDispatcherHandle");

    class_<UsdFallbackProxyWatcher, std::unique_ptr<UsdFallbackProxyWatcher, nodelete>>(m, "UsdFallbackProxyWatcher")
        .def_static("register_invalid_proxy_callback",
                    [](const std::function<void(const std::vector<UsdPrimFallbackProxy>&)>& callback) {
                        return UsdFallbackProxyWatcher::register_invalid_proxy_callback(pybind_safe_callback(callback));
                    })
        .def_static("unregister_invalid_proxy_callback", &UsdFallbackProxyWatcher::unregister_invalid_proxy_callback);

    class_<UsdPropertySource, std::unique_ptr<UsdPropertySource, nodelete>>(m, "UsdPropertySource")
        .def(init<const PXR_NS::TfToken&, const PXR_NS::TfType&>())
        .def("get_source_group", &UsdPropertySource::get_source_group)
        .def("get_source_plugin", &UsdPropertySource::get_source_plugin);

    class_<UsdPropertyProxy, UsdPropertyProxyPtr>(m, "UsdPropertyProxy")
        .def("get", &get, arg("time") = UsdTimeCode::Default())
        .def("get_default", &UsdPropertyProxy::get_default)
        .def("set", &set, arg("value"), arg("time") = UsdTimeCode::Default())
        .def("get_type_name", [](UsdPropertyProxy* self) { return self->get_type_name().GetAsToken(); })
        .def("get_name_token", &UsdPropertyProxy::get_name_token)
        .def("get_display_name", &UsdPropertyProxy::get_display_name)
        .def("get_display_group", &UsdPropertyProxy::get_display_group)
        .def("get_allowed_tokens", &UsdPropertyProxy::get_allowed_tokens)
        .def("get_documentation", &UsdPropertyProxy::get_documentation)
        .def("get_display_widget", &UsdPropertyProxy::get_display_widget)
        .def("get_display_widget_hints", &UsdPropertyProxy::get_display_widget_hints)
        .def("get_all_metadata", &UsdPropertyProxy::get_all_metadata)
        .def("get_metadata", &get_metadata)
        .def("set_metadata", &set_metadata)
        .def("get_sources", &UsdPropertyProxy::get_sources, return_value_policy::reference)
        .def("append_source", overload_cast<const UsdPropertySource&>(&UsdPropertyProxy::append_source), arg("source"))
        .def("append_source", overload_cast<const TfToken&, const TfType&>(&UsdPropertyProxy::append_source), arg("source_token"), arg("source_type"))
        .def("get_property", &UsdPropertyProxy::get_property)
        .def("get_attribute", &UsdPropertyProxy::get_attribute)
        .def("get_relationship", &UsdPropertyProxy::get_relationship)
        .def("get_type", &UsdPropertyProxy::get_type)
        .def("get_prim", &UsdPropertyProxy::get_prim)
        .def("is_authored", &UsdPropertyProxy::is_authored)
        .def("__repr__", &property_proxy_repr);
}

OPENDCC_NAMESPACE_CLOSE
