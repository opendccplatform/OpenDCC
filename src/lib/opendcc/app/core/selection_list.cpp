// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/selection_list.h"
OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

SelectionList::SelectionList()
    : m_data(std::make_shared<SelectionListData>())
{
}

SelectionList::SelectionList(const SdfPathVector& selected_paths)
    : SelectionList()
{
    set_selected_paths(selected_paths);
}

SelectionList::SelectionList(const SelectionMap& sel_map)
    : SelectionList()
{
    m_data->m_prim_selection_map = sel_map;
}

SdfPathVector SelectionList::get_fully_selected_paths() const
{
    SdfPathVector result;
    for (const auto& entry : m_data->m_prim_selection_map)
    {
        if (entry.second.is_fully_selected())
            result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end(), [this](const SdfPath& left, const SdfPath& right) {
        return m_data->m_prim_selection_map[left].get_id() < m_data->m_prim_selection_map[right].get_id();
    });
    return result;
}

const SdfPathVector& SelectionList::get_selected_paths() const
{
    if (m_data->m_selected_paths.empty())
    {
        m_data->m_selected_paths.resize(m_data->m_prim_selection_map.size());
        std::transform(m_data->m_prim_selection_map.begin(), m_data->m_prim_selection_map.end(), m_data->m_selected_paths.begin(),
                       [](const SelectionMap::value_type& entry) { return entry.first; });
        std::sort(m_data->m_selected_paths.begin(), m_data->m_selected_paths.end(), [this](const SdfPath& left, const SdfPath& right) {
            return m_data->m_prim_selection_map[left].get_id() < m_data->m_prim_selection_map[right].get_id();
        });
    }
    return m_data->m_selected_paths;
}

void SelectionList::add_prims(const SdfPathVector& paths)
{
    try_detach();

    if (!paths.empty())
    {
        for (const auto& path : paths)
        {
            auto& data = m_data->m_prim_selection_map[path];
            data.set_fully_selected(true);
            data.increment_id();
        }
        mark_selected_paths_dirty();
    }
}

void SelectionList::remove_prims(const SdfPathVector& paths)
{
    try_detach();

    if (!paths.empty())
    {
        for (const auto& path : paths)
            m_data->m_prim_selection_map.erase(path);
        mark_selected_paths_dirty();
    }
}

void SelectionList::set_selected_paths(const SdfPathVector& new_selection)
{
    try_detach();

    m_data->m_prim_selection_map = {};
    for (const auto& selection : new_selection)
    {
        m_data->m_prim_selection_map[selection] = SelectionData(true);
    }
    mark_selected_paths_dirty();
}

void SelectionList::clear()
{
    try_detach();

    m_data->m_prim_selection_map.clear();
    m_data->m_selected_paths.clear();
}

void SelectionList::set_selection_data(const SdfPath& path, const SelectionData& selection_data)
{
    try_detach();
    if (selection_data.empty())
    {
        m_data->m_prim_selection_map.erase(path);
    }
    else
    {
        selection_data.increment_id();
        m_data->m_prim_selection_map[path] = selection_data;
    }
    mark_selected_paths_dirty();
}

void SelectionList::set_selection_data(const SdfPath& path, SelectionData&& selection_data)
{
    try_detach();
    if (selection_data.empty())
    {
        m_data->m_prim_selection_map.erase(path);
    }
    else
    {
        selection_data.increment_id();
        m_data->m_prim_selection_map[path] = std::move(selection_data);
    }
    mark_selected_paths_dirty();
}

bool SelectionList::equals(const SelectionList& other) const
{
    return m_data->m_prim_selection_map == other.m_data->m_prim_selection_map;
}

void SelectionList::set_full_selection(const SdfPath& path, bool full_selection)
{
    try_detach();

    auto iter = m_data->m_prim_selection_map.find(path);
    if (iter != m_data->m_prim_selection_map.end())
    {
        if (iter->second.is_fully_selected() == full_selection)
            return;

        iter->second.set_fully_selected(full_selection);
        if (iter->second.empty())
        {
            m_data->m_prim_selection_map.erase(iter);
            mark_selected_paths_dirty();
        }
    }
    else if (full_selection)
    {
        m_data->m_prim_selection_map.emplace(path, full_selection);
        mark_selected_paths_dirty();
    }
}

void SelectionList::merge(const SelectionList& selection_list, SelectionMask merge_mask)
{
    if (merge_mask == SelectionFlags::NONE)
        return;

    try_detach();

    for (const auto& selection_entry : selection_list)
    {
        const auto& path = selection_entry.first;
        const auto& selection_data = selection_entry.second;
        auto cur_data_iter = m_data->m_prim_selection_map.find(path);
        if (cur_data_iter == m_data->m_prim_selection_map.end())
        {
            SelectionData selection(false);

            if (merge_mask & SelectionFlags::FULL_SELECTION)
                selection.set_fully_selected(selection_data.is_fully_selected());
            if (merge_mask & SelectionFlags::INSTANCES)
                selection.set_instance_indices(selection_data.get_instance_index_intervals());
            if (merge_mask & SelectionFlags::EDGES)
                selection.set_edge_indices(selection_data.get_edge_index_intervals());
            if (merge_mask & SelectionFlags::ELEMENTS)
                selection.set_element_indices(selection_data.get_element_index_intervals());
            if (merge_mask & SelectionFlags::POINTS)
                selection.set_point_indices(selection_data.get_point_index_intervals());
            if (merge_mask & SelectionFlags::PROPERTIES)
                selection.set_properties(selection_data.get_properties());

            if (!selection.empty())
            {
                m_data->m_prim_selection_map.emplace(path, selection);
                mark_selected_paths_dirty();
            }
        }
        else
        {
            auto& current_data = cur_data_iter->second;

            if (merge_mask & SelectionFlags::FULL_SELECTION)
                current_data.set_fully_selected(current_data.is_fully_selected() || selection_data.is_fully_selected());
            if (merge_mask & SelectionFlags::INSTANCES)
                current_data.add_instance_indices(selection_data.get_instance_index_intervals());
            if (merge_mask & SelectionFlags::EDGES)
                current_data.add_edge_indices(selection_data.get_edge_index_intervals());
            if (merge_mask & SelectionFlags::ELEMENTS)
                current_data.add_element_indices(selection_data.get_element_index_intervals());
            if (merge_mask & SelectionFlags::POINTS)
                current_data.add_point_indices(selection_data.get_point_index_intervals());
            if (merge_mask & SelectionFlags::PROPERTIES)
                current_data.add_properties(selection_data.get_properties());
        }
    }
}

void SelectionList::difference(const SelectionList& selection_list, SelectionMask merge_mask)
{
    if (merge_mask == SelectionFlags::NONE)
        return;

    try_detach();
    for (const auto& selection_entry : selection_list)
    {
        const auto& path = selection_entry.first;
        const auto& selection_data = selection_entry.second;

        auto cur_data_iter = m_data->m_prim_selection_map.find(path);
        if (cur_data_iter == m_data->m_prim_selection_map.end())
        {
            continue;
        }

        // difference
        auto& current_data = cur_data_iter->second;

        if (merge_mask & SelectionFlags::FULL_SELECTION)
            current_data.set_fully_selected(current_data.is_fully_selected() && !selection_data.is_fully_selected());
        if (merge_mask & SelectionFlags::INSTANCES)
            current_data.remove_instance_indices(selection_data.get_instance_index_intervals());
        if (merge_mask & SelectionFlags::EDGES)
            current_data.remove_edge_indices(selection_data.get_edge_index_intervals());
        if (merge_mask & SelectionFlags::ELEMENTS)
            current_data.remove_element_indices(selection_data.get_element_index_intervals());
        if (merge_mask & SelectionFlags::POINTS)
            current_data.remove_point_indices(selection_data.get_point_index_intervals());
        if (merge_mask & SelectionFlags::PROPERTIES)
            current_data.remove_properties(selection_data.get_properties());

        if (current_data.empty())
        {
            m_data->m_prim_selection_map.erase(cur_data_iter);
            mark_selected_paths_dirty();
        }
    }
}

void SelectionList::update(const SelectionList& selection_list, SelectionMask mask /*= SelectionFlags::ALL*/)
{
    if (mask == SelectionFlags::NONE)
        return;

    for (const auto& entry : selection_list)
    {
        const auto& entry_sel_data = entry.second;
        auto& sel_data = m_data->m_prim_selection_map[entry.first];
        const bool is_new_path = sel_data.empty();

        if (mask & SelectionFlags::POINTS)
            sel_data.set_point_indices(entry_sel_data.get_point_index_intervals());
        if (mask & SelectionFlags::EDGES)
            sel_data.set_edge_indices(entry_sel_data.get_edge_index_intervals());
        if (mask & SelectionFlags::ELEMENTS)
            sel_data.set_element_indices(entry_sel_data.get_element_index_intervals());
        if (mask & SelectionFlags::INSTANCES)
            sel_data.set_instance_indices(entry_sel_data.get_instance_index_intervals());
        if (mask & SelectionFlags::FULL_SELECTION)
            sel_data.set_fully_selected(entry_sel_data.is_fully_selected());
        if (mask & SelectionFlags::PROPERTIES)
            sel_data.set_properties(entry_sel_data.get_properties());

        if (is_new_path && sel_data.empty())
            m_data->m_prim_selection_map.erase(entry.first);
        else if (is_new_path && !sel_data.empty())
            mark_selected_paths_dirty();
    }
}

SelectionList SelectionList::extract(SelectionMask mask /*= SelectionFlags::ALL*/) const
{
    SelectionList result;
    if (mask == SelectionFlags::NONE)
        return result;

    result.update(*this, mask);
    return result;
}

SelectionList SelectionList::extract(const SdfPathVector& paths, SelectionMask mask /*= SelectionFlags::ALL*/) const
{
    SelectionList result;
    if (mask == SelectionFlags::NONE)
        return result;

    for (const auto& path : paths)
    {
        auto iter = m_data->m_prim_selection_map.find(path);
        if (iter == m_data->m_prim_selection_map.end())
            continue;

        SelectionData new_data;
        new_data.m_id = iter->second.m_id;
        if (mask & SelectionFlags::POINTS)
            new_data.set_point_indices(iter->second.get_point_index_intervals());
        if (mask & SelectionFlags::EDGES)
            new_data.set_edge_indices(iter->second.get_edge_index_intervals());
        if (mask & SelectionFlags::ELEMENTS)
            new_data.set_element_indices(iter->second.get_element_index_intervals());
        if (mask & SelectionFlags::INSTANCES)
            new_data.set_instance_indices(iter->second.get_instance_index_intervals());
        if (mask & SelectionFlags::FULL_SELECTION)
            new_data.set_fully_selected(iter->second.is_fully_selected());
        if (mask & SelectionFlags::PROPERTIES)
            new_data.set_properties(iter->second.get_properties());
        result.set_selection_data(path, std::move(new_data));
    }
    return result;
}

size_t SelectionList::fully_selected_paths_size() const
{
    return m_data->m_prim_selection_map.size();
}

size_t SelectionList::size() const
{
    return m_data->m_prim_selection_map.size();
}

bool SelectionList::empty() const
{
    return m_data->m_prim_selection_map.empty();
}

bool SelectionList::contains(const SdfPath& path) const
{
    return m_data->m_prim_selection_map.find(path) != m_data->m_prim_selection_map.end();
}

void SelectionList::try_detach()
{
    if (!m_data.unique())
        m_data = std::make_shared<SelectionListData>(*m_data);
    mark_selected_paths_dirty();
}

void SelectionList::mark_selected_paths_dirty()
{
    m_data->m_selected_paths.clear();
}

bool SelectionList::operator==(const SelectionList& other) const
{
    return m_data->m_prim_selection_map == other.m_data->m_prim_selection_map;
}

bool SelectionList::operator!=(const SelectionList& other) const
{
    return !((*this) == other);
}

const SelectionData& SelectionList::get_selection_data(const SdfPath& prim_path) const
{
    static SelectionData empty;
    const auto iter = m_data->m_prim_selection_map.find(prim_path);
    return iter == m_data->m_prim_selection_map.cend() ? empty : iter->second;
}

uint64_t SelectionData::s_global_id = 1;

uint64_t SelectionData::get_id() const
{
    return m_id;
}

void SelectionData::increment_id() const
{
    m_id = s_global_id++;
}

SelectionData::SelectionData(bool prim_selected, const std::vector<IndexType>& point_indices, const std::vector<IndexType>& edge_indices,
                             const std::vector<IndexType>& element_indices, const std::vector<IndexType>& instance_indices,
                             const std::unordered_set<TfToken, TfHash>& properties)
    : m_prim_selected(prim_selected)
    , m_point_indices(IndexIntervals::from_collection(point_indices))
    , m_edge_indices(IndexIntervals::from_collection(edge_indices))
    , m_element_indices(IndexIntervals::from_collection(element_indices))
    , m_instance_indices(IndexIntervals::from_collection(instance_indices))
    , m_properties(properties)
{
    increment_id();
}

SelectionData::SelectionData(bool prim_selected, const VtArray<IndexType>& point_indices, const VtArray<IndexType>& edge_indices,
                             const VtArray<IndexType>& element_indices, const VtArray<IndexType>& instance_indices,
                             const VtArray<TfToken>& properties)
    : m_prim_selected(prim_selected)
    , m_point_indices(IndexIntervals::from_collection(point_indices))
    , m_edge_indices(IndexIntervals::from_collection(edge_indices))
    , m_element_indices(IndexIntervals::from_collection(element_indices))
    , m_instance_indices(IndexIntervals::from_collection(instance_indices))
{
    increment_id();
    m_properties.insert(properties.begin(), properties.end());
}

SelectionData::SelectionData(bool prim_selected, const IndexIntervals& point_indices, const IndexIntervals& edge_indices,
                             const IndexIntervals& element_indices, const IndexIntervals& instance_indices, const VtTokenArray& properties)
    : m_prim_selected(prim_selected)
    , m_point_indices(point_indices)
    , m_edge_indices(edge_indices)
    , m_element_indices(element_indices)
    , m_instance_indices(instance_indices)
{
    increment_id();
    m_properties.insert(properties.begin(), properties.end());
}

bool SelectionData::empty() const
{
    return !m_prim_selected && m_instance_indices.empty() && m_element_indices.empty() && m_edge_indices.empty() && m_point_indices.empty() &&
           m_properties.empty();
}

SelectionData::IndexIntervals::RangeProxy SelectionData::get_instance_indices() const
{
    return m_instance_indices;
}

const OPENDCC_NAMESPACE::SelectionData::IndexIntervals& OPENDCC_NAMESPACE::SelectionData::get_instance_index_intervals() const
{
    return m_instance_indices;
}

void SelectionData::set_instance_indices(const IndexIntervals& instance_indices)
{
    m_instance_indices = instance_indices;
}

SelectionData::IndexIntervals::RangeProxy SelectionData::get_element_indices() const
{
    return m_element_indices;
}

const OPENDCC_NAMESPACE::SelectionData::IndexIntervals& OPENDCC_NAMESPACE::SelectionData::get_element_index_intervals() const
{
    return m_element_indices;
}

void SelectionData::set_element_indices(const IndexIntervals& element_indices)
{
    m_element_indices = element_indices;
}

SelectionData::IndexIntervals::RangeProxy SelectionData::get_edge_indices() const
{
    return m_edge_indices;
}

const OPENDCC_NAMESPACE::SelectionData::IndexIntervals& OPENDCC_NAMESPACE::SelectionData::get_edge_index_intervals() const
{
    return m_edge_indices;
}

void SelectionData::set_edge_indices(const IndexIntervals& edge_indices)
{
    m_edge_indices = edge_indices;
}

SelectionData::IndexIntervals::RangeProxy SelectionData::get_point_indices() const
{
    return m_point_indices;
}

const OPENDCC_NAMESPACE::SelectionData::IndexIntervals& OPENDCC_NAMESPACE::SelectionData::get_point_index_intervals() const
{
    return m_point_indices;
}

void SelectionData::set_point_indices(const IndexIntervals& point_indices)
{
    m_point_indices = point_indices;
}

const std::unordered_set<TfToken, TfHash>& SelectionData::get_properties() const
{
    return m_properties;
}

void SelectionData::set_properties(const std::unordered_set<TfToken, TfHash>& properties)
{
    m_properties = properties;
}

void OPENDCC_NAMESPACE::SelectionData::add_edge_indices(const IndexIntervals& edge_indices)
{
    m_edge_indices.insert(edge_indices);
}

void OPENDCC_NAMESPACE::SelectionData::add_point_indices(const IndexIntervals& point_indices)
{
    m_point_indices.insert(point_indices);
}

void OPENDCC_NAMESPACE::SelectionData::add_element_indices(const IndexIntervals& element_indices)
{
    m_element_indices.insert(element_indices);
}

void OPENDCC_NAMESPACE::SelectionData::add_instance_indices(const IndexIntervals& instance_indices)
{
    m_instance_indices.insert(instance_indices);
}

void SelectionData::add_properties(const std::unordered_set<TfToken, TfHash>& properties)
{
    m_properties.insert(properties.begin(), properties.end());
}

void OPENDCC_NAMESPACE::SelectionData::remove_properties(const std::unordered_set<TfToken, TfHash>& props_to_remove)
{
    for (const auto& property : props_to_remove)
        m_properties.erase(property);
}

void OPENDCC_NAMESPACE::SelectionData::remove_instance_indices(const IndexIntervals& instance_indices)
{
    m_instance_indices.erase(instance_indices);
}

void OPENDCC_NAMESPACE::SelectionData::remove_element_indices(const IndexIntervals& element_indices)
{
    m_element_indices.erase(element_indices);
}

void OPENDCC_NAMESPACE::SelectionData::remove_edge_indices(const IndexIntervals& edge_indices)
{
    m_edge_indices.erase(edge_indices);
}

void OPENDCC_NAMESPACE::SelectionData::remove_point_indices(const IndexIntervals& point_indices)
{
    m_point_indices.erase(point_indices);
}

bool SelectionData::operator!=(const SelectionData& data) const
{
    return !((*this) == data);
}

bool SelectionData::operator==(const SelectionData& data) const
{
    return m_prim_selected == data.m_prim_selected && m_instance_indices == data.m_instance_indices && m_element_indices == data.m_element_indices &&
           m_edge_indices == data.m_edge_indices && m_point_indices == data.m_point_indices && m_properties == data.m_properties;
}

bool SelectionData::is_fully_selected() const
{
    return m_prim_selected;
}

void SelectionData::set_fully_selected(bool fully_selected)
{
    m_prim_selected = fully_selected;
}

OPENDCC_NAMESPACE_CLOSE
