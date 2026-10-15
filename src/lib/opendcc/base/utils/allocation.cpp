// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "allocation.h"
#include <limits>

OPENDCC_NAMESPACE_OPEN

namespace base::utils
{

    std::tuple<std::string, bool> dynamic_alloc_read(size_t init_size, const std::function<bool(char*, size_t&)>& alloc_callback)
    {
        std::unique_ptr<char, std::default_delete<char[]>> buffer;
        buffer.reset(new char[init_size]);

        auto cur_size = init_size;
        while (!alloc_callback(buffer.get(), cur_size))
        {
            if (cur_size == std::numeric_limits<size_t>::max())
            {
                return std::make_tuple<std::string, bool>({}, false);
            }
            buffer.reset(new char[cur_size]);
        }

        return { buffer.get(), true };
    }

}

OPENDCC_NAMESPACE_CLOSE
