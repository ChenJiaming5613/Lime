"""Tests for the harness: script discovery, running and engine discovery.

These exercise the library rather than the engine, so most need no running instance.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from lime_automation import LimeClient, find_repo_root, resolve_executable
from lime_automation.discovery import discover_engines
from lime_automation.errors import LimeAutomationError
from lime_automation.paths import project_output_dir, project_source_dir
from lime_automation.runner import discover_scripts, find_script, run_script, script_directory


def test_repo_root_is_found() -> None:
    root = find_repo_root()

    assert (root / "CMakeLists.txt").is_file()
    assert (root / "Engine").is_dir()


def test_executable_resolves(config: str, preset: str, project: str) -> None:
    executable = resolve_executable(project=project, config=config, preset=preset)

    assert executable.exists()
    assert executable.stem == project


def test_project_owns_its_output_directory(config: str, preset: str, project: str) -> None:
    """Each project must have a private directory, or their settings files overwrite each other.

    The engine resolves Shaders, Content, Saved and ProjectSettings.json relative to the executable,
    so a shared directory makes one project read another's configuration and fail to find its shaders.
    """
    executable = resolve_executable(project=project, config=config, preset=preset)
    expected = project_output_dir(project=project, config=config, preset=preset)

    assert executable.parent == expected, f"{executable} is not inside {expected}"

    # The outputs belong to the project's own directory, not to a shared build tree.
    assert executable.is_relative_to(project_source_dir(project=project)), (
        f"{executable} is outside {project_source_dir(project=project)}"
    )

    # The runtime dependencies have to be inside that directory, not a level up.
    assert (executable.parent / "ProjectSettings.json").is_file()
    assert (executable.parent / "Shaders").is_dir()


def test_project_settings_copy_matches_the_project(config: str, preset: str, project: str) -> None:
    """The deployed settings file must describe this project and no other."""
    import json

    executable = resolve_executable(project=project, config=config, preset=preset)
    settings = json.loads((executable.parent / "ProjectSettings.json").read_text(encoding="utf-8"))

    assert settings["name"] == project, f"the deployed settings name '{settings['name']}' is not '{project}'"


def test_project_shaders_are_under_the_project_directory(config: str, preset: str, project: str) -> None:
    """Engine shaders are copied in and the project's own compiled in; both must be present."""
    executable = resolve_executable(project=project, config=config, preset=preset)
    shaders = executable.parent / "Shaders"

    # At least one backend directory from the engine set.
    assert any((shaders / platform).is_dir() for platform in ("DXIL", "SPIRV")), f"no engine shaders in {shaders}"

    project_shaders = shaders / project
    if project_shaders.is_dir():
        assert any(project_shaders.iterdir()), f"{project_shaders} is empty"


def test_project_scripts_are_discovered(project: str) -> None:
    scripts = discover_scripts(project=project)
    directory = script_directory(project=project)

    if not directory.is_dir():
        pytest.skip(f"{project} ships no automation scripts")

    assert scripts, f"no scripts found in {directory}"
    for script in scripts:
        assert script.path.suffix == ".py"
        # The description comes from the docstring, so every script should carry one.
        assert script.description, f"{script.name} has no docstring"


def test_script_lookup_is_case_insensitive(project: str) -> None:
    scripts = discover_scripts(project=project)
    if not scripts:
        pytest.skip(f"{project} ships no automation scripts")

    name = scripts[0].name
    assert find_script(name.upper(), project=project).path == scripts[0].path


def test_unknown_script_reports_alternatives(project: str) -> None:
    with pytest.raises(LimeAutomationError) as error:
        find_script("definitely-not-a-script", project=project)

    # The message should help rather than just say no.
    assert "Available:" in str(error.value)


def test_running_engine_is_discoverable(engine: LimeClient, config: str, preset: str) -> None:
    """The endpoint file lets a script attach to an engine it did not start."""
    engines = discover_engines(config=config, preset=preset)
    assert engines, "the running engine did not publish a discoverable endpoint"

    assert any(item.url == engine.url for item in engines), f"{engine.url} is not among {[item.url for item in engines]}"

    for item in engines:
        assert item.pid > 0
        assert item.protocol == 1


def test_discovered_endpoint_connects(engine: LimeClient, config: str, preset: str) -> None:
    endpoint = next(item for item in discover_engines(config=config, preset=preset) if item.url == engine.url)
    client = endpoint.connect()

    assert client.is_alive()
    assert client.engine_info()["project"] == endpoint.project


def test_endpoint_file_lives_in_the_project_directory(
    engine: LimeClient, config: str, preset: str, project: str
) -> None:
    """Writable state must stay inside the project's own directory.

    Sharing Saved/ between projects would make them overwrite each other's layout, logs and endpoint
    files, and would make an endpoint ambiguous about which project it belongs to.
    """
    endpoint = next(item for item in discover_engines(config=config, preset=preset) if item.url == engine.url)
    expected = project_output_dir(project=project, config=config, preset=preset)

    assert endpoint.source.is_relative_to(expected), f"{endpoint.source} is outside {expected}"
    assert endpoint.source.parent == expected / "Saved" / "Automation"
    assert endpoint.project == project


def test_script_runs_against_an_engine(engine: LimeClient, project: str, tmp_path: Path) -> None:
    """Runs a real project script, which is the path the launcher uses."""
    scripts = discover_scripts(project=project)
    if not scripts:
        pytest.skip(f"{project} ships no automation scripts")

    result = run_script(scripts[0].name, engine, project=project)

    assert result.ok, f"{result.name} failed: {result.error}\n{result.traceback_text}"


def test_script_failure_is_captured_not_raised(engine: LimeClient, tmp_path: Path, monkeypatch) -> None:
    """A failing script must be reported as a result, so a batch run continues past it."""
    directory = tmp_path / "Automation"
    directory.mkdir()
    (directory / "failing.py").write_text('"""Fails on purpose."""\n\n\ndef run(engine):\n    assert False, "expected"\n')

    monkeypatch.setattr("lime_automation.runner.script_directory", lambda **_: directory)

    result = run_script("failing", engine)

    assert result.ok is False
    assert "expected" in result.error
    assert result.traceback_text, "a failure should carry a traceback for diagnosis"


def test_script_without_entry_point_is_reported(engine: LimeClient, tmp_path: Path, monkeypatch) -> None:
    directory = tmp_path / "Automation"
    directory.mkdir()
    (directory / "noentry.py").write_text('"""No run function."""\n\nvalue = 1\n')

    monkeypatch.setattr("lime_automation.runner.script_directory", lambda **_: directory)

    result = run_script("noentry", engine)

    assert result.ok is False
    assert "run(engine)" in result.error
