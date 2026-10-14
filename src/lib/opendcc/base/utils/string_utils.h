/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"

#include "opendcc/base/utils/api.h"

#include <string>
#include <vector>
#include <algorithm>

OPENDCC_NAMESPACE_OPEN

/**
 * @brief Splits a string into the substrings delimited by the specified character.
 * @param string The string to be split.
 * @param separator The character to use as the delimiter.
 * @return The substrings.
 */
UTILS_API std::vector<std::string> split(const std::string& string, char separator);
/** @brief Split a string, optionally omitting empty substrings. */
UTILS_API std::vector<std::string> split(const std::string& string, char separator, bool skip_empty);

UTILS_API bool starts_with(const std::string_view& input, const std::string_view& prefix);

template <class TPredicate>
void trim_left_if(std::string& input, const TPredicate& predicate)
{
    input.erase(input.begin(), std::find_if(input.begin(), input.end(), [predicate](auto c) { return !predicate(c); }));
}

template <class TPredicate>
void trim_right_if(std::string& input, const TPredicate& predicate)
{
    input.erase(std::find_if(input.rbegin(), input.rend(), [predicate](auto c) { return !predicate(c); }).base(), input.end());
}

template <class TPredicate>
void trim_if(std::string& input, const TPredicate& predicate)
{
    trim_left_if(input, predicate);
    trim_right_if(input, predicate);
}

OPENDCC_NAMESPACE_CLOSE
