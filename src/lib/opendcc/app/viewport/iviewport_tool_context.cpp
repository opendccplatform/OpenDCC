// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/iviewport_tool_context.h"
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.tools");

PXR_NAMESPACE_USING_DIRECTIVE;

ViewportToolContextRegistry& ViewportToolContextRegistry::instance()
{
    static ViewportToolContextRegistry instance;
    return instance;
}

bool ViewportToolContextRegistry::register_tool_context(const TfToken& context, const TfToken& name, ViewportToolContextRegistryCallback callback)
{
    if (!callback)
    {
        OPENDCC_ERROR("Failed to register tool context '{}': factory is empty.", name.GetText());
        return false;
    }
    auto& self = instance();
    auto& context_map = self.m_registry_map[context];

    auto iter = context_map.find(name);
    if (iter != context_map.end())
    {
        OPENDCC_ERROR("Failed to register tool context for context '{}' with name '{}': context with specified name is already registered.",
                      context.GetText(), name.GetText());
        return false;
    }
    context_map.emplace(name, callback);
    return true;
}

bool ViewportToolContextRegistry::unregister_tool_context(const TfToken& context, const TfToken& name)
{
    auto& self = instance();
    auto context_iter = self.m_registry_map.find(context);
    if (context_iter == self.m_registry_map.end())
    {
        OPENDCC_WARN("Failed to unregister tool context for context '{}' with name '{}': specified context doesn't exist.", context.GetText(),
                     name.GetText());
        return false;
    }

    auto name_iter = context_iter->second.find(name);
    if (name_iter == context_iter->second.end())
    {
        OPENDCC_WARN("Failed to unregister tool context for context '{}' with name '{}': specified tool context not found.", context.GetText(),
                     name.GetText());
        return false;
    }

    context_iter->second.erase(name_iter);
    return true;
}

IViewportToolContext* ViewportToolContextRegistry::create_tool_context(const TfToken& context, const TfToken& name)
{
    auto& self = instance();
    auto context_iter = self.m_registry_map.find(context);
    if (context_iter == self.m_registry_map.end())
    {
        OPENDCC_ERROR("Failed to create tool context for context '{}' with name '{}': specified context doesn't exist.", context.GetText(),
                      name.GetText());
        return nullptr;
    }

    auto name_iter = context_iter->second.find(name);
    if (name_iter == context_iter->second.end())
    {
        OPENDCC_ERROR("Failed to create tool context for context '{}' with name '{}': specified tool context not found.", context.GetText(),
                      name.GetText());
        return nullptr;
    }

    return name_iter->second();
}

OPENDCC_NAMESPACE_CLOSE
