// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "wrap_selection_list.h"
#include "opendcc/base/pybind_bridge/usd.h"
#include "opendcc/app/core/selection_list.h"
#include <pxr/base/tf/pyResultConversions.h>
#include <pxr/base/tf/pyContainerConversions.h>
#include <pxr/base/vt/wrapArray.h>
#include <pxr/usd/sdf/types.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE;

using MergeFlags = SelectionList::SelectionFlags;
using IndexType = SelectionList::IndexType;
using VtIndexArray = VtArray<IndexType>;

namespace
{
    template <class T>
    VtArray<T> slice_vtarray(const pybind11::object& src)
    {
        namespace bp = PXR_BOOST_PYTHON_NAMESPACE;
        VtArray<T> result;
        result.resize(len(src));
        Vt_WrapArray::setArraySlice(result, bp::slice(0, result.size()), bp::object(bp::handle<>(bp::borrowed(src.ptr()))));
        return result;
    }
}

struct SelectionListPairToTuple
{
    template <class>
    struct apply
    {
        using type = Tf_PyPairToTupleConverter<typename SelectionList::SelectionMap::key_type, typename SelectionList::SelectionMap::mapped_type>;
    };
};

IntervalVector<IndexType> extract_intervals(const pybind11::object& object)
{
    using namespace pybind11;
    if (isinstance<VtIndexArray>(object))
    {
        return IntervalVector<IndexType>::from_collection(slice_vtarray<IndexType>(object));
    }
    else if (auto iterable = list(object))
    {
        const auto length = len(iterable);
        std::vector<IntervalVector<IndexType>::Interval> intervals;
        intervals.reserve(length);
        for (size_t i = 0; i < length; i++)
        {
            if (isinstance<IndexType>(iterable[i]))
            {
                intervals.emplace_back(iterable[i].cast<IndexType>());
            }
            else if (auto py_tuple = tuple(iterable[i]))
            {
                const auto start = py_tuple[0].cast<IndexType>();
                const auto end = py_tuple[1].cast<IndexType>();
                if (start < end)
                    intervals.emplace_back(start, end);
                else
                    intervals.emplace_back(end, start);
            }
            else
            {
                throw cast_error_unable_to_convert_call_arg(repr(iterable[i]));
            }
        }

        return IntervalVector<IndexType>::from_intervals(intervals);
    }

    return IntervalVector<IndexType>::from_collection(slice_vtarray<IndexType>(object));
}

#define GENERATE_SELECTION_DATA_ACCESSOR(data_name)                   \
    VtValue get_selection_data_##data_name(const SelectionData* self) \
    {                                                                 \
        TfPyLock lock;                                                \
        const auto& cpp_data = self->get_##data_name##_intervals();   \
        return VtValue(cpp_data.flatten<VtUIntArray>());              \
    }

GENERATE_SELECTION_DATA_ACCESSOR(point_index)
GENERATE_SELECTION_DATA_ACCESSOR(edge_index)
GENERATE_SELECTION_DATA_ACCESSOR(element_index)
GENERATE_SELECTION_DATA_ACCESSOR(instance_index)

template <class T>
static std::string get_array_repr(const T& collection)
{
    std::ostringstream stream;
    if (collection.size() > 100)
    {
        auto begin_it = collection.begin();
        stream << "[" << *begin_it << ", " << *(++begin_it) << ", " << *(++begin_it) << ", ..., " << *(++begin_it) << ", " << *(++begin_it) << ", "
               << *(++begin_it) << "]";
    }
    else
    {
        stream << "[";
        for (auto it = collection.begin(); it != collection.end(); ++it)
            stream << (it != collection.begin() ? ", " : "") << pybind11::repr(pybind11::cast(*it));
        stream << "]";
    }

    return stream.str();
}

static std::string get_interval_repr(const IntervalVector<IndexType>& intervals)
{
    std::string result;
    if (intervals.interval_count() > 100)
    {
        std::ostringstream stream;
        auto iter = intervals.begin();
        stream << "[";
        if (iter->start == iter->end)
            stream << iter->start;
        else
            stream << "(" << iter->start << ", " << iter->end << ")";
        for (int i = 1; i < 3; i++, ++iter)
        {
            if (iter->start == iter->end)
                stream << ", " << iter->start;
            else
                stream << ", (" << iter->start << ", " << iter->end << ")";
        }

        stream << ", ...";
        for (iter = intervals.end() - 3; iter != intervals.end(); ++iter)
        {
            if (iter->start == iter->end)
                stream << ", " << iter->start;
            else
                stream << ", (" << iter->start << ", " << iter->end << ")";
        }
        stream << "]";
        return stream.str();
    }
    else
    {
        result.reserve(intervals.size() * 16 + 2); // [(*****, *****), ]  (16 characters) + 2
        result += '[';
        auto iter = intervals.begin();
        if (iter->start == iter->end)
            result += std::to_string(iter->start);
        else
            result += "(" + std::to_string(iter->start) + ", " + std::to_string(iter->end) + ")";

        for (++iter; iter != intervals.end(); ++iter)
        {
            if (iter->start == iter->end)
                result += ", " + std::to_string(iter->start);
            else
                result += ", (" + std::to_string(iter->start) + ", " + std::to_string(iter->end) + ")";
        }
        result += ']';
        result.shrink_to_fit();
        return result;
    }
}

static std::string selection_data_repr(const SelectionData& self)
{
    const std::string full_repr = self.is_fully_selected() ? "full=True" : "";
    const auto points_repr = self.get_point_indices().empty() ? "" : "points=" + get_interval_repr(self.get_point_index_intervals());
    const auto edges_repr = self.get_edge_indices().empty() ? "" : "edges=" + get_interval_repr(self.get_edge_index_intervals());
    const auto elements_repr = self.get_element_indices().empty() ? "" : "elements=" + get_interval_repr(self.get_element_index_intervals());
    const auto instances_repr = self.get_instance_indices().empty() ? "" : "instances=" + get_interval_repr(self.get_instance_index_intervals());
    const auto properties_repr = self.get_properties().empty() ? "" : "properties=" + get_array_repr(self.get_properties());

    std::ostringstream result;
    result << "opendcc.core.SelectionData(";
    bool has_first = false;
    for (const auto& repr : { full_repr, points_repr, edges_repr, elements_repr, instances_repr, properties_repr })
    {
        if (!repr.empty())
        {
            result << (has_first ? ", " : "") << repr;
            has_first = true;
        }
    }
    result << ")";
    return result.str();
}

static std::string selection_list_repr(const SelectionList& self)
{
    std::ostringstream stream;
    stream << "{";
    for (auto it = self.begin(); it != self.end(); ++it)
    {
        stream << (it != self.begin() ? ", " : "") << pybind11::repr(pybind11::cast(it->first.GetString())) << ": "
               << pybind11::repr(pybind11::cast(it->second));
    }
    stream << "}";

    const auto dictionary = stream.str();

    return "opendcc.core.SelectionList(" + (dictionary == "{}" ? "" : dictionary) + ")";
}

static SelectionData* selection_data_constructor(bool py_full, pybind11::object py_points, pybind11::object py_edges, pybind11::object py_elements,
                                                 pybind11::object py_instances, pybind11::object py_properties)
{
    using namespace pybind11;

    using IntervalVector = IntervalVector<SelectionList::IndexType>;
    bool full = py_full;
    IntervalVector points, edges, elements, instances;
    VtTokenArray properties;

    if (!py_points.is_none())
    {
        points = extract_intervals(py_points);
    }
    if (!py_edges.is_none())
    {
        edges = extract_intervals(py_edges);
    }
    if (!py_elements.is_none())
    {
        elements = extract_intervals(py_elements);
    }
    if (!py_instances.is_none())
    {
        instances = extract_intervals(py_instances);
    }
    if (!py_properties.is_none())
    {
        properties = slice_vtarray<TfToken>(py_properties);
    }

    return new SelectionData(full, points, edges, elements, instances, properties);
}

void py_interp::bind::wrap_selection_list(pybind11::module& m)
{
    using namespace pybind11;

    enum_<MergeFlags>(m, "MergeFlags")
        .value("NONE", MergeFlags::NONE)
        .value("POINTS", MergeFlags::POINTS)
        .value("EDGES", MergeFlags::EDGES)
        .value("ELEMENTS", MergeFlags::ELEMENTS)
        .value("INSTANCES", MergeFlags::INSTANCES)
        .value("FULL_SELECTION", MergeFlags::FULL_SELECTION)
        .value("ALL", MergeFlags::ALL)
        .export_values();

    class_<SelectionData>(m, "SelectionData")
        .def(init([](bool full = false, object points = none(), object edges = none(), object elements = none(), object instances = none(),
                     object properties = none()) { return selection_data_constructor(full, points, edges, elements, instances, properties); }),
             arg("full") = false, arg("points") = none(), arg("edges") = none(), arg("elements") = none(), arg("instances") = none(),
             arg("properties") = none())
        .def("empty", &SelectionData::empty)
        .def("__repr__", &selection_data_repr)
        .def_property_readonly("fully_selected", &SelectionData::is_fully_selected)
        .def_property_readonly("point_indices", &get_selection_data_point_index)
        .def_property_readonly("edge_indices", &get_selection_data_edge_index)
        .def_property_readonly("element_indices", &get_selection_data_element_index)
        .def_property_readonly("instance_indices", &get_selection_data_instance_index)
        .def_property_readonly("properties", &SelectionData::get_properties, return_value_policy::reference)
        .def(self == self)
        .def(self != self);

    class_<SelectionList>(m, "SelectionList")
        .def(init<>())
        .def(init<const SdfPathVector&>())
        .def(init<const SelectionList::SelectionMap&>())
        .def("get_fully_selected_paths", &SelectionList::get_fully_selected_paths)
        .def("get_selected_paths", &SelectionList::get_selected_paths, return_value_policy::reference)
        .def("set_fully_selected_paths", &SelectionList::set_selected_paths)
        .def("add_prims", &SelectionList::add_prims)
        .def("add_points", &SelectionList::add_points<VtIntArray>)
        .def("add_edges", &SelectionList::add_edges<VtIntArray>)
        .def("add_elements", &SelectionList::add_elements<VtIntArray>)
        .def("add_instances", &SelectionList::add_instances<VtIntArray>)
        .def("add_properties", overload_cast<const SdfPath&, const VtTokenArray&>(&SelectionList::add_properties))
        .def("remove_prims", &SelectionList::remove_prims)
        .def("remove_points", &SelectionList::remove_points<VtIntArray>)
        .def("remove_edges", &SelectionList::remove_edges<VtIntArray>)
        .def("remove_elements", &SelectionList::remove_elements<VtIntArray>)
        .def("remove_instances", &SelectionList::remove_instances<VtIntArray>)
        .def("remove_properties", overload_cast<const SdfPath&, const VtTokenArray&>(&SelectionList::remove_properties))
        .def("set_full_selection", &SelectionList::set_full_selection)
        .def("merge", &SelectionList::merge, arg("selection_list"), arg("merge_mask") = SelectionList::SelectionFlags::ALL)
        .def("difference", &SelectionList::difference, arg("selection_list"), arg("merge_mask") = SelectionList::SelectionFlags::ALL)
        .def("clear", &SelectionList::clear)
        .def("get_selection_data", &SelectionList::get_selection_data, return_value_policy::reference)
        .def("set_selection_data", overload_cast<const SdfPath&, const SelectionData&>(&SelectionList::set_selection_data))
        .def("size", &SelectionList::size)
        .def("fully_selected_paths_size", &SelectionList::fully_selected_paths_size)
        .def("empty", &SelectionList::empty)
        .def("contains", &SelectionList::contains)
        .def("equals", &SelectionList::equals)
        .def("__len__", &SelectionList::size)
        .def("__contains__", &SelectionList::contains)
        .def("__getitem__", &SelectionList::get_selection_data, return_value_policy::reference)
        .def("__setitem__", overload_cast<const SdfPath&, const SelectionData&>(&SelectionList::set_selection_data))
        .def(self == self)
        .def(self != self)
        .def(
            "__iter__", [](SelectionList& self) { return make_iterator(self.begin(), self.end()); }, keep_alive<0, 1>())
        .def("__repr__", &selection_list_repr);
}

OPENDCC_NAMESPACE_CLOSE
