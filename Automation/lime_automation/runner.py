"""Runs project automation scripts against an engine.

A project ships scripts under Automation/ and the engine can be asked to run one at startup. The
engine embeds no interpreter, so "run a script" means: launch the engine, wait for it, then execute
the script in this process with a connected client injected into it.

That split keeps the engine free of Python while still letting a single command reproduce a scenario.
"""

from __future__ import annotations

import importlib.util
import inspect
import sys
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from .client import LimeClient
from .errors import LimeAutomationError
from .paths import DEFAULT_CONFIG, DEFAULT_PRESET, DEFAULT_PROJECT, find_repo_root

SCRIPT_DIRECTORY_NAME = "Automation"
# The callable a script must expose. Keeping it fixed means a script needs no registration.
ENTRY_POINT = "run"


@dataclass
class ScriptResult:
    """Outcome of one script."""

    name: str
    ok: bool
    error: str = ""
    traceback_text: str = ""
    returned: Any = None
    artifacts: list[Path] = field(default_factory=list)

    def __bool__(self) -> bool:
        return self.ok


@dataclass(frozen=True)
class ScriptInfo:
    """A discovered script, before it is loaded."""

    name: str
    path: Path
    description: str


def script_directory(project: str = DEFAULT_PROJECT, root: Path | None = None) -> Path:
    """Where a project keeps its scripts. Mirrors the engine's own resolution."""
    root = root or find_repo_root()
    return root / "Projects" / project / SCRIPT_DIRECTORY_NAME


def discover_scripts(project: str = DEFAULT_PROJECT, root: Path | None = None) -> list[ScriptInfo]:
    """Lists the scripts a project ships, sorted by name."""
    directory = script_directory(project=project, root=root)
    if not directory.is_dir():
        return []

    scripts: list[ScriptInfo] = []
    for path in sorted(directory.glob("*.py")):
        # Skips module plumbing such as __init__.py and conftest.py.
        if path.stem.startswith("_") or path.stem == "conftest":
            continue
        scripts.append(ScriptInfo(name=path.stem, path=path, description=_read_description(path)))
    return scripts


def _read_description(path: Path) -> str:
    """First docstring line, matching what the engine reports for the same file."""
    try:
        source = path.read_text(encoding="utf-8")
    except OSError:
        return ""

    try:
        module = compile(source, str(path), "exec", dont_inherit=True, optimize=0)
    except SyntaxError:
        return ""

    docstring = module.co_consts[0] if module.co_consts else None
    if isinstance(docstring, str):
        return docstring.strip().splitlines()[0] if docstring.strip() else ""
    return ""


def find_script(name: str, project: str = DEFAULT_PROJECT, root: Path | None = None) -> ScriptInfo:
    """Locates one script by name, case insensitively."""
    scripts = discover_scripts(project=project, root=root)
    for script in scripts:
        if script.name.lower() == name.lower():
            return script

    available = ", ".join(script.name for script in scripts) or "none"
    raise LimeAutomationError(f"No automation script named '{name}' in project '{project}'. Available: {available}")


def _load_entry_point(script: ScriptInfo) -> Callable[..., Any]:
    """Imports the script and returns its run function.

    Loaded under a unique module name so two projects may ship scripts with the same file name without
    one shadowing the other in sys.modules.
    """
    module_name = f"lime_script_{script.path.parent.name}_{script.name}"
    spec = importlib.util.spec_from_file_location(module_name, script.path)
    if spec is None or spec.loader is None:
        raise LimeAutomationError(f"Cannot load {script.path}")

    module = importlib.util.module_from_spec(spec)
    # Registered before execution so a script that imports itself, directly or through dataclasses,
    # sees a consistent module object.
    sys.modules[module_name] = module

    # The script's own directory goes on the path, so it can import helpers next to it.
    directory = str(script.path.parent)
    inserted = directory not in sys.path
    if inserted:
        sys.path.insert(0, directory)

    try:
        spec.loader.exec_module(module)
    finally:
        if inserted:
            sys.path.remove(directory)

    entry = getattr(module, ENTRY_POINT, None)
    if not callable(entry):
        raise LimeAutomationError(f"{script.path.name} defines no callable '{ENTRY_POINT}(engine)'")
    return entry


def run_script(
    script: ScriptInfo | str,
    engine: LimeClient,
    project: str = DEFAULT_PROJECT,
    root: Path | None = None,
    **kwargs: Any,
) -> ScriptResult:
    """Executes one script against a connected engine.

    A raised exception is captured rather than propagated, so a caller running several scripts gets a
    result for each instead of stopping at the first failure.
    """
    if isinstance(script, str):
        script = find_script(script, project=project, root=root)

    try:
        entry = _load_entry_point(script)
    except LimeAutomationError as error:
        return ScriptResult(name=script.name, ok=False, error=str(error))

    # Extra keyword arguments are only passed when the script declares them, so the common
    # "def run(engine)" signature stays valid.
    accepted = _filter_kwargs(entry, kwargs)

    try:
        returned = entry(engine, **accepted)
    except AssertionError as error:
        # A bare assert is the natural way to write a check, so it is reported as a failed script
        # rather than as a crash.
        return ScriptResult(
            name=script.name,
            ok=False,
            error=str(error) or "assertion failed",
            traceback_text=traceback.format_exc(),
        )
    except Exception as error:  # noqa: BLE001 - a script may raise anything
        return ScriptResult(
            name=script.name,
            ok=False,
            error=f"{type(error).__name__}: {error}",
            traceback_text=traceback.format_exc(),
        )

    # A script may return False to fail without raising.
    if returned is False:
        return ScriptResult(name=script.name, ok=False, error="the script returned False")

    artifacts = _collect_artifacts(returned)
    return ScriptResult(name=script.name, ok=True, returned=returned, artifacts=artifacts)


def _filter_kwargs(entry: Callable[..., Any], kwargs: dict[str, Any]) -> dict[str, Any]:
    try:
        signature = inspect.signature(entry)
    except (TypeError, ValueError):
        return {}

    if any(parameter.kind == inspect.Parameter.VAR_KEYWORD for parameter in signature.parameters.values()):
        return kwargs
    return {name: value for name, value in kwargs.items() if name in signature.parameters}


def _collect_artifacts(returned: Any) -> list[Path]:
    """Extracts written file paths from a script's return value, for reporting."""
    if isinstance(returned, dict):
        candidates = returned.get("artifacts", [])
    elif isinstance(returned, (list, tuple)):
        candidates = returned
    else:
        return []

    return [Path(item) for item in candidates if isinstance(item, (str, Path))]


def run_scripts(
    names: list[str],
    engine: LimeClient,
    project: str = DEFAULT_PROJECT,
    root: Path | None = None,
    **kwargs: Any,
) -> list[ScriptResult]:
    """Runs several scripts against one engine, continuing past failures."""
    return [run_script(name, engine, project=project, root=root, **kwargs) for name in names]
