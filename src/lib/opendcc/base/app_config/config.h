/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <opendcc/opendcc.h>
#define CPPTOML_NO_RTTI
#include <opendcc/base/vendor/cpptoml/cpptoml.h>
#include <opendcc/base/app_config/api.h>
#include <opendcc/base/logging/logger.h>

OPENDCC_NAMESPACE_OPEN

namespace base::app_config
{

    /**
     * @brief The ApplicationConfig class provides methods for managing the configuration of the application.
     *
     * The application configuration is read from a .toml file.
     */

    class OPENDCC_BASE_APP_CONFIG_API ApplicationConfig
    {
    public:
        /**
         * @brief Constructs a new ApplicationConfig using default values.
         *
         */
        ApplicationConfig() = default;

        ApplicationConfig(const ApplicationConfig&) = default;

        ApplicationConfig(ApplicationConfig&&) = default;
        /**
         * @brief Constructs a new ApplicationConfig from a .toml file.
         *
         * @param filename The path to the .toml file from which to load the configuration.
         */
        ApplicationConfig(const std::string& filename);

        ApplicationConfig& operator=(const ApplicationConfig&) = default;

        ApplicationConfig& operator=(ApplicationConfig&&) = default;

        /**
         * @brief Gets a value from the loaded configuration file with the specified key.
         *
         * @tparam T The type of the value to get.
         * @param key The key to look for the value under.
         * @param default_value The default value to return if the specified value is not found.
         * @return The value found under the key or a default value if not found.
         */
        template <class T>
        T get(const std::string& key, const T& default_value = T()) const
        {
            if (!is_valid())
            {
                OPENDCC_ERROR("Attempt to get value '{}' from in invalid config.", key);
                return default_value;
            }
            return m_config->get_qualified_as<T>(key).value_or(default_value);
        }

        /**
         * @brief Gets an array of values from the loaded configuration file with the specified key.
         *
         * @tparam T The type of the value to get.
         * @param key The key to look for the value under.
         * @param default_value The default value to return if the specified value is not found.
         * @return A vector of values found under the key or a default value if not found.
         */
        template <class T>
        std::vector<T> get_array(const std::string& key, const std::vector<T>& default_value = {}) const
        {
            if (!is_valid())
            {
                OPENDCC_ERROR("Attempt to get value '{}' from in invalid config.", key);
                return default_value;
            }
            return m_config->get_qualified_array_of<T>(key).value_or(default_value);
        }
        /**
         * @brief Checks whether the ApplicationConfig is valid.
         *
         * @return true if the configuration is valid, false otherwise.
         */
        bool is_valid() const;

        /**
         * @brief Get the table then was parsed from the TOML config file.
         *
         * @return A smart pointer to a table with raw data from the TOML file.
         */
        std::shared_ptr<cpptoml::table> get_raw() const { return m_config; }

    private:
        std::shared_ptr<cpptoml::table> m_config;
    };

}

OPENDCC_NAMESPACE_CLOSE
