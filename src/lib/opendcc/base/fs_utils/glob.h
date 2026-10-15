/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/utils/api.h"
#include "opendcc/base/utils/expected.h"
#include <string>
#include <system_error>
#include <vector>

OPENDCC_NAMESPACE_OPEN

namespace base::fs_utils
{

    enum class GlobErrorCode
    {
        /** Access to a searched path was denied. */
        PERMISSION_DENIED,
        /** Discovery could not allocate memory. */
        OUT_OF_MEMORY,
        /** Another filesystem operation failed. */
        IO_ERROR
    };

    struct GlobDiagnostic
    {
        GlobErrorCode code;
        std::string path;
        std::error_code system_error;
    };

    using GlobPaths = std::vector<std::string>;
    using GlobDiagnostics = std::vector<GlobDiagnostic>;

    /** @brief Failed glob discovery, including matches collected before or alongside errors. */
    struct GlobError
    {
        GlobPaths partial_matches;
        GlobDiagnostics diagnostics;
    };

    using GlobResult = base::utils::Expected<GlobPaths, GlobError>;

    /** @brief Expand filesystem wildcards and mark directory matches with a trailing slash.
     * Windows uses native '*' and '?' wildcards; bracket expressions are literal. Unix uses system glob syntax.
     * Unmatched patterns succeed with no paths. Duplicate matches are retained. Results are sorted
     * per pattern on Unix and across all patterns on Windows. Errors retain partial matches and
     * diagnostics containing the affected path and error code.
     */
    OPENDCC_BASE_UTILS_API GlobResult glob_paths(const GlobPaths& patterns);

}

OPENDCC_NAMESPACE_CLOSE
