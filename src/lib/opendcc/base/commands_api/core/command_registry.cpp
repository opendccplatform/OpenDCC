// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/commands_api/core/command_registry.h"

#include "opendcc/base/commands_api/core/command_interface.h"
#include "opendcc/base/commands_api/core/router.h"
#include "opendcc/base/utils/hash.h"
#include "opendcc/base/logging/logger.h"
#include <mutex>

OPENDCC_NAMESPACE_OPEN

namespace base::commands
{

    namespace
    {
        class bad_numeric_cast : public std::runtime_error
        {
        public:
            bad_numeric_cast(const char* msg)
                : std::runtime_error(msg)
            {
            }
        };

        template <class TFrom, class TTo>
        TTo numeric_cast(const TFrom& value)
        {
            static_assert(std::is_arithmetic_v<TFrom>, "TFrom must be a numeric type");
            static_assert(std::is_arithmetic_v<TTo>, "TTo must be a numeric type");

            try
            {
                // Check if TFrom is convertible to TTo without data loss
                if constexpr (std::is_integral_v<TFrom> && std::is_integral_v<TTo>)
                {
                    if (value < static_cast<TFrom>(std::numeric_limits<TTo>::min()) || value > static_cast<TFrom>(std::numeric_limits<TTo>::max()))
                    {
                        throw bad_numeric_cast("Integral numeric_cast out of range");
                    }
                }
                else if constexpr (std::is_floating_point_v<TFrom> && std::is_integral_v<TTo>)
                {
                    if (value < static_cast<TFrom>(std::numeric_limits<TTo>::min()) || value > static_cast<TFrom>(std::numeric_limits<TTo>::max()) ||
                        static_cast<TFrom>(static_cast<TTo>(value)) != value)
                    {
                        throw bad_numeric_cast("Floating-point to integral numeric_cast out of range or loss of precision");
                    }
                }
                else if constexpr (std::is_integral_v<TFrom> && std::is_floating_point_v<TTo>)
                {
                    // No range check needed for integral to floating-point (except for huge numbers)
                    if (value < static_cast<TFrom>(std::numeric_limits<TTo>::lowest()) || value > static_cast<TFrom>(std::numeric_limits<TTo>::max()))
                    {
                        throw bad_numeric_cast("Integral to floating-point numeric_cast out of range");
                    }
                }
                else if constexpr (std::is_floating_point_v<TFrom> && std::is_floating_point_v<TTo>)
                {
                    if (value < static_cast<TFrom>(std::numeric_limits<TTo>::lowest()) || value > static_cast<TFrom>(std::numeric_limits<TTo>::max()))
                    {
                        throw bad_numeric_cast("Floating-point numeric_cast out of range");
                    }
                }
            }
            catch (const bad_numeric_cast& e)
            {
                OPENDCC_ERROR("Failed to cast " + std::to_string(value) + " (" + typeid(TFrom).name() + ") to " + typeid(TTo).name() + ": " +
                              e.what());
                return TTo {};
            }

            return static_cast<TTo>(value);
        }

        template <class T>
        T extract_numeric_value(std::shared_ptr<CommandArgBase> value)
        {
            if (!value)
                return T();

            auto derived_value = std::dynamic_pointer_cast<CommandArg<T>>(value);
            if (!derived_value)
                return T();

            return derived_value->get_value();
        }

        template <class TFrom, class TTo>
        std::shared_ptr<CommandArgBase> safe_numeric_cast(std::shared_ptr<CommandArgBase> value)
        {
            TFrom from_value = extract_numeric_value<TFrom>(value);
            TTo to_value = numeric_cast<TFrom, TTo>(from_value);
            return std::make_shared<CommandArg<TTo>>(to_value);
        }

        template <class T, class U>
        struct BoolToFloatCast
        {
            static constexpr auto value = (std::is_same<T, bool>::value && std::is_floating_point<U>::value) ||
                                          (std::is_same<U, bool>::value && std::is_floating_point<T>::value);
        };

        template <class T, class U>
        static constexpr bool BoolFloatCast_v = BoolToFloatCast<T, U>::value;

        class ConverterRegistry
        {
        public:
            static bool is_convertible(const std::type_index& from, const std::type_index& to)
            {
                return from == to || s_conversions.find({ from, to }) != s_conversions.end();
            }

            static void register_conversion(const std::type_index& from, const std::type_index& to, CommandRegistry::ConversionFn conversion_fn)
            {
                s_conversions[ConversionPair(from, to)] = conversion_fn;
            }

            ConverterRegistry& operator=(const ConverterRegistry&) = delete;
            ConverterRegistry& operator=(ConverterRegistry&&) = delete;

        private:
            ConverterRegistry()
            {
                register_all_numeric_conversions<bool>();
                register_all_numeric_conversions<char>();
                register_all_numeric_conversions<unsigned char>();
                register_all_numeric_conversions<signed char>();
                register_all_numeric_conversions<short>();
                register_all_numeric_conversions<unsigned short>();
                register_all_numeric_conversions<int>();
                register_all_numeric_conversions<unsigned int>();
                register_all_numeric_conversions<long int>();
                register_all_numeric_conversions<unsigned long int>();
                register_all_numeric_conversions<long long int>();
                register_all_numeric_conversions<unsigned long long int>();
                register_all_numeric_conversions<float>();
                register_all_numeric_conversions<double>();
                register_all_numeric_conversions<long double>();
            }

            ConverterRegistry(const ConverterRegistry&) = delete;
            ConverterRegistry(ConverterRegistry&&) = delete;

            template <class T>
            void register_all_numeric_conversions()
            {
                register_numeric_conversion<T, bool>();
                register_numeric_conversion<T, char>();
                register_numeric_conversion<T, unsigned char>();
                register_numeric_conversion<T, signed char>();
                register_numeric_conversion<T, short>();
                register_numeric_conversion<T, unsigned short>();
                register_numeric_conversion<T, int>();
                register_numeric_conversion<T, unsigned int>();
                register_numeric_conversion<T, long int>();
                register_numeric_conversion<T, unsigned long int>();
                register_numeric_conversion<T, long long int>();
                register_numeric_conversion<T, unsigned long long int>();
                register_numeric_conversion<T, float>();
                register_numeric_conversion<T, double>();
                register_numeric_conversion<T, long double>();
            }

            // avoid implicit conversion bool <-> floating types
            // in most cases this is not intended
            template <class TFrom, class TTo>
            auto register_numeric_conversion() -> std::enable_if_t<!BoolFloatCast_v<TFrom, TTo>, void>
            {
                using FromDecayed = std::decay_t<TFrom>;
                using ToDecayed = std::decay_t<TTo>;
                if (typeid(FromDecayed) == typeid(ToDecayed))
                    return;

                register_conversion(typeid(FromDecayed), typeid(ToDecayed),
                                    [](std::shared_ptr<CommandArgBase> val) { return safe_numeric_cast<FromDecayed, ToDecayed>(val); });
                register_conversion(typeid(ToDecayed), typeid(FromDecayed),
                                    [](std::shared_ptr<CommandArgBase> val) { return safe_numeric_cast<ToDecayed, FromDecayed>(val); });
            }

            template <class TFrom, class TTo>
            auto register_numeric_conversion() -> std::enable_if_t<BoolFloatCast_v<TFrom, TTo>, void>
            {
                // empty
            }

            using ConversionPair = std::pair<std::type_index, std::type_index>;
            struct ConversionPairHash
            {
                size_t operator()(const ConversionPair& pair) const
                {
                    size_t result = pair.first.hash_code();
                    utils::hash_combine(result, pair.second.hash_code());
                    return result;
                }
            };

            static std::unordered_map<ConversionPair, CommandRegistry::ConversionFn, ConversionPairHash> s_conversions;
        };

        std::unordered_map<ConverterRegistry::ConversionPair, CommandRegistry::ConversionFn, ConverterRegistry::ConversionPairHash>
            ConverterRegistry::s_conversions;

    };

    void CommandRegistry::register_command_interface(CommandInterface& interface)
    {
        instance().m_command_APIs.push_back(interface);
        for (const auto& cmd : instance().m_command_registry)
            interface.register_command(cmd.first, cmd.second.syntax);
    }

    void CommandRegistry::unregister_command_interface(CommandInterface& interface)
    {
        auto& registry = instance();

        auto find = std::find_if(registry.m_command_APIs.begin(), registry.m_command_APIs.end(),
                                 [&interface](std::reference_wrapper<CommandInterface> api) { return &api.get() == &interface; });

        if (find != registry.m_command_APIs.end())
        {
            registry.m_command_APIs.erase(find);
        }
    }

    void CommandRegistry::register_command(const std::string& name, const CommandSyntax& syntax, FactoryFn factory_fn)
    {
        instance().m_command_registry[name] = { factory_fn, syntax };
        for (const auto& interface : instance().m_command_APIs)
            interface.get().register_command(name, syntax);
    }

    void CommandRegistry::unregister_command(const std::string& name)
    {
        instance().m_command_registry.erase(name);
        for (const auto& interface : instance().m_command_APIs)
            interface.get().unregister_command(name);
    }

    std::shared_ptr<Command> CommandRegistry::create_command(const std::string& name)
    {
        auto factory_iter = instance().m_command_registry.find(name);
        if (factory_iter == instance().m_command_registry.end())
            return nullptr;
        auto cmd = factory_iter->second.factory_fn();
        cmd->m_command_name = name;
        return cmd;
    }

    bool CommandRegistry::get_command_syntax(const std::string& name, CommandSyntax& result)
    {
        auto factory_iter = instance().m_command_registry.find(name);
        if (factory_iter == instance().m_command_registry.end())
        {
            result = CommandSyntax();
            return false;
        }
        result = factory_iter->second.syntax;
        return true;
    }

    bool CommandRegistry::is_convertible(const std::type_index& from, const std::type_index& to)
    {
        return ConverterRegistry::is_convertible(from, to);
    }

    void CommandRegistry::command_executed(const std::shared_ptr<Command>& cmd, const CommandArgs& args, const CommandResult& result)
    {
        if (CommandRouter::lock_execute())
        {
            CommandRouter::add_command(cmd);
        }
        else
        {
            for (const auto& interface : instance().m_command_APIs)
            {
                interface.get().on_command_execute(cmd, args, result);
            }
        }
    }

    CommandRegistry::CommandRegistry() {}

    CommandRegistry& CommandRegistry::instance()
    {
        static CommandRegistry _instance;
        return _instance;
    }

    void CommandRegistry::register_conversion_impl(const std::type_index& from, const std::type_index& to, const ConversionFn& conversion_fn)
    {
        ConverterRegistry::register_conversion(from, to, conversion_fn);
    }

}

OPENDCC_NAMESPACE_CLOSE
