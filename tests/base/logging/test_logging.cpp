/*
 * Copyright Contributors to the OpenDCC project
 * SPDX-License-Identifier: Apache-2.0
 */
#include "opendcc/base/logging/logger.h"
#include "opendcc/base/logging/logging_delegate.h"
#include "logging_test_header.h"
#include <thread>
#define DOCTEST_CONFIG_NO_SHORT_MACRO_NAMES
#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#define DOCTEST_CONFIG_IMPLEMENTATION_IN_DLL
#include <doctest/doctest.h>
OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LOG_CHANNEL("opendcc.logging_test.source");
using base::logging::LoggingDelegate;
using base::logging::MessageContext;
using base::logging::LogLevel;
using base::logging::Logger;

namespace
{
    struct Capture : LoggingDelegate
    {
        std::vector<MessageContext> records;
        LogLevel previous = Logger::get_log_level();
        Capture()
        {
            Logger::add_logging_delegate(this);
            Logger::set_log_level(LogLevel::Debug);
        }
        ~Capture()
        {
            Logger::set_log_level(previous);
            Logger::clear_channel_log_level("opendcc.logging_test");
            Logger::clear_channel_log_level("opendcc.logging_test.source");
        }
        void log(const MessageContext& context, const std::string&) override { records.push_back(context); }
    };
}
DOCTEST_TEST_SUITE("LoggingNative")
{
    DOCTEST_TEST_CASE_FIXTURE(Capture, "local_source_and_header_channels_are_independent")
    {
        OPENDCC_ERROR("source message");
        log_from_test_header();
        std::thread worker([] { OPENDCC_WARN("worker message"); });
        worker.join();
        DOCTEST_REQUIRE(records.size() == 3);
        DOCTEST_CHECK(records[0].channel == "opendcc.logging_test.source");
        DOCTEST_CHECK(records[1].channel == "opendcc.logging_test.header");
        DOCTEST_CHECK(records[2].channel == records[0].channel);
        DOCTEST_CHECK(std::string(records[0].file) != records[1].file);
        DOCTEST_CHECK(std::string(Logger::get_source_channel("unregistered.cpp", "library.default")) == "library.default");
    }
    DOCTEST_TEST_CASE_FIXTURE(Capture, "inherited_levels_filter_records_and_child_overrides_win")
    {
        Logger::set_channel_log_level("opendcc.logging_test", LogLevel::Warning);
        OPENDCC_INFO("filtered");
        log_from_test_header();
        DOCTEST_REQUIRE(records.size() == 1);
        Logger::set_channel_log_level("opendcc.logging_test.source", LogLevel::Debug);
        OPENDCC_DEBUG("debug");
        OPENDCC_INFO("info");
        DOCTEST_REQUIRE(records.size() == 3);
        DOCTEST_CHECK(records[1].level == LogLevel::Debug);
        DOCTEST_CHECK(records[2].level == LogLevel::Info);
        DOCTEST_CHECK(Logger::get_channel_log_level("opendcc.logging_test.source.child") == LogLevel::Debug);
        DOCTEST_CHECK(Logger::get_channel_log_level("opendcc.logging_test_sibling") == LogLevel::Debug);
        Logger::clear_channel_log_level("opendcc.logging_test.source");
        OPENDCC_INFO("filtered again");
        DOCTEST_CHECK(records.size() == 3);
        DOCTEST_CHECK(Logger::get_channel_log_level("opendcc.logging_test.source") == LogLevel::Warning);
        Logger::clear_channel_log_level("opendcc.logging_test");
        Logger::set_log_level(LogLevel::Info);
        OPENDCC_DEBUG("filtered debug");
        OPENDCC_INFO("visible info");
        DOCTEST_CHECK(records.size() == 4);
    }
    DOCTEST_TEST_CASE("source_channels_remain_valid_when_more_libraries_register")
    {
        Logger::register_source_channel("test-stable-source", "opendcc.logging_test.stable");
        const auto channel = Logger::get_source_channel("test-stable-source", nullptr);
        for (int i = 0; i < 256; ++i)
            Logger::register_source_channel(("test-source-" + std::to_string(i)).c_str(), "opendcc.logging_test.additional");
        DOCTEST_CHECK(std::string(channel) == "opendcc.logging_test.stable");
        DOCTEST_CHECK_FALSE(Logger::register_source_channel("test-stable-source", channel));
    }
}
OPENDCC_NAMESPACE_CLOSE
