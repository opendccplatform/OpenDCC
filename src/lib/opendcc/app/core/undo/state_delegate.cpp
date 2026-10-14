// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/undo/state_delegate.h"
#include "opendcc/base/logging/logger.h"
#include <pxr/base/tf/refPtr.h>
#include "opendcc/app/core/undo/router.h"
#include "opendcc/app/core/undo/inverse.h"

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.app.undo");
PXR_NAMESPACE_USING_DIRECTIVE
using namespace commands;

OPENDCC_REGISTER_LAYER_STATE_DELEGATE(UndoStateDelegate::get_name(), UndoStateDelegate);

TF_DEFINE_PRIVATE_TOKENS(UndoStateDelegateTokens, (UndoStateDelegate));

namespace
{
    class UsdEdit : public Edit
    {
    private:
        std::vector<std::function<bool()>> m_inversions;

    public:
        UsdEdit() = default;
        virtual ~UsdEdit() override = default;
        UsdEdit(const std::function<bool()>& inverse)
            : m_inversions({ inverse })
        {
        }

        virtual bool operator()() override
        {
            auto result = true;
            for (const auto& inv : m_inversions)
                result &= inv();
            return result;
        }
        virtual bool merge_with(const Edit* other) override
        {
            if (get_edit_type_id() != other->get_edit_type_id())
                return false;

            const auto usd_edit = dynamic_cast<const UsdEdit*>(other);
            m_inversions.insert(m_inversions.end(), usd_edit->m_inversions.begin(), usd_edit->m_inversions.end());
            return true;
        }

        virtual size_t get_edit_type_id() const override { return commands::get_edit_type_id<UsdEdit>(); }
    };

    class UsdFieldEdit : public UsdEdit
    {
    public:
        UsdFieldEdit(
            const std::function<bool(LayerStateDelegateProxyPtr, const SdfPath& path, const TfToken& field_name, const VtValue& inverse)>& inverse,
            LayerStateDelegateProxyPtr proxy, const SdfPath& path, const TfToken& field_name, const VtValue& value)
            : m_proxy(proxy)
        {
            m_field_edits[path][field_name] = { value, inverse };
        }

        virtual ~UsdFieldEdit() override = default;
        virtual bool operator()() override
        {
            bool result = true;
            for (const auto& path_edits : m_field_edits)
            {
                for (const auto& edit : path_edits.second)
                {
                    result &= edit.second.inverse(m_proxy, path_edits.first, edit.first, edit.second.val);
                }
            }
            return result;
        }

        virtual bool merge_with(const Edit* other) override
        {
            if (other->get_edit_type_id() != this->get_edit_type_id())
                return false;

            const auto usd_field_edit = dynamic_cast<const UsdFieldEdit*>(other);
            if (!usd_field_edit || usd_field_edit->m_proxy.IsExpired() || m_proxy.IsExpired())
                return false;
            if (usd_field_edit->m_proxy->get_layer() != m_proxy->get_layer())
                return false;

            for (const auto& other_edited_paths : usd_field_edit->m_field_edits)
            {
                auto it = m_field_edits.find(other_edited_paths.first);
                if (it == m_field_edits.end())
                {
                    m_field_edits[other_edited_paths.first] = other_edited_paths.second;
                }
                else
                {
                    for (const auto& other_edit : other_edited_paths.second)
                    {
                        if (it->second.find(other_edit.first) == it->second.end())
                            it->second[other_edit.first] = other_edit.second;
                    }
                }
            }
            return true;
        }

        virtual size_t get_edit_type_id() const override { return commands::get_edit_type_id<UsdFieldEdit>(); }

    private:
        LayerStateDelegateProxyPtr m_proxy;

        struct Edit
        {
            VtValue val;
            std::function<bool(LayerStateDelegateProxyPtr proxy, const SdfPath& path, const TfToken& field_name, const VtValue& inverse)> inverse;
        };

        std::unordered_map<SdfPath, std::unordered_map<TfToken, Edit, TfToken::HashFunctor>, SdfPath::Hash> m_field_edits;
    };

    class UsdTimeSampleEdit : public UsdEdit
    {
    public:
        UsdTimeSampleEdit(const std::function<bool(LayerStateDelegateProxyPtr, const SdfPath& path, double time, const VtValue& inverse)>& inverse,
                          LayerStateDelegateProxyPtr proxy, const SdfPath& path, double time, const VtValue& value)
            : m_proxy(proxy)
        {
            m_timesample_edits[path].push_back(Edit { value, time, inverse });
        }

        virtual ~UsdTimeSampleEdit() override = default;
        virtual bool operator()() override
        {
            bool result = true;
            for (const auto& path_entry : m_timesample_edits)
            {
                for (const auto& edit : path_entry.second)
                {
                    result &= edit.inverse(m_proxy, path_entry.first, edit.time, edit.val);
                }
            }

            return result;
        }

        virtual bool merge_with(const Edit* other) override
        {
            if (other->get_edit_type_id() != this->get_edit_type_id())
                return false;

            const auto usd_field_edit = dynamic_cast<const UsdTimeSampleEdit*>(other);
            if (!usd_field_edit || usd_field_edit->m_proxy.IsExpired() || m_proxy.IsExpired())
                return false;
            if (usd_field_edit->m_proxy->get_layer() != m_proxy->get_layer())
                return false;

            for (const auto& other_edited_paths : usd_field_edit->m_timesample_edits)
            {
                auto it = m_timesample_edits.find(other_edited_paths.first);
                if (it == m_timesample_edits.end())
                {
                    m_timesample_edits[other_edited_paths.first] = other_edited_paths.second;
                }
                else
                {

                    for (const auto& other_edit : other_edited_paths.second)
                    {
                        auto ts_it = std::lower_bound(it->second.begin(), it->second.end(), other_edit);
                        if (ts_it == it->second.end())
                            it->second.insert(it->second.begin(), other_edit);
                        else if (ts_it->time != other_edit.time)
                            it->second.insert(ts_it, other_edit);
                    }
                }
            }
            return true;
        }

        virtual size_t get_edit_type_id() const override { return commands::get_edit_type_id<UsdTimeSampleEdit>(); }

    private:
        LayerStateDelegateProxyPtr m_proxy;
        struct Edit
        {
            VtValue val;
            double time;
            std::function<bool(LayerStateDelegateProxyPtr, const SdfPath&, double, const VtValue&)> inverse;
            bool operator<(const Edit& other) const { return time < other.time; }
        };
        std::unordered_map<SdfPath, std::vector<Edit>, SdfPath::Hash> m_timesample_edits;
    };

    void add_inverse(std::shared_ptr<Edit> inverse)
    {
        if (!UndoRouter::is_muted())
        {
            UndoRouter::add_inverse(inverse);
        }
        else
        {
            OPENDCC_DEBUG("Performance Warning. Inverse should be muted earlier in stack.");
        }
    }

    void copy_spec(const SdfAbstractData& src, SdfAbstractData* dst, const SdfPath& path)
    {
        dst->CreateSpec(path, src.GetSpecType(path));
        std::vector<TfToken> fields = src.List(path);
        TF_FOR_ALL(i, fields)
        {
            dst->Set(path, *i, src.Get(path, *i));
        }
    }
}

bool UndoStateDelegate::_IsDirty()
{
    return m_state_delegate_proxy->IsDirty();
}

void UndoStateDelegate::_MarkCurrentStateAsClean()
{
    // empty
}

void UndoStateDelegate::_MarkCurrentStateAsDirty()
{
    // empty
}

void UndoStateDelegate::_OnSetLayer(const SdfLayerHandle& layer) {}

void UndoStateDelegate::_OnSetField(const SdfPath& path, const TfToken& field_name, const VtValue& value)
{
    on_set_field_impl(path, field_name);
}

void UndoStateDelegate::_OnSetField(const SdfPath& path, const TfToken& field_name, const SdfAbstractDataConstValue& value)
{
    on_set_field_impl(path, field_name);
}

void UndoStateDelegate::_OnSetFieldDictValueByKey(const SdfPath& path, const TfToken& fieldName, const TfToken& keyPath, const VtValue& value)
{
    on_set_field_dict_value_by_key_impl(path, fieldName, keyPath);
}

void UndoStateDelegate::_OnSetFieldDictValueByKey(const SdfPath& path, const TfToken& fieldName, const TfToken& keyPath,
                                                  const SdfAbstractDataConstValue& value)
{
    on_set_field_dict_value_by_key_impl(path, fieldName, keyPath);
}

void UndoStateDelegate::_OnSetTimeSample(const SdfPath& path, double time, const VtValue& value)
{
    on_set_time_sample_impl(path, time);
}

void UndoStateDelegate::_OnSetTimeSample(const SdfPath& path, double time, const SdfAbstractDataConstValue& value)
{
    on_set_time_sample_impl(path, time);
}

void UndoStateDelegate::_OnCreateSpec(const SdfPath& path, SdfSpecType specType, bool inert)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    add_inverse(std::make_shared<UsdEdit>(std::bind(&UndoStateDelegate::invert_create_spec, m_state_delegate_proxy, path, inert)));
}

void UndoStateDelegate::_OnDeleteSpec(const SdfPath& path, bool inert)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    SdfDataRefPtr deleted_data = TfCreateRefPtr(new SdfData);
    m_state_delegate_proxy->get_layer()->Traverse(
        path, [src = m_state_delegate_proxy->get_layer_data(), dst = get_pointer(deleted_data)](const SdfPath& path) { copy_spec(*src, dst, path); });

    const auto deleted_spec_type = m_state_delegate_proxy->get_layer()->GetSpecType(path);
    add_inverse(std::make_shared<UsdEdit>(
        std::bind(&UndoStateDelegate::invert_delete_spec, m_state_delegate_proxy, path, inert, deleted_spec_type, deleted_data)));
}

void UndoStateDelegate::_OnMoveSpec(const SdfPath& oldPath, const SdfPath& newPath)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    add_inverse(std::make_shared<UsdEdit>(std::bind(&UndoStateDelegate::invert_move_spec, m_state_delegate_proxy, oldPath, newPath)));
}

void UndoStateDelegate::_OnPushChild(const SdfPath& parentPath, const TfToken& fieldName, const TfToken& value)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    add_inverse(
        std::make_shared<UsdEdit>(std::bind(&UndoStateDelegate::invert_push_token_child, m_state_delegate_proxy, parentPath, fieldName, value)));
}

void UndoStateDelegate::_OnPushChild(const SdfPath& parentPath, const TfToken& fieldName, const SdfPath& value)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    add_inverse(
        std::make_shared<UsdEdit>(std::bind(&UndoStateDelegate::invert_push_path_child, m_state_delegate_proxy, parentPath, fieldName, value)));
}

void UndoStateDelegate::_OnPopChild(const SdfPath& parentPath, const TfToken& fieldName, const TfToken& oldValue)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    add_inverse(
        std::make_shared<UsdEdit>(std::bind(&UndoStateDelegate::invert_pop_token_child, m_state_delegate_proxy, parentPath, fieldName, oldValue)));
}

void UndoStateDelegate::_OnPopChild(const SdfPath& parentPath, const TfToken& fieldName, const SdfPath& oldValue)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    add_inverse(
        std::make_shared<UsdEdit>(std::bind(&UndoStateDelegate::invert_pop_path_child, m_state_delegate_proxy, parentPath, fieldName, oldValue)));
}

bool UndoStateDelegate::invert_set_field(LayerStateDelegateProxyPtr proxy, const SdfPath& path, const TfToken& field_name, const VtValue& inverse)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert field for expired layer.");
        return false;
    }
    proxy->SetField(path, field_name, inverse);
    return true;
}

bool UndoStateDelegate::invert_set_field_dict_value_by_key(LayerStateDelegateProxyPtr proxy, const SdfPath& path, const TfToken& field_name,
                                                           const TfToken& key_path, const VtValue& inverse)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert field dictionary value for expired layer.");
        return false;
    }
    proxy->SetFieldDictValueByKey(path, field_name, key_path, inverse);
    return true;
}

bool UndoStateDelegate::invert_set_time_sample(LayerStateDelegateProxyPtr proxy, const SdfPath& path, double time, const VtValue& value)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert time sample for expired layer.");
        return false;
    }
    proxy->SetTimeSample(path, time, value);
    return true;
}

bool UndoStateDelegate::invert_create_spec(LayerStateDelegateProxyPtr proxy, const SdfPath& path, bool inert)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert spec creation for expired layer.");
        return false;
    }
    proxy->DeleteSpec(path, inert);
    return true;
}

bool UndoStateDelegate::invert_delete_spec(LayerStateDelegateProxyPtr proxy, const SdfPath& path, bool inert, SdfSpecType deleted_spec_type,
                                           const SdfDataRefPtr& deleted_data)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert spec deletion for expired layer.");
        return false;
    }

    SdfChangeBlock change_block;
    proxy->CreateSpec(path, deleted_spec_type, inert);

    struct SpecCopier : public SdfAbstractDataSpecVisitor
    {
        explicit SpecCopier(SdfAbstractData* dst_)
            : dst(dst_)
        {
        }

        virtual bool VisitSpec(const SdfAbstractData& src, const SdfPath& path)
        {
            copy_spec(src, dst, path);
            return true;
        }

        virtual void Done(const SdfAbstractData&)
        {
            // Do nothing
        }

        SdfAbstractData* const dst = nullptr;
    };

    SpecCopier spec_copier(get_pointer(proxy->get_layer_data()));
    deleted_data->VisitSpecs(&spec_copier);
    return true;
}

bool UndoStateDelegate::invert_move_spec(LayerStateDelegateProxyPtr proxy, const SdfPath& oldPath, const SdfPath& newPath)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert spec move for expired layer.");
        return false;
    }
    proxy->MoveSpec(newPath, oldPath);
    return true;
}

bool UndoStateDelegate::invert_push_token_child(LayerStateDelegateProxyPtr proxy, const SdfPath& parentPath, const TfToken& fieldName,
                                                const TfToken& value)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert push child for expired layer.");
        return false;
    }
    proxy->PopChild(parentPath, fieldName, value);
    return true;
}

bool UndoStateDelegate::invert_pop_token_child(LayerStateDelegateProxyPtr proxy, const SdfPath& parentPath, const TfToken& fieldName,
                                               const TfToken& oldValue)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert pop child for expired layer.");
        return false;
    }
    proxy->PushChild(parentPath, fieldName, oldValue);
    return true;
}

bool UndoStateDelegate::invert_push_path_child(LayerStateDelegateProxyPtr proxy, const SdfPath& parentPath, const TfToken& fieldName,
                                               const SdfPath& value)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert push child for expired layer.");
        return false;
    }
    proxy->PopChild(parentPath, fieldName, value);
    return true;
}

bool UndoStateDelegate::invert_pop_path_child(LayerStateDelegateProxyPtr proxy, const SdfPath& parentPath, const TfToken& fieldName,
                                              const SdfPath& oldValue)
{
    if (proxy.IsExpired())
        return false;

    if (!proxy->get_layer())
    {
        OPENDCC_ERROR("Cannot invert pop child for expired layer.");
        return false;
    }
    proxy->PushChild(parentPath, fieldName, oldValue);
    return true;
}

void UndoStateDelegate::on_set_field_impl(const SdfPath& path, const TfToken& field_name)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    const auto inverse_value = m_state_delegate_proxy->get_layer()->GetField(path, field_name);
    add_inverse(std::make_shared<UsdFieldEdit>(&UndoStateDelegate::invert_set_field, m_state_delegate_proxy, path, field_name, inverse_value));
}

void UndoStateDelegate::on_set_field_dict_value_by_key_impl(const SdfPath& path, const TfToken& field_name, const TfToken& key_path)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    const auto inverse_value = m_state_delegate_proxy->get_layer()->GetFieldDictValueByKey(path, field_name, key_path);
    add_inverse(std::make_shared<UsdEdit>(
        std::bind(&UndoStateDelegate::invert_set_field_dict_value_by_key, m_state_delegate_proxy, path, field_name, key_path, inverse_value)));
}

void UndoStateDelegate::on_set_time_sample_impl(const SdfPath& path, double time)
{
    m_state_delegate_proxy->set_dirty(true);
    if (UndoRouter::get_depth() == 0)
        return;

    if (m_state_delegate_proxy->get_layer()->HasField(path, SdfFieldKeys->TimeSamples))
    {
        VtValue old_value;
        m_state_delegate_proxy->get_layer()->QueryTimeSample(path, time, &old_value);
        add_inverse(std::make_shared<UsdTimeSampleEdit>(&UndoStateDelegate::invert_set_time_sample, m_state_delegate_proxy, path, time, old_value));
    }
    else
    {
        add_inverse(
            std::make_shared<UsdFieldEdit>(&UndoStateDelegate::invert_set_field, m_state_delegate_proxy, path, SdfFieldKeys->TimeSamples, VtValue()));
    }
}

TfToken UndoStateDelegate::get_name()
{
    return UndoStateDelegateTokens->UndoStateDelegate;
}

UndoStateDelegate::UndoStateDelegate(LayerStateDelegateProxyPtr state_delegate_proxy)
    : m_state_delegate_proxy(state_delegate_proxy)
{
}
OPENDCC_NAMESPACE_CLOSE
