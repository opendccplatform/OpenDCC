// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/toml_parser.h"
#include <algorithm>
#include <cstdint>
#include "package_test_utils.h"

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
OPENDCC_NAMESPACE_OPEN

DOCTEST_TEST_SUITE("PackagingTOMLParserTests")
{
    DOCTEST_TEST_CASE("deserialization")
    {

        packaging_tests::TemporaryDirectory directory;
        PackageData actual;
        auto assert_attribute = [&actual](const std::string& name, const std::any& expected) {
            DOCTEST_INFO("Manifest attribute: " << name);
            const auto attr = std::find_if(actual.raw_attributes.begin(), actual.raw_attributes.end(),
                                           [&name](const PackageAttribute& attr) { return attr.name == name; });
            DOCTEST_REQUIRE(attr != actual.raw_attributes.end());
            DOCTEST_CHECK(attr->value.type() == expected.type());
            DOCTEST_CHECK(packaging_tests::values_equal(attr->value, expected));
        };
        auto parser = TOMLParser();

        DOCTEST_SUBCASE("basic_types")
        {
            const auto text = R"(
bool = true
int = 1
float = 3.1415
str = 'some str'
arr = [1, 2, 3]
mixed_arr = [3.41, 0]

[base]
name = 'asdf'

[table]
val1 = 'cx'
val2 = 3
inline = { a = 'a', b = 'c' }

[[table_arr]]
a = 42

[[table_arr.val]]
"a.b" = 64

[[table_arr]]
c = 'c'
)";
            const auto test_file = directory.write("package.toml", text);

            actual = parser.parse(test_file);
            DOCTEST_CHECK_EQ(actual.name, std::string("asdf"));
            assert_attribute("base", std::any(PackageAnyDictionary { { "name", std::any(std::string("asdf")) } }));
            assert_attribute("bool", std::any(true));
            assert_attribute("int", std::any(int64_t { 1 }));
            assert_attribute("float", std::any(3.1415));
            assert_attribute("str", std::any(std::string("some str")));
            assert_attribute("arr", std::any(PackageAnyArray { std::any(int64_t { 1 }), std::any(int64_t { 2 }), std::any(int64_t { 3 }) }));
            assert_attribute("mixed_arr", std::any(PackageAnyArray { std::any(3.41), std::any(int64_t { 0 }) }));

            const PackageAnyDictionary expected_inline_table = { { "a", std::any(std::string("a")) }, { "b", std::any(std::string("c")) } };
            const PackageAnyDictionary expected_table = { { "val1", std::any(std::string("cx")) },
                                                          { "val2", std::any(int64_t { 3 }) },
                                                          { "inline", std::any(expected_inline_table) } };
            assert_attribute("table", std::any(expected_table));

            const PackageAnyArray expected_nested_tables = { PackageAnyDictionary { { "a.b", std::any(int64_t { 64 }) } } };
            const PackageAnyArray expected_table_array = { PackageAnyDictionary { { "a", std::any(int64_t { 42 }) },
                                                                                  { "val", std::any(expected_nested_tables) } },
                                                           PackageAnyDictionary { { "c", std::any(std::string("c")) } } };
            assert_attribute("table_arr", std::any(expected_table_array));
        }
    }

    DOCTEST_TEST_CASE("invalid_manifests")
    {
        packaging_tests::TemporaryDirectory directory;
        TOMLParser parser;
        for (const auto& text : { "[base\nname = 'broken'\n", "[base]\nroot = '.'\n", "[base]\nname = 42\n" })
        {
            DOCTEST_INFO("Invalid manifest: " << text);
            const auto filename = directory.write("package.toml", text);
            DOCTEST_CHECK_FALSE(parser.parse(filename).is_valid());
        }
        DOCTEST_CHECK_FALSE(parser.parse((directory.path() / "missing.toml").generic_u8string()).is_valid());
    }
}

OPENDCC_NAMESPACE_CLOSE
