"""Finds engines that are already running.

Two ways to locate one, tried in that order:

  The engine publishes an endpoint file per process under Saved/Automation inside its own project
  directory. This is the fast path and the only one that reports which project a port belongs to.

  Failing that, a port range is scanned. That covers an engine started from a build tree this
  checkout does not know about, or one whose endpoint file could not be written.

Both confirm a candidate with a handshake against GET /, so a file left behind by a crash or an
unrelated service listening on the port is never mistaken for an engine.
"""

from __future__ import annotations

import json
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from .client import LimeClient
from .errors import CommandError, EngineNotFoundError, TransportError
from .paths import legacy_build_output_dir, project_output_dir, projects_root

# The engine's default automation port, and the range a scan covers around it.
DEFAULT_PORT = 5613
SCAN_PORT_COUNT = 16

# A live engine on loopback answers in well under this. Kept short because an unreachable port has to
# time out before the next candidate is tried.
HANDSHAKE_TIMEOUT = 1.0

# Endpoint files accumulate when a process is killed rather than closed, and probing each one
# sequentially would stall a shell for seconds. Probes are independent, so they run concurrently.
MAX_PROBE_WORKERS = 16


@dataclass(frozen=True)
class EngineEndpoint:
    """One discovered engine."""

    port: int
    url: str
    project: str
    pid: int
    protocol: int
    source: Path

    @property
    def discovered_by_scan(self) -> bool:
        """True when this came from a port handshake rather than an endpoint file.

        A scan cannot report the pid, since GET / does not carry one, so callers that print it need
        to know the difference.
        """
        return self.pid < 0

    def describe(self) -> str:
        """One line identifying the engine, for a shell banner or a listing."""
        who = f"{self.project}" if self.project else "unknown project"
        where = "port scan" if self.discovered_by_scan else f"pid {self.pid}"
        return f"{self.url} ({who}, {where})"

    def connect(self, timeout: float = 35.0) -> LimeClient:
        return LimeClient(self.url, timeout=timeout)


def _read_endpoint(path: Path) -> EngineEndpoint | None:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None

    port = payload.get("port")
    if not isinstance(port, int) or port <= 0:
        return None

    return EngineEndpoint(
        port=port,
        url=payload.get("url", f"http://127.0.0.1:{port}"),
        project=payload.get("project", ""),
        pid=payload.get("pid", 0),
        protocol=payload.get("protocol", 0),
        source=path,
    )


def _endpoint_directories(config: str, preset: str, root: Path | None) -> list[Path]:
    """Every Saved/Automation directory an engine could have published into.

    Each project owns Projects/<Name>/Binaries/<Config>/Saved/Automation, so the project directories
    are the primary source. The old shared build tree is scanned as well, so a tree built before
    projects owned their outputs still resolves.
    """
    directories: list[Path] = []

    projects = projects_root(root=root)
    if projects.is_dir():
        for project in (item for item in projects.iterdir() if item.is_dir()):
            directories.append(project_output_dir(project=project.name, config=config, preset=preset, root=root))

    legacy = legacy_build_output_dir(config=config, preset=preset, root=root)
    if legacy.is_dir():
        directories.append(legacy)
        directories.extend(item for item in legacy.iterdir() if item.is_dir())

    return directories


def handshake(port: int, timeout: float = HANDSHAKE_TIMEOUT) -> EngineEndpoint | None:
    """Confirms an automation server is listening on a port, and returns what it says about itself.

    GET / is the handshake: it answers before the first frame and without going through command
    dispatch, so it works even while the engine is busy. The reply identifies the project, pid and
    protocol, which is what separates an engine from any other service that happens to hold the port.
    """
    client = LimeClient.from_port(port, timeout=timeout)
    try:
        payload = client.info()
    except (TransportError, CommandError):
        return None

    if not isinstance(payload, dict) or "protocol" not in payload:
        # Something answered, but it is not an automation server.
        return None

    return EngineEndpoint(
        port=port,
        url=client.url,
        project=payload.get("project", ""),
        # GET / does not carry a pid: only the endpoint file knows it. Negative marks it unknown
        # rather than pretending it is process 0.
        pid=-1,
        protocol=payload.get("protocol", 0),
        source=Path(),
    )


def scan_ports(
    start: int = DEFAULT_PORT,
    count: int = SCAN_PORT_COUNT,
    timeout: float = HANDSHAKE_TIMEOUT,
) -> list[EngineEndpoint]:
    """Handshakes with a contiguous port range and returns whatever answered.

    Used when no endpoint file describes a running engine, which happens when it was started from a
    different build tree or could not write its file. Probes run concurrently: an unreachable port
    has to time out, and doing that one at a time would take count * timeout seconds.
    """
    ports = list(range(start, start + max(count, 1)))
    with ThreadPoolExecutor(max_workers=min(MAX_PROBE_WORKERS, len(ports))) as pool:
        found = pool.map(lambda port: handshake(port, timeout=timeout), ports)
    return [endpoint for endpoint in found if endpoint is not None]


def discover_engines(
    config: str = "Debug",
    preset: str = "ninja",
    root: Path | None = None,
    verify: bool = True,
    scan: bool = True,
) -> list[EngineEndpoint]:
    """Lists running engines, newest first.

    Every project directory is searched, so an engine is found regardless of which one it belongs to.

    With verify=True each candidate is probed, which filters out files left behind by a crash. Those
    accumulate whenever a process is killed rather than closed, so the probes run concurrently: doing
    them one at a time made a shell appear to hang for tens of seconds.

    With scan=True a port range is also tried when no file yielded a live engine.
    """
    endpoint_files: list[Path] = []
    for directory in _endpoint_directories(config=config, preset=preset, root=root):
        automation = directory / "Saved" / "Automation"
        if automation.is_dir():
            endpoint_files.extend(automation.glob("*.json"))

    # Newest first, so the most recently started engine is the one a caller gets by default.
    ordered = sorted(endpoint_files, key=lambda item: item.stat().st_mtime, reverse=True)
    candidates = [endpoint for endpoint in map(_read_endpoint, ordered) if endpoint is not None]

    if verify and candidates:
        with ThreadPoolExecutor(max_workers=min(MAX_PROBE_WORKERS, len(candidates))) as pool:
            alive = pool.map(lambda item: item.connect(timeout=HANDSHAKE_TIMEOUT).is_alive(), candidates)
        candidates = [endpoint for endpoint, ok in zip(candidates, alive) if ok]

    if candidates:
        return candidates

    if scan:
        # Nothing on disk pointed at a live engine, so fall back to asking the ports directly.
        return scan_ports()
    return []


def find_engine(
    project: str | None = None,
    config: str = "Debug",
    preset: str = "ninja",
    root: Path | None = None,
    port: int | None = None,
) -> EngineEndpoint:
    """Returns one running engine, optionally filtered by project name.

    With `port` the search is skipped and that port is handshaked directly, which is the way to reach
    an engine this checkout cannot discover, or to pick one out of several deliberately.

    Raises EngineNotFoundError rather than returning None, since every caller needs an engine and the
    message explains how to start one.
    """
    if port is not None:
        endpoint = handshake(port)
        if endpoint is None:
            raise EngineNotFoundError(f"Nothing answered the automation handshake on port {port}")
        if project is not None and endpoint.project != project:
            raise EngineNotFoundError(
                f"Port {port} is project '{endpoint.project}', not '{project}'"
            )
        return endpoint

    engines = discover_engines(config=config, preset=preset, root=root)
    if project is not None:
        engines = [engine for engine in engines if engine.project == project]

    if not engines:
        target = f" for project '{project}'" if project else ""
        raise EngineNotFoundError(
            f"No running engine found{target}. Start one with --automation, or use LimeSession to launch it."
        )
    return engines[0]
