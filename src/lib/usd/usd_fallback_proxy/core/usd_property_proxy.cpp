// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include <pxr/usd/usd/attribute.h>
#include "usd_fallback_proxy/core/usd_property_proxy.h"
#include "opendcc/base/utils/string_utils.h"

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<UsdPropertyProxy>();
}

static const std::vector<TfToken> s_ignored_keys = { SdfFieldKeys->Default, SdfFieldKeys->TargetPaths };

bool UsdPropertyProxy::get(VtValue* value, UsdTimeCode time /*= UsdTimeCode::Default()*/) const
{
    if (!m_prim || m_type == SdfSpecType::SdfSpecTypeUnknown)
        return false;

    if (m_type == SdfSpecType::SdfSpecTypeAttribute)
    {
        if (auto attribute = get_attribute())
        {
            return attribute.Get(value, time);
        }
        if (!get_metadata(SdfFieldKeys->Default, *value))
        {
            *value = get_type_name().GetDefaultValue();
        }
    }
    else if (m_type == SdfSpecType::SdfSpecTypeRelationship)
    {
        if (auto relationship = get_relationship())
        {
            SdfPathVector result;
            if (relationship.GetTargets(&result))
            {
                *value = VtValue(result);
                return true;
            }
        }
        else if (!get_metadata(SdfFieldKeys->TargetPaths, *value))
        {
            *value = VtValue(SdfPathVector());
        }
    }

    return true;
}

bool UsdPropertyProxy::get_default(VtValue* value) const
{
    if (!m_prim)
        return false;

    if (m_type == SdfSpecTypeRelationship)
    {
        *value = VtValue(SdfPathVector());
        return true;
    }

    if (m_property_spec && m_property_spec->HasField(SdfFieldKeys->Default))
    {
        *value = m_property_spec->GetField(SdfFieldKeys->Default);
        return true;
    }

    auto iter = m_metadata.find(SdfFieldKeys->Default);
    if (iter == m_metadata.end())
    {
        *value = get_type_name().GetDefaultValue();
        return false;
    }
    *value = iter->second;
    return true;
}

bool UsdPropertyProxy::set(const VtValue& value, UsdTimeCode time /*= UsdTimeCode::Default()*/)
{
    if (!m_prim || m_type == SdfSpecType::SdfSpecTypeUnknown)
        return false;

    if (m_type == SdfSpecType::SdfSpecTypeAttribute)
    {
        if (auto attribute = get_attribute())
        {
            return attribute.Set(value, time);
        }
        else
        {
            SdfChangeBlock block;
            auto edit_target = m_prim.GetStage()->GetEditTarget();
            auto target_path = edit_target.MapToSpecPath(m_prim.GetPath());
            auto prim_spec = target_path.IsEmpty() ? SdfPrimSpecHandle() : SdfCreatePrimInLayer(edit_target.GetLayer(), target_path);
            auto attr_spec = prim_spec->GetAttributeAtPath(target_path.AppendProperty(m_name));
            if (TF_VERIFY(!attr_spec, "Failed to create attribute spec at path '%s'.", target_path.AppendProperty(m_name).GetText()))
            {
                attr_spec = SdfAttributeSpec::New(prim_spec, m_name.GetString(), get_type_name());
                if (time.IsDefault())
                {
                    attr_spec->SetDefaultValue(value);
                }
                else
                {
                    edit_target.GetLayer()->SetTimeSample(attr_spec->GetPath(), time.GetValue(), value);
                }
                return true;
            }
        }
    }
    else if (m_type == SdfSpecType::SdfSpecTypeRelationship && value.IsHolding<SdfPathVector>())
    {
        if (auto relationship = get_relationship())
        {
            return value.IsHolding<SdfPathVector>() ? relationship.SetTargets(value.UncheckedGet<SdfPathVector>()) : false;
        }
        else
        {
            SdfChangeBlock block;
            auto edit_target = m_prim.GetStage()->GetEditTarget();
            auto target_path = edit_target.MapToSpecPath(m_prim.GetPath());
            auto prim_spec = target_path.IsEmpty() ? SdfPrimSpecHandle() : SdfCreatePrimInLayer(edit_target.GetLayer(), target_path);
            auto relationship_spec = prim_spec->GetRelationshipAtPath(target_path.AppendProperty(m_name));
            if (TF_VERIFY(!relationship_spec, "Failed to create relationship spec at path '%s'.", target_path.AppendProperty(m_name).GetText()))
            {
                relationship_spec = SdfRelationshipSpec::New(prim_spec, m_name.GetString(), false);
                auto path_editor = relationship_spec->GetTargetPathList();
                path_editor.ClearEditsAndMakeExplicit();
                path_editor.GetExplicitItems() = value.UncheckedGet<SdfPathVector>();
                return true;
            }
        }
    }

    return false;
}

void UsdPropertyProxy::set_property_spec(SdfPropertySpecHandle property_spec)
{
    m_property_spec = property_spec;
}

SdfPropertySpecHandle UsdPropertyProxy::get_property_spec() const
{
    return m_property_spec;
}

TfToken UsdPropertyProxy::get_name_token() const
{
    return m_name;
}

SdfValueTypeName UsdPropertyProxy::get_type_name() const
{
    TfToken type_name;
    if (get_metadata(SdfFieldKeys->TypeName, type_name))
    {
        return SdfSchema::GetInstance().FindType(type_name);
    }
    return SdfValueTypeName();
}

std::string UsdPropertyProxy::get_display_group() const
{
    std::string display_group;
    get_metadata(SdfFieldKeys->DisplayGroup, display_group);
    return display_group;
}

bool UsdPropertyProxy::has_display_name()
{
    std::string display_name;
    return get_metadata(SdfFieldKeys->DisplayName, display_name);
}

std::string UsdPropertyProxy::get_display_name() const
{
    std::string display_name;
    get_metadata(SdfFieldKeys->DisplayName, display_name);
    return display_name;
}

VtTokenArray UsdPropertyProxy::get_allowed_tokens() const
{
    VtTokenArray allowed_tokens;
    get_metadata(SdfFieldKeys->AllowedTokens, allowed_tokens);
    return allowed_tokens;
}

VtTokenArray UsdPropertyProxy::get_options() const
{
    VtTokenArray options_array;
    VtDictionary sdr_metadata;
    get_metadata(TfToken("sdrMetadata"), sdr_metadata);

    if (sdr_metadata.empty())
    {
        get_metadata(SdfFieldKeys->AllowedTokens, options_array);
        return options_array;
    }

    VtValue options = sdr_metadata["options"];
    if (options.IsHolding<std::string>())
    {
        for (auto option : base::utils::split(options.UncheckedGet<std::string>(), '|'))
            options_array.push_back(TfToken(option));
    }

    return options_array;
}

std::string UsdPropertyProxy::get_documentation() const
{
    std::string documentation;
    get_metadata(SdfFieldKeys->Documentation, documentation);
    return documentation;
}

TfToken UsdPropertyProxy::get_display_widget() const
{
    TfToken display_widget;
    get_metadata(TfToken("displayWidget"), display_widget);
    return display_widget;
}

VtDictionary UsdPropertyProxy::get_display_widget_hints() const
{
    VtDictionary display_widget_hints;
    get_metadata(TfToken("displayWidgetHints"), display_widget_hints);
    return display_widget_hints;
}

UsdMetadataValueMap UsdPropertyProxy::get_all_metadata() const
{
    UsdMetadataValueMap result;
    if (auto prop = get_property())
    {
        result = prop.GetAllAuthoredMetadata();
    }
    for (const auto& entry : m_metadata)
    {
        if (result.find(entry.first) == result.end())
            result[entry.first] = entry.second;
    }

    if (m_property_spec)
    {
        for (const auto& prop_name : m_property_spec->ListFields())
        {
            if (result.find(prop_name) == result.end())
                result[prop_name] = m_property_spec->GetField(prop_name);
        }
    }
    return result;
}

const std::vector<UsdPropertySource>& UsdPropertyProxy::get_sources() const
{
    return m_sources;
}

void UsdPropertyProxy::append_source(const TfToken& source_group, const TfType& source_plugin)
{
    if (source_plugin.IsUnknown())
        return;

    m_sources.emplace_back(source_group, source_plugin);
}

void UsdPropertyProxy::append_source(const UsdPropertySource& source)
{
    if (source.get_source_plugin().IsUnknown())
        return;
    m_sources.push_back(source);
}

bool UsdPropertyProxy::get_metadata(const TfToken& key, VtValue& value) const
{
    if (!m_prim)
        return false;

    if (auto prop = get_property())
    {
        auto authored_metadata = prop.GetAllAuthoredMetadata();
        auto metadata_iter = authored_metadata.find(key);
        if (metadata_iter != authored_metadata.end())
        {
            value = metadata_iter->second;
            return true;
        }
    }

    auto iter = m_metadata.find(key);
    if (iter != m_metadata.end())
    {
        value = iter->second;
        return true;
    }
    if (m_property_spec)
    {
        value = m_property_spec->GetField(key);
        return !value.IsEmpty();
    }

    return false;
}

bool UsdPropertyProxy::set_metadata(const TfToken& key, const VtValue& value)
{
    if (!m_prim)
        return false;

    auto ignored_iter = std::find(s_ignored_keys.begin(), s_ignored_keys.end(), key);
    if (ignored_iter != s_ignored_keys.end())
    {
        m_metadata[key] = value;
        return true;
    }
    if (auto property = get_property())
    {
        property.SetMetadata(key, value);
        return true;
    }

    return false;
}

UsdProperty UsdPropertyProxy::get_property() const
{
    return m_prim.GetProperty(m_name);
}

UsdAttribute UsdPropertyProxy::get_attribute() const
{
    return m_prim.GetAttribute(m_name);
}

bool UsdPropertyProxy::is_relationship() const
{
    return m_prim.GetProperty(m_name).Is<UsdRelationship>();
}

UsdRelationship UsdPropertyProxy::get_relationship() const
{
    return m_prim.GetRelationship(m_name);
}

SdfSpecType UsdPropertyProxy::get_type() const
{
    return m_type;
}

UsdPrim UsdPropertyProxy::get_prim() const
{
    return m_prim;
}

bool UsdPropertyProxy::is_authored() const
{
    return get_property().IsAuthored();
}

OPENDCC_NAMESPACE_CLOSE
