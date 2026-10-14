/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */

#include "opendcc/base/utils/string_utils.h"
#include <ostream>

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN

DOCTEST_TEST_SUITE("StringUtils")
{
    DOCTEST_TEST_CASE("split_preserves_empty_substrings_unless_requested")
    {
        struct TestCase
        {
            std::string input;
            std::vector<std::string> keep_empty;
            std::vector<std::string> skip_empty;
        };
        const std::vector<TestCase> cases {
            { "", { "" }, {} },
            { ".", { "", "" }, {} },
            { "...", { "", "", "", "" }, {} },
            { "alpha", { "alpha" }, { "alpha" } },
            { "a.b", { "a", "b" }, { "a", "b" } },
            { ".a..b.", { "", "a", "", "b", "" }, { "a", "b" } },
            { " a . b ", { " a ", " b " }, { " a ", " b " } },
        };
        for (const auto& test : cases)
        {
            DOCTEST_INFO("Input: " << test.input);
            DOCTEST_CHECK(split(test.input, '.') == test.keep_empty);
            DOCTEST_CHECK(split(test.input, '.', false) == test.keep_empty);
            DOCTEST_CHECK(split(test.input, '.', true) == test.skip_empty);
        }
        DOCTEST_CHECK(split("/a//b/", '/', true) == std::vector<std::string> { "a", "b" });
    }
}

OPENDCC_NAMESPACE_CLOSE
