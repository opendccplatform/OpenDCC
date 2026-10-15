/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/packaging/api.h"
#include "opendcc/base/utils/expected.h"
#include <any>
#include <cstdint>
#include <map>
#include <functional>
#include <string>
#include <vector>

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    using PackageAnyDictionary = std::map<std::string, std::any, std::less<>>;
    using PackageAnyArray = std::vector<std::any>;

    enum class PackageValueError
    {
        /** The value contains a payload outside packaging's supported types. */
        UNSUPPORTED_TYPE,
        /** The requested dotted path is absent or cannot be traversed. */
        NOT_FOUND
    };

    template <class T>
    using PackageValueResult = base::utils::Expected<T, PackageValueError>;

    using PackageAttributeResult = PackageValueResult<std::reference_wrapper<const std::any>>;

    /** @brief Rebuild supported values recursively in packaging so they can outlive the caller DLL.
     * @note Call while the source DLL is loaded; consumers must use the same C++ ABI and runtime.
     * @return An owned value, or UNSUPPORTED_TYPE for an unsupported payload at any depth.
     */
    OPENDCC_BASE_PACKAGING_API PackageValueResult<std::any> copy_package_value(const std::any& value);

    /** @brief Find an attribute through dot-delimited dictionary keys; return NOT_FOUND if absent.
     * @note The result is borrowed; the dictionary and matching entry must remain alive and unchanged.
     */
    OPENDCC_BASE_PACKAGING_API PackageAttributeResult find_package_attribute(const PackageAnyDictionary& dictionary, const std::string& path);
    /** @brief Set an attribute through dot-delimited keys, creating intermediate dictionaries.
     * @return UNSUPPORTED_TYPE on failure, leaving the dictionary unchanged.
     */
    OPENDCC_BASE_PACKAGING_API PackageValueResult<void> set_package_attribute(PackageAnyDictionary& dictionary, const std::string& path,
                                                                              std::any value);

}

OPENDCC_NAMESPACE_CLOSE
