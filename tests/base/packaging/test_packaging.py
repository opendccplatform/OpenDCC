# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from textwrap import dedent
import tempfile
import unittest

import opendcc.packaging as packaging


class TestPackaging(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="opendcc-packaging-")
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)

    def write_manifest(self, folder, text):
        path = self.directory / folder / "package.toml"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(dedent(text), encoding="utf-8")
        return path

    def test_parser_exposes_manifest_attributes(self):
        path = self.write_manifest(
            "extension",
            """
            [base]
            name = 'test.extension'

            [test]
            text = 'value'
            enabled = true
            count = 42
            ratio = 0.5
            """,
        )
        data = packaging.TOMLParser().parse(str(path))
        self.assertEqual(data.name, "test.extension")
        self.assertEqual(Path(data.path), path.parent)
        attributes = {attribute.name: attribute.value for attribute in data.raw_attributes}
        self.assertEqual(attributes["base"]["name"], "test.extension")
        self.assertEqual(
            attributes["test"], {"text": "value", "enabled": True, "count": 42, "ratio": 0.5}
        )
        for name, expected_type in (
            ("text", str),
            ("enabled", bool),
            ("count", int),
            ("ratio", float),
        ):
            with self.subTest(attribute=name):
                self.assertIs(type(attributes["test"][name]), expected_type)

    def test_attribute_value_round_trip_and_rejected_assignments(self):
        path = self.write_manifest("round_trip", "[base]\nname = 'round_trip'\n")
        attribute = packaging.TOMLParser().parse(str(path)).raw_attributes[0]
        values = [
            None,
            True,
            False,
            -(2**63),
            2**63 - 1,
            0.5,
            "текст",
            [],
            {},
            [1, "two", False, None, {"literal.key": [3.5, {}]}],
        ]
        for value in values:
            with self.subTest(value=value):
                attribute.value = value
                self.assertEqual(attribute.value, value)
                self.assertIs(type(attribute.value), type(value))
        attribute.value = {"preserved": True}
        for value, error in (
            (object(), TypeError),
            ({1: "invalid"}, TypeError),
            (2**63, OverflowError),
            (-(2**63) - 1, OverflowError),
            (["valid", object()], TypeError),
        ):
            with self.subTest(value=value):
                with self.assertRaises(error):
                    attribute.value = value
                self.assertEqual(attribute.value, {"preserved": True})
        recursive = []
        recursive.append(recursive)
        with self.assertRaises(RecursionError):
            attribute.value = recursive
        self.assertEqual(attribute.value, {"preserved": True})
        recursive_dictionary = {}
        recursive_dictionary["self"] = recursive_dictionary
        with self.assertRaises(RecursionError):
            attribute.value = recursive_dictionary
        self.assertEqual(attribute.value, {"preserved": True})

    def test_empty_nested_and_table_arrays(self):
        path = self.write_manifest(
            "arrays",
            """
            empty = []
            nested = [[1, 2], [3]]
            [base]
            name = 'arrays'
            [[entries]]
            "literal.key" = 'value'
            [[entries]]
            enabled = true
        """,
        )
        data = packaging.TOMLParser().parse(str(path))
        values = {attribute.name: attribute.value for attribute in data.raw_attributes}
        self.assertEqual(values["empty"], [])
        self.assertEqual(values["nested"], [[1, 2], [3]])
        self.assertEqual(values["entries"], [{"literal.key": "value"}, {"enabled": True}])

    def test_parser_rejects_malformed_or_unnamed_manifests(self):
        for name, text in (
            ("malformed", "[base\nname = 'broken'\n"),
            ("missing_name", "[base]\nroot = '.'\n"),
            ("wrong_name_type", "[base]\nname = 42\n"),
            ("empty_name", "[base]\nname = ''\n"),
        ):
            with self.subTest(name=name):
                data = packaging.TOMLParser().parse(str(self.write_manifest(name, text)))
                self.assertEqual(data.name, "")
        self.assertEqual(
            packaging.TOMLParser().parse(str(self.directory / "missing.toml")).name, ""
        )

    def test_provider_discovers_unique_packages_and_refetches(self):
        self.write_manifest("first", "[base]\nname = 'test.first'\n")
        self.write_manifest("duplicate", "[base]\nname = 'test.first'\n")
        self.write_manifest("second", "[base]\nname = 'test.second'\n")
        self.write_manifest("invalid", "[base]\nroot = '.'\n")
        provider = packaging.FileSystemPackageProvider()
        pattern = str(self.directory / "*")
        provider.add_path(pattern)
        provider.add_path(pattern)
        provider.fetch()
        self.assertEqual(
            {package.name for package in provider.get_cached_packages()},
            {"test.first", "test.second"},
        )
        self.assertEqual(len(provider.get_cached_packages()), 2)
        provider.remove_path(pattern)
        provider.fetch()
        self.assertEqual(len(provider.get_cached_packages()), 2)
        provider.remove_path(pattern)
        provider.fetch()
        self.assertEqual(provider.get_cached_packages(), [])

    def test_application_registry_loads_native_and_python_packages_idempotently(self):
        import opendcc.core as core

        registry = core.Application.instance().get_package_registry()
        packages = registry.get_all_packages()
        self.assertTrue(packages)
        native = registry.get_package("opendcc.usd_editor.common_cmds")
        python = registry.get_package("opendcc.usd_editor.live_share")
        for package in (native, python):
            with self.subTest(package=package.get_name()):
                self.assertTrue(package.is_loaded())
                self.assertIsNone(package.get_attribute("test.missing.attribute"))
                self.assertIsInstance(package.get_all_attributes(), dict)
                self.assertIsInstance(package.get_direct_dependencies(), dict)
                name = package.get_attribute("base.name")
                self.assertTrue(registry.load(package))
                self.assertTrue(registry.load(package.get_name()))
                self.assertTrue(package.is_loaded())
                self.assertEqual(package.get_attribute("base.name"), name)
                self.assertEqual(
                    registry.get_package(package.get_name()).get_root_dir(), package.get_root_dir()
                )
        self.assertFalse(registry.load("test.package.that.does.not.exist"))
