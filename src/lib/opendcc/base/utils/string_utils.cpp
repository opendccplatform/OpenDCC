// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/utils/string_utils.h"

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    std::vector<std::string> split(const std::string& string, char separator)
    {
        return split(string, separator, false);
    }

    std::vector<std::string> split(const std::string& string, char separator, bool skip_empty)
    {
        std::vector<std::string> result;
        std::string::size_type last = 0;
        std::string::size_type find;
        while ((find = string.find(separator, last)) != std::string::npos)
        {
            if (!skip_empty || find != last)
                result.emplace_back(string, last, find - last);
            last = find + 1;
        }
        if (!skip_empty || last != string.size())
            result.emplace_back(string, last);
        return result;
    }

    bool starts_with(const std::string_view& input, const std::string_view& prefix)
    {
        return input.rfind(prefix, 0) == 0;
    }

}

OPENDCC_NAMESPACE_CLOSE
