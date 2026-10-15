/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "opendcc/opendcc.h"
#include "opendcc/base/commands_api/core/api.h"

#include <typeindex>
#include <string>
#include <vector>
#include <unordered_map>
#include <tuple>

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    class TypeIndex
    {
    public:
        TypeIndex() = default;
        TypeIndex(const TypeIndex&) = default;
        TypeIndex(TypeIndex&&) = default;
        TypeIndex& operator=(const TypeIndex&) = default;
        TypeIndex& operator=(TypeIndex&&) = default;
        TypeIndex(const std::type_index& type_index)
            : m_type_index(type_index)
        {
        }
        TypeIndex(const std::type_info& type_info)
            : m_type_index(type_info)
        {
        }

        operator std::type_index() const { return m_type_index; }
        bool operator==(const TypeIndex& other) const { return m_type_index == other.m_type_index; }
        bool operator!=(const TypeIndex& other) const { return !(*this == other); }

    private:
        std::type_index m_type_index = typeid(nullptr);
    };

    using TypeIndices = std::vector<TypeIndex>;

    namespace detail
    {
        template <class T, class... Types>
        struct has_type;

        template <class T, class... Types>
        struct has_type<T, T, Types...> : std::true_type
        {
        };

        template <class T>
        struct has_type<T> : std::false_type
        {
        };

        template <class T, class U, class... Types>
        struct has_type<T, U, Types...> : has_type<T, Types...>
        {
        };

        template <class T>
        void fill_type_index_vector(TypeIndices& result)
        {
            result.push_back(typeid(std::decay_t<T>));
        }

        template <class T, class U, class... Types>
        void fill_type_index_vector(TypeIndices& result)
        {
            fill_type_index_vector<T>(result);
            fill_type_index_vector<U, Types...>(result);
        }

        template <class... Types>
        TypeIndices make_type_index_vector()
        {
            static_assert(sizeof...(Types) > 0 && !has_type<void, Types...>::value,
                          "Argument descriptor doesn't contains any types or contains type 'void'");
            TypeIndices result;
            fill_type_index_vector<Types...>(result);
            return result;
        }
    };

    class OPENDCC_BASE_COMMANDS_API CommandSyntax
    {
    public:
        struct ArgDescriptor
        {
            std::string name;
            TypeIndices type_indices;
            std::string description;

            ArgDescriptor() = default;
            ArgDescriptor(const ArgDescriptor&) = default;
            ArgDescriptor(ArgDescriptor&&) = default;
            ArgDescriptor& operator=(const ArgDescriptor&) = default;
            ArgDescriptor& operator=(ArgDescriptor&&) = default;

            ArgDescriptor(const std::string& name, const TypeIndices& type_indices, const std::string& description)
                : name(name)
                , type_indices(type_indices)
                , description(description)
            {
            }
            bool is_valid() const { return !type_indices.empty(); }
        };

        CommandSyntax();
        CommandSyntax(const CommandSyntax&) = default;
        CommandSyntax(CommandSyntax&&) = default;
        CommandSyntax& operator=(const CommandSyntax&) = default;
        CommandSyntax& operator=(CommandSyntax&&) = default;

        CommandSyntax& arg(const std::string& name, const TypeIndices& arg_types, const std::string& description = "");
        template <class... T>
        CommandSyntax& arg(const std::string& name, const std::string& description = "")
        {
            return arg(name, detail::make_type_index_vector<T...>(), description);
        }
        CommandSyntax& kwarg(const std::string& name, const TypeIndices& arg_types, const std::string& description = "");
        template <class... T>
        CommandSyntax& kwarg(const std::string& name, const std::string& description = "")
        {
            return kwarg(name, detail::make_type_index_vector<T...>(), description);
        }

        CommandSyntax& result(std::type_index result_type, const std::string& description = "");
        template <class T>
        CommandSyntax& result(const std::string& description = "")
        {
            return result(typeid(T), description);
        }

        CommandSyntax& description(const std::string& description);

        const ArgDescriptor& get_result_descriptor() const;
        const ArgDescriptor& get_arg_descriptor(uint32_t pos) const;
        const ArgDescriptor& get_kwarg_descriptor(const std::string& name) const;

        bool has_arg(const std::string& name) const;

        const std::vector<ArgDescriptor>& get_arg_descriptors() const;
        const std::unordered_map<std::string, ArgDescriptor>& get_kwarg_descriptors() const;
        const std::string& get_command_description() const;

    private:
        std::vector<ArgDescriptor> m_arg_descriptors;
        std::unordered_map<std::string, ArgDescriptor> m_kwarg_descriptors;
        ArgDescriptor m_result_descriptor;

        std::string m_cmd_description;
    };
}

OPENDCC_NAMESPACE_CLOSE
