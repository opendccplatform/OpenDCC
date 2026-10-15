// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/ui/logger/usd_logging_delegate.h"

#include "opendcc/app/ui/application_ui.h"

#include <pxr/base/arch/threads.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/arch/stackTrace.h>
#include <pxr/base/tf/pyExceptionState.h>

OPENDCC_NAMESPACE_OPEN
PXR_NAMESPACE_USING_DIRECTIVE

using base::logging::MessageContext;
using base::logging::LogLevel;
using base::logging::Logger;

struct LogEntry
{
    MessageContext ctx;
    std::string msg;
};

static LogEntry get_pretty_string(const PXR_NS::TfDiagnosticBase& diagnostic_base)
{
    const auto& code = diagnostic_base.GetDiagnosticCode();
    const auto& context = diagnostic_base.GetContext();
    const auto& msg = diagnostic_base.GetCommentary();

    std::string output;
    std::string codeName = PXR_NS::TfDiagnosticMgr::GetCodeName(code);
    MessageContext ctx;
    ctx.channel = "opendcc.usd.diagnostics";

    if (context.IsHidden() || !strcmp(context.GetFunction(), "") || !strcmp(context.GetFile(), ""))
    {
        output =
            PXR_NS::TfStringPrintf("%s%s: %s [%s]", codeName.c_str(),
                                   PXR_NS::ArchIsMainThread() ? "" : i18n("logger.usd_logging_delegate", " (secondary thread)").toStdString().c_str(),
                                   msg.c_str(), PXR_NS::ArchGetProgramNameForErrors());
    }
    else
    {
        output = PXR_NS::TfStringPrintf(
            i18n("logger.usd_logging_delegate", "%s%s: %s").toStdString().c_str(), codeName.c_str(),
            PXR_NS::ArchIsMainThread() ? "" : i18n("logger.usd_logging_delegate", " (secondary thread)").toStdString().c_str(), msg.c_str());
        ctx.file = context.GetFile();
        ctx.function = context.GetFunction();
        ctx.line = context.GetLine();
    }

#ifdef PXR_PYTHON_SUPPORT_ENABLED
    const auto& exception = diagnostic_base.GetInfo<PXR_NS::TfPyExceptionState>();
    if (exception != nullptr)
    {
        output += PXR_NS::TfStringPrintf("%s", exception->GetExceptionString().c_str());
    }
#endif // PXR_PYTHON_SUPPORT_ENABLED

    return { ctx, output };
}

void UsdLoggingDelegate::IssueError(const PXR_NS::TfError& err)
{
    if (!err.GetQuiet())
    {
        auto log_entry = get_pretty_string(err);
        log_entry.ctx.level = LogLevel::Error;
        Logger::log(log_entry.ctx, log_entry.msg.c_str());
    }
}

void UsdLoggingDelegate::IssueFatalError(const PXR_NS::TfCallContext& context, const std::string& msg)
{
    std::string output = i18n("logger.usd_logging_delegate", "Fatal error: ").toStdString() + msg + ' ' + PXR_NS::ArchGetProgramNameForErrors();
    MessageContext ctx;
    ctx.channel = "opendcc.usd.diagnostics";
    ctx.level = LogLevel::Fatal;
    ctx.function = context.GetFunction();
    ctx.file = context.GetFile();
    ctx.line = context.GetLine();
    Logger::log(ctx, output.c_str());
}

void UsdLoggingDelegate::IssueStatus(const PXR_NS::TfStatus& status)
{
    if (!status.GetQuiet())
    {
        auto log_entry = get_pretty_string(status);
        log_entry.ctx.level = LogLevel::Info;
        Logger::log(log_entry.ctx, log_entry.msg.c_str());
    }
}

void UsdLoggingDelegate::IssueWarning(const PXR_NS::TfWarning& warning)
{
    if (!warning.GetQuiet())
    {
        auto log_entry = get_pretty_string(warning);
        log_entry.ctx.level = LogLevel::Warning;
        Logger::log(log_entry.ctx, log_entry.msg.c_str());
    }
}

OPENDCC_NAMESPACE_CLOSE
