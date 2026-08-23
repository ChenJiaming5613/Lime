"""Locates the repository, project outputs and executables.

Kept separate from the client so a script can find an executable without starting anything, and so
the layout assumptions live in exactly one place.

Each project is self contained: its binaries, content and writable state live under
Projects/<Name>/Binaries/<Config>/, not in the shared build tree. The build tree still holds engine
level outputs such as LimeTests.exe and the shader staging area.
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

# Names of the per project directories, mirroring CMake/LimeProject.cmake.
BINARIES_DIRECTORY_NAME = "Binaries"
PROJECTS_DIRECTORY_NAME = "Projects"


def find_repo_root(start: Path | None = None) -> Path:
    """Walks upwards until the repository root is found.

    Allows the package to be imported from anywhere. It searches for marker entries rather than
    counting directory levels, so moving the package does not break it: a test run from
    Tests/Python and a script run from the repository root resolve the same paths.
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


def projects_root(root: Path | None = None) -> Path:
    """Directory holding every project's source tree, and now its outputs as well."""
    root = root or find_repo_root()
    return root / PROJECTS_DIRECTORY_NAME


def project_source_dir(project: str = DEFAULT_PROJECT, root: Path | None = None) -> Path:
    """A project's authored directory, the one tracked in git."""
    return projects_root(root=root) / project


def project_output_dir(
    project: str = DEFAULT_PROJECT,
    config: str = DEFAULT_CONFIG,
    preset: str = DEFAULT_PRESET,
    root: Path | None = None,
    build_dir: Path | None = None,
) -> Path:
    """A project's private output directory, which is also its working directory at run time.

    Each project owns one so that Shaders, Content, Saved and ProjectSettings.json cannot collide
    between projects; the engine resolves all of them relative to the executable.

    preset and build_dir are accepted but unused for the current layout, because the outputs no
    longer depend on which build tree produced them. They are kept so callers, including the ctest
    integration, need not know that and so the legacy fallback below can still use them.

    Single config generators put binaries directly under Binaries, multi config ones under
    Binaries/<Config>. Both are accepted so the caller does not need to know which generator was used.
    """
    base = project_source_dir(project=project, root=root) / BINARIES_DIRECTORY_NAME
    with_config = base / config
    if with_config.is_dir():
        return with_config
    return base


def legacy_build_output_dir(
    config: str = DEFAULT_CONFIG,
    preset: str = DEFAULT_PRESET,
    root: Path | None = None,
    build_dir: Path | None = None,
) -> Path:
    """Root of the old shared binary tree, Build/<preset>/Bin/<Config>.

    Only used as a fallback, so a tree built before projects owned their outputs still resolves
    rather than failing with a confusing "not found".
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
    suffix = ".exe" if os.name == "nt" else ""
    directory = project_output_dir(project=project, config=config, preset=preset, root=root, build_dir=build_dir)
    executable = directory / f"{project}{suffix}"
    if executable.exists():
        return executable

    # Falls back to the old shared build tree, both the per project and the flat layout it used.
    legacy_base = legacy_build_output_dir(config=config, preset=preset, root=root, build_dir=build_dir)
    for legacy in (legacy_base / project / f"{project}{suffix}", legacy_base / f"{project}{suffix}"):
        if legacy.exists():
            return legacy

    raise LaunchError(f"{executable} not found; build it first with ./Scripts/Build.ps1 -Config {config}")


def saved_dir(executable: Path) -> Path:
    """The engine's writable directory, which sits next to the executable."""
    return executable.parent / "Saved"
