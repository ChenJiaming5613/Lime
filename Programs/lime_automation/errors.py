"""Exception hierarchy.

Distinguishing transport failures from command failures matters for tests: a broken connection means
the engine died and the run should stop, while a rejected command is often the assertion itself.
"""

from __future__ import annotations


class LimeAutomationError(Exception):
    """Base class for every error raised by this package."""


class TransportError(LimeAutomationError):
    """The request never produced a reply: connection refused, timed out or closed."""


class CommandError(LimeAutomationError):
    """The engine replied with ok=false.

    Carries the command and parameters so a failure message identifies the call without the caller
    having to repeat them.
    """

    def __init__(self, command: str, message: str, params: dict | None = None) -> None:
        self.command = command
        self.message = message
        self.params = params or {}
        super().__init__(f"{command}: {message}")


class EngineNotFoundError(LimeAutomationError):
    """No running engine could be discovered, or the named one is not there."""


class LaunchError(LimeAutomationError):
    """The engine process could not be started or exited before becoming responsive."""
