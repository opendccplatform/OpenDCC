// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "usd_edits.h"
#include "serialization.h"
#include <pxr/usd/sdf/layerStateDelegate.h>
#include <pxr/usd/sdf/abstractData.h>
#include <array>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

std::vector<char> UsdEditBase::write() const
{
    Writer packer;
    write_data(packer);
    return packer.get_buffer();
}

std::unique_ptr<UsdEditBase> UsdEditBase::read(const std::vector<char>& buffer)
{
    static const std::array<std::function<std::unique_ptr<UsdEditBase>()>, static_cast<size_t>(UsdEditType::COUNT)> factory = {
        [] { return std::make_unique<UsdEditSetField>(); },
        [] { return std::make_unique<UsdEditSetFieldDictValueByKey>(); },
        [] { return std::make_unique<UsdEditSetTimesample>(); },
        [] { return std::make_unique<UsdEditCreateSpec>(); },
        [] { return std::make_unique<UsdEditDeleteSpec>(); },
        [] { return std::make_unique<UsdEditMoveSpec>(); },
        [] { return std::make_unique<UsdEditPushChild>(); },
        [] { return std::make_unique<UsdEditPopChild>(); },
        [] {
            return std::make_unique<UsdEditChangeBlockClosed>();
        }
    };
    auto reader = Reader(buffer);
    const auto edit_type_ind = static_cast<size_t>(reader.read<UsdEditType>());
    if (edit_type_ind >= static_cast<size_t>(UsdEditType::COUNT))
        return nullptr;

    auto edit = factory[edit_type_ind]();
    edit->read_data(reader);
    return edit;
}

UsdEditSetField::UsdEditSetField(const std::string& layer_id, const SdfPath& path, const TfToken& field_name, const VtValue& value)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_field_name(field_name)
    , m_value(value)
{
}

UsdEditSetField::UsdEditSetField(const std::string& layer_id, const SdfPath& path, const TfToken& field_name, const SdfAbstractDataConstValue& value)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_field_name(field_name)
{
    value.GetValue(&m_value);
}

void UsdEditSetField::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    layer_state_delegate->SetField(m_path, m_field_name, m_value);
}

void UsdEditSetField::write_data(Writer& packer) const
{
    packer.write(UsdEditType::SET_FIELD);
    packer.write(m_path);
    packer.write(m_field_name);
    packer.write(m_value);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditSetField::read_data(Reader& reader)
{
    m_path = reader.read<SdfPath>();
    m_field_name = reader.read<TfToken>();
    m_value = reader.read<VtValue>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditSetField::operator==(const UsdEditSetField& other) const
{
    return m_layer_id == other.m_layer_id && m_path == other.m_path && m_field_name == other.m_field_name && m_value == other.m_value;
}

UsdEditSetFieldDictValueByKey::UsdEditSetFieldDictValueByKey(const std::string& layer_id, const SdfPath& path, const TfToken& field_name,
                                                             const TfToken& key_path, const VtValue& value)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_field_name(field_name)
    , m_key_path(key_path)
    , m_value(value)
{
}

UsdEditSetFieldDictValueByKey::UsdEditSetFieldDictValueByKey(const std::string& layer_id, const SdfPath& path, const TfToken& field_name,
                                                             const TfToken& key_path, const SdfAbstractDataConstValue& value)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_field_name(field_name)
    , m_key_path(key_path)
{
    value.GetValue(&m_value);
}

void UsdEditSetFieldDictValueByKey::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    layer_state_delegate->SetFieldDictValueByKey(m_path, m_field_name, m_key_path, m_value);
}

void UsdEditSetFieldDictValueByKey::write_data(Writer& packer) const
{
    packer.write(UsdEditType::SET_FIELD_DICT_VALUE_BY_KEY);
    packer.write(m_path);
    packer.write(m_field_name);
    packer.write(m_key_path);
    packer.write(m_value);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditSetFieldDictValueByKey::read_data(Reader& reader)
{
    m_path = reader.read<SdfPath>();
    m_field_name = reader.read<TfToken>();
    m_key_path = reader.read<TfToken>();
    m_value = reader.read<VtValue>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditSetFieldDictValueByKey::operator==(const UsdEditSetFieldDictValueByKey& other) const
{
    return m_layer_id == other.m_layer_id && m_path == other.m_path && m_field_name == other.m_field_name && m_key_path == other.m_key_path &&
           m_value == other.m_value;
}

UsdEditSetTimesample::UsdEditSetTimesample(const std::string& layer_id, const SdfPath& path, double time, const VtValue& value)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_time(time)
    , m_value(value)
{
}

UsdEditSetTimesample::UsdEditSetTimesample(const std::string& layer_id, const SdfPath& path, double time, const SdfAbstractDataConstValue& value)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_time(time)
{
    value.GetValue(&m_value);
}

void UsdEditSetTimesample::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    layer_state_delegate->SetTimeSample(m_path, m_time, m_value);
}

void UsdEditSetTimesample::write_data(Writer& packer) const
{
    packer.write(UsdEditType::SET_TIMESAMPLE);
    packer.write(m_path);
    packer.write(m_time);
    packer.write(m_value);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditSetTimesample::read_data(Reader& reader)
{
    m_path = reader.read<SdfPath>();
    m_time = reader.read<double>();
    m_value = reader.read<VtValue>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditSetTimesample::operator==(const UsdEditSetTimesample& other) const
{
    return m_layer_id == other.m_layer_id && m_path == other.m_path && m_time == other.m_time && m_value == other.m_value;
}

UsdEditCreateSpec::UsdEditCreateSpec(const std::string& layer_id, const SdfPath& path, SdfSpecType specType, bool inert)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_spec_type(specType)
    , m_inert(inert)
{
}

void UsdEditCreateSpec::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    layer_state_delegate->CreateSpec(m_path, m_spec_type, m_inert);
}

void UsdEditCreateSpec::write_data(Writer& packer) const
{
    packer.write(UsdEditType::CREATE_SPEC);
    packer.write(m_path);
    packer.write(m_spec_type);
    packer.write(m_inert);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditCreateSpec::read_data(Reader& reader)
{
    m_path = reader.read<SdfPath>();
    m_spec_type = reader.read<SdfSpecType>();
    m_inert = reader.read<bool>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditCreateSpec::operator==(const UsdEditCreateSpec& other) const
{
    return m_layer_id == other.m_layer_id && m_path == other.m_path && m_spec_type == other.m_spec_type && m_inert == other.m_inert;
}

UsdEditDeleteSpec::UsdEditDeleteSpec(const std::string& layer_id, const SdfPath& path, bool inert)
    : UsdEditLayerDependent(layer_id)
    , m_path(path)
    , m_inert(inert)
{
}

void UsdEditDeleteSpec::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    layer_state_delegate->DeleteSpec(m_path, m_inert);
}

void UsdEditDeleteSpec::write_data(Writer& packer) const
{
    packer.write(UsdEditType::DELETE_SPEC);
    packer.write(m_path);
    packer.write(m_inert);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditDeleteSpec::read_data(Reader& reader)
{
    m_path = reader.read<SdfPath>();
    m_inert = reader.read<bool>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditDeleteSpec::operator==(const UsdEditDeleteSpec& other) const
{
    return m_layer_id == other.m_layer_id && m_path == other.m_path && m_inert == other.m_inert;
}

UsdEditMoveSpec::UsdEditMoveSpec(const std::string& layer_id, const SdfPath& old_path, const SdfPath& new_path)
    : UsdEditLayerDependent(layer_id)
    , m_old_path(old_path)
    , m_new_path(new_path)
{
}

void UsdEditMoveSpec::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    layer_state_delegate->MoveSpec(m_old_path, m_new_path);
}

void UsdEditMoveSpec::write_data(Writer& packer) const
{
    packer.write(UsdEditType::MOVE_SPEC);
    packer.write(m_old_path);
    packer.write(m_new_path);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditMoveSpec::read_data(Reader& reader)
{
    m_old_path = reader.read<SdfPath>();
    m_new_path = reader.read<SdfPath>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditMoveSpec::operator==(const UsdEditMoveSpec& other) const
{
    return m_layer_id == other.m_layer_id && m_new_path == other.m_new_path && m_old_path == other.m_old_path;
}

UsdEditPushChild::UsdEditPushChild(const std::string& layer_id, const SdfPath& parent_path, const TfToken& field_name, const TfToken& value)
    : UsdEditLayerDependent(layer_id)
    , m_parent_path(parent_path)
    , m_field_name(field_name)
    , m_value(VtValue(value))
{
}

UsdEditPushChild::UsdEditPushChild(const std::string& layer_id, const SdfPath& parent_path, const TfToken& field_name, const SdfPath& value)
    : UsdEditLayerDependent(layer_id)
    , m_parent_path(parent_path)
    , m_field_name(field_name)
    , m_value(VtValue(value))
{
}

void UsdEditPushChild::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    if (m_value.IsHolding<SdfPath>())
        layer_state_delegate->PushChild(m_parent_path, m_field_name, m_value.UncheckedGet<SdfPath>());
    else
        layer_state_delegate->PushChild(m_parent_path, m_field_name, m_value.UncheckedGet<TfToken>());
}

void UsdEditPushChild::write_data(Writer& packer) const
{
    packer.write(UsdEditType::PUSH_CHILD);
    packer.write(m_parent_path);
    packer.write(m_field_name);
    packer.write(m_value);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditPushChild::read_data(Reader& reader)
{
    m_parent_path = reader.read<SdfPath>();
    m_field_name = reader.read<TfToken>();
    m_value = reader.read<VtValue>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditPushChild::operator==(const UsdEditPushChild& other) const
{
    return m_layer_id == other.m_layer_id && m_parent_path == other.m_parent_path && m_field_name == other.m_field_name && m_value == m_value;
}

UsdEditPopChild::UsdEditPopChild(const std::string& layer_id, const SdfPath& parent_path, const TfToken& field_name, const TfToken& old_value)
    : UsdEditLayerDependent(layer_id)
    , m_parent_path(parent_path)
    , m_field_name(field_name)
    , m_old_value(VtValue(old_value))
{
}

UsdEditPopChild::UsdEditPopChild(const std::string& layer_id, const SdfPath& parent_path, const TfToken& field_name, const SdfPath& old_value)
    : UsdEditLayerDependent(layer_id)
    , m_parent_path(parent_path)
    , m_field_name(field_name)
    , m_old_value(VtValue(old_value))
{
}

void UsdEditPopChild::apply(SdfLayerStateDelegateBasePtr layer_state_delegate)
{
    if (m_old_value.IsHolding<SdfPath>())
        layer_state_delegate->PopChild(m_parent_path, m_field_name, m_old_value.UncheckedGet<SdfPath>());
    else
        layer_state_delegate->PopChild(m_parent_path, m_field_name, m_old_value.UncheckedGet<TfToken>());
}

void UsdEditPopChild::write_data(Writer& packer) const
{
    packer.write(UsdEditType::POP_CHILD);
    packer.write(m_parent_path);
    packer.write(m_field_name);
    packer.write(m_old_value);
    UsdEditLayerDependent::write_data(packer);
}

void UsdEditPopChild::read_data(Reader& reader)
{
    m_parent_path = reader.read<SdfPath>();
    m_field_name = reader.read<TfToken>();
    m_old_value = reader.read<VtValue>();
    UsdEditLayerDependent::read_data(reader);
}

bool UsdEditPopChild::operator==(const UsdEditPopChild& other) const
{
    return m_layer_id == other.m_layer_id && m_parent_path == other.m_parent_path && m_field_name == other.m_field_name &&
           m_old_value == other.m_old_value;
}

void UsdEditChangeBlockClosed::apply(SdfLayerStateDelegateBasePtr layer_state_delegate) {}

void UsdEditChangeBlockClosed::write_data(Writer& packer) const
{
    packer.write(UsdEditType::CHANGE_BLOCK_CLOSED);
}

void UsdEditChangeBlockClosed::read_data(Reader& reader) {}

bool UsdEditChangeBlockClosed::operator==(const UsdEditChangeBlockClosed& other) const
{
    return true;
}

UsdEditLayerDependent::UsdEditLayerDependent(const std::string& layer_id)
    : m_layer_id(layer_id)
{
}

const std::string& UsdEditLayerDependent::get_layer_id() const
{
    return m_layer_id;
}

void UsdEditLayerDependent::write_data(Writer& packer) const
{
    packer.write(m_layer_id);
}

void UsdEditLayerDependent::read_data(Reader& buffer)
{
    m_layer_id = buffer.read<std::string>();
}
OPENDCC_NAMESPACE_CLOSE
