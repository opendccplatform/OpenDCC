// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/base/qt_python.h"
#include <pxr/base/tf/type.h>
#include <pxr/base/arch/env.h>

#include "opendcc/app/core/application.h"
#include "entry_point.h"
#include "session.h"
#ifdef OPENDCC_EXPRESSIONS_USE_COMPUTE_GRAPH
#include "expression_compute_node.h"
#endif

#include <regex>

PXR_NAMESPACE_USING_DIRECTIVE

OPENDCC_NAMESPACE_OPEN
OPENDCC_INITIALIZE_LIBRARY_LOG_CHANNEL(OPENDCC_PACKAGE_NAME);

static bool is_f_variable_old_mode()
{
    if (auto env_value = getenv("OPENDCC_EXPRESSION_F_VARIABLE_OLD_MODE"))
    {
        auto value = std::string(env_value);
        return value == "1" || value == "ON" || value == "on";
    }
    return false;
}

static bool to_string(const VtValue& value, std::string& dest)
{
    std::stringstream stream;

    stream << value;

    dest = stream.str();

    return stream.good();
}

// Evaluate a variable with leading zeros.
// An expression with a variable contains ${Fn}, where n is an integer value from 0 to 999.
// This variable is replaced by the frame number with the specified total number of digits.
// If the length of the frame number is less than that specified in the variable, then the missing digits are padded with leading zeros.
//
// Examples:
//    head_${F0}.png -> head_1.png
//    head_${F1}.png -> head_1.png
//    head_${F2}.png -> head_01.png
//    head_${F5}.png -> head_00001.png
//
// Note: You can revert to the old mode (where F is equivalent to F4, and Fn is not evaluated)
//       by setting the environment variable OPENDCC_EXPRESSION_F_VARIABLE_OLD_MODE=ON
static bool evaluate_f_num(const ExpressionContext& context, const std::string& key, std::string& value)
{
    // Check the key in format Fn, where n is a value from 2 to 999
    if (std::regex_search(key, std::regex(R"(^F(\d|[1-9]\d\d?)$)", std::regex_constants::ECMAScript | std::regex_constants::icase)))
    {
        std::smatch m;
        // Search for the number in the key to determine the count of leading zeros used for padding
        if (std::regex_search(key, m, std::regex(R"(\d+)", std::regex_constants::ECMAScript | std::regex_constants::icase)))
        {
            const size_t num_zero = stoi(m[0].str());
            std::string f_value;
            // Get the value of variable F
            to_string(context.variables.at("F"), f_value);
            // Padded with leading zeros
            value = std::string(num_zero - std::min(num_zero, f_value.length()), '0') + f_value;
            return !value.empty();
        }
    }
    return false;
}

static bool evaluate_by_context(const ExpressionContext& context, const std::string& key, std::string& value)
{
    auto it = context.variables.find(key);

    if (it != context.variables.end())
    {
        const VtValue& vt_value = it->second;
        return to_string(vt_value, value);
    }

    if (!is_f_variable_old_mode())
        return evaluate_f_num(context, key, value);
    else
        return false;
}

static bool evaluate_by_environment(const ExpressionContext& context, const std::string& key, std::string& value)
{
    value = ArchGetEnv(key.c_str());

    return !value.empty();
}

OPENDCC_DEFINE_PACKAGE_ENTRY_POINT(ExpressionEntryPoint);

void ExpressionEntryPoint::initialize(const Package& package)
{
    auto& session = ExpressionSession::instance();

    session.add_evaluate_function(evaluate_by_context);
    session.add_evaluate_function(evaluate_by_environment);
#ifdef OPENDCC_EXPRESSIONS_USE_COMPUTE_GRAPH
    ComputeNodeFactory::instance().register_node(expr_compute_tokens->eval_expression, make_compute_node_descriptor());
#endif
}

void ExpressionEntryPoint::uninitialize(const Package& package) {}

OPENDCC_NAMESPACE_CLOSE
