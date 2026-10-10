// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/interval_vector.h"

#include <stdint.h>

OPENDCC_NAMESPACE_USING

#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
// Note: this define should be used once per shared lib
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>

using UIntIntervalVector = IntervalVector<uint32_t>;
DOCTEST_TEST_SUITE("IntervalVector")
{
    DOCTEST_TEST_CASE("empty")
    {
        UIntIntervalVector empty;
        DOCTEST_CHECK(empty.empty());
    }

    DOCTEST_TEST_CASE("insert_1_value")
    {
        UIntIntervalVector vector;

        vector.insert(2);
        DOCTEST_CHECK(vector.size() == 1);
        DOCTEST_CHECK(vector.contains(2));

        vector.insert(3);
        DOCTEST_CHECK(vector.size() == 2);
        DOCTEST_CHECK(vector.interval_count() == 1);
        DOCTEST_CHECK(vector.contains(3));

        vector.insert(4);
        DOCTEST_CHECK(vector.size() == 3);
        DOCTEST_CHECK(vector.interval_count() == 1);
        DOCTEST_CHECK(vector.contains(4));

        vector.insert(6);
        DOCTEST_CHECK(vector.size() == 4);
        DOCTEST_CHECK(vector.interval_count() == 2);
        DOCTEST_CHECK(vector.contains(6));

        vector.insert(7);
        DOCTEST_CHECK(vector.size() == 5);
        DOCTEST_CHECK(vector.interval_count() == 2);
        DOCTEST_CHECK(vector.contains(7));

        auto insert_left = vector;
        insert_left.insert(0); // [0, (2,4), (6,7) ]
        DOCTEST_CHECK(insert_left.size() == 6);
        DOCTEST_CHECK(insert_left.interval_count() == 3);
        DOCTEST_CHECK(insert_left.contains(0));

        auto insert_left_adjacent = vector;
        insert_left_adjacent.insert(1); // [(1,4), (6,7) ]
        DOCTEST_CHECK(insert_left_adjacent.size() == 6);
        DOCTEST_CHECK(insert_left_adjacent.interval_count() == 2);
        DOCTEST_CHECK(insert_left_adjacent.contains(1));

        auto insert_into_range = vector;
        insert_into_range.insert(3); // [(2,4), (6,7) ]
        DOCTEST_CHECK(insert_into_range.size() == 5);
        DOCTEST_CHECK(insert_into_range.interval_count() == 2);
        DOCTEST_CHECK(insert_into_range.contains(3));

        auto insert_right_adjacent = vector;
        insert_right_adjacent.insert(8); // [(2,4), (6,8) ]
        DOCTEST_CHECK(insert_right_adjacent.size() == 6);
        DOCTEST_CHECK(insert_right_adjacent.interval_count() == 2);
        DOCTEST_CHECK(insert_right_adjacent.contains(8));

        auto insert_into_middle = vector;
        insert_into_middle.insert(5); // [(2,7) ]
        DOCTEST_CHECK(insert_into_middle.size() == 6);
        DOCTEST_CHECK(insert_into_middle.interval_count() == 1);
        DOCTEST_CHECK(insert_into_middle.contains(5));
    }
    DOCTEST_TEST_CASE("insert_collection")
    {
        UIntIntervalVector vector;
        std::vector<uint32_t> other;

        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.empty());

        other = { 1 };
        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.size() == 1);
        DOCTEST_CHECK(vector.contains(1));

        other = { 1, 2, 2, 3, 4, 4 };
        vector.clear();
        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.size() == 4);
        DOCTEST_CHECK(vector.interval_count() == 1);
        for (const auto& val : other)
            DOCTEST_CHECK(vector.contains(val));

        other = { 1, 3, 3, 5, 6, 8, 10, 11, 12, 12 };
        vector.clear();
        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.size() == 8);
        DOCTEST_CHECK(vector.interval_count() == 5); // [ 1, 3, (5,6), 8, (10, 12) ]
        for (const auto& val : other)
            DOCTEST_CHECK(vector.contains(val));

        other = {};
        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.size() == 8);
        DOCTEST_CHECK(vector.interval_count() == 5); // [ 1, 3, (5,6), 8, (10, 12) ]
        for (const auto& val : other)
            DOCTEST_CHECK(vector.contains(val));

        other = { 1, 1, 1, 3, 8 };
        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.size() == 8);
        DOCTEST_CHECK(vector.interval_count() == 5); // [ 1, 3, (5,6), 8, (10, 12) ]
        for (const auto& val : other)
            DOCTEST_CHECK(vector.contains(val));

        other = { 2, 2, 4, 4, 9 };
        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.size() == 11);
        DOCTEST_CHECK(vector.interval_count() == 2); // [ (1, 6), (8, 12) ]
        for (const auto& val : { 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 })
            DOCTEST_CHECK(vector.contains(val));

        other = { 0, 0, 0, 0, 0, 13, 14 };
        vector.insert(other.begin(), other.end());
        DOCTEST_CHECK(vector.size() == 14);
        DOCTEST_CHECK(vector.interval_count() == 2); // [ (0, 6), (8, 14) ]
        for (const auto& val : { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14 })
            DOCTEST_CHECK(vector.contains(val));
    }
    DOCTEST_TEST_CASE("insert_interval_vector")
    {
        UIntIntervalVector vector;
        UIntIntervalVector other;

        vector.insert(other);
        DOCTEST_CHECK(vector.empty());

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1 });
        vector.insert(other);
        DOCTEST_CHECK(vector.size() == 1);
        DOCTEST_CHECK(vector.contains(1));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 3, 4, 5, 9, 10, 11 });
        vector.clear();
        vector.insert(other);
        DOCTEST_CHECK(vector.size() == 6);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto& val : { 3, 4, 5, 9, 10, 11 }) // [(3,5), (9,11)]
            DOCTEST_CHECK(vector.contains(val));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 0 });
        auto insert_left = vector;
        insert_left.insert(other);
        DOCTEST_CHECK(insert_left.size() == 7);
        DOCTEST_CHECK(insert_left.interval_count() == 3);
        for (const auto& val : { 0, 3, 4, 5, 9, 10, 11 }) // [0, (3,5), (9,11)]
            DOCTEST_CHECK(insert_left.contains(val));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 0, 1 });
        insert_left = vector;
        insert_left.insert(other);
        DOCTEST_CHECK(insert_left.size() == 8);
        DOCTEST_CHECK(insert_left.interval_count() == 3);
        for (const auto& val : { 0, 1, 3, 4, 5, 9, 10, 11 }) // [(0, 1) (3,5), (9,11)]
            DOCTEST_CHECK(insert_left.contains(val));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 3, 4 });
        auto insert_into = vector;
        insert_into.insert(other);
        DOCTEST_CHECK(insert_into.size() == 6);
        DOCTEST_CHECK(insert_into.interval_count() == 2);
        for (const auto& val : { 3, 4, 5, 9, 10, 11 }) // [(3,5), (9,11)]
            DOCTEST_CHECK(insert_into.contains(val));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 5, 6 });
        auto insert_right = vector;
        insert_right.insert(other);
        DOCTEST_CHECK(insert_right.size() == 7);
        DOCTEST_CHECK(insert_right.interval_count() == 2);
        for (const auto& val : { 3, 4, 5, 6, 9, 10, 11 }) // [(3,6), (9,11)]
            DOCTEST_CHECK(insert_right.contains(val));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 7 });
        auto insert_middle = vector;
        insert_middle.insert(other);
        DOCTEST_CHECK(insert_middle.size() == 7);
        DOCTEST_CHECK(insert_middle.interval_count() == 3);
        for (const auto& val : { 3, 4, 5, 7, 9, 10, 11 }) // [(3,5), 7, (9,11)]
            DOCTEST_CHECK(insert_middle.contains(val));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 5, 6, 7, 8 });
        auto insert_concat = vector;
        insert_concat.insert(other);
        DOCTEST_CHECK(insert_concat.size() == 9);
        DOCTEST_CHECK(insert_concat.interval_count() == 1);
        for (const auto& val : { 3, 4, 5, 6, 7, 8, 9, 10, 11 }) // [(3,11)]
            DOCTEST_CHECK(insert_concat.contains(val));

        other = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 13, 14 });
        auto insert_end = vector;
        insert_end.insert(other);
        DOCTEST_CHECK(insert_end.size() == 8);
        DOCTEST_CHECK(insert_end.interval_count() == 3);
        for (const auto& val : { 3, 4, 5, 9, 10, 11, 13, 14 }) // [(3,5), (9, 11), (13,14)]
            DOCTEST_CHECK(insert_end.contains(val));
    }
    DOCTEST_TEST_CASE("from_intervals")
    {
        auto vector = UIntIntervalVector::from_intervals({ { 1, 2 } });
        DOCTEST_CHECK(vector.size() == 2);
        DOCTEST_CHECK(vector.interval_count() == 1);
        for (const auto val : { 1, 2 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_intervals({ { 1, 2 }, { 1, 2 }, { 4, 7 }, { 4, 7 }, { 0, 2 }, { 3, 9 } });
        DOCTEST_CHECK(vector.size() == 10);
        DOCTEST_CHECK(vector.interval_count() == 1); // [(0,9)]
        for (const auto val : { 0, 1, 2, 3, 4, 5, 6, 7, 9 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_intervals({ { 1, 2 }, { 5, 7 }, { 0, 2 }, { 0, 3 }, { 0, 3 }, { 0, 3 } });
        DOCTEST_CHECK(vector.size() == 7);
        DOCTEST_CHECK(vector.interval_count() == 2); // [(0,3),(5,7)]
        for (const auto val : { 0, 1, 2, 3, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_intervals({ { 2, 2 }, { 7, 9 }, { 7, 9 }, { 2, 2 }, { 5, 5 } });
        DOCTEST_CHECK(vector.size() == 5);
        DOCTEST_CHECK(vector.interval_count() == 3); // [2, 5, (7, 9)]
        for (const auto val : { 2, 5, 7, 8, 9 })
            DOCTEST_CHECK(vector.contains(val));
    }

    DOCTEST_TEST_CASE("erase_1_value")
    {
        UIntIntervalVector vector;

        vector.erase(2);
        DOCTEST_CHECK(vector.size() == 0);

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        vector.erase(0);
        DOCTEST_CHECK(vector.size() == 6);

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        vector.erase(4);
        DOCTEST_CHECK(vector.size() == 6);

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        vector.erase(3);
        DOCTEST_CHECK(vector.size() == 5);
        DOCTEST_CHECK(vector.interval_count() == 2); // [(1,2) (5,7)]

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 5, 6, 7 });
        vector.erase(1);
        DOCTEST_CHECK(vector.size() == 3);
        DOCTEST_CHECK(vector.interval_count() == 1); // [(5,7)]

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        vector.erase(2);
        DOCTEST_CHECK(vector.size() == 5);
        DOCTEST_CHECK(vector.interval_count() == 3); // [1, 3, (5,7)]

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        vector.erase(5);
        DOCTEST_CHECK(vector.size() == 5);
        DOCTEST_CHECK(vector.interval_count() == 2); // [(1,3), (6,7)]
    }
    DOCTEST_TEST_CASE("erase_collection")
    {
        UIntIntervalVector vector;

        std::vector<uint32_t> other = { 1, 3, 6 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK(vector.empty());

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 5 });
        other = { 5 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 0 && vector.interval_count() == 0));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 0, 1 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 5 && vector.interval_count() == 2));
        for (const auto val : { 2, 3, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 2 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 5 && vector.interval_count() == 3));
        for (const auto val : { 1, 3, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 3 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 5 && vector.interval_count() == 2));
        for (const auto val : { 1, 2, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 4 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 6 && vector.interval_count() == 2));
        for (const auto val : { 1, 2, 3, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 8, 8, 9, 9 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 6 && vector.interval_count() == 2));
        for (const auto val : { 1, 2, 3, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 0, 0, 1, 2, 3, 3 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 3 && vector.interval_count() == 1));
        for (const auto val : { 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 3, 3, 3, 4, 4, 5, 5 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 4 && vector.interval_count() == 2));
        for (const auto val : { 1, 2, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 6, 7, 7, 8 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 4 && vector.interval_count() == 2));
        for (const auto val : { 1, 2, 3, 5 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 2, 6, 6, 6 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 4 && vector.interval_count() == 4));
        for (const auto val : { 1, 3, 5, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = { 1, 2, 3, 5, 6, 7 };
        vector.erase(other.begin(), other.end());
        DOCTEST_CHECK((vector.size() == 0 && vector.interval_count() == 0));
    }
    DOCTEST_TEST_CASE("erase_intervals")
    {
        UIntIntervalVector vector;
        UIntIntervalVector other;

        vector.erase(other);
        DOCTEST_CHECK(vector.empty());

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 1, 3 }, { 5, 7 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 0);
        DOCTEST_CHECK(vector.interval_count() == 0);

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 1, 7 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 0);
        DOCTEST_CHECK(vector.interval_count() == 0);

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 0, 8 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 0);
        DOCTEST_CHECK(vector.interval_count() == 0);

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 2, 7 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 1);
        DOCTEST_CHECK(vector.interval_count() == 1);
        for (const auto val : { 1 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 0, 2 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 4);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 3, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 0, 3 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 3);
        DOCTEST_CHECK(vector.interval_count() == 1);
        for (const auto val : { 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 0, 4 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 3);
        DOCTEST_CHECK(vector.interval_count() == 1);
        for (const auto val : { 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 2, 4 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 4);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 1, 5, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 2, 6 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 2);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 1, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 5 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 5);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 1, 2, 3, 6, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 5, 6 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 4);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 1, 2, 3, 7 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7 });
        other = UIntIntervalVector::from_intervals({ { 6, 8 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 4);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 1, 2, 3, 5 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7, 10, 11, 12 });
        other = UIntIntervalVector::from_intervals({ { 0, 2 }, { 4, 7 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 4);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 3, 10, 11, 12 })
            DOCTEST_CHECK(vector.contains(val));

        vector = UIntIntervalVector::from_sorted_collection(std::vector<uint32_t> { 1, 2, 3, 5, 6, 7, 10, 11, 12 });
        other = UIntIntervalVector::from_intervals({ { 0, 5 }, { 7, 8 }, { 9 }, { 11, 12 } });
        vector.erase(other);
        DOCTEST_CHECK(vector.size() == 2);
        DOCTEST_CHECK(vector.interval_count() == 2);
        for (const auto val : { 6, 10 })
            DOCTEST_CHECK(vector.contains(val));
    }

    DOCTEST_TEST_CASE("iterators")
    {
        UIntIntervalVector vector;

        UIntIntervalVector::RangeProxy proxy(vector);
        DOCTEST_CHECK(proxy.empty());
        DOCTEST_CHECK(proxy.size() == 0);
        DOCTEST_CHECK(proxy.begin() == proxy.end());

        {
            vector = UIntIntervalVector::from_intervals({ { 1, 4 } });
            auto proxy = UIntIntervalVector::RangeProxy(vector);
            DOCTEST_CHECK(proxy.size() == 4);
            auto it = proxy.begin();
            for (const auto val : { 1, 2, 3, 4 })
            {
                DOCTEST_CHECK(val == *it);
                ++it;
            }
            DOCTEST_CHECK(it == proxy.end());
        }

        {
            vector = UIntIntervalVector::from_intervals({ { 1, 4 }, { 6, 9 } });
            auto proxy = UIntIntervalVector::RangeProxy(vector);
            DOCTEST_CHECK(proxy.size() == 8);
            auto it = proxy.begin();
            for (const auto val : { 1, 2, 3, 4, 6, 7, 8, 9 })
            {
                DOCTEST_CHECK(val == *it);
                ++it;
            }
            DOCTEST_CHECK(it == proxy.end());
        }
    }
    DOCTEST_TEST_CASE("flatten")
    {
        UIntIntervalVector vector;

        std::vector<uint32_t> flatten;
        DOCTEST_CHECK(vector.flatten<std::vector<uint32_t>>() == flatten);

        vector = UIntIntervalVector::from_intervals({ { 1, 3 }, { 4 }, { 7, 9 } });
        flatten = { 1, 2, 3, 4, 7, 8, 9 };
        DOCTEST_CHECK(vector.flatten<std::vector<uint32_t>>() == flatten);

        std::vector<int> flatten_int = { 1, 2, 3, 4, 7, 8, 9 };
        DOCTEST_CHECK(vector.flatten<std::vector<int>>() == flatten_int);
    }
}
