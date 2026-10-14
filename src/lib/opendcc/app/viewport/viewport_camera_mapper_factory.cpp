// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/viewport_camera_mapper_factory.h"
#include "opendcc/base/logging/logger.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.viewport.camera");

PXR_NAMESPACE_USING_DIRECTIVE

void ViewportCameraMapperFactory::register_camera_mapper(const TfToken& name, std::function<ViewportCameraMapperPtr()> mapper_creator)
{
    if (!mapper_creator)
    {
        OPENDCC_ERROR("Failed to insert camera mapper factory function. The function is empty.");
        return;
    }

    auto insert_result = get_instance().m_registry.emplace(name, mapper_creator);
    if (!insert_result.second)
        OPENDCC_ERROR("Failed to insert camera mapper factory function. The camera mapper with the same name already exists.");
}

void ViewportCameraMapperFactory::unregister_camera_mapper(const TfToken& name)
{
    if (!get_instance().m_registry.erase(name))
    {
        OPENDCC_WARN("Failed to unregister camera mapper factory function. The factory with the name '{}' doesn't exist.", name.GetText());
    }
}

ViewportCameraMapperPtr ViewportCameraMapperFactory::create_camera_mapper(const TfToken& name)
{
    auto& self = get_instance();
    auto iter = self.m_registry.find(name);
    if (iter == self.m_registry.end())
    {
        if (name == TfToken("USD") || self.m_registry.find(TfToken("USD")) == self.m_registry.end())
        {
            OPENDCC_ERROR("Failed to find camera mapper '{}': no USD fallback is registered.", name.GetText());
            return nullptr;
        }
        OPENDCC_WARN("Failed to find camera mapper creator with the name '{}': return 'USD' mapper", name.GetText());
        return create_camera_mapper(TfToken("USD"));
    }

    return iter->second();
}

ViewportCameraMapperFactory& ViewportCameraMapperFactory::get_instance()
{
    static ViewportCameraMapperFactory instance;
    return instance;
}

OPENDCC_NAMESPACE_CLOSE
