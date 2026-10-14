# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import importlib.abc
import importlib.util
from pathlib import Path
import os
import sys
import tempfile
import unittest


class BlockUsd(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname == "pxr" or fullname.startswith("pxr."):
            raise ImportError("USD imports are blocked for this test")
        return None


sys.meta_path.insert(0, BlockUsd())
module_path = Path(sys.argv[1])
library_paths = sys.argv[2:]
del sys.argv[1:]
dll_directories = []
if sys.platform == "win32":
    dll_directories = [os.add_dll_directory(str(Path(path).resolve())) for path in library_paths]
spec = importlib.util.spec_from_file_location("opendcc.packaging", module_path)
packaging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packaging)


class TestWithoutUsd(unittest.TestCase):
    def test_parser_and_writable_values_without_usd(self):
        with tempfile.TemporaryDirectory(prefix="opendcc-no-usd-") as directory:
            manifest = Path(directory) / "package.toml"
            manifest.write_text("[base]\nname = 'without_usd'\n", encoding="utf-8")
            data = packaging.TOMLParser().parse(str(manifest))
            self.assertEqual(data.name, "without_usd")
            attribute = data.raw_attributes[0]
            self.assertEqual(attribute.value, {"name": "without_usd"})
            expected = {"values": [None, True, 42, 0.5, "text", {"nested": []}]}
            attribute.value = expected
            self.assertEqual(attribute.value, expected)
            self.assertIs(type(attribute.value["values"][1]), bool)
        self.assertFalse(any(name == "pxr" or name.startswith("pxr.") for name in sys.modules))


if __name__ == "__main__":
    unittest.main()
