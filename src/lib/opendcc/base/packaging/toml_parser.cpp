// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#define CPPTOML_NO_RTTI
#include "opendcc/base/packaging/toml_parser.h"
#include <fstream>
#include "opendcc/base/vendor/cpptoml/cpptoml.h"
#include "opendcc/base/logging/logger.h"
#include <unordered_map>
#include <vector>
#include <filesystem>

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{
    namespace
    {
        std::any parse_val(cpptoml::base& val)
        {
            switch (val.type())
            {
            case cpptoml::base_type::STRING:
                return std::any(val.as<std::string>()->get());
            case cpptoml::base_type::INT:
                return std::any(val.as<int64_t>()->get());
            case cpptoml::base_type::FLOAT:
                return std::any(val.as<double>()->get());
            case cpptoml::base_type::BOOL:
                return std::any(val.as<bool>()->get());
            case cpptoml::base_type::ARRAY:
            {
                const auto& vals_array = val.as_array()->get();
                PackageAnyArray res_array;
                for (const auto& val : vals_array)
                {
                    res_array.push_back(parse_val(*val));
                }
                return std::any(res_array);
            }
            case cpptoml::base_type::TABLE:
            {
                const auto& vals_table = val.as_table();
                PackageAnyDictionary res_table;
                for (const auto& val : *vals_table)
                {
                    res_table[std::string(val.first)] = parse_val(*val.second);
                }
                return std::any(res_table);
            }
            case cpptoml::base_type::TABLE_ARRAY:
            {
                const auto& vals_table_arr = val.as_table_array();
                PackageAnyArray res_table_arr;
                for (const auto& val : *vals_table_arr)
                {
                    res_table_arr.push_back(parse_val(*val));
                }
                return std::any(std::move(res_table_arr));
            }
                // rest not implemented yet: date
            default:
                return std::any();
            }
        }
    };

    PackageData TOMLParser::parse(const std::string& path)
    {
        auto result = PackageData();
        std::shared_ptr<cpptoml::table> table;
        try
        {
            table = cpptoml::parse_file(path);
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

        result.path = std::filesystem::u8path(path).parent_path().generic_u8string();
        const auto name_opt = table->get_qualified_as<std::string>("base.name");
        if (!name_opt || name_opt->empty())
        {
            OPENDCC_ERROR("Failed to parse package at path '{}': 'base.name' must be a non-empty string.", path);
            return result;
        }
        result.name = *name_opt;

        for (const auto& val_entry : *table)
        {
            PackageAttribute attr;
            attr.name = val_entry.first;
            attr.value = parse_val(*val_entry.second);
            result.raw_attributes.push_back(std::move(attr));
        }

        return result;
    }

}

OPENDCC_NAMESPACE_CLOSE
