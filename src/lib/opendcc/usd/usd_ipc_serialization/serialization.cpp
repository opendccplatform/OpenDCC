// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "serialization.h"
#include <array>
OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

std::unordered_map<std::type_index, std::function<void(const VtValue&, Writer&)>> Writer::s_pack_value_functions;
std::array<std::function<void(Reader&, VtValue&)>, static_cast<size_t>(TypeEnum::NumTypes)> Reader::s_unpack_value_functions;

Writer::Writer()
{
    static std::once_flag once;
    std::call_once(once, [] {
#define xx(ENUMTYPE, ENUMVALUE, CPPTYPE, _unused2) register_type<CPPTYPE>(TypeEnum::ENUMTYPE);

#include "usd_data_types.h"

#undef xx
    });
}

Writer::Writer(const std::vector<char>& buffer)
    : Writer()
{
    m_buffer = buffer;
}

void Writer::write(const VtValue& val)
{
    const std::type_index type_index = val.IsArrayValued() ? val.GetElementTypeid() : val.GetTypeid();
    auto packer_iter = s_pack_value_functions.find(type_index);
    if (TF_VERIFY(packer_iter != s_pack_value_functions.end(), "Failed to serialize vtvalue of type \"%s\".", type_index.name()))
        packer_iter->second(val, *this);
}

void Writer::write(const SdfTimeCode& val)
{
    write(val.GetValue());
}

void Writer::write(const SdfAssetPath& val)
{
    write(val.GetAssetPath());
}

void Writer::write(const VtDictionary& val)
{
    write_map(val);
}

void Writer::write(const TfToken& val)
{
    write_array(val.GetString());
}

void Writer::write(const SdfPath& val)
{
    write_array(val.GetString());
}

void Writer::write(const SdfUnregisteredValue& val)
{
    write(val.GetValue());
}

void Writer::write(const SdfVariantSelectionMap& val)
{
    write_map(val);
}

void Writer::write(const SdfLayerOffset& val)
{
    write(val.GetOffset());
    write(val.GetScale());
}

void Writer::write(const SdfReference& val)
{
    write(val.GetAssetPath());
    write(val.GetPrimPath());
    write(val.GetLayerOffset());
    write(val.GetCustomData());
}

void Writer::write(const SdfPayload& val)
{
    write(val.GetAssetPath());
    write(val.GetPrimPath());
    write(val.GetLayerOffset());
}

void Writer::write(const std::string& val)
{
    write_array(val);
}

Reader::Reader(const std::vector<char>& buffer, size_t offset)
    : m_buffer(buffer)
    , m_offset(offset)
{
    std::once_flag once;
    std::call_once(once, [] {
#define xx(ENUMTYPE, ENUMVALUE, CPPTYPE, _unused2) register_type<CPPTYPE>(TypeEnum::ENUMTYPE);
#include "usd_data_types.h"
#undef xx
    });
}

SdfPayload Reader::read(SdfPayload*)
{
    const auto asset_path = read_array<std::string>();
    const auto prim_path = read<SdfPath>();
    const auto layer_offset = read<SdfLayerOffset>();
    return SdfPayload(asset_path, prim_path, layer_offset);
}

SdfReference Reader::read(SdfReference*)
{
    const auto asset_path = read_array<std::string>();
    const auto prim_path = read<SdfPath>();
    const auto layer_offset = read<SdfLayerOffset>();
    const auto custom_data = read_map<VtDictionary>();
    return SdfReference(asset_path, prim_path, layer_offset, custom_data);
}

SdfLayerOffset Reader::read(SdfLayerOffset*)
{
    const auto layer_offset = read<double>();
    const auto scale = read<double>();
    return SdfLayerOffset(layer_offset, scale);
}

SdfVariantSelectionMap Reader::read(SdfVariantSelectionMap*)
{
    return read_map<SdfVariantSelectionMap>();
}

SdfUnregisteredValue Reader::read(SdfUnregisteredValue*)
{
    const auto val = read<VtValue>();
    if (val.IsHolding<std::string>())
        return SdfUnregisteredValue(val.UncheckedGet<std::string>());
    if (val.IsHolding<VtDictionary>())
        return SdfUnregisteredValue(val.UncheckedGet<VtDictionary>());
    if (val.IsHolding<SdfUnregisteredValueListOp>())
        return SdfUnregisteredValue(val.UncheckedGet<SdfUnregisteredValueListOp>());
    TF_CODING_ERROR("SdfUnregisteredValue contains invalid "
                    "type '%s' = '%s'; expected string, VtDictionary or "
                    "SdfUnregisteredValueListOp; returning empty",
                    val.GetTypeName().c_str(), TfStringify(val).c_str());
    return SdfUnregisteredValue();
}

SdfTimeCode Reader::read(SdfTimeCode*)
{
    return SdfTimeCode(read<double>());
}

SdfAssetPath Reader::read(SdfAssetPath*)
{
    return SdfAssetPath(read_array<std::string>());
}

VtDictionary Reader::read(VtDictionary*)
{
    return read_map<VtDictionary>();
}

TfToken Reader::read(TfToken*)
{
    return TfToken(read_array<std::string>());
}

SdfPath Reader::read(SdfPath*)
{
    return SdfPath(read_array<std::string>());
}

VtValue Reader::read(VtValue*)
{
    const auto type = read<TypeEnum>();
    VtValue result;
    s_unpack_value_functions[static_cast<size_t>(type)](*this, result);
    return result;
}

std::string Reader::read(std::string*)
{
    return read_array<std::string>();
}
OPENDCC_NAMESPACE_CLOSE
