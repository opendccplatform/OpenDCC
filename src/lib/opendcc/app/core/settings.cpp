// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/opendcc.h"
#include "opendcc/app/core/settings.h"

#include <pxr/base/tf/getenv.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/error.h>
#include <fstream>

#include "opendcc/app/core/application.h"

OPENDCC_NAMESPACE_OPEN

namespace
{
    static const std::string session_prefix = "session";

    bool path_starts_with(const std::string& path, const std::string& prefix)
    {
        if (path.size() < prefix.size())
            return false;

        for (size_t i = 0; i < prefix.size(); i++)
        {
            if (path[i] != prefix[i])
                return false;
        }

        return (path.size() == prefix.size()) || (path[prefix.size()] == Settings::get_separator());
    }
};

PXR_NAMESPACE_USING_DIRECTIVE

Settings::Settings()
{
    static std::once_flag flag;
    std::call_once(flag, [] {
#define REGISTER_VALUE_TYPE(val_type, cond, extract)                                                                                              \
    Settings::register_type<val_type>([](const nonstd::any& val) { return Json::Value(static_cast<val_type>(nonstd::any_cast<val_type>(val))); }, \
                                      [](const Json::Value& val) {                                                                                \
                                          if (cond)                                                                                               \
                                              return nonstd::make_any<val_type>(static_cast<val_type>(extract));                                  \
                                          return nonstd::any();                                                                                   \
                                      })
#define REGISTER_VECTOR_TYPE(val_type, cond, extract)                          \
    Settings::register_type<std::vector<val_type>>(                            \
        [](const nonstd::any& val) {                                           \
            const auto vector = nonstd::any_cast<std::vector<val_type>>(val);  \
            Json::Value result(Json::ValueType::arrayValue);                   \
            result.resize(vector.size());                                      \
            for (auto i = 0; i < vector.size(); i++)                           \
                result[i] = static_cast<val_type>(vector[i]);                  \
            return result;                                                     \
        },                                                                     \
        [](const Json::Value& json_val) {                                      \
            if (!json_val.isArray())                                           \
                return nonstd::any();                                          \
            if (!json_val.empty())                                             \
            {                                                                  \
                const auto& val = json_val[0];                                 \
                if (!cond)                                                     \
                    return nonstd::any();                                      \
            }                                                                  \
            std::vector<val_type> result(json_val.size());                     \
            for (auto i = 0; i < json_val.size(); i++)                         \
            {                                                                  \
                const auto& val = json_val[i];                                 \
                result[i] = static_cast<val_type>(extract);                    \
            }                                                                  \
            return nonstd::make_any<std::vector<val_type>>(std::move(result)); \
        })

#define REGISTER_TYPE(val_type, cond, extract)    \
    REGISTER_VALUE_TYPE(val_type, cond, extract); \
    REGISTER_VECTOR_TYPE(val_type, cond, extract);
        REGISTER_TYPE(bool, val.isBool(), val.asBool());
        REGISTER_TYPE(uint8_t, val.isUInt(), val.asUInt());
        REGISTER_TYPE(uint16_t, val.isUInt(), val.asUInt());
        REGISTER_TYPE(uint32_t, val.isUInt(), val.asUInt());
        REGISTER_TYPE(uint64_t, val.isUInt(), val.asUInt());
        REGISTER_TYPE(int8_t, val.isInt(), val.asInt());
        REGISTER_TYPE(int16_t, val.isInt(), val.asInt());
        REGISTER_TYPE(int32_t, val.isInt(), val.asInt());
        REGISTER_TYPE(int64_t, val.isInt(), val.asInt());
        REGISTER_TYPE(float, val.isDouble(), val.asDouble());
        REGISTER_TYPE(double, val.isDouble(), val.asDouble());
        REGISTER_TYPE(std::string, val.isString(), val.asString());
#undef REGISTER_TYPE
#undef REGISTER_VALUE_TYPE
#undef REGISTER_VECTOR_TYPE
    });
}

Settings::Settings(const std::string& settings_path)
    : Settings()
{
    m_settings_file = settings_path;
    Json::Value root;
    std::ifstream file(m_settings_file);
    if (!file.is_open())
    {
        TF_WARN("Failed to open application settings file. The settings file will be recreated.");
        return;
    }
    Json::CharReaderBuilder builder;
    std::string errs;
    if (!parseFromStream(builder, file, &m_json_root, &errs) || !errs.empty())
    {
        TF_RUNTIME_ERROR("Settings parse error: %s", errs.c_str());
        m_settings_file.clear();
        return;
    }
    deserialize();
}

Settings::SettingChangedHandle Settings::register_setting_changed(const std::string& path, const std::function<SettingChangedCallback>& callback)
{
    return m_dispatchers[path].append(callback);
}

Settings::SettingChangedHandle Settings::register_setting_changed(const std::string& path, const std::function<void()>& callback)
{
    return m_dispatchers[path].append([callback](const std::string&, const Value&, ChangeType) { callback(); });
}

Settings::SettingChangedHandle Settings::register_setting_changed(const std::string& path, const std::function<void(const Value&)>& callback)
{
    return m_dispatchers[path].append([callback](const std::string&, const Value& val, ChangeType) { callback(val); });
}

Settings::SettingChangedHandle Settings::register_setting_changed(const std::string& path,
                                                                  const std::function<void(const std::string&, const Value&)>& callback)
{
    return m_dispatchers[path].append([callback](const std::string& path, const Value& val, ChangeType) { callback(path, val); });
}

void Settings::unregister_setting_changed(const std::string& path, SettingChangedHandle handle)
{
    auto iter = m_dispatchers.find(path);
    if (iter == m_dispatchers.end())
        return;
    iter->second.remove(handle);
}

Settings::ValueHolder Settings::get_raw(const std::string& path) const
{
    return get_impl(path);
}

void Settings::reset(const std::string& path)
{
    if (!is_valid_path(path))
        return;
    bool should_serialize = false;
    const auto persistent = !path_starts_with(path, session_prefix);
    for (auto it = m_values.begin(); it != m_values.end();)
    {
        if (path_starts_with(it->first, path))
        {
            const auto cur_path = it->first;
            auto default_val = m_defaults.find(cur_path);
            if (persistent)
            {
                if (default_val != m_defaults.end())
                    set_value_at_path(cur_path, default_val->second);
                else
                    remove_value_at_path(cur_path);
                should_serialize = true;
            }
            it = m_values.erase(it);
            notify_change(cur_path, default_val != m_defaults.end() ? default_val->second : ValueHolder(), ChangeType::RESET);
        }
        else
        {
            ++it;
        }
    }
    if (should_serialize)
        serialize();
}

void Settings::remove(const std::string& path)
{
    if (!is_valid_path(path))
        return;
    std::unordered_set<std::string> removed;
    for (auto it = m_values.begin(); it != m_values.end();)
    {
        if (path_starts_with(it->first, path))
        {
            removed.insert(it->first);
            it = m_values.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = m_defaults.begin(); it != m_defaults.end();)
    {
        if (path_starts_with(it->first, path))
        {
            removed.insert(it->first);
            it = m_defaults.erase(it);
        }
        else
        {
            ++it;
        }
    }

    const auto should_serialize = !path_starts_with(path, session_prefix) && !removed.empty();
    for (const auto& entry : removed)
    {
        notify_change(entry, ValueHolder(), ChangeType::REMOVED);
        if (should_serialize)
            remove_value_at_path(entry);
    }

    if (should_serialize)
        serialize();
}

bool Settings::has(const std::string& path) const
{
    if (!is_valid_path(path))
        return false;

    for (const auto& dict : { m_defaults, m_values })
    {
        for (const auto& entry : dict)
        {
            if (path_starts_with(entry.first, path))
                return true;
        }
    }

    return false;
}

char Settings::get_separator()
{
    return '.';
}

void Settings::notify_change(const std::string& path, const ValueHolder& value, ChangeType event_type) const
{
    auto current_path = path;
    while (true)
    {
        auto iter = m_dispatchers.find(current_path);
        if (iter != m_dispatchers.end())
            iter->second(path, value, event_type);
        const auto sep_pos = current_path.rfind(get_separator());
        if (sep_pos == std::string::npos)
            break;
        current_path = current_path.substr(0, sep_pos);
    };
}

void Settings::set(const std::string& path, const ValueHolder& value, const std::type_info& type)
{
    set_impl(path, value, m_values, type);
}

void Settings::set_default(const std::string& path, const ValueHolder& value, const std::type_info& type)
{
    set_impl(path, value, m_defaults, type);
}

void Settings::set_impl(const std::string& path, const ValueHolder& value, std::unordered_map<std::string, ValueHolder>& collection,
                        const std::type_info& type)
{
    if (value.isObject())
    {
        TF_RUNTIME_ERROR("Failed to set setting at path '%s': json object values are not supported.", path.c_str());
        return;
    }
    if (value.isNull())
    {
        TF_RUNTIME_ERROR("Failed to set setting at path '%s': json value is null.", path.c_str());
        return;
    }
    auto iter = collection.find(path);
    if (iter != collection.end())
    {
        if (iter->second == value)
            return;
        iter->second = value;
    }
    else
    {
        if (!is_valid_path(path))
            return;
        for (const auto& e : collection)
        {
            if (path_starts_with(e.first, path) || path_starts_with(path, e.first))
                return;
        }
        collection[path] = value;
    }

    // Notify and serialize only if the actual value is really changes
    // if edited collection is not default or if there are no value
    if (&collection == &m_values || m_values.find(path) == m_values.end())
    {
        notify_change(path, value, ChangeType::UPDATED);
        if (!path_starts_with(path, session_prefix))
        {
            set_value_at_path(path, value);
            serialize();
        }
    }
}

Settings::ValueHolder Settings::get_impl(const std::string& path) const
{
    const auto result = get_impl(path, m_values);
    if (!result.empty())
        return result;
    return get_default_impl(path);
}

Settings::ValueHolder Settings::get_default_impl(const std::string& path) const
{
    return get_impl(path, m_defaults);
}

Settings::ValueHolder Settings::get_impl(const std::string& path, const std::unordered_map<std::string, ValueHolder>& collection) const
{
    auto iter = collection.find(path);
    if (iter != collection.end())
        return iter->second;

    return ValueHolder();
}

bool Settings::is_valid_path(const std::string& path) const
{
    // empty first token
    if (path.empty())
        return false;

    auto pos = path.find(get_separator(), 0);
    auto prev_pos = std::string::npos;

    // check for empty substrings
    while (pos != std::string::npos)
    {
        if (pos == prev_pos + 1)
            return false;
        prev_pos = pos;
        pos = path.find(get_separator(), pos + 1);
    }

    // empty last token
    if (prev_pos == path.length() - 1)
        return false;
    return true;
}

void Settings::serialize() const
{
    if (m_settings_file.empty())
        return;

    auto file_stream = std::ofstream(m_settings_file);
    if (!file_stream.is_open())
    {
        TF_RUNTIME_ERROR("Failed to open application settings file.");
        return;
    }

    Json::StreamWriterBuilder writer_builder;
    auto writer = std::unique_ptr<Json::StreamWriter>(writer_builder.newStreamWriter());
    writer->write(m_json_root, &file_stream);
}

void Settings::deserialize()
{
    std::function<void(const Json::Value& node, const std::string& path)> traverse = [&traverse, this](const Json::Value& node,
                                                                                                       const std::string& path) {
        if (!node.isObject())
        {
            m_values[path] = node;
            return;
        }

        const auto path_prefix = path + (path.empty() ? "" : ".");
        for (const auto& name : node.getMemberNames())
        {
            const auto new_path = path_prefix + name;
            traverse(node[name], new_path);
        }
    };

    traverse(m_json_root, "");
}

void Settings::remove_value_at_path(const std::string& path)
{
    size_t pos = 0;
    size_t prev_pos = 0;
    Json::Value* cur_val = &m_json_root;
    std::vector<Json::Value*> vals = { &m_json_root };
    std::vector<std::string> tokens = {};

    while ((pos = path.find(get_separator(), prev_pos)) != std::string::npos)
    {
        const auto token = path.substr(prev_pos, pos - prev_pos);
        tokens.push_back(token);
        cur_val = &(*cur_val)[token];
        vals.push_back(cur_val);
        prev_pos = pos + 1;
    }

    tokens.push_back(path.substr(prev_pos));

    for (int i = vals.size() - 1; i >= 0; i--)
    {
        auto cur_val = vals[i];
        cur_val->removeMember(tokens[i]);
        if (!cur_val->empty())
            return;
    }
}

void Settings::set_value_at_path(const std::string& path, const ValueHolder& val)
{
    size_t pos = 0;
    size_t prev_pos = 0;
    Json::Value* cur_val = &m_json_root;
    while ((pos = path.find(get_separator(), prev_pos)) != std::string::npos)
    {
        const auto token = path.substr(prev_pos, pos - prev_pos);
        cur_val = &(*cur_val)[token];
        prev_pos = pos + 1;
    }
    (*cur_val)[path.substr(prev_pos)] = std::move(val);
}

std::unordered_map<std::type_index, Settings::TypeHelpers> Settings::s_type_helpers;

OPENDCC_NAMESPACE_CLOSE
