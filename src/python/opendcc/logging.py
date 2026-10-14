# Copyright Contributors to the OpenDCC project
# SPDX-License-Identifier: Apache-2.0

import logging

from opendcc.core import _core


class LoggingHandler(logging.Handler):
    """Forward Python log records to the OpenDCC logger."""

    def emit(self, record):
        try:
            _core._log_python(
                record.levelno,
                record.name,
                self.format(record),
                record.pathname,
                record.funcName or "",
                record.lineno,
            )
        except Exception:
            self.handleError(record)


def install():
    """Install the OpenDCC handler, preserving explicitly configured levels and handlers."""
    root = logging.getLogger()
    namespace = logging.getLogger("opendcc")
    if namespace.level == logging.NOTSET:
        namespace.setLevel(logging.DEBUG)
    for handler in root.handlers:
        if isinstance(handler, LoggingHandler):
            return handler
    handler = LoggingHandler()
    root.addHandler(handler)
    return handler


def set_level(level, channel="opendcc"):
    """Set the native minimum severity for a channel and its descendants."""
    if level not in (logging.DEBUG, logging.INFO, logging.WARNING, logging.ERROR, logging.CRITICAL):
        raise ValueError("Use a standard Python logging severity.")
    _core._set_channel_log_level(channel, level)


def clear_level(channel="opendcc"):
    """Remove a native channel override and restore parent-level inheritance."""
    _core._clear_channel_log_level(channel)


def get_level(channel="opendcc"):
    """Return a channel's effective native minimum severity."""
    return _core._get_channel_log_level(channel)
