// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/viewport/usd_render.h"

#include "opendcc/app/core/session.h"
#include "opendcc/usd/usd_live_share/live_share_edits.h"
#include "opendcc/usd/usd_live_share/live_share_session.h"
#include "opendcc/app/viewport/render_process.h"

#include <filesystem>

#include <pxr/base/js/json.h>

#include "opendcc/base/utils/process.h"

#include <atomic>
#include <fstream>
#include <iomanip>
#include <functional>
#include <mutex>

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

using namespace render_system;
UsdRender::UsdRender(const RenderCmdFn& render_cmd_fn)
    : m_render_cmd_fn(render_cmd_fn)
{
    TF_AXIOM(m_render_cmd_fn);
}

UsdRender::~UsdRender()
{
    stop_render();
    if (!m_tmp_dir.empty())
        std::filesystem::remove_all(m_tmp_dir);
}

void UsdRender::set_attributes(const RenderAttributes& attributes)
{
    for (auto it : attributes)
        m_attributes[it.first] = it.second;
}

void UsdRender::create_render_catalog()
{
    const auto t = std::time(nullptr);
    const auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S %d-%m-%Y");
    m_log_data.catalog = oss.str();
    m_log_data.catalog_data = std::make_shared<CatalogData>();
    m_log_data.catalog_data->frame_time = static_cast<float>(Application::instance().get_current_time());
    RenderCatalog::instance().create_new_catalog(m_log_data.catalog, m_log_data.catalog_data);
}

void UsdRender::update_render_cmd(const std::string& stage_path, const std::string& transfer_layer_cfg)
{
    m_render_cmd = m_render_cmd_fn();
    m_render_cmd += " --stage_file \"" + stage_path + "\" ";
    switch (m_render_method)
    {
    case render_system::RenderMethod::DISK:
        m_render_cmd += "--type disk ";
        break;
    case render_system::RenderMethod::IPR:
        m_render_cmd += "--type ipr ";
        break;
    default:
        m_render_cmd += "--type preview ";
        break;
    }

    if (!transfer_layer_cfg.empty())
        m_render_cmd += "--transferred_layers \"" + transfer_layer_cfg + "\" ";

    auto time_range_attr = m_attributes.find("time_range");
    if (time_range_attr != m_attributes.end())
    {
        m_render_cmd += "-f " + std::get<std::string>(time_range_attr->second);
    }
}

std::filesystem::path UsdRender::write_transfer_layers() const
{
    auto usd_tmp_folder = m_tmp_dir / "usd";

    auto layer_tree = Application::instance().get_session()->get_current_stage_layer_tree();
    auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
    {
        return {};
    }

    for (const auto& layer : stage->GetLayerStack(false))
    {
        if (layer->IsAnonymous())
        {
            OPENDCC_WARN("Current stage contains anonymous layers which are not currently supported. Render result might be inaccurate.");
            break;
        }
    }

    std::unordered_map<std::string, std::string> layers_remap;
    for (auto layer : layer_tree->get_all_layers())
    {
        if (!layer->IsDirty() || layer->IsAnonymous())
            continue;

        const auto base_name = TfGetBaseName(layer->GetRealPath());
        auto path = usd_tmp_folder / std::filesystem::u8path(base_name);
        layers_remap[layer->GetIdentifier()] = "/usd/" + base_name;
        layer->Export(path.u8string());
    }

    if (layers_remap.empty())
        return std::filesystem::path();
    auto config_path = (m_tmp_dir / "usd_layer_transfer_content.json");
    auto file_stream = std::ofstream(config_path);
    if (!file_stream.is_open())
        return {};

    JsWriter writer(file_stream, JsWriter::Style::Pretty);
    writer.BeginObject();
    for (auto item : layers_remap)
        writer.WriteKeyValue(item.first, item.second);
    writer.EndObject();
    return config_path;
}

void UsdRender::init_temp_dir()
{
    try
    {
        if (!m_tmp_dir.empty())
        {
            std::filesystem::remove_all(m_tmp_dir);
            m_tmp_dir.clear();
        }

        static std::atomic_ullong render_id(0);
        const auto id = render_id++;

        m_tmp_dir = std::filesystem::temp_directory_path() / ("hydra_render_" + base::utils::get_pid_string() + '_' + std::to_string(id));
        std::filesystem::create_directories(m_tmp_dir);
        auto usd_tmp_folder = m_tmp_dir / "usd";
        std::filesystem::create_directories(usd_tmp_folder);
    }
    catch (const std::filesystem::filesystem_error& e)
    {
        RenderCatalog::instance().add_msg(m_log_data.catalog, "Failed to create temp folder " + m_tmp_dir.u8string() + " : " + e.what());
    }
}

bool UsdRender::start_render_impl()
{
    const auto ipr = m_render_method == render_system::RenderMethod::IPR;
    if (m_render_process)
    {
        m_render_process->stop();
    }
    m_render_process = std::make_unique<RenderProcess>(m_render_cmd, m_log_data.catalog, m_log_data.catalog_data);

    if (ipr)
    {
        ShareEditsContext::ConnectionSettings connection_settings;
        m_live_share = std::make_unique<LiveShareSession>(Application::instance().get_session()->get_current_stage(), connection_settings);
        m_live_share->start_share();
    }
    std::thread process_thread([this] { m_render_process->start(); });
    process_thread.detach();

    return true;
}

void UsdRender::finished(std::function<void(RenderStatus)> cb) {}

render_system::RenderStatus UsdRender::render_status()
{
    return m_render_process ? m_render_process->get_status() : RenderStatus::NOT_STARTED;
}

void UsdRender::wait_render()
{
    if (m_render_process)
    {
        m_render_process->wait();
    }
}

void UsdRender::update_render() {}

bool UsdRender::stop_render()
{
    if (m_render_process)
    {
        if (m_live_share)
        {
            m_live_share->stop_share();
            m_live_share.reset();
        }

        m_render_process.reset();
    }
    return true;
}

bool UsdRender::resume_render()
{
    return false;
}

bool UsdRender::pause_render()
{
    return false;
}

bool UsdRender::start_render()
{
    if (m_render_cmd.empty())
    {
        return false;
    }

    create_render_catalog();
    return start_render_impl();
}

bool UsdRender::init_render(RenderMethod type)
{
    stop_render();
    m_render_cmd.clear();
    m_render_method = type;
    if (type == render_system::RenderMethod::NONE)
        return false;

    auto stage = Application::instance().get_session()->get_current_stage();
    if (!stage)
        return false;

    init_temp_dir();
    if (m_tmp_dir.empty())
        return false;
    const auto cfg_path = write_transfer_layers();

    if (stage->GetRootLayer()->IsAnonymous())
    {
        return false;
    }
    update_render_cmd(stage->GetRootLayer()->GetRealPath(), cfg_path.u8string());
    return true;
}

OPENDCC_NAMESPACE_CLOSE
