/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/opendcc.h"
#include <atomic>
#include <memory>

OPENDCC_NAMESPACE_OPEN

namespace scene_outliner
{
    // Copies let callbacks and workers observe cancellation without retaining their owner.
    class CancellationToken
    {
    public:
        bool is_cancelled() const { return m_cancelled->load(std::memory_order_acquire); }
        void cancel() const { m_cancelled->store(true, std::memory_order_release); }
        CancellationToken restart()
        {
            cancel();
            m_cancelled = std::make_shared<std::atomic<bool>>(false);
            return *this;
        }

    private:
        std::shared_ptr<std::atomic<bool>> m_cancelled = std::make_shared<std::atomic<bool>>(false);
    };
}

OPENDCC_NAMESPACE_CLOSE
