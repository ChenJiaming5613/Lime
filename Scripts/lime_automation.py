"""Client library for the LimeEngine automation server.

The engine exposes a newline delimited JSON protocol on a loopback socket. This module wraps it in a
typed client plus a context manager that can launch the executable, wait for it to become responsive
and shut it down again.

Typical use:

    from lime_automation import LimeSession

    with LimeSession(project="HelloTriangle", backend="d3d12") as session:
        session.set_pass_values("TrianglePass", {"RotationSpeed": 0.0})
        session.screenshot("triangle.png")
"""

from __future__ import annotations

import json
import socket
import subprocess
import time
from contextlib import AbstractContextManager
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PORT = 8787


class LimeAutomationError(RuntimeError):
    """Raised when the engine reports a command failure or the transport breaks."""


class LimeClient:
    """Synchronous client for one automation connection."""

    def __init__(self, port: int = DEFAULT_PORT, host: str = "127.0.0.1", timeout: float = 35.0) -> None:
        self._host = host
        self._port = port
        # Slightly longer than the engine side dispatch timeout, so a stalled main thread surfaces as
        # the engine's own error rather than as a socket timeout here.
        self._timeout = timeout
        self._socket: socket.socket | None = None
        self._buffer = b""
        self._next_id = 1

    def connect(self) -> None:
        self._socket = socket.create_connection((self._host, self._port), timeout=self._timeout)
        self._socket.settimeout(self._timeout)

    def close(self) -> None:
        if self._socket is not None:
            self._socket.close()
            self._socket = None
        self._buffer = b""

    def __enter__(self) -> LimeClient:
        self.connect()
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def call(self, command: str, **params: Any) -> dict[str, Any]:
        """Sends one command and returns its result object, raising on failure."""
        if self._socket is None:
            raise LimeAutomationError("Not connected")

        request_id = self._next_id
        self._next_id += 1
        request = {"id": request_id, "command": command, "params": params}
        self._socket.sendall((json.dumps(request) + "\n").encode("utf-8"))

        reply = self._read_reply()
        if not reply.get("ok", False):
            raise LimeAutomationError(f"{command}: {reply.get('error', 'unknown error')}")
        return reply.get("result", {})

    def _read_reply(self) -> dict[str, Any]:
        # One reply per line; anything past the newline belongs to the next reply.
        while b"\n" not in self._buffer:
            try:
                chunk = self._socket.recv(4096)  # type: ignore[union-attr]
            except socket.timeout as error:
                raise LimeAutomationError("Timed out waiting for a reply") from error
            if not chunk:
                raise LimeAutomationError("The engine closed the connection")
            self._buffer += chunk

        line, _, self._buffer = self._buffer.partition(b"\n")
        return json.loads(line.decode("utf-8"))

    # Convenience wrappers. Each maps to one command; see "help" for the full list.

    def help(self) -> list[dict[str, str]]:
        return self.call("help")["commands"]

    def ping(self) -> dict[str, Any]:
        return self.call("ping")

    def engine_info(self) -> dict[str, Any]:
        return self.call("engine.info")

    def engine_stats(self) -> dict[str, Any]:
        return self.call("engine.stats")

    def quit(self) -> None:
        self.call("engine.quit")

    def logs(self, count: int = 50, level: str | None = None, category: str | None = None) -> list[dict[str, Any]]:
        params: dict[str, Any] = {"count": count}
        if level is not None:
            params["level"] = level
        if category is not None:
            params["category"] = category
        return self.call("log.tail", **params)["entries"]

    def clear_logs(self) -> None:
        self.call("log.clear")

    def list_passes(self) -> list[dict[str, Any]]:
        return self.call("pass.list")["passes"]

    def describe_pass(self, name: str) -> list[dict[str, Any]]:
        return self.call("pass.describe", **{"pass": name})["fields"]

    def get_pass_values(self, name: str) -> dict[str, Any]:
        return self.call("pass.get", **{"pass": name})["values"]

    def set_pass_values(self, name: str, values: dict[str, Any]) -> dict[str, Any]:
        return self.call("pass.set", **{"pass": name, "values": values})["values"]

    def list_panels(self) -> list[dict[str, Any]]:
        return self.call("panel.list")["panels"]

    def show_panel(self, name: str, visible: bool = True) -> None:
        self.call("panel.show", panel=name, visible=visible)

    def reset_layout(self) -> None:
        self.call("panel.resetLayout")

    def get_settings(self) -> dict[str, Any]:
        return self.call("settings.get")["settings"]

    def set_settings(self, settings: dict[str, Any]) -> dict[str, Any]:
        return self.call("settings.set", settings=settings)["settings"]

    def save_settings(self) -> None:
        self.call("settings.save")

    def screenshot(self, path: str = "Screenshot.png", source: str = "backBuffer") -> str:
        """Captures a PNG and returns the absolute path the engine wrote."""
        return self.call("screenshot.capture", path=path, source=source)["path"]

    def wait_frames(self, count: int = 2) -> None:
        """Blocks until the engine has advanced by at least `count` frames.

        Commands run between frames, so a single ping already implies a frame boundary. This matters
        when a change needs to be visible in a rendered frame before the next assertion.
        """
        start = self.ping()["frame"]
        deadline = time.monotonic() + self._timeout
        while time.monotonic() < deadline:
            if self.ping()["frame"] >= start + count:
                return
            time.sleep(0.01)
        raise LimeAutomationError(f"The engine did not advance {count} frame(s)")


class LimeSession(AbstractContextManager):
    """Launches the engine, connects to it and tears both down on exit."""

    def __init__(
        self,
        project: str = "HelloTriangle",
        config: str = "Debug",
        preset: str = "ninja",
        backend: str | None = None,
        width: int | None = None,
        height: int | None = None,
        no_editor: bool = False,
        port: int = 0,
        extra_args: list[str] | None = None,
        startup_timeout: float = 40.0,
        keep_layout: bool = False,
    ) -> None:
        self._exe_dir = REPO_ROOT / "Build" / preset / "Bin" / config
        self._exe = self._exe_dir / f"{project}.exe"
        self._port = port
        self._startup_timeout = startup_timeout
        self._keep_layout = keep_layout
        self._process: subprocess.Popen[bytes] | None = None
        self._client: LimeClient | None = None
        self._log_path = self._exe_dir / "Saved" / "Logs" / "LimeEngine.log"

        # Port 0 lets the OS assign one, which is what allows several sessions to run at once. The
        # engine writes the chosen value to AutomationPort.txt.
        self._args = [str(self._exe), "--automation", f"--automation-port={port}"]
        if backend is not None:
            self._args.append(f"--rhi={backend}")
        if width is not None:
            self._args.append(f"--width={width}")
        if height is not None:
            self._args.append(f"--height={height}")
        if no_editor:
            self._args.append("--no-editor")
        if extra_args:
            self._args.extend(extra_args)

    @property
    def client(self) -> LimeClient:
        if self._client is None:
            raise LimeAutomationError("The session is not started")
        return self._client

    def __enter__(self) -> LimeClient:
        if not self._exe.exists():
            raise LimeAutomationError(f"{self._exe} not found; build it first with ./Scripts/Build.ps1")

        if not self._keep_layout:
            # A stale layout hides panels added since it was written, which is usually the opposite of
            # what a test wants to observe.
            layout = self._exe_dir / "Saved" / "EditorLayout.ini"
            layout.unlink(missing_ok=True)

        port_file = self._exe_dir / "AutomationPort.txt"
        port_file.unlink(missing_ok=True)

        self._process = subprocess.Popen(
            self._args,
            cwd=str(self._exe_dir),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        self._client = self._wait_for_server(port_file)
        return self._client

    def _wait_for_server(self, port_file: Path) -> LimeClient:
        deadline = time.monotonic() + self._startup_timeout
        resolved_port = self._port

        while time.monotonic() < deadline:
            if self._process is not None and self._process.poll() is not None:
                raise LimeAutomationError(f"The engine exited early with code {self._process.returncode}\n{self._tail_log()}")

            if resolved_port == 0:
                # Written once the socket is bound, so its presence also means the port is listening.
                if port_file.exists():
                    try:
                        resolved_port = int(port_file.read_text().strip())
                    except ValueError:
                        resolved_port = 0
                if resolved_port == 0:
                    time.sleep(0.1)
                    continue

            client = LimeClient(port=resolved_port)
            try:
                client.connect()
                client.ping()
                return client
            except (OSError, LimeAutomationError):
                client.close()
                time.sleep(0.1)

        raise LimeAutomationError(f"The automation server did not come up within {self._startup_timeout}s\n{self._tail_log()}")

    def _tail_log(self, lines: int = 20) -> str:
        if not self._log_path.exists():
            return "(no log file)"
        content = self._log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        return "\n".join(content[-lines:])

    def log_issues(self) -> list[str]:
        """Returns log lines at warning level or above, for use as a test assertion."""
        if self._client is None:
            return []
        return [f"[{entry['level']}] {entry['category']}: {entry['message']}" for entry in self._client.logs(count=0, level="warning")]

    def __exit__(self, *_exc: object) -> None:
        # A clean exit is attempted first so the engine runs its own shutdown path.
        if self._client is not None:
            try:
                self._client.quit()
            except LimeAutomationError:
                pass
            self._client.close()
            self._client = None

        if self._process is not None:
            try:
                self._process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait(timeout=5)
            self._process = None
