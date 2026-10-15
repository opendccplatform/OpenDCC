/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    template <class TFn>
    class ScopeGuard
    {
    public:
        ScopeGuard(TFn&& func) noexcept
            : m_func(std::move(func))
        {
        }
        ScopeGuard(const TFn& func) noexcept
            : m_func(func)
        {
        }
        ScopeGuard() = delete;
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard(ScopeGuard&& other) noexcept
            : m_func(other.m_func)
            , m_dismiss(other.m_dismiss)
        {
            other.m_func = nullptr;
            other.m_dismiss = false;
        }

        ScopeGuard& operator=(const ScopeGuard&) = delete;
        ScopeGuard& operator=(ScopeGuard&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            m_func = std::move(other.m_dismiss);
            m_dismiss = other.m_dismiss;
            other.m_func = nullptr;
            other.m_dismiss = true;
            return *this;
        }

        ~ScopeGuard()
        {
            if (!m_dismiss)
            {
                m_func();
            }
        }

        void dismiss(bool dismiss) noexcept { m_dismiss = dismiss; }

    private:
        TFn m_func;
        bool m_dismiss = false;
    };

}

OPENDCC_NAMESPACE_CLOSE
