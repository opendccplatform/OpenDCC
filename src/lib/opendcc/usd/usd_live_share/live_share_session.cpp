// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/usd_live_share/live_share_session.h"
#include "opendcc/base/utils/process.h"

#include <pxr/usd/usdGeom/pointBased.h>
#include <filesystem>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

LiveShareSession::LiveShareSession(PXR_NS::UsdStageRefPtr stage, const ShareEditsContext::ConnectionSettings& connection_settings)
    : m_stage(stage)
    , m_connection_settings(connection_settings)
{
    auto tmp_folder_path = std::filesystem::temp_directory_path() / ("opendcc_live_share_" + base::utils::get_pid_string());
    std::filesystem::create_directories(tmp_folder_path);
    auto usd_tmp_folder = tmp_folder_path / "usd";
    std::filesystem::create_directories(usd_tmp_folder);
    m_layer_transfer_path = tmp_folder_path.u8string();
}

LiveShareSession::~LiveShareSession()
{
    stop_share();
}

void LiveShareSession::start_share()
{
    m_layer_tree_watcher = std::make_shared<LayerTreeWatcher>(m_stage);
    m_layer_state_delegates = std::make_shared<LayerStateDelegatesHolder>(m_layer_tree_watcher);
    m_context =
        std::make_unique<ShareEditsContext>(m_stage, m_layer_transfer_path, m_layer_tree_watcher, m_layer_state_delegates, m_connection_settings);
}

void LiveShareSession::stop_share()
{
    m_context = nullptr;
    m_layer_state_delegates = nullptr;
    m_layer_tree_watcher = nullptr;
}

void OPENDCC_NAMESPACE::LiveShareSession::process()
{
    m_context->process();
}

OPENDCC_NAMESPACE_CLOSE
