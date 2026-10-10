# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def test_environment(directory, runtime_paths):
    env = os.environ.copy()
    profile_paths = {
        "HOME": "home",
        "USERPROFILE": "home",
        "APPDATA": "config",
        "LOCALAPPDATA": "cache",
        "XDG_CONFIG_HOME": "config",
        "XDG_DATA_HOME": "data",
        "XDG_CACHE_HOME": "cache",
    }
    for folder in set(profile_paths.values()):
        (directory / folder).mkdir()
    for name, folder in profile_paths.items():
        env[name] = str(directory / folder)

    path_variables = ["PATH"]
    if sys.platform == "darwin":
        path_variables.append("DYLD_LIBRARY_PATH")
    elif sys.platform != "win32":
        path_variables.append("LD_LIBRARY_PATH")
    for name in path_variables:
        paths = list(runtime_paths)
        if env.get(name):
            paths.append(env[name])
        env[name] = os.pathsep.join(paths)
    return env


def main():
    parser = argparse.ArgumentParser(description="Run a CTest command with a fresh user profile.")
    parser.add_argument(
        "--runtime-path", action="append", default=[], help="prepend a library directory"
    )
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("a test command is required")

    # Keep failed runs under the CTest working directory for inspection.
    directory = Path(tempfile.mkdtemp(prefix="run-", dir=Path.cwd()))
    env = test_environment(directory, args.runtime_path)
    print(f"Test directory: {directory}", flush=True)
    try:
        result = subprocess.run(command, cwd=directory, env=env, check=False).returncode
    except OSError as error:
        print(f"Could not launch {command[0]}: {error}", file=sys.stderr)
        result = 1

    if result == 0:
        shutil.rmtree(directory)
    else:
        print(f"Failed test artifacts: {directory}", file=sys.stderr)
    # subprocess reports Unix signals as negative return codes.
    return 128 - result if result < 0 else result


if __name__ == "__main__":
    sys.exit(main())
