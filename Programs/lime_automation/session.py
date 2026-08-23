"""Engine process lifetime.

LimeSession launches an executable, waits for it to answer, hands back a client and shuts it down on
exit. Port 0 is the default so concurrent sessions never collide, which is what lets a test suite run
several engines at once.
"""

from __future__ import annotations

import subprocess
import time
from pathlib import Path
from types import TracebackType

from .client import LimeClient
from .discovery import _read_endpoint
from .errors import LaunchError, TransportError
from .paths import DEFAULT_CONFIG, DEFAULT_PRESET, DEFAULT_PROJECT, resolve_executable

DEFAULT_STARTUP_TIMEOUT = 60.0
# Long enough for a driver to initialize on a cold start, short enough to fail a hung run.
SHUTDOWN_TIMEOUT = 15.0


class LimeSession:
    """Context manager that owns one engine process.

    Entering returns a connected LimeClient:

        with LimeSession(backend="vulkan") as engine:
            engine.set_pass_values("Triangle", {"bPaused": True})
    """

    def __init__(
        self,
        project: str = DEFAULT_PROJECT,
        config: str = DEFAULT_CONFIG,
        preset: str = DEFAULT_PRESET,
        backend: str | None = None,
        width: int | None = None,
        height: int | None = None,
        no_editor: bool = False,
        vsync: bool | None = None,
        validation: str | None = None,
        port: int = 0,
        extra_args: list[str] | None = None,
        startup_timeout: float = DEFAULT_STARTUP_TIMEOUT,
        keep_layout: bool = False,
        capture_output: bool = False,
        root: Path | None = None,
    ) -> None:
        self.executable = resolve_executable(project=project, config=config, preset=preset, root=root)
        self.working_dir = self.executable.parent
        self.project = project
        self.startup_timeout = startup_timeout
        self.keep_layout = keep_layout
        self.capture_output = capture_output

        self._process: subprocess.Popen | None = None
        self._client: LimeClient | None = None
        self._log_path = self.working_dir / "Saved" / "Logs" / "LimeEngine.log"

        # Port 0 asks the OS for a free port, which is what allows concurrent sessions. The engine
        # publishes the result, so the client does not have to guess.
        self.args = [str(self.executable), f"--automation-port={port}"]
        if backend is not None:
            self.args.append(f"--rhi={backend}")
        if width is not None:
            self.args.append(f"--width={width}")
        if height is not None:
            self.args.append(f"--height={height}")
        if no_editor:
            self.args.append("--no-editor")
        if vsync is False:
            self.args.append("--no-vsync")
        if validation is not None:
            self.args.append(f"--validation={validation}")
        if extra_args:
            self.args.extend(extra_args)

    # -- lifetime ----------------------------------------------------------------------------

    @property
    def client(self) -> LimeClient:
        if self._client is None:
            raise LaunchError("The session is not started")
        return self._client

    @property
    def pid(self) -> int | None:
        return self._process.pid if self._process is not None else None

    def start(self) -> LimeClient:
        if self._client is not None:
            return self._client

        if not self.keep_layout:
            # A saved layout hides panels added since it was written, which is usually the opposite of
            # what a test wants to observe.
            (self.working_dir / "Saved" / "EditorLayout.ini").unlink(missing_ok=True)

        # Removed up front so the file that appears can only belong to this run.
        endpoint_file = self.working_dir / "AutomationEndpoint.json"
        endpoint_file.unlink(missing_ok=True)

        output = subprocess.PIPE if self.capture_output else subprocess.DEVNULL
        self._process = subprocess.Popen(self.args, cwd=str(self.working_dir), stdout=output, stderr=output)
        self._client = self._wait_until_ready(endpoint_file)
        return self._client

    def _wait_until_ready(self, endpoint_file: Path) -> LimeClient:
        deadline = time.monotonic() + self.startup_timeout
        pid = self._process.pid if self._process else 0
        per_process_file = self.working_dir / "Saved" / "Automation" / f"{pid}.json"

        while time.monotonic() < deadline:
            if self._process is not None and self._process.poll() is not None:
                raise LaunchError(
                    f"The engine exited with code {self._process.returncode} before it was ready\n{self.log_tail()}"
                )

            # The per process file is preferred: it is unambiguous even when another engine is running
            # from the same directory and rewrote the shared one.
            for candidate in (per_process_file, endpoint_file):
                endpoint = _read_endpoint(candidate) if candidate.exists() else None
                if endpoint is None:
                    continue

                client = LimeClient(endpoint.url)
                if client.is_alive():
                    return client

            time.sleep(0.05)

        self.stop()
        raise LaunchError(f"The automation server did not start within {self.startup_timeout}s\n{self.log_tail()}")

    def stop(self) -> None:
        """Asks the engine to exit, then makes sure the process is gone."""
        if self._client is not None:
            try:
                self._client.quit()
            except (TransportError, Exception):
                # Already gone, or wedged; the process handling below deals with both.
                pass
            self._client = None

        if self._process is None:
            return

        try:
            self._process.wait(timeout=SHUTDOWN_TIMEOUT)
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait(timeout=5)
        finally:
            self._process = None

    def __enter__(self) -> LimeClient:
        return self.start()

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc_value: BaseException | None,
        traceback: TracebackType | None,
    ) -> None:
        self.stop()

    # -- diagnostics -------------------------------------------------------------------------

    def log_tail(self, lines: int = 25) -> str:
        """Last lines of the engine log, used to explain a startup failure."""
        if not self._log_path.exists():
            return "(no log file)"
        content = self._log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        return "\n".join(content[-lines:])

    def log_issues(self, minimum_level: str = "warning") -> list[str]:
        """Formatted log entries at or above a level, for use as an assertion."""
        if self._client is None:
            return []
        try:
            entries = self._client.logs(count=0, level=minimum_level)
        except (TransportError, Exception):
            return []
        return [f"[{entry['level']}] {entry['category']}: {entry['message']}" for entry in entries]
