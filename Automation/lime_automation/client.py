"""HTTP client for the automation server.

One class covers the whole protocol. Command specific helpers are thin wrappers so a script reads as
engine operations rather than as HTTP calls, but `call` stays available for commands a project adds
without this library knowing about them.
"""

from __future__ import annotations

import json
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from .errors import CommandError, TransportError

DEFAULT_TIMEOUT = 35.0
JSON_CONTENT_TYPE = "application/json"


@dataclass(frozen=True)
class CommandReply:
    """One reply from a batch, where a failure must not raise but still be inspectable."""

    ok: bool
    result: dict[str, Any]
    error: str = ""


class LimeClient:
    """Talks to one engine instance.

    Stateless over HTTP, so an instance can be reused, shared across tests and kept while the engine
    restarts, as long as the port stays the same.
    """

    def __init__(self, url: str = "http://127.0.0.1:5613", timeout: float = DEFAULT_TIMEOUT) -> None:
        self.url = url.rstrip("/")
        # Slightly longer than the engine's own dispatch timeout, so a stalled main thread surfaces as
        # the engine's error message rather than as an opaque client side timeout.
        self.timeout = timeout

    @classmethod
    def from_port(cls, port: int, timeout: float = DEFAULT_TIMEOUT) -> LimeClient:
        return cls(f"http://127.0.0.1:{port}", timeout=timeout)

    def __repr__(self) -> str:
        return f"LimeClient({self.url})"

    # -- transport ---------------------------------------------------------------------------

    def _request(self, method: str, path: str, body: bytes | None = None, content_type: str | None = None) -> tuple[int, bytes, str]:
        request = urllib.request.Request(f"{self.url}{path}", data=body, method=method)
        if content_type:
            request.add_header("Content-Type", content_type)

        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                return response.status, response.read(), response.headers.get("Content-Type", "")
        except urllib.error.HTTPError as error:
            # A 4xx or 5xx still carries a JSON body describing the problem, which is more useful than
            # the status code alone.
            return error.code, error.read(), error.headers.get("Content-Type", "")
        except urllib.error.URLError as error:
            raise TransportError(f"{method} {path} failed: {error.reason}") from error
        except TimeoutError as error:
            raise TransportError(f"{method} {path} timed out after {self.timeout}s") from error

    def _request_json(self, method: str, path: str, payload: Any | None = None) -> dict[str, Any]:
        body = json.dumps(payload).encode("utf-8") if payload is not None else None
        status, raw, _ = self._request(method, path, body, JSON_CONTENT_TYPE if body else None)

        try:
            return json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError as error:
            raise TransportError(f"{method} {path} returned status {status} with a non-JSON body: {raw[:200]!r}") from error

    # -- commands ----------------------------------------------------------------------------

    def call(self, command: str, **params: Any) -> dict[str, Any]:
        """Runs one command and returns its result, raising CommandError on failure."""
        reply = self._request_json("POST", "/command", {"command": command, "params": params})
        if not reply.get("ok", False):
            raise CommandError(command, reply.get("error", "unknown error"), params)
        return reply.get("result", {})

    def try_call(self, command: str, **params: Any) -> CommandReply:
        """Same, but a failure is returned rather than raised."""
        reply = self._request_json("POST", "/command", {"command": command, "params": params})
        return CommandReply(ok=reply.get("ok", False), result=reply.get("result", {}), error=reply.get("error", ""))

    def batch(self, commands: Iterable[tuple[str, dict[str, Any]] | str]) -> list[CommandReply]:
        """Runs several commands in one round trip, in order.

        Worth using when a test changes a handful of values before observing the result: each command
        still executes on the main thread, but the network cost is paid once.
        """
        payload = []
        for entry in commands:
            if isinstance(entry, str):
                payload.append({"command": entry, "params": {}})
            else:
                name, params = entry
                payload.append({"command": name, "params": params})

        reply = self._request_json("POST", "/batch", payload)
        if not reply.get("ok", False):
            raise CommandError("batch", reply.get("error", "unknown error"))

        return [
            CommandReply(ok=item.get("ok", False), result=item.get("result", {}), error=item.get("error", ""))
            for item in reply.get("replies", [])
        ]

    # -- server metadata ---------------------------------------------------------------------

    def info(self) -> dict[str, Any]:
        """Server identity from GET /. Needs no command dispatch, so it answers before the first frame."""
        return self._request_json("GET", "/")

    def commands(self) -> list[dict[str, str]]:
        return self._request_json("GET", "/commands").get("commands", [])

    def is_alive(self) -> bool:
        try:
            self.info()
            return True
        except (TransportError, CommandError):
            return False

    # -- engine ------------------------------------------------------------------------------

    def ping(self) -> dict[str, Any]:
        return self.call("ping")

    def engine_info(self) -> dict[str, Any]:
        return self.call("engine.info")

    def engine_stats(self) -> dict[str, Any]:
        return self.call("engine.stats")

    def quit(self) -> None:
        self.call("engine.quit")

    @property
    def frame(self) -> int:
        return int(self.ping()["frame"])

    def wait_frames(self, count: int = 2) -> int:
        """Blocks until the engine has advanced by at least `count` frames.

        Commands run between frames, so a change is already visible in the frame that follows the call
        that made it. Waiting matters when several frames must pass, for instance to let an animation
        or a queued resize settle.
        """
        start = self.frame
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            current = self.frame
            if current >= start + count:
                return current
            time.sleep(0.005)
        raise TransportError(f"The engine did not advance {count} frame(s) within {self.timeout}s")

    # -- logs --------------------------------------------------------------------------------

    def logs(self, count: int = 50, level: str | None = None, category: str | None = None) -> list[dict[str, Any]]:
        """Recent log entries. count=0 returns every match rather than none."""
        params: dict[str, Any] = {"count": count}
        if level is not None:
            params["level"] = level
        if category is not None:
            params["category"] = category
        return self.call("log.tail", **params)["entries"]

    def clear_logs(self) -> None:
        self.call("log.clear")

    def errors(self) -> list[dict[str, Any]]:
        """Every entry at error level or above, which is what a test asserts on."""
        return self.logs(count=0, level="error")

    def warnings(self) -> list[dict[str, Any]]:
        return self.logs(count=0, level="warning")

    # -- render passes -----------------------------------------------------------------------

    def list_passes(self) -> list[dict[str, Any]]:
        return self.call("pass.list")["passes"]

    def describe_pass(self, name: str) -> list[dict[str, Any]]:
        """Field metadata: type, range and tooltip, so valid values need not be hardcoded."""
        return self.call("pass.describe", **{"pass": name})["fields"]

    def get_pass_values(self, name: str) -> dict[str, Any]:
        return self.call("pass.get", **{"pass": name})["values"]

    def set_pass_values(self, name: str, values: dict[str, Any]) -> dict[str, Any]:
        return self.call("pass.set", **{"pass": name, "values": values})["values"]

    def find_pass(self, *, exclude_engine: bool = True) -> str | None:
        """First pass exposing reflected settings, skipping the engine's own by default.

        Lets a test address a project pass without hardcoding a name that the project is free to
        change.
        """
        for entry in self.list_passes():
            if exclude_engine and entry["name"] == "ImGui":
                continue
            if entry.get("hasSettings"):
                return entry["name"]
        return None

    # -- editor panels -----------------------------------------------------------------------

    def list_panels(self) -> list[dict[str, Any]]:
        return self.call("panel.list")["panels"]

    def show_panel(self, name: str, visible: bool = True) -> None:
        self.call("panel.show", panel=name, visible=visible)

    def panel_visible(self, name: str) -> bool:
        for panel in self.list_panels():
            if panel["name"] == name:
                return bool(panel["visible"])
        raise CommandError("panel.list", f"No panel named '{name}'")

    def reset_layout(self) -> None:
        self.call("panel.resetLayout")

    # -- settings ----------------------------------------------------------------------------

    def get_settings(self) -> dict[str, Any]:
        return self.call("settings.get")["settings"]

    def set_settings(self, settings: dict[str, Any]) -> dict[str, Any]:
        return self.call("settings.set", settings=settings)["settings"]

    def save_settings(self) -> None:
        """Writes ProjectSettings.json in the project source tree."""
        self.call("settings.save")

    # -- scripts -----------------------------------------------------------------------------

    def list_scripts(self) -> list[dict[str, Any]]:
        """Automation scripts the running project ships."""
        return self.call("script.list")["scripts"]

    # -- screenshots -------------------------------------------------------------------------

    def screenshot(self, source: str = "backBuffer") -> bytes:
        """Returns PNG bytes.

        The image travels over HTTP, so nothing is written to disk and the caller does not need to
        share a file system with the engine.
        """
        query = urllib.parse.urlencode({"source": source})
        status, raw, content_type = self._request("GET", f"/screenshot?{query}")

        if content_type.startswith("image/png"):
            return raw

        # An error on this route still arrives as JSON.
        try:
            reply = json.loads(raw.decode("utf-8"))
            raise CommandError("screenshot", reply.get("error", f"status {status}"))
        except json.JSONDecodeError as error:
            raise TransportError(f"/screenshot returned status {status} and {content_type}") from error

    def save_screenshot(self, path: str | Path, source: str = "backBuffer") -> Path:
        """Captures over HTTP and writes the bytes locally, creating parent directories."""
        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(self.screenshot(source=source))
        return destination

    def screenshot_to_engine_disk(self, name: str = "Screenshot.png", source: str = "backBuffer") -> str:
        """Asks the engine to write the file itself, and returns the path it wrote.

        Only useful when the file is wanted on the engine's own machine; prefer `save_screenshot`.
        """
        return self.call("screenshot.capture", path=name, source=source)["path"]
