// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/block.h"
#include "opendcc/base/commands_api/core/router.h"

#include <cassert>

OPENDCC_NAMESPACE_OPEN

UndoCommandBlock::UndoCommandBlock(const std::string& block_name /* = "UndoCommandBlock" */)
{
    auto& router = CommandRouter::instance();
    assert(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        if (router.m_commands.size() != 0)
        {
            assert(false && "Opening fragmented command block");
        }

        router.m_block_name = block_name;
    }

    router.m_depth++;
}

UndoCommandBlock::~UndoCommandBlock()
{
    auto& router = CommandRouter::instance();
    router.m_depth--;
    assert(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        if (router.m_commands.size() != 0)
        {
            CommandRouter::create_group_command();
        }
    }
}

CommandBlock::CommandBlock()
{
    auto& router = CommandRouter::instance();
    assert(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        if (router.m_commands.size() != 0)
        {
            assert(false && "Opening fragmented command block");
        }
    }
    router.m_depth++;
}

CommandBlock::~CommandBlock()
{
    auto& router = CommandRouter::instance();
    router.m_depth--;
    assert(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        router.clear();
    }
}

OPENDCC_NAMESPACE_CLOSE
