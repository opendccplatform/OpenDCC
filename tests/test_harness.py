# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class TestHarness(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="test-harness-")
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.root = Path(__file__).parent.parent

    def test_empty_python_module_fails(self):
        result = subprocess.run(
            [
                sys.executable,
                str(self.root / "tests/run_tests.py"),
                str(self.root / "tests/fixtures/no_tests.py"),
            ],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("No tests found", result.stderr)

    def test_unmatched_doctest_filter_fails_ctest(self):
        result = subprocess.run(
            [self.ctest, "--test-dir", self.build, "-C", self.configuration, "--show-only=json-v1"],
            capture_output=True,
            text=True,
            check=True,
            timeout=30,
        )
        test = next(
            test
            for test in json.loads(result.stdout)["tests"]
            if test["name"] == "RichSelectionTests"
        )
        command = [
            "--test-suite=NoSuchHarnessSuite" if arg.startswith("--test-suite=") else arg
            for arg in test["command"]
        ]
        properties = {prop["name"]: prop["value"] for prop in test["properties"]}
        failure_patterns = properties["FAIL_REGULAR_EXPRESSION"]
        # Reuse the real registration, so this also checks that CMake installs the guard.
        quoted_command = " ".join("[=[" + arg + "]=]" for arg in command)
        patterns = ";".join(failure_patterns)
        (self.directory / "CTestTestfile.cmake").write_text(
            "add_test(EmptyDoctest " + quoted_command + ")\n"
            "set_tests_properties(EmptyDoctest PROPERTIES FAIL_REGULAR_EXPRESSION [=["
            + patterns
            + "]=])\n"
        )
        result = subprocess.run(
            [self.ctest, "--test-dir", str(self.directory), "--output-on-failure"],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("test cases: 0", result.stdout)
        self.assertIn("0% tests passed", result.stdout)

    def test_embedded_arguments_rejected_at_configuration(self):
        script = self.directory / "invalid_args.cmake"
        script.write_text(
            'include("' + (self.root / "cmake/macros/MakeTests.cmake").as_posix() + '")\n'
            "opendcc_add_python_test(InvalidTest SOURCE test.py ARGS unexpected)\n"
        )
        result = subprocess.run(
            [self.cmake, "-P", str(script)],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("ARGS requires STANDALONE", result.stderr)


if __name__ == "__main__":
    TestHarness.cmake, TestHarness.build, TestHarness.configuration = sys.argv[1:]
    TestHarness.ctest = str(
        Path(TestHarness.cmake).with_name("ctest.exe" if sys.platform == "win32" else "ctest")
    )
    unittest.main(argv=[__file__], verbosity=2)
