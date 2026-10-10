# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest


class TestApplicationCli(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="application-cli-")
        self.addCleanup(self.directory.cleanup)
        self.script = Path(self.directory.name) / "script with spaces.py"

    def run_application(self, *args):
        return subprocess.run(
            [self.application, *args],
            cwd=self.directory.name,
            capture_output=True,
            text=True,
            timeout=60,
        )

    def run_script(self, fixture, options=(), args=()):
        shutil.copyfile(Path(__file__).parent / "fixtures" / fixture, self.script)
        return self.run_application(*options, "--script", str(self.script), *args)

    def check_result(self, result, code, message=None):
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        if message:
            self.assertIn(message, result.stdout + result.stderr)

    def test_script_does_not_initialize_ui(self):
        self.check_result(self.run_script("script_mode.py"), 0, "script completed")

    def test_script_exit_code(self):
        self.check_result(self.run_script("exit_code.py"), 7)

    def test_gui_script_exit_code_after_ui_startup(self):
        self.check_result(self.run_script("gui_mode.py", ("--gui",)), 7, "GUI script executed")

    def test_gui_stage_can_close_before_startup_paints(self):
        self.check_result(
            self.run_script("close_stage_on_startup.py", ("--gui",)), 0, "startup stages closed"
        )

    def test_startup_and_shutdown_callbacks(self):
        fixtures = Path(__file__).parent / "fixtures"
        config = Path(self.application).parent.parent / "configs" / "default.toml"
        text = config.read_text(encoding="utf-8")
        init_ui = "import opendcc.startup;opendcc.startup.init_ui()"
        hook = str(fixtures / "lifecycle_callbacks.py")
        text = text.replace(
            json.dumps(init_ui),
            json.dumps(init_ui + ";import runpy;runpy.run_path(" + repr(hook) + ")"),
        )
        custom_config = Path(self.directory.name) / "callbacks.toml"
        custom_config.write_text(text, encoding="utf-8")
        result = self.run_script("check_lifecycle.py", ("--config", str(custom_config), "--gui"))
        self.check_result(result, 0, "UI load callback completed")
        self.assertEqual(
            result.stdout.count("shutdown callback completed"), 1, result.stdout + result.stderr
        )

    def test_gui_script_exception_fails(self):
        result = self.run_script("raise_exception.py", ("--gui",))
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("expected CLI test failure", result.stdout + result.stderr)

    def test_gui_script_can_keep_open(self):
        self.check_result(
            self.run_script("keep_open.py", ("--gui", "--keep-open")), 11, "event loop continued"
        )

    def test_script_arguments_are_not_application_options(self):
        for options in ((), ("--gui",)):
            with self.subTest(options=options):
                result = self.run_script(
                    "arguments.py",
                    options,
                    (
                        "--gui",
                        "--exec",
                        "argument with spaces",
                        "-platform",
                        "opendcc-test-script-argument",
                    ),
                )
                self.check_result(result, 0, "arguments preserved")

    def test_exec_runs_inline_code_and_exits_when_requested(self):
        result = self.run_application(
            "--exit-after-exec",
            "--exec",
            "print('inline executed', flush=True); raise SystemExit(7)",
        )
        self.check_result(result, 7, "inline executed")

    def test_exec_success_exit_code(self):
        self.check_result(
            self.run_application(
                "--exit-after-exec", "--exec", "print('inline completed', flush=True)"
            ),
            0,
            "inline completed",
        )

    def test_exec_system_exit_without_code_succeeds(self):
        self.check_result(
            self.run_application("--exit-after-exec", "--exec", "raise SystemExit"), 0
        )

    def test_exec_exception_fails_when_exit_requested(self):
        result = self.run_application(
            "--exit-after-exec", "--exec", "raise RuntimeError('expected inline failure')"
        )
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("expected inline failure", result.stdout + result.stderr)

    def test_exec_keeps_gui_open_by_default(self):
        fixture = Path(__file__).parent / "fixtures" / "keep_open.py"
        result = self.run_application(
            "--exec",
            "import runpy, sys; runpy.run_path(sys.argv[1], run_name='__main__')",
            str(fixture),
        )
        self.check_result(result, 11, "event loop continued")

    def test_exec_keeps_gui_open_after_exception(self):
        fixture = Path(__file__).parent / "fixtures" / "keep_open.py"
        result = self.run_application(
            "--exec",
            "import runpy, sys; runpy.run_path(sys.argv[1]); raise RuntimeError('inline failure')",
            str(fixture),
        )
        self.check_result(result, 11, "event loop continued")
        self.assertIn("inline failure", result.stdout + result.stderr)

    def test_exec_arguments_are_not_application_options(self):
        result = self.run_application(
            "--exit-after-exec",
            "--exec",
            "import sys; assert sys.argv[1:] == ['--script', 'file with spaces.py', '-platform', 'opendcc-test-script-argument']",
            "--script",
            "file with spaces.py",
            "-platform",
            "opendcc-test-script-argument",
        )
        self.check_result(result, 0)

    def test_invalid_mode_combinations(self):
        cases = (
            (("--shell", "--exec", "pass"), "--exec cannot be combined"),
            (("--with-tests", "--exec", "pass"), "--exec cannot be combined"),
            (("--shell", "--script", "unused.py"), "--script cannot be combined"),
            (("--with-tests", "--script", "unused.py"), "--script cannot be combined"),
            (("--gui",), "--gui requires"),
            (("--keep-open", "--script", "unused.py"), "--keep-open requires"),
            (("--keep-open", "--exec", "pass"), "--keep-open requires"),
            (("--exit-after-exec", "--script", "unused.py"), "--exit-after-exec requires"),
        )
        for args, message in cases:
            with self.subTest(args=args):
                self.check_result(self.run_application(*args), 1, message)

    def test_missing_execution_source(self):
        for option in ("--script", "--exec"):
            with self.subTest(option=option):
                self.check_result(self.run_application(option), 1, "Missing value")


if __name__ == "__main__":
    TestApplicationCli.application = sys.argv[1]
    unittest.main(argv=[__file__], verbosity=2)
