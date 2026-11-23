/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "opendcc/ui/scene_outliner/item_id.h"
#include <functional>
#include <type_traits>

OPENDCC_NAMESPACE_OPEN

/** @brief Item predicate used when expanding or collapsing a branch. */
class SceneOutlinerItemPredicate
{
public:
    SceneOutlinerItemPredicate() = default;

    template <class Function, std::enable_if_t<std::is_invocable_r_v<bool, Function, const SceneItemId&>, int> = 0>
    SceneOutlinerItemPredicate(Function function)
        : m_function(std::move(function))
    {
    }

    explicit operator bool() const { return static_cast<bool>(m_function); }
    bool operator()(const SceneItemId& item) const { return m_function && m_function(item); }

private:
    std::function<bool(const SceneItemId&)> m_function;
};

OPENDCC_NAMESPACE_CLOSE
