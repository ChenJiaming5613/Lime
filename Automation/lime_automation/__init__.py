"""Client library and test harness for the LimeEngine automation server.

The engine serves JSON over HTTP on the loopback interface. This package wraps that protocol, manages
engine processes and provides the fixtures the pytest suite builds on.

Only the standard library is used, so the package runs from a checkout with no install step. pytest is
needed for the test suite, not for the client itself.

    from lime_automation import LimeSession

    with LimeSession(backend="d3d12") as engine:
        engine.set_pass_values("Triangle", {"RotationSpeed": 0.0})
        engine.save_screenshot("triangle.png")
"""

from __future__ import annotations

from .client import CommandReply, LimeClient
from .discovery import EngineEndpoint, discover_engines, find_engine
from .errors import (
    CommandError,
    EngineNotFoundError,
    LimeAutomationError,
    LaunchError,
    TransportError,
)
from .paths import find_repo_root, resolve_executable
from .session import LimeSession

__all__ = [
    "CommandError",
    "CommandReply",
    "EngineEndpoint",
    "EngineNotFoundError",
    "LaunchError",
    "LimeAutomationError",
    "LimeClient",
    "LimeSession",
    "TransportError",
    "discover_engines",
    "find_engine",
    "find_repo_root",
    "resolve_executable",
]

__version__ = "1.0.0"

# Bumped when the request or reply shape changes. The engine reports its own value at GET /, and the
# client refuses to talk to a server whose protocol it does not implement.
PROTOCOL_VERSION = 1
