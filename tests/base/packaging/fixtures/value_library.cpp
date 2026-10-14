/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/packaging/package_attributes.h"
#include "opendcc/base/export.h"

OPENDCC_NAMESPACE_OPEN

extern "C" OPENDCC_API_EXPORT void produce_package_value(std::any* result, bool use_factory)
{
    PackageAnyArray values { true,
                             int64_t { 42 },
                             2.5,
                             std::string("text"),
                             std::any(),
                             PackageAnyDictionary { { "key", int64_t { 7 } } },
                             PackageAnyArray { int64_t { 8 } } };
    PackageAnyDictionary dictionary { { "nested", std::move(values) } };
    *result = use_factory ? std::move(*copy_package_value(dictionary)) : std::any(std::move(dictionary));
}

extern "C" OPENDCC_API_EXPORT bool check_package_value(const std::any* value)
{
    const auto dictionary = std::any_cast<PackageAnyDictionary>(value);
    if (!dictionary)
        return false;
    const auto nested = dictionary->find("nested");
    if (nested == dictionary->end())
        return false;
    const auto array = std::any_cast<PackageAnyArray>(&nested->second);
    if (!array || array->size() != 7)
        return false;
    const auto boolean = std::any_cast<bool>(&array->at(0));
    const auto integer = std::any_cast<int64_t>(&array->at(1));
    const auto number = std::any_cast<double>(&array->at(2));
    const auto text = std::any_cast<std::string>(&array->at(3));
    const auto table = std::any_cast<PackageAnyDictionary>(&array->at(5));
    const auto list = std::any_cast<PackageAnyArray>(&array->at(6));
    return boolean && *boolean && integer && *integer == 42 && number && *number == 2.5 && text && *text == "text" && !array->at(4).has_value() &&
           table && std::any_cast<int64_t>(table->at("key")) == 7 && list && std::any_cast<int64_t>(list->at(0)) == 8;
}

OPENDCC_NAMESPACE_CLOSE
