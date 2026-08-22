"""Locates the repository, build outputs and executables.

Kept separate from the client so a script can find an executable without starting anything, and so
the layout assumptions live in exactly one place.
"""

from __future__ import annotations

import os
from pathlib import Path

from .errors import LaunchError

# Marks the repository root. Checking for several entries avoids matching a directory that merely
# happens to contain a CMakeLists.txt.
ROOT_MARKERS = ("CMakeLists.txt", "Engine", "Projects", "ThirdParty")

DEFAULT_PRESET = "ninja"
DEFAULT_CONFIG = "Debug"
DEFAULT_PROJECT = "HelloTriangle"


def find_repo_root(start: Path | None = None) -> Path:
    """Walks upwards until the repository root is found.

    Allows the package to be imported from anywhere: a test run from Automation/ and one run from the
    repository root resolve the same paths.
    """
    override = os.environ.get("LIME_ROOT")
    if override:
        candidate = Path(override).expanduser().resolve()
        if _is_repo_root(candidate):
            return candidate

    current = (start or Path(__file__)).resolve()
    for directory in (current, *current.parents):
        if _is_repo_root(directory):
            return directory
    raise LaunchError(f"No LimeEngine repository root above {current}; set LIME_ROOT to point at it")


def _is_repo_root(directory: Path) -> bool:
    return directory.is_dir() and all((directory / marker).exists() for marker in ROOT_MARKERS)


def build_output_dir(
    config: str = DEFAULT_CONFIG,
    preset: str = DEFAULT_PRESET,
    root: Path | None = None,
    build_dir: Path | None = None,
) -> Path:
    """Directory holding the binaries for one preset and configuration.

    A caller may pass build_dir to name the CMake binary directory outright, which is what the ctest
    integration does: deriving it from a preset name would break for any non-default layout.

    Single config generators put binaries directly under Bin, multi config ones under Bin/<Config>.
    Both layouts are accepted so the caller does not need to know which generator was used.
    """
    if build_dir is None:
        environment_override = os.environ.get("LIME_BUILD_DIR")
        if environment_override:
            build_dir = Path(environment_override)

    if build_dir is not None:
        base = Path(build_dir).expanduser().resolve() / "Bin"
    else:
        root = root or find_repo_root()
        base = root / "Build" / preset / "Bin"

    with_config = base / config
    if with_config.is_dir():
        return with_config
    return base


def resolve_executable(
    project: str = DEFAULT_PROJECT,
    config: str = DEFAULT_CONFIG,
    preset: str = DEFAULT_PRESET,
    root: Path | None = None,
    build_dir: Path | None = None,
) -> Path:
    """Absolute path of a project executable. Raises when it has not been built."""
    directory = build_output_dir(config=config, preset=preset, root=root, build_dir=build_dir)
    suffix = ".exe" if os.name == "nt" else ""
    executable = directory / f"{project}{suffix}"
    if not executable.exists():
        raise LaunchError(f"{executable} not found; build it first with ./Scripts/Build.ps1 -Config {config}")
    return executable


def saved_dir(executable: Path) -> Path:
    """The engine's writable directory, which sits next to the executable."""
    return executable.parent / "Saved"
