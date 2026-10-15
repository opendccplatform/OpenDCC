// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/package_attributes.h"
#include "opendcc/base/utils/string_utils.h"

OPENDCC_NAMESPACE_OPEN

namespace base::packaging
{

    using base::utils::Unexpected;

    PackageValueResult<std::any> copy_package_value(const std::any& value)
    {
        if (!value.has_value())
            return std::any();
        if (const auto item = std::any_cast<bool>(&value))
            return std::any(*item);
        if (const auto item = std::any_cast<int64_t>(&value))
            return std::any(*item);
        if (const auto item = std::any_cast<double>(&value))
            return std::any(*item);
        if (const auto item = std::any_cast<std::string>(&value))
            return std::any(*item);
        if (const auto items = std::any_cast<PackageAnyDictionary>(&value))
        {
            PackageAnyDictionary result;
            for (const auto& item : *items)
            {
                auto copied = copy_package_value(item.second);
                if (!copied)
                    return Unexpected<PackageValueError>(copied.error());
                result[item.first] = std::move(*copied);
            }
            return std::any(std::move(result));
        }
        if (const auto items = std::any_cast<PackageAnyArray>(&value))
        {
            PackageAnyArray result;
            result.reserve(items->size());
            for (const auto& item : *items)
            {
                auto copied = copy_package_value(item);
                if (!copied)
                    return Unexpected<PackageValueError>(copied.error());
                result.push_back(std::move(*copied));
            }
            return std::any(std::move(result));
        }
        return Unexpected<PackageValueError>(PackageValueError::UNSUPPORTED_TYPE);
    }

    PackageAttributeResult find_package_attribute(const PackageAnyDictionary& dictionary, const std::string& path)
    {
        const auto keys = utils::split(path, '.', true);
        const auto* current = &dictionary;
        const std::any* value = nullptr;
        for (size_t index = 0; index < keys.size(); ++index)
        {
            const auto it = current->find(keys[index]);
            if (it == current->end())
                return Unexpected<PackageValueError>(PackageValueError::NOT_FOUND);
            value = &it->second;
            if (index + 1 < keys.size())
            {
                current = std::any_cast<PackageAnyDictionary>(value);
                if (!current)
                    return Unexpected<PackageValueError>(PackageValueError::NOT_FOUND);
            }
        }
        if (!value)
            return Unexpected<PackageValueError>(PackageValueError::NOT_FOUND);
        return std::cref(*value);
    }

    PackageValueResult<void> set_package_attribute(PackageAnyDictionary& dictionary, const std::string& path, std::any value)
    {
        auto copied = copy_package_value(value);
        if (!copied)
            return Unexpected<PackageValueError>(copied.error());
        const auto keys = utils::split(path, '.', true);
        auto* current = &dictionary;
        for (size_t index = 0; index < keys.size(); ++index)
        {
            auto& entry = (*current)[keys[index]];
            if (index + 1 == keys.size())
                entry = std::move(*copied);
            else
            {
                if (!std::any_cast<PackageAnyDictionary>(&entry))
                    entry = PackageAnyDictionary();
                current = std::any_cast<PackageAnyDictionary>(&entry);
            }
        }
        return {};
    }

}

OPENDCC_NAMESPACE_CLOSE
