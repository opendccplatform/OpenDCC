// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "parser.h"
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/sdr/shaderNode.h>
#include "node_definitions.h"
#include "pxr/usd/usdShade/shader.h"
#include "pxr/usd/sdr/shaderProperty.h"
#include <pxr/usd/usdShade/shaderDefUtils.h>
#include <pxr/usd/usdShade/connectableAPI.h>
#if PXR_VERSION < 2108
#include <pxr/usd/sdr/shaderMetadataHelpers.h>
#endif

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE
SDR_REGISTER_PARSER_PLUGIN(NdrCyclesParserPlugin)
#if PXR_VERSION < 2108
TF_DEFINE_PRIVATE_TOKENS(_tokens, (cycles)(binary)(defaultInput)(implementationName));
#else
TF_DEFINE_PRIVATE_TOKENS(_tokens, (cycles)(binary));
#endif

namespace
{
#if PXR_VERSION < 2108
    size_t get_array_size(VtValue* val)
    {
        if (val && !val->IsEmpty() && val->IsArrayValued())
            return val->GetArraySize();
        return 0;
    }

    void conform_int_type_value(const SdfValueTypeName& type_name, VtValue* val)
    {
        if (val && !val->IsEmpty())
        {
            if (type_name == SdfValueTypeNames->Bool)
            {
                if (val->IsHolding<bool>())
                {
                    const auto& bool_val = val->UncheckedGet<bool>();
                    *val = VtValue(bool_val ? 1 : 0);
                }
            }
            else if (type_name == SdfValueTypeNames->BoolArray)
            {
                if (val->IsHolding<VtArray<bool>>())
                {
                    const auto& bool_vals = val->UncheckedGet<VtArray<bool>>();
                    VtIntArray int_vals;
                    int_vals.reserve(bool_vals.size());
                    for (const auto& bool_val : bool_vals)
                        int_vals.push_back(bool_val ? 1 : 0);
                    *val = VtValue::Take(int_vals);
                }
            }
        }
    }

    void conform_string_type_value(const SdfValueTypeName& type_name, VtValue* val)
    {
        if (val && !val->IsEmpty())
        {
            if (type_name == SdfValueTypeNames->Token)
            {
                if (val->IsHolding<TfToken>())
                {
                    const auto& tokenVal = val->UncheckedGet<TfToken>();
                    *val = VtValue(tokenVal.GetString());
                }
            }
            else if (type_name == SdfValueTypeNames->TokenArray)
            {
                if (val->IsHolding<VtArray<TfToken>>())
                {
                    const auto& token_vals = val->UncheckedGet<VtArray<TfToken>>();
                    VtStringArray string_vals;
                    string_vals.reserve(token_vals.size());
                    for (const auto& token_val : token_vals)
                        string_vals.push_back(token_val.GetString());
                    *val = VtValue::Take(string_vals);
                }
            }
        }
    }

    std::pair<TfToken, size_t> get_type_and_array_size(const SdfValueTypeName& type_name, const NdrTokenMap& metadata, VtValue* default_val)
    {
        if (ShaderMetadataHelpers::IsPropertyATerminal(metadata))
            return std::make_pair(SdrPropertyTypes->Terminal, get_array_size(default_val));

        // Determine SdrPropertyType from given SdfValueTypeName
        if (type_name == SdfValueTypeNames->Int || type_name == SdfValueTypeNames->IntArray || type_name == SdfValueTypeNames->Bool ||
            type_name == SdfValueTypeNames->BoolArray)
        {
            conform_int_type_value(type_name, default_val);
            return std::make_pair(SdrPropertyTypes->Int, get_array_size(default_val));
        }
        else if (type_name == SdfValueTypeNames->String || type_name == SdfValueTypeNames->Token || type_name == SdfValueTypeNames->Asset ||
                 type_name == SdfValueTypeNames->StringArray || type_name == SdfValueTypeNames->TokenArray ||
                 type_name == SdfValueTypeNames->AssetArray)
        {
            conform_string_type_value(type_name, default_val);
            return std::make_pair(SdrPropertyTypes->String, get_array_size(default_val));
        }
        else if (type_name == SdfValueTypeNames->Float || type_name == SdfValueTypeNames->FloatArray)
        {
            return std::make_pair(SdrPropertyTypes->Float, get_array_size(default_val));
        }
        else if (type_name == SdfValueTypeNames->Float2 || type_name == SdfValueTypeNames->Float2Array)
        {
            return std::make_pair(SdrPropertyTypes->Float, 2);
        }
        else if (type_name == SdfValueTypeNames->Float3 || type_name == SdfValueTypeNames->Float3Array)
        {
            return std::make_pair(SdrPropertyTypes->Float, 3);
        }
        else if (type_name == SdfValueTypeNames->Float4 || type_name == SdfValueTypeNames->Float4Array)
        {
            return std::make_pair(SdrPropertyTypes->Float, 4);
        }
        else if (type_name == SdfValueTypeNames->Color3f || type_name == SdfValueTypeNames->Color3fArray)
        {
            return std::make_pair(SdrPropertyTypes->Color, get_array_size(default_val));
        }
        else if (type_name == SdfValueTypeNames->Point3f || type_name == SdfValueTypeNames->Point3fArray)
        {
            return std::make_pair(SdrPropertyTypes->Point, get_array_size(default_val));
        }
        else if (type_name == SdfValueTypeNames->Vector3f || type_name == SdfValueTypeNames->Vector3fArray)
        {
            return std::make_pair(SdrPropertyTypes->Vector, get_array_size(default_val));
        }
        else if (type_name == SdfValueTypeNames->Normal3f || type_name == SdfValueTypeNames->Normal3fArray)
        {
            return std::make_pair(SdrPropertyTypes->Normal, get_array_size(default_val));
        }
        else if (type_name == SdfValueTypeNames->Matrix4d || type_name == SdfValueTypeNames->Matrix4dArray)
        {
            return std::make_pair(SdrPropertyTypes->Matrix, get_array_size(default_val));
        }
        else
        {
            TF_RUNTIME_ERROR("Shader property has unsupported type '%s'", type_name.GetAsToken().GetText());
            return std::make_pair(SdrPropertyTypes->Unknown, 0);
        }
    }

    template <class ShaderProperty>
    SdrShaderPropertyUniquePtr create_sdr_shader_property(const ShaderProperty& shader_prop, bool is_output, VtValue default_value,
                                                          NdrTokenMap metadata)
    {
        NdrTokenMap hints;

        const auto type_name = shader_prop.GetTypeName();
        // Update metadata if string should represent a SdfAssetPath
        if (type_name == SdfValueTypeNames->Asset || type_name == SdfValueTypeNames->AssetArray)
            metadata[SdrPropertyMetadata->IsAssetIdentifier] = "1";

        TfToken prop_type;
        size_t array_size;
        std::tie(prop_type, array_size) = get_type_and_array_size(type_name, metadata, &default_value);

        return SdrShaderPropertyUniquePtr(
            new SdrShaderProperty(shader_prop.GetBaseName(), prop_type, default_value, is_output, array_size, metadata, hints, NdrOptionVec()));
    }
#endif
};

NdrCyclesParserPlugin::NdrCyclesParserPlugin() {}

NdrCyclesParserPlugin::~NdrCyclesParserPlugin() {}

#if PXR_VERSION >= 2508
SdrShaderNodeUniquePtr NdrCyclesParserPlugin::ParseShaderNode(const SdrShaderNodeDiscoveryResult& discoveryResult)
#else
NdrNodeUniquePtr NdrCyclesParserPlugin::Parse(const SdrShaderNodeDiscoveryResult& discoveryResult)
#endif
{
    auto node_definitions = get_node_definitions();
    auto prim = node_definitions->GetPrimAtPath(SdfPath(TfStringPrintf("/%s", discoveryResult.name.c_str())));
    if (!prim)
        return nullptr;
    auto shader = UsdShadeConnectableAPI(prim);
    if (!shader)
        return nullptr;

#if PXR_VERSION >= 2508
    SdrShaderPropertyUniquePtrVec props = UsdShadeShaderDefUtils::GetProperties(shader);
#elif PXR_VERSION >= 2108
    SdrShaderPropertyUniquePtrVec props = UsdShadeShaderDefUtils::GetShaderProperties(shader);
#else
    SdrShaderPropertyUniquePtrVec props;
    for (auto& input : shader.GetInputs())
    {
        // Only inputs will have default value provided
        VtValue default_val;
        input.Get(&default_val);

        NdrTokenMap metadata = input.GetSdrMetadata();

        // Only inputs might have this metadata key
        auto iter = metadata.find(_tokens->defaultInput);
        if (iter != metadata.end())
        {
            metadata[SdrPropertyMetadata->DefaultInput] = "1";
            metadata.erase(_tokens->defaultInput);
        }

        // Only inputs have the GetConnectability method
        metadata[SdrPropertyMetadata->Connectable] = input.GetConnectability() == UsdShadeTokens->interfaceOnly ? "0" : "1";

        auto impl_name = metadata.find(_tokens->implementationName);
        if (impl_name != metadata.end())
        {
            metadata[SdrPropertyMetadata->ImplementationName] = impl_name->second;
            metadata.erase(impl_name);
        }

        props.emplace_back(create_sdr_shader_property(
            /* shaderProperty */ input,
            /* isOutput */ false,
            /* shaderDefaultValue */ default_val,
            /* shaderMetadata */ metadata));
    }

    for (auto& output : shader.GetOutputs())
    {
        props.emplace_back(create_sdr_shader_property(
            /* shaderProperty */ output,
            /* isOutput */ true,
            /* shaderDefaultValue */ VtValue(),
            /* shaderMetadata */ output.GetSdrMetadata()));
    }

#endif
    // Not PXR_MINOR/PATCH_VERSION: those are 25 and 2 for USD 25.02, so the old
    // `MINOR >= 20 && PATCH >= 5` test picked the 8-argument overload dropped in 20.05.
    // 2508+ keeps this overload deprecated; the replacement derives context from metadata.
#if PXR_VERSION >= 2005
    return SdrShaderNodeUniquePtr(new SdrShaderNode(discoveryResult.identifier, // identifier
                                                    discoveryResult.version, // version
                                                    discoveryResult.name, // name
                                                    discoveryResult.family, // family
                                                    discoveryResult.discoveryType, // context
                                                    discoveryResult.sourceType, // sourceType
                                                    discoveryResult.uri, // uri
                                                    discoveryResult.uri, // resolvedUri
                                                    std::move(props)));
#else
    return SdrShaderNodeUniquePtr(new SdrShaderNode(discoveryResult.identifier, // identifier
                                                    discoveryResult.version, // version
                                                    discoveryResult.name, // name
                                                    discoveryResult.family, // family
                                                    discoveryResult.discoveryType, // context
                                                    discoveryResult.sourceType, // sourceType
                                                    discoveryResult.uri, // uri
                                                    std::move(props)));
#endif
}

const SdrTokenVec& NdrCyclesParserPlugin::GetDiscoveryTypes() const
{
    static const SdrTokenVec ret = { _tokens->cycles };
    return ret;
}

const TfToken& NdrCyclesParserPlugin::GetSourceType() const
{
    return _tokens->cycles;
}

OPENDCC_NAMESPACE_CLOSE
