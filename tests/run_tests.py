# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import argparse
from pathlib import Path
import sys
import unittest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run a Python test suite in OpenDCC.")
    parser.add_argument("file", help="Python unittest file to run")
    args = parser.parse_args()
    test_file = Path(args.file).resolve()
    sys.path.insert(0, str(test_file.parent))
    suite = unittest.defaultTestLoader.loadTestsFromName(test_file.stem)
    if suite.countTestCases() == 0:
        parser.exit(1, f"No tests found in {test_file}\n")
    result = unittest.TextTestRunner(stream=sys.__stderr__, verbosity=2).run(suite)
    sys.exit(not result.wasSuccessful())
