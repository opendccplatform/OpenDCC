/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include "opendcc/opendcc.h"
#include "opendcc/base/logging/logger.h"
#include "opendcc/app/core/api.h"
#include <pxr/base/js/json.h>
#include <pxr/base/vt/dictionary.h>
#include <pxr/base/js/converter.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>
#include "opendcc/base/vendor/eventpp/eventdispatcher.h"
#include <type_traits>
#include <opendcc/base/vendor/jsoncpp/json.h>
#include <opendcc/base/vendor/nonstd/any.hpp>

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.settings");

namespace details
{
    namespace settings
    {
        template <class T, class U = void>
        struct underlying_type
        {
            using type = T;
        };

        template <class T>
        struct underlying_type<T, std::enable_if_t<std::is_same<long double, std::decay_t<T>>::value>>
        {
            using type = double;
        };

        template <class T>
        struct underlying_type<T, std::enable_if_t<std::is_same<const char*, std::decay_t<T>>::value>>
        {
            using type = std::string;
        };

        template <class T>
        struct underlying_type<T, std::enable_if_t<std::is_same<char*, std::decay_t<T>>::value>>
        {
            using type = std::string;
        };

    };
};

/**
*
*   @brief A class for managing settings with serialization and deserialization capabilities.
*
*   The Settings class allows the user to manage application settings with support for serialization
*   and deserialization capabilities. It provides functions for registering setting changed callbacks,
*   setting and getting values of different types, and retrieving the raw JSON value of a setting.
*   The class also provides template functions for registering and handling different types of settings.

*   If a setting starts with the string "session", it will not be serialized. This can be useful for
*   settings that are specific to a particular session and should not persist beyond the lifetime of that session.
*/
class OPENDCC_API Settings
{
public:
    /**
     * @brief A function for serializing a value of type `std::any` to JSON.
     */
    using SerializeFn = std::function<Json::Value(const nonstd::any&)>;
    /**
     * @brief A function for deserializing a value of type `Json::Value` to `std::any`.
     */
    using DeserializeFn = std::function<nonstd::any(const Json::Value&)>;
    /**
     * @brief An enum class for specifying the type of change that occurred to a setting.
     */
    enum class ChangeType
    {
        REMOVED, ///< The setting was removed.
        RESET, ///< The setting was reset to its default value.
        UPDATED ///< The setting was updated with a new value.
    };
    /**
     * @brief A type alias for storing the value of a setting.
     */
    using ValueHolder = Json::Value;
    /**
     * @brief A class for representing the value of a setting.
     */
    class Value
    {
    public:
        /**
         * @brief Default constructor.
         */
        Value() = default;
        /**
         * @brief Constructor that initializes the value with a `ValueHolder`.
         *
         * @param[in] value The value to initialize the `Value` object with.
         */
        Value(const ValueHolder& value)
            : m_value(value)
        {
        }

        /**
         * @brief Attempts to retrieve the value of the setting and store it in the specified variable.
         * @tparam T The type of the variable to store the value in.
         * @param[out] result Pointer to the variable to store the retrieved value in.
         * @return `true` if the value was successfully retrieved and stored, `false` otherwise.
         */
        template <class T>
        bool try_get(typename details::settings::underlying_type<T>::type* result) const
        {
            using TDecayed = typename details::settings::underlying_type<T>::type;
            if (!m_value.empty())
            {
                auto held_type_data = Settings::s_type_helpers.find(typeid(TDecayed));
                if (held_type_data != Settings::s_type_helpers.end())
                {
                    const auto result_val = held_type_data->second.from_json(m_value);
                    if (result_val.type() == typeid(TDecayed))
                    {
                        *result = nonstd::any_cast<TDecayed>(result_val);
                        return true;
                    }
                }
            }
            return false;
        }
        /**
         * @brief Retrieves the value of the setting and returns it.
         * @tparam T The type of the value to retrieve.
         * @param[in] fallback_value The value to return if the setting value cannot be retrieved.
         * @return The retrieved value, or the fallback value if the setting value cannot be retrieved.
         *
         * If the value cannot be retrieved, the specified fallback value is returned instead.
         */
        template <class T>
        auto get(T&& fallback_value = T()) const -> typename details::settings::underlying_type<T>::type
        {
            using TDecayed = typename details::settings::underlying_type<T>::type;
            TDecayed result;
            if (try_get<TDecayed>(&result))
                return std::move(result);
            return fallback_value;
        }
        /**
         * @brief Conversion operator that returns `true` if the value is not empty, `false` otherwise.
         */
        operator bool() const { return !m_value.empty(); }

    private:
        ValueHolder m_value;
    };

    using SettingChangedCallback = void(const std::string&, const Value&, ChangeType);
    using SettingChangedDispatcher = eventpp::CallbackList<SettingChangedCallback>;
    using SettingChangedHandle = SettingChangedDispatcher::Handle;

    /**
     * @brief Default constructor.
     *
     * This constructor creates a new instance of the `Settings` class. The object created this way ignores serialization capabilities.
     */
    Settings();
    /**
     * @brief Construct a new `Settings` object with a settings path.
     * @param settings_path The path to load the settings from.
     *
     * This constructor creates a new instance of the `Settings` class and loads the
     * settings from the specified path.
     */
    Settings(const std::string& settings_path);
    /**
     * @brief Register a callback function to be called when a setting is changed.
     * @param path The path of the setting to monitor.
     * @param callback The callback function to register.
     * @return A handle to the registered callback function.
     *
     * This function registers a callback function to be called when a setting at the specified
     * path or descendants are changed. The callback function should have the following signature:
     * `void(const std::string&, const Settings::Value&, ChangeType)`.
     */
    SettingChangedHandle register_setting_changed(const std::string& path, const std::function<SettingChangedCallback>& callback);
    /**
     * @brief Register a callback function to be called when a setting is changed.
     * @param path The path of the setting to monitor.
     * @param callback The callback function to register.
     * @return A handle to the registered callback function.
     *
     * This function registers a callback function to be called when a setting at the specified
     * path or descendants are changed. The callback function should have the following signature: `void(const std::string&, const Settings::Value&)`.
     */
    SettingChangedHandle register_setting_changed(const std::string& path, const std::function<void(const std::string&, const Value&)>& callback);
    /**
     * @brief Register a callback function to be called when a setting is changed.
     * @param path The path of the setting to monitor.
     * @param callback The callback function to register.
     * @return A handle to the registered callback function.
     *
     * This function registers a callback function to be called when a setting at the specified
     * path or descendants are changed. The callback function should have the following signature: `void(const Settings::Value&)`.
     */
    SettingChangedHandle register_setting_changed(const std::string& path, const std::function<void(const Value&)>& callback);
    /**
     * @brief Register a callback function to be called when a setting is changed.
     * @param path The path of the setting to monitor.
     * @param callback The callback function to register.
     * @return A handle to the registered callback function.
     *
     * This function registers a callback function to be called when a setting at the specified
     * path or descendants are changed. The callback function should have the following signature: `void()`.
     */
    SettingChangedHandle register_setting_changed(const std::string& path, const std::function<void()>& callback);
    /**
     * @brief Unregister a callback function for a setting.
     * @param path The path of the setting the callback was registered for.
     * @param handle The handle to the callback function to unregister.
     *
     * This function unregisters a callback function that was previously registered for a setting.
     */
    void unregister_setting_changed(const std::string& path, SettingChangedHandle handle);

    /**
     * @brief Sets the value of a setting.
     * @tparam T The type of the setting value.
     * @param path The path of the setting.
     * @param value The value of the setting.
     *
     * This function sets the value of a setting with the specified path.
     * If the setting already exists, its value will be updated to the specified value.
     * If the specified value cannot be converted to JSON, a runtime error will be thrown.
     * If the type of the setting is unknown, a runtime error will be thrown.
     * If the path starts with "session", the setting will not be serialized.
     *
     * Example usage:
     * @code{.cpp}
     * settings.set<int>("foo", 42);
     * settings.set("session.bar", 84);
     * @endcode
     */
    template <class T>
    void set(const std::string& path, const T& value)
    {
        using TDecayed = typename details::settings::underlying_type<T>::type;
        auto converter = s_type_helpers.find(typeid(TDecayed));
        if (converter == s_type_helpers.end())
        {
            PXR_NAMESPACE_USING_DIRECTIVE
            OPENDCC_ERROR("Attempt to set unknown type. Try register this type via register_type<T> method.");
            return;
        }
        set(path, converter->second.to_json(nonstd::make_any<TDecayed>(value)), typeid(TDecayed));
    }
    /**
     * @brief Sets the default value of a setting.
     * @tparam T The type of the setting value.
     * @param path The path of the setting.
     * @param value The default value of the setting.
     *
     * This function sets the default value of a setting with the specified path.
     * If the setting already exists, its default value will be updated to the specified value.
     * If the specified value cannot be converted to JSON, a runtime error will be thrown.
     * If the type of the setting is unknown, a runtime error will be thrown.
     * If the path starts with "session", the setting will not be serialized.
     *
     * Example usage:
     * @code{.cpp}
     * settings.set_default<int>("foo", 42);
     * settings.set_default("session.bar", 84);
     * @endcode
     */
    template <class T>
    void set_default(const std::string& path, const T& value)
    {
        using TDecayed = typename details::settings::underlying_type<T>::type;
        auto converter = s_type_helpers.find(typeid(TDecayed));
        if (converter == s_type_helpers.end())
        {
            PXR_NAMESPACE_USING_DIRECTIVE
            OPENDCC_ERROR("Attempt to set unknown type. Try register this type via register_type<T> method.");
            return;
        }
        set_default(path, converter->second.to_json(nonstd::make_any<TDecayed>((value))), typeid(TDecayed));
    }

    /**
     * @brief Gets the value of a setting.
     * @tparam T The type of the setting value.
     * @param path The path of the setting.
     * @param fallback_value The fallback value if the setting doesn't exist or its type cannot be converted to T.
     * @return The value of the setting.
     *
     * This function gets the value of a setting with the specified path.
     * If the setting doesn't exist or its type cannot be converted to T, the fallback value will be returned.
     * If the type of the setting is unknown, a runtime error will be thrown.
     *
     * Example usage:
     * @code{.cpp}
     * auto foo = settings.get<int>("foo", 0);
     * @endcode
     */
    template <class T>
    auto get(const std::string& path, const T& fallback_value = T()) const -> typename details::settings::underlying_type<T>::type
    {
        using TDecayed = typename details::settings::underlying_type<T>::type;
        auto converter = s_type_helpers.find(typeid(TDecayed));
        if (converter == s_type_helpers.end())
        {
            PXR_NAMESPACE_USING_DIRECTIVE
            OPENDCC_ERROR("Attempt to get unknown type. Try register this type via register_type<T> method.");
            return fallback_value;
        }
        const auto result = get_impl(path);
        if (!result.empty())
        {
            const auto holder = converter->second.from_json(result);
            if (holder.type() == typeid(TDecayed))
                return nonstd::any_cast<TDecayed>(holder);
        }
        return fallback_value;
    }
    /**
     * @brief Gets the default value of a setting.
     * @tparam T The type of the setting value.
     * @param path The path of the setting.
     * @param fallback_value The fallback value if the setting doesn't exist or its type cannot be converted to T.
     * @return The value of the setting.
     *
     * This function gets the default value of a setting with the specified path.
     * If the setting doesn't exist or its type cannot be converted to T, the fallback value will be returned.
     * If the type of the setting is unknown, a runtime error will be thrown.
     *
     * Example usage:
     * @code{.cpp}
     * auto foo = settings.get_default<int>("foo", 0);
     * @endcode
     */
    template <class T>
    auto get_default(const std::string& path, const T& fallback_value = T()) const -> typename details::settings::underlying_type<T>::type
    {
        using TDecayed = typename details::settings::underlying_type<T>::type;
        auto converter = s_type_helpers.find(typeid(TDecayed));
        if (converter == s_type_helpers.end())
        {
            PXR_NAMESPACE_USING_DIRECTIVE
            OPENDCC_ERROR("Attempt to get unknown type. Try register this type via register_type<T> method.");
            return fallback_value;
        }
        const auto result = get_default_impl(path);
        if (!result.empty())
        {
            const auto holder = converter->second.from_json(result);
            if (holder.type() == typeid(TDecayed))
                return nonstd::any_cast<TDecayed>(holder);
        }
        return fallback_value;
    }
    /**
     * @brief Gets the raw JSON value of a setting.
     * @param path The path of the setting.
     * @return The raw JSON value of the setting.
     *
     * This function retrieves the raw JSON value of a setting with the specified path.
     * If the setting doesn't exist, an empty JSON value will be returned.
     *
     * Example usage:
     * @code{.cpp}
     * Json::Value foo = settings.get_raw("foo");
     * @endcode
     */
    Json::Value get_raw(const std::string& path) const;
    /**
     * @brief Resets the value of a setting and its children.
     * @param path The path of the setting to reset.
     *
     * This function resets the value of a setting with the specified path and all its children settings to its default value.
     * If the setting is not found, this function does nothing.
     * If the setting is found, its value will be set to the default value if it is defined, otherwise it will be removed.
     * This function will notify all registered observers of the change with ChangeType::RESET.
     *
     * Example usage:
     * @code{.cpp}
     * settings.reset("foo.bar");
     * @endcode
     */
    void reset(const std::string& path);
    /**
     * @brief Removes the value of a setting and its children.
     * @param path The path of the setting to remove.
     *
     * This function removes the value of a setting with the specified path and all its children settings.
     * If the setting is not found, this function does nothing.
     * This function will notify all registered observers of the change with ChangeType::REMOVED.
     *
     * Example usage:
     * @code{.cpp}
     * settings.remove("foo.bar");
     * @endcode
     */
    void remove(const std::string& path);
    /**
     * @brief Checks if a setting with the specified path or its children are exist.
     *
     * @param path The path of the setting to check.
     * @return True if a setting with the specified path or its children are exist, false otherwise.
     */
    bool has(const std::string& path) const;
    /**
     * @brief Returns the separator character used in the path of settings.
     *
     * This function returns the separator character used in the path of settings.
     * The separator character is '.' by default.
     *
     * @return The separator character used in the path of settings.
     */
    static char get_separator();
    /**
     * @brief Registers serialization and deserialization functions for a type.
     * @tparam T The type to register serialization and deserialization functions for.
     * @param serialize_fn A function that serializes objects of type T stored in `std::any` to Json::Value.
     * @param deserialize_fn A function that deserializes objects of type Json::Value to type T stored in `std::any`.
     *
     * This function is used to register serialization and deserialization functions for a specific type T.
     */
    template <class T>
    static void register_type(SerializeFn serialize_fn, DeserializeFn deserialize_fn)
    {
        using type = std::decay_t<T>;
        auto& type_helper = s_type_helpers[typeid(type)];
        type_helper.to_json = serialize_fn;
        type_helper.from_json = deserialize_fn;
    }

private:
    struct TypeHelpers
    {
        SerializeFn to_json;
        DeserializeFn from_json;
    };

    void notify_change(const std::string& path, const ValueHolder& value, ChangeType event_type) const;
    void set(const std::string& path, const ValueHolder& value, const std::type_info& type);
    void set_default(const std::string& path, const ValueHolder& value, const std::type_info& type);
    void set_impl(const std::string& path, const ValueHolder& value, std::unordered_map<std::string, ValueHolder>& collection,
                  const std::type_info& type);

    ValueHolder get_impl(const std::string& path) const;
    ValueHolder get_default_impl(const std::string& path) const;
    ValueHolder get_impl(const std::string& path, const std::unordered_map<std::string, ValueHolder>& collection) const;
    bool is_valid_path(const std::string& path) const;

    void serialize() const;
    void deserialize();

    void set_value_at_path(const std::string& path, const ValueHolder& val);
    void remove_value_at_path(const std::string& path);

    std::unordered_map<std::string, ValueHolder> m_defaults;
    std::unordered_map<std::string, ValueHolder> m_values;
    std::unordered_map<std::string, SettingChangedDispatcher> m_dispatchers;
    Json::Value m_json_root;
    std::string m_settings_file;

    static std::unordered_map<std::type_index, TypeHelpers> s_type_helpers;
};

OPENDCC_NAMESPACE_CLOSE
