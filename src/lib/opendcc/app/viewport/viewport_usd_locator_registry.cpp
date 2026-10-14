// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_usd_locator_registry.h"
#include "opendcc/base/logging/logger.h"
#include "pxr/usd/usdLux/domeLight.h"
#include "opendcc/app/viewport/viewport_light_locators.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.locators");

PXR_NAMESPACE_USING_DIRECTIVE

ViewportUsdLocatorPtr ViewportUsdLocatorRegistry::create_locator(ViewportLocatorDelegate* delegate, const PXR_NS::UsdPrim& prim)
{
    if (!delegate)
        return nullptr;

    auto& self = instance();
    const auto& type_name = prim.GetTypeName();
    auto iter = self.m_registry.find(type_name);
    if (iter == self.m_registry.end())
        return nullptr;
    return iter->second(delegate, prim);
}

bool ViewportUsdLocatorRegistry::register_locator_factory(const TfToken& type, LocatorFactoryFn fn)
{
    if (!fn)
    {
        OPENDCC_ERROR("Failed to register locator factory for type '{}': factory is null.", type.GetText());
        return false;
    }

    auto& self = instance();
    auto iter = self.m_registry.find(type);
    if (iter != self.m_registry.end())
    {
        OPENDCC_ERROR("Failed to register locator factory for type '{}': factory is already registered.", type.GetText());
        return false;
    }

    self.m_registry[type] = fn;
    return true;
}

bool ViewportUsdLocatorRegistry::unregister_locator_factory(const TfToken& type)
{
    auto& self = ViewportUsdLocatorRegistry::instance();
    auto iter = self.m_registry.find(type);
    if (iter == self.m_registry.end())
    {
        OPENDCC_WARN("Failed to unregister locator factory for type '{}': factory not found.", type.GetText());
        return false;
    }
    self.m_registry.erase(iter);
    return true;
}

bool ViewportUsdLocatorRegistry::has_factory(const TfToken& type)
{
    return instance().m_registry.find(type) != instance().m_registry.end();
}

ViewportUsdLocatorRegistry& ViewportUsdLocatorRegistry::instance()
{
    static ViewportUsdLocatorRegistry instance;
    return instance;
}

OPENDCC_NAMESPACE_CLOSE