// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/usd/usd_ipc_serialization/serialization.h"

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

DOCTEST_TEST_SUITE("serialization")
{
    DOCTEST_TEST_CASE("serialize_multiple_basic_types")
    {
        Writer packer;
        packer.write(true);
        packer.write(3);
        packer.write(-87);
        packer.write((size_t)264425);
        packer.write(76.15f);
        packer.write(567.134);
        packer.write('d');

        const auto actual_size = packer.get_buffer().size();
        const auto expected_size = sizeof(bool) + sizeof(int) + sizeof(int) + sizeof(size_t) + sizeof(float) + sizeof(double) + sizeof(char);
        DOCTEST_CHECK(actual_size == expected_size);

        Reader reader(packer.get_buffer());
        DOCTEST_CHECK(reader.read<bool>() == true);
        DOCTEST_CHECK(reader.read<int>() == 3);
        DOCTEST_CHECK(reader.read<int>() == -87);
        DOCTEST_CHECK(reader.read<size_t>() == 264425);
        DOCTEST_CHECK(reader.read<float>() == 76.15f);
        DOCTEST_CHECK(reader.read<double>() == 567.134);
        DOCTEST_CHECK(reader.read<char>() == 'd');
        DOCTEST_CHECK(reader.tell() == expected_size);
    }

    DOCTEST_TEST_CASE("serialize_strings")
    {
        Writer packer;
        packer.write(std::string("/asdfj/fasd45/13hn,;.")); // len 21
        packer.write(SdfPath("/abc/def/ghjk231")); // len 16
        packer.write(TfToken("my_token")); // len 8

        const std::vector<std::string> array_of_strings = {
            "asdfzxcvb", "ncbntqrgqrg", "r4yq38467tq" // len 9, 11, 11
        };
        const std::vector<SdfPath> array_of_paths = {
            SdfPath("/asdf/vgfaf"), SdfPath("/afawef/adfvgar"), SdfPath("/gjuq3/t213opa")
            // len 11, 15, 14
        };
        packer.write_array(array_of_strings);
        packer.write_array(array_of_paths);
        const auto actual_size = packer.get_buffer().size();
        const auto expected_size = sizeof(char) * (21 + 16 + 8 + 9 + 11 + 11 + 11 + 15 + 14) + sizeof(size_t) * 11;
        DOCTEST_CHECK(actual_size == expected_size);

        Reader reader(packer.get_buffer());
        DOCTEST_CHECK(reader.read<std::string>() == "/asdfj/fasd45/13hn,;.");
        DOCTEST_CHECK(reader.read<SdfPath>() == SdfPath("/abc/def/ghjk231"));
        DOCTEST_CHECK(reader.read<TfToken>() == TfToken("my_token"));
        DOCTEST_CHECK(reader.read_array<std::vector<std::string>>() == array_of_strings);
        DOCTEST_CHECK(reader.read_array<std::vector<SdfPath>>() == array_of_paths);
        DOCTEST_CHECK(reader.tell() == expected_size);
    }

    DOCTEST_TEST_CASE("serialize_basic_types_arrays")
    {
        Writer packer;
        packer.write_array(VtIntArray({ 3, 7, -2 }));
        packer.write_array(VtFloatArray({ 76.15, 32.5, -13.43 }));
        packer.write_array(std::vector<double>({ 31, 43.23 }));

        const auto actual_size = packer.get_buffer().size();
        const auto expected_size = sizeof(int) * 3 + sizeof(float) * 3 + sizeof(double) * 2 + sizeof(size_t) * 3;
        DOCTEST_CHECK(actual_size == expected_size);

        Reader reader(packer.get_buffer());
        DOCTEST_CHECK(reader.read_array<VtIntArray>() == VtIntArray({ 3, 7, -2 }));
        DOCTEST_CHECK(reader.read_array<VtFloatArray>() == VtFloatArray({ 76.15, 32.5, -13.43 }));
        DOCTEST_CHECK(reader.read_array<std::vector<double>>() == std::vector<double>({ 31, 43.23 }));
        DOCTEST_CHECK(reader.tell() == expected_size);
    }

    DOCTEST_TEST_CASE("serialize_vtvalue")
    {
        Writer packer;
        packer.write(VtValue(true));
        packer.write(VtValue(5));
        packer.write(VtValue(VtFloatArray({ 76.15, 32.5, -13.43 })));
        packer.write(VtValue(SdfPath("/asdf/xcvb")));
        packer.write(VtValue(GfVec3f(5, 8, 9)));
        packer.write(VtValue(std::string("deus_vult")));
        packer.write(VtValue(VtValue(std::string("tyuio"))));
        packer.write(VtValue());

        Reader reader(packer.get_buffer());
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue(true));
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue(5));
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue(VtFloatArray({ 76.15, 32.5, -13.43 })));
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue(SdfPath("/asdf/xcvb")));
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue(GfVec3f(5, 8, 9)));
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue(std::string("deus_vult")));
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue(VtValue(std::string("tyuio"))));
        DOCTEST_CHECK(reader.read<VtValue>() == VtValue());
    }
}

OPENDCC_NAMESPACE_CLOSE
