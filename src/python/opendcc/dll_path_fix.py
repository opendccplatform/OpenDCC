# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

# Explanation:
# Since Python 3.8 python deprecated dll resolution via PATH env variable.
# The new recommended way is using os.add_dll_directory()
# The code below is needed for correct Qt and PySide dll resolution in internal builds where
# environment is configured via .bat script or rez.
import sys

if sys.platform == "win32" and sys.version_info[0] >= 3 and sys.version_info[1] >= 8:
    import os

    # similar to https://github.com/PixarAnimationStudios/OpenUSD/blob/dev/pxr/base/tf/__init__.py#L40-L45
    import_paths = os.getenv("PATH", "")
    for path in reversed(import_paths.split(os.pathsep)):
        if os.path.exists(path) and path != ".":
            abs_path = os.path.abspath(path)
            os.add_dll_directory(abs_path)

    import ctypes.util as cu
    import importlib.util

    # Qt's core DLL name is stable, so it can be found by name.
    for lib_name in ("Qt6Core.dll", "Qt5Core.dll"):
        lib_path = cu.find_library(lib_name)
        if lib_path:
            os.add_dll_directory(os.path.dirname(lib_path))

    # shiboken's DLL carries a CPython ABI tag that moves with the interpreter, so locate the
    # package and use its directory rather than guessing the file name. find_spec does not import
    # the module, so this is safe before the DLL directories are in place.
    for mod_name in ("shiboken6", "shiboken2", "PySide6", "PySide2"):
        try:
            spec = importlib.util.find_spec(mod_name)
        except (ImportError, ValueError):
            continue
        if spec and spec.submodule_search_locations:
            for location in spec.submodule_search_locations:
                if os.path.isdir(location):
                    os.add_dll_directory(location)
