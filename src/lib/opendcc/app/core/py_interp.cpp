// Copyright Contributors to the OpenDCC project
// SPDX-License-Identifier: Apache-2.0

#include "opendcc/app/core/py_interp.h"
#include <opendcc/base/vendor/ghc/filesystem.hpp>
#include <pxr/pxr.h>
#include <pxr/base/tf/pyInterpreter.h>
#include <pxr/base/tf/pyError.h>
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/arch/systemInfo.h>
#include "opendcc/app/core/application.h"
#include "opendcc/base/defines.h"
#include <locale>
#include <codecvt>
#include <pxr/base/tf/scriptModuleLoader.h>
#include <pxr/base/arch/threads.h>
#include "opendcc/base/pybind_bridge/pybind11.h"
#ifndef OPENDCC_OS_WINDOWS
#include <signal.h>
#endif

OPENDCC_NAMESPACE_OPEN

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{
    // This function is taken from Pixar's pyInterpreter.cpp with slightly changes
    // for unicode reading of current executable path.
    void py_initialize()
    {
        static std::once_flag once;
        std::call_once(once, [] {
            // This mutex is, sadly, recursive since the call to the scriptModuleLoader
            // at the end of this function can end up reentering this function, while
            // importing python modules.  In this case we'll quickly return since
            // Py_IsInitialized will return true, but we need to keep other threads from
            // entering.
            if (!Py_IsInitialized())
            {

                if (!ArchIsMainThread() && !PyEval_ThreadsInitialized())
                {
                    // Python claims that PyEval_InitThreads "should be called in the
                    // main thread before creating a second thread or engaging in any
                    // other thread operations."  So we'll issue a warning here.
                    OPENDCC_WARN("Calling PyEval_InitThreads() for the first time outside "
                                 "the 'main thread'.  Python doc says not to do this.");
                }

                const std::string s = ArchGetExecutablePath();

#if PY_MAJOR_VERSION == 2
                // In Python 2 it is safe to call this before Py_Initialize(), but this
                // is no longer true in python 3.
                //
                // Initialize Python threading.  This grabs the GIL.  We'll release it
                // at the end of this function.
                PyEval_InitThreads();
#endif

                // Setting the program name is necessary in order for python to
                // find the correct built-in modules.
#if PY_MAJOR_VERSION == 2
                static std::string programName(s.begin(), s.end());
                Py_SetProgramName(const_cast<char*>(programName.c_str()));
#else
                std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                static const auto program_name_w = converter.from_bytes(s.c_str());
                Py_SetProgramName(const_cast<wchar_t*>(program_name_w.c_str()));
#endif

                // We're here when this is a C++ program initializing python (i.e. this
                // is a case of "embedding" a python interpreter, as opposed to
                // "extending" python with extension modules).
                //
                // In this case we don't want python to change the sigint handler.  Save
                // it before calling Py_Initialize and restore it after.
#if !defined(ARCH_OS_WINDOWS)
                struct sigaction origSigintHandler;
                sigaction(SIGINT, NULL, &origSigintHandler);
#endif
                Py_Initialize();

#if !defined(ARCH_OS_WINDOWS)
                // Restore original sigint handler.
                sigaction(SIGINT, &origSigintHandler, NULL);
#endif

#if PY_MAJOR_VERSION > 2
                // In python 3 PyEval_InitThreads must be called after Py_Initialize()
                // see https://docs.python.org/3/c-api/init.html
                //
                // Initialize Python threading.  This grabs the GIL.  We'll release it
                // at the end of this function.
                PyEval_InitThreads();
#endif

#if PY_MAJOR_VERSION == 2
                char emptyArg[] = { '\0' };
                char* empty[] = { emptyArg };
#else
                wchar_t emptyArg[] = { '\0' };
                wchar_t *empty[] = { emptyArg };
#endif
                PySys_SetArgv(1, empty);

                // Kick the module loading mechanism for any loaded libs that have
                // corresponding python binding modules.  We do this after we've
                // published that we're done initializing as this may reenter
                // TfPyInitialize().
                TfScriptModuleLoader::GetInstance().LoadModules();

                // Release the GIL and restore thread state.
                // When TfPyInitialize returns, we expect GIL is released
                // and python's internal PyThreadState is NULL
                // Previously this only released the GIL without resetting the ThreadState
                // This can lead to a situation where python executes without the GIL
                // PyGILState_Ensure checks the current thread state and decides
                // to take the lock based on that; so if the GIL is released but the
                // current thread is valid it leads to cases where python executes
                // without holding the GIL and mismatched lock/release calls in TfPyLock
                // (See the discussion in 141041)

                PyThreadState* currentState = PyGILState_GetThisThreadState();
                PyEval_ReleaseThread(currentState);
            }
        });
    }
}

void py_interp::init_py_interp(std::vector<std::string>& args)
{
#ifdef OPENDCC_EMBEDDED_PYTHON_HOME
#ifdef OPENDCC_OS_WINDOWS
    static auto python_home_utf8 = Application::instance().get_application_root_path() + "/python/";
#else
    static auto python_home_utf8 = Application::instance().get_application_root_path();
#endif
#if PY_MAJOR_VERSION >= 3
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    static auto python_home_str = converter.from_bytes(python_home_utf8.c_str());
    Py_SetPythonHome(const_cast<wchar_t*>(python_home_str.c_str()));
#else
    Py_SetPythonHome(const_cast<char*>(python_home_utf8.c_str()));
#endif
#endif
    py_initialize();
    if (args.size() > 0)
    {
        TfPyLock lock;
        std::vector<char*> _argv;
        for (auto& arg : args)
            _argv.push_back(&arg.front());

#if PY_MAJOR_VERSION >= 3
        // https://github.com/AcademySoftwareFoundation/wg-python3/blob/master/references/sidefx_cpython_notes.md#wide-character-string-arguments
        wchar_t** wargv = static_cast<wchar_t**>(PyMem_Malloc(sizeof(wchar_t*) * args.size()));
        for (int i = 0; i < args.size(); i++)
        {
            wchar_t* warg = Py_DecodeLocale(args[i].c_str(), nullptr);
            wargv[i] = warg;
        }

        PySys_SetArgv(args.size(), wargv);

        // Free allocated wchar arguments.
        for (int i = 0; i < args.size(); i++)
            PyMem_Free(wargv[i]);
        PyMem_Free(wargv);
#else
        PySys_SetArgv(args.size(), _argv.data());
#endif
    }

#if 0
    TfPyRunSimpleString("import warnings;warnings.simplefilter('always', DeprecationWarning)");
#endif

#ifdef OPENDCC_OS_WINDOWS
    TfPyRunSimpleString("import os;os.environ['PATH'] = \"" + ghc::filesystem::path(ArchGetExecutablePath()).parent_path().generic_string() +
                        "\" + os.pathsep + os.environ['PATH']");
#endif

    TfPyRunSimpleString("import sys;import opendcc.core as dcc_core;sys.modules['dcc_core'] = dcc_core");
    // we override excepthook because exceptions in PyQt5 QActions could close the app
    TfPyRunSimpleString("import sys;sys.excepthook = lambda type, value,tback:sys.__excepthook__(type,value,tback)");
    // Add executable path to PATH env in order to resolove all dll dependencies if current working directory is not "bin"
}

void py_interp::run_init()
{
    TfPyRunSimpleString(Application::get_app_config().get<std::string>("python.init"));
}

void py_interp::run_init_ui()
{
    TfPyRunSimpleString(Application::get_app_config().get<std::string>("python.init_ui"));
}

void py_interp::init_shell()
{

    for (;;)
    {
        TfPyLock lock;
        PyRun_InteractiveLoop(stdin, "<stdin>");
    }
    Py_Finalize();
}

namespace
{
    template <class Execute>
    int run_python(const Execute& execute)
    {
        using namespace pybind11;
        TfPyInitialize();
        TfPyLock py_lock;
        try
        {
            object main_module = module_::import("__main__");
            object default_globals = main_module.attr("__dict__");
            execute(default_globals);
        }
        catch (const error_already_set& exc)
        {
            if (exc.matches(PyExc_SystemExit))
            {
                auto exc_val = exc.value();
                if (pybind11::hasattr(exc_val, "code"))
                {
                    auto code_attr = exc_val.attr("code");
                    if (code_attr.is_none())
                        return 0;
                    if (pybind11::isinstance<pybind11::int_>(code_attr))
                    {
                        const auto code = code_attr.cast<int>();
                        if (code != 0)
                        {
                            py_log_error(exc.what());
                        }
                        return code;
                    }
                }
                py_log_error(exc.what());
                return 1;
            }
            else
            {
                py_log_error(exc.what());
                return -1;
            }
        }
        return 0;
    }
}

int py_interp::run_script(const std::string& filepath)
{
    if (!ghc::filesystem::exists(filepath))
    {
        TF_CODING_ERROR("Could not open file '%s'!", filepath.c_str());
        return -1;
    }
    return run_python([&filepath](const pybind11::object& globals) { pybind11::eval_file(pybind11::str(filepath), globals, globals); });
}

int py_interp::run_command(const std::string& code)
{
    return run_python([&code](const pybind11::object& globals) { pybind11::exec(code, globals, globals); });
}
OPENDCC_NAMESPACE_CLOSE
