// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/packaging/toml_parser.h"
#include <pxr/base/vt/array.h>
#include <fstream>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

DOCTEST_TEST_SUITE("PackagingTOMLParserTests")
{
    DOCTEST_TEST_CASE("deserialization")
    {

        char tmp_filename[1024] = {};
        tmpnam(tmp_filename);
        std::string test_file(tmp_filename);
        auto write_toml = [&test_file](const std::string& text) {
            std::ofstream out(test_file);
            out << text;
        };
        PackageData actual;
        auto validate_attr = [&actual](const std::string& name, const VtValue& expected) {
            for (const auto& attr : actual.raw_attributes)
            {
                if (attr.name == name)
                {
                    DOCTEST_CHECK_EQ(attr.value, expected);
                }
            }
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
            write_toml(text);

            auto actual = parser.parse(test_file);
            DOCTEST_CHECK_EQ(actual.name, std::string("asdf"));
            validate_attr("name", VtValue("asdf"));
            validate_attr("bool", VtValue(true));
            validate_attr("int", VtValue(1ll));
            validate_attr("float", VtValue(3.1415));
            validate_attr("str", VtValue("some str"));
            validate_attr("arr", VtValue(VtArray<int64_t> { 1, 2, 3 }));
            validate_attr("mixed_arr", VtValue(VtArray<VtValue> { VtValue(3.41), VtValue(0ll) }));
            validate_attr("table", VtValue(VtDictionary { { std::string("val1"), VtValue("cx") },
                                                          { std::string("val2"), VtValue(3ll) },
                                                          { std::string("inline"), VtValue(VtDictionary { { std::string("a"), VtValue("a") },
                                                                                                          { std::string("b"), VtValue("b") } }) } }));
            validate_attr("table_arr", VtValue(VtArray<VtDictionary> {
                                           VtDictionary { { std::string("a"), VtValue(42ll) },
                                                          { std::string("val"), VtValue(VtDictionary { { std::string("a.b"), VtValue(64ll) } }) } },
                                           VtDictionary { { std::string("c"), VtValue("c") } } }));
        }
        std::remove(tmp_filename);
    }
}

OPENDCC_NAMESPACE_CLOSE
