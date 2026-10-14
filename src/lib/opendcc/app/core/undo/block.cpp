// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/undo/block.h"
#include "opendcc/base/logging/logger.h"
#include <stdexcept>

#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/undo/stack.h"

#include "opendcc/app/core/application.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.undo");

PXR_NAMESPACE_USING_DIRECTIVE

using namespace commands;

UsdEditsUndoBlock::UsdEditsUndoBlock()
{
    auto& router = UndoRouter::instance();
    TF_VERIFY(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        if (router.m_inversion.size() != 0)
        {
            OPENDCC_ERROR("Opening fragmented undo block. This may be because of an undo "
                          "command running inside of an edit block.");
            throw std::logic_error("Cannot open a fragmented undo block.");
        }
    }
    router.m_depth++;
}

UsdEditsUndoBlock::~UsdEditsUndoBlock()
{
    auto& router = UndoRouter::instance();
    router.m_depth--;
    TF_VERIFY(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        if (router.m_inversion.size() != 0)
        {
            UndoStackNotice(router.take_inversions()).Send();
        }
    }
}

UsdEditsBlock::UsdEditsBlock()
{
    auto& router = UndoRouter::instance();
    TF_VERIFY(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        if (router.m_inversion.size() != 0)
        {
            OPENDCC_ERROR("Opening fragmented undo block. This may be because of an undo "
                          "command running inside of an edit block.");
            throw std::logic_error("Cannot open a fragmented undo block.");
        }
    }
    router.m_depth++;
}

std::unique_ptr<UndoInverse> UsdEditsBlock::take_edits()
{
    return UndoRouter::instance().take_inversions();
}

UsdEditsBlock::~UsdEditsBlock()
{
    auto& router = UndoRouter::instance();
    router.m_depth--;
    TF_VERIFY(router.m_depth >= 0);
    if (router.m_depth == 0)
    {
        router.m_inversion.clear();
    }
}

OPENDCC_NAMESPACE_CLOSE
