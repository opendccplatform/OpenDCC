// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#define CPPTOML_NO_RTTI
#include "opendcc/base/packaging/toml_parser.h"
#include <fstream>
#include "opendcc/base/vendor/cpptoml/cpptoml.h"
#include "opendcc/base/logging/logger.h"
#include <unordered_map>
#include <vector>
#include "pxr/base/vt/array.h"
#include "opendcc/base/vendor/ghc/filesystem.hpp"

PXR_NAMESPACE_USING_DIRECTIVE
OPENDCC_NAMESPACE_OPEN
using namespace cpptoml;

namespace
{
    VtValue parse_val(base& val)
    {
        switch (val.type())
        {
        case base_type::STRING:
            return VtValue(val.as<std::string>()->get());
        case base_type::INT:
            return VtValue(val.as<int64_t>()->get());
        case base_type::FLOAT:
            return VtValue(val.as<double>()->get());
        case base_type::BOOL:
            return VtValue(val.as<bool>()->get());
        case base_type::ARRAY:
        {
            const auto& vals_array = val.as_array()->get();
            VtArray<VtValue> res_array;
            for (const auto& val : vals_array)
            {
                res_array.push_back(parse_val(*val));
            }
            return VtValue(res_array);
        }
        case base_type::TABLE:
        {
            const auto& vals_table = val.as_table();
            VtDictionary res_table;
            for (const auto& val : *vals_table)
            {
                res_table[std::string(val.first)] = parse_val(*val.second);
            }
            return VtValue(res_table);
        }
        case base_type::TABLE_ARRAY:
        {
            const auto& vals_table_arr = val.as_table_array();
            VtArray<VtDictionary> res_table_arr;
            for (const auto& val : *vals_table_arr)
            {
                res_table_arr.push_back(parse_val(*val).Get<VtDictionary>());
            }
            return VtValue(std::move(res_table_arr));
        }
            // rest not implemented yet: date
        default:
            return VtValue();
        }
    }
};

PackageData TOMLParser::parse(const std::string& path)
{
    auto result = PackageData();
    std::shared_ptr<cpptoml::table> table;
    try
    {
        table = parse_file(path);
    }
    catch (cpptoml::parse_exception e)
    {
        OPENDCC_ERROR("Failed to parse package at path '{}': {}", path, e.what());
        return result;
    }

    if (!table)
    {
        return result;
    }

    result.path = ghc::filesystem::path(path).parent_path().generic_string();
    if (auto name_opt = table->get_qualified_as<std::string>("base.name"))
    {
        result.name = *name_opt;
    }
    else
    {
        return result;
    }

    for (const auto& val_entry : *table)
    {
        PackageAttribute attr;
        attr.name = val_entry.first;
        attr.value = parse_val(*val_entry.second);
        result.raw_attributes.push_back(std::move(attr));
    }

    return result;
}

OPENDCC_NAMESPACE_CLOSE
