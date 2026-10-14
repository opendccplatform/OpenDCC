// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/render/render_app.h"
#include "opendcc/usd/render/render_app_controller.h"
#include <opendcc/base/logging/logger.h>
#include <opendcc/app/core/application.h>
#include <opendcc/render_system/irender.h>
#include <QApplication>
#include <opendcc/app/viewport/viewport_scene_context.h>
#include "opendcc/usd/render/ipc_render.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL("opendcc.usd.render");
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    bool get_time_code_ranges(const std::vector<std::string>& frame_ranges, std::vector<UsdUtilsTimeCodeRange>& time_code_ranges)
    {
        time_code_ranges.resize(frame_ranges.size());
        for (int i = 0; i < frame_ranges.size(); ++i)
        {
            auto converted = UsdUtilsTimeCodeRange::CreateFromFrameSpec(frame_ranges[i]);
            if (!converted.IsValid())
            {
                OPENDCC_ERROR("Failed to convert string '{}' to a valid time code range.", frame_ranges[i]);
                return false;
            }
            time_code_ranges[i] = converted;
        }
        return true;
    }
};

UsdRenderApp::UsdRenderApp(std::unique_ptr<CLI::App> app, const PXR_NS::TfToken& render_type)
    : m_app(std::move(app))
    , m_render_type(render_type)
{
    m_app->add_option("--type,-t", m_common_options.type, "Render type: preview/ipr/disk");
    m_app->add_option("--transferred_layers", m_common_options.transferred_layers, "JSON description of transferred layers");
    m_app->add_option("--frame,-f", m_common_options.frame, "Frame to render");
    m_app->add_option("--stage_file", m_common_options.stage_file, "File to stage")->expected(1);
}

int UsdRenderApp::exec(int argc, char* argv[])
{
    CLI11_PARSE(*m_app, argc, argv);

    // init extensions and packages
    Application& app = Application::instance();
    std::vector<std::string> dummy_py_args;
    app.init_python(dummy_py_args);
    app.initialize_extensions();

    auto [time_ranges, render_method, process_status] = handle_common_args();
    if (process_status != 0)
    {
        return process_status;
    }

    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
    QApplication qt_app(argc, argv);

    auto controller = RenderAppControllerFactory::get_instance().make_app_controller(m_render_type);
    if (!TF_AXIOM(controller))
    {
        return -1;
    }
    controller->process_args(*m_app);

    render_system::RenderStatus status;
    auto viewport_scene_context = ViewportSceneContextRegistry::get_instance().create_scene_context(m_render_type);
    switch (render_method)
    {
    case render_system::RenderMethod::PREVIEW:
    {
        status = preview_render(viewport_scene_context, time_ranges);
        break;
    }
    case render_system::RenderMethod::IPR:
    {
        status = ipr_render(viewport_scene_context, time_ranges);
        break;
    }
    case render_system::RenderMethod::DISK:
    {
        status = disk_render(viewport_scene_context, time_ranges);
        break;
    }
    }

    app.uninitialize_extensions();

    return status == render_system::RenderStatus::FINISHED ? 0 : static_cast<int>(status);
}

UsdRenderApp::CommonArgsHandling UsdRenderApp::handle_common_args()
{
    // Open stage to render
    if (m_common_options.stage_file.empty())
    {
        return m_app->exit(CLI::Error("Error", "Empty file to stage"));
        ;
    }

    auto& app = Application::instance();
    app.get_session()->open_stage(m_common_options.stage_file);

    // Transfer dirty USD changes
    std::vector<PXR_NS::SdfLayerRefPtr> layers_cache;
    if (!m_common_options.transferred_layers.empty())
    {
        auto tmp_folder = std::filesystem::path(m_common_options.transferred_layers).parent_path().string();

        auto file_stream = std::ifstream(m_common_options.transferred_layers);
        if (!file_stream.is_open())
        {
            std::cout << "Failed to open transfer layers file. File path is : " << m_common_options.transferred_layers;
            app.uninitialize_extensions();
            return -1;
        }

        PXR_NS::JsParseError err;
        const auto value = PXR_NS::JsParseStream(file_stream, &err);
        if (!value.IsObject())
        {
            std::cout << "Transfer layers file has error : " << m_common_options.transferred_layers << err.line << err.column << err.reason.c_str();

            app.uninitialize_extensions();
            return -1;
        }

        std::unordered_map<std::string, std::string> layers_map;
        for (const auto& entry : value.GetJsObject())
        {
            layers_map[entry.first] = tmp_folder + entry.second.GetString();
        }

        for (auto item : layers_map)
        {
            if (item.first.empty())
            {
                continue;
            }

            if (PXR_NS::SdfLayerRefPtr layer = PXR_NS::SdfLayer::FindOrOpen(item.first))
            {
                layers_cache.push_back(layer);
                if (PXR_NS::SdfLayerRefPtr copy_layer = PXR_NS::SdfLayer::FindOrOpen(item.second))
                {
                    layer->TransferContent(copy_layer);
                }
            }
        }
    }

    CommonArgsHandling result;
    // Specify render type
    result.render_method = render_system::RenderMethod::PREVIEW;
    if (!m_common_options.type.empty())
    {
        if (m_common_options.type == "ipr")
        {
            result.render_method = render_system::RenderMethod::IPR;
        }
        else if (m_common_options.type == "disk")
        {
            result.render_method = render_system::RenderMethod::DISK;
        }
    }

    // parse time code ranges
    if (!get_time_code_ranges(m_common_options.frame, result.time_ranges))
    {
        std::cout << "Failed to parse time ranges.\n";
        return static_cast<int>(render_system::RenderStatus::FAILED);
    }

    if (result.time_ranges.empty())
    {
        result.time_ranges = { UsdUtilsTimeCodeRange { 0.0 } };
    }

    return result;
}

OPENDCC_NAMESPACE_CLOSE
