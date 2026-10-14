// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/core_properties.h"

OPENDCC_NAMESPACE_OPEN

std::unordered_map<std::string, std::any> s_core_property_defaults = {
    { "base.name", std::any(std::string("")) },           { "base.first_entry_point", std::any(std::string("cpp")) },
    { "base.root", std::any(std::string(".")) },          { "base.unloadable", std::any(false) },
    { "dependencies", std::any(PackageAnyDictionary()) }, { "native.entry_point", std::any(PackageAnyArray()) },
    { "native.load", std::any(PackageAnyArray()) },       { "python.entry_point", std::any(PackageAnyArray()) },
    { "python.import", std::any(PackageAnyArray()) },
};

OPENDCC_NAMESPACE_CLOSE
