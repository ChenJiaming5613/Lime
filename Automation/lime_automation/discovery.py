"""Finds engines that are already running.

The engine publishes an endpoint file per process under Saved/Automation inside its own project
directory, so a script can attach to a session someone started by hand instead of launching its own.
That is what makes this usable for interactive debugging rather than only for batch tests.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .client import LimeClient
from .errors import EngineNotFoundError
from .paths import legacy_build_output_dir, project_output_dir, projects_root


@dataclass(frozen=True)
class EngineEndpoint:
    """One discovered engine."""

    port: int
    url: str
    project: str
    pid: int
    protocol: int
    source: Path

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


def discover_engines(
    config: str = "Debug",
    preset: str = "ninja",
    root: Path | None = None,
    verify: bool = True,
) -> list[EngineEndpoint]:
    """Lists running engines, newest first.

    Every project directory is searched, so an engine is found regardless of which one it belongs to.

    With verify=True each candidate is probed, which filters out files left behind by a crash. That
    costs one request per entry but avoids handing back an endpoint nothing is listening on.
    """
    endpoint_files: list[Path] = []
    for directory in _endpoint_directories(config=config, preset=preset, root=root):
        automation = directory / "Saved" / "Automation"
        if automation.is_dir():
            endpoint_files.extend(automation.glob("*.json"))

    candidates: list[EngineEndpoint] = []
    for path in sorted(endpoint_files, key=lambda item: item.stat().st_mtime, reverse=True):
        endpoint = _read_endpoint(path)
        if endpoint is None:
            continue
        if verify and not endpoint.connect(timeout=2.0).is_alive():
            continue
        candidates.append(endpoint)
    return candidates


def find_engine(
    project: str | None = None,
    config: str = "Debug",
    preset: str = "ninja",
    root: Path | None = None,
) -> EngineEndpoint:
    """Returns one running engine, optionally filtered by project name.

    Raises EngineNotFoundError rather than returning None, since every caller needs an engine and the
    message explains how to start one.
    """
    engines = discover_engines(config=config, preset=preset, root=root)
    if project is not None:
        engines = [engine for engine in engines if engine.project == project]

    if not engines:
        target = f" for project '{project}'" if project else ""
        raise EngineNotFoundError(
            f"No running engine found{target}. Start one with --automation, or use LimeSession to launch it."
        )
    return engines[0]
