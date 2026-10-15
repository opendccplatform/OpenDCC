/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/usd/render/api.h"
#include "opendcc/base/vendor/cli11/CLI11.hpp"
#include "opendcc/base/utils/noncopyable.h"
#include <pxr/base/tf/token.h>

OPENDCC_NAMESPACE_OPEN

struct RenderAppOption
{
    std::string name;
    std::string description;

    RenderAppOption(const std::string_view& name, const std::string_view& description)
        : name(name)
        , description(description)
    {
    }
};

class RenderAppController
{
public:
    virtual ~RenderAppController() {}
    virtual int process_args(const CLI::App& app) = 0;
};

class OPENDCC_USD_RENDER_API RenderAppControllerFactory : base::utils::Noncopyable
{
public:
    using FactoryFn = std::function<std::unique_ptr<RenderAppController>()>;
    static RenderAppControllerFactory& get_instance();

    void register_app_controller(const PXR_NS::TfToken& render_type, const FactoryFn& factory_fn);
    void unregister_app_controller(const PXR_NS::TfToken& render_type);
    std::unique_ptr<RenderAppController> make_app_controller(const PXR_NS::TfToken& render_type);

private:
    RenderAppControllerFactory();

    std::unordered_map<PXR_NS::TfToken, FactoryFn, PXR_NS::TfToken::HashFunctor> m_registry;
};

class UsdRenderAppController final : public RenderAppController
{
public:
    int process_args(const CLI::App& app) override;
};

OPENDCC_NAMESPACE_CLOSE
