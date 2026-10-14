# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import logging
from pathlib import Path
import sys
import types
import threading
import unittest
from unittest import mock

_core = mock.Mock(
    spec=[
        "_log_python",
        "_set_channel_log_level",
        "_get_channel_log_level",
        "_clear_channel_log_level",
    ]
)
core_module = types.ModuleType("opendcc.core")
core_module._core = _core
spec = importlib.util.spec_from_file_location(
    "opendcc_logging_test_target",
    Path(__file__).resolve().parents[3] / "src/python/opendcc/logging.py",
)
bridge = importlib.util.module_from_spec(spec)
with mock.patch.dict(sys.modules, {"opendcc.core": core_module}):
    spec.loader.exec_module(bridge)

LoggingHandler = bridge.LoggingHandler
install = bridge.install
set_level = bridge.set_level
get_level = bridge.get_level
clear_level = bridge.clear_level


class TestLogging(unittest.TestCase):
    def setUp(self):
        _core.reset_mock()
        root = logging.getLogger()
        self.addCleanup(setattr, root, "handlers", list(root.handlers))
        self.addCleanup(root.setLevel, root.level)
        namespace = logging.getLogger("opendcc")
        self.addCleanup(namespace.setLevel, namespace.level)

    def test_install_preserves_root_configuration_and_is_idempotent(self):
        root = logging.getLogger()
        existing = logging.NullHandler()
        root.addHandler(existing)
        self.addCleanup(root.removeHandler, existing)
        handlers = list(root.handlers)
        level = root.level

        handler = install()
        self.assertIs(install(), handler)
        self.assertEqual(root.level, level)
        self.assertTrue(all(item in root.handlers for item in handlers))
        self.assertEqual(sum(isinstance(item, LoggingHandler) for item in root.handlers), 1)

    def test_channel_level_controls_forward_to_native_backend(self):
        channel = "opendcc.test_levels"
        set_level(logging.WARNING, channel)
        _core._set_channel_log_level.assert_called_once_with(channel, logging.WARNING)
        _core._get_channel_log_level.return_value = logging.DEBUG
        self.assertEqual(get_level(channel), logging.DEBUG)
        _core._get_channel_log_level.assert_called_once_with(channel)
        clear_level(channel)
        _core._clear_channel_log_level.assert_called_once_with(channel)

    def test_nonstandard_channel_level_is_rejected_before_native_call(self):
        with self.assertRaises(ValueError):
            set_level(35, "opendcc.test_levels")
        _core._set_channel_log_level.assert_not_called()

    def test_install_preserves_explicit_namespace_level(self):
        namespace = logging.getLogger("opendcc")
        self.addCleanup(namespace.setLevel, namespace.level)
        namespace.setLevel(logging.ERROR)
        install()
        self.assertEqual(namespace.level, logging.ERROR)

    def test_default_namespace_allows_native_debug_filtering(self):
        namespace = logging.getLogger("opendcc")
        self.addCleanup(namespace.setLevel, namespace.level)
        namespace.setLevel(logging.NOTSET)
        install()
        with mock.patch.object(_core, "_log_python") as sink:
            logging.getLogger("opendcc.debug_test").debug("Debug details")
        sink.assert_called_once()
        self.assertEqual(
            sink.call_args.args[:3], (logging.DEBUG, "opendcc.debug_test", "Debug details")
        )

    def test_handler_preserves_message_source_and_severity(self):
        for level in (
            logging.DEBUG,
            logging.INFO,
            logging.WARNING,
            logging.ERROR,
            logging.CRITICAL,
            35,
        ):
            with self.subTest(level=level):
                record = logging.LogRecord(
                    "opendcc.test",
                    level,
                    "plugin.py",
                    42,
                    "Value %s contains {}",
                    ("test",),
                    None,
                    func="run_plugin",
                )
                with mock.patch.object(_core, "_log_python") as sink:
                    LoggingHandler().handle(record)
                sink.assert_called_once_with(
                    level, "opendcc.test", "Value test contains {}", "plugin.py", "run_plugin", 42
                )

    def test_handler_preserves_exception_traceback(self):
        try:
            raise ValueError("plugin failed")
        except ValueError:
            record = logging.LogRecord(
                "opendcc.test",
                logging.ERROR,
                "plugin.py",
                42,
                "Operation failed",
                (),
                sys.exc_info(),
            )
        with mock.patch.object(_core, "_log_python") as sink:
            LoggingHandler().handle(record)
        message = sink.call_args.args[2]
        self.assertIn("Operation failed", message)
        self.assertIn("Traceback (most recent call last)", message)
        self.assertIn("ValueError: plugin failed", message)

    def test_standard_logging_routes_worker_thread_records(self):
        logger = logging.Logger("opendcc.worker")
        logger.addHandler(LoggingHandler())
        with mock.patch.object(_core, "_log_python") as sink:
            thread = threading.Thread(target=lambda: logger.error("Worker failed: %s", "details"))
            thread.start()
            thread.join(timeout=5)
        self.assertFalse(thread.is_alive())
        sink.assert_called_once()
        self.assertEqual(
            sink.call_args.args[:3], (logging.ERROR, "opendcc.worker", "Worker failed: details")
        )

    def test_sink_failure_does_not_raise_from_plugin_logging(self):
        logger = logging.Logger("opendcc.test")
        handler = LoggingHandler()
        logger.addHandler(handler)
        with mock.patch.object(_core, "_log_python", side_effect=RuntimeError("sink failed")):
            with mock.patch.object(handler, "handleError") as handle_error:
                logger.error("Plugin message")
        handle_error.assert_called_once()


if __name__ == "__main__":
    unittest.main()
