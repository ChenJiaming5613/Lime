"""Shared pytest fixtures.

An engine process costs several seconds to start, so it is shared per module and per backend rather
than per test. Tests therefore have to leave the engine as they found it; the `engine` fixture asserts
that no errors were logged, which catches a test that corrupted the state for the next one.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

# The package lives one directory up, so the suite runs from a checkout without installation.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from lime_automation import LimeClient, LimeSession  # noqa: E402
from lime_automation.errors import LaunchError  # noqa: E402
from lime_automation.paths import DEFAULT_CONFIG, DEFAULT_PRESET, DEFAULT_PROJECT  # noqa: E402

# Kept small so a test run does not depend on the host's display size.
TEST_WIDTH = 1024
TEST_HEIGHT = 576


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--backend", action="append", default=[], choices=["d3d12", "vulkan"],
                     help="Backend to test; repeat for several. Defaults to d3d12.")
    parser.addoption("--config", default=DEFAULT_CONFIG, help="Build configuration to test")
    parser.addoption("--preset", default=DEFAULT_PRESET, help="CMake preset whose Build directory to use")
    parser.addoption("--project", default=DEFAULT_PROJECT, help="Project executable to run")


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "editor: requires the editor UI to be running")
    config.addinivalue_line("markers", "slow: launches an extra engine process")


def pytest_generate_tests(metafunc: pytest.Metafunc) -> None:
    """Parametrizes every backend dependent fixture over the selected backends.

    Done here rather than with a fixture parameter so --backend controls the matrix from the command
    line and each backend gets its own reported test id.
    """
    if "backend" in metafunc.fixturenames:
        backends = metafunc.config.getoption("backend") or ["d3d12"]
        metafunc.parametrize("backend", backends, scope="module")


@pytest.fixture(scope="session")
def config(pytestconfig: pytest.Config) -> str:
    return pytestconfig.getoption("config")


@pytest.fixture(scope="session")
def preset(pytestconfig: pytest.Config) -> str:
    return pytestconfig.getoption("preset")


@pytest.fixture(scope="session")
def project(pytestconfig: pytest.Config) -> str:
    return pytestconfig.getoption("project")


@pytest.fixture(scope="module")
def session(backend: str, config: str, preset: str, project: str):
    """One engine process per module and backend, with the editor enabled."""
    launcher = LimeSession(
        project=project,
        config=config,
        preset=preset,
        backend=backend,
        width=TEST_WIDTH,
        height=TEST_HEIGHT,
    )
    try:
        launcher.start()
    except LaunchError as error:
        pytest.skip(f"Cannot launch the engine: {error}")

    yield launcher
    launcher.stop()


@pytest.fixture
def engine(session) -> LimeClient:
    """A connected client, with the log cleared so assertions only see this test's output."""
    client = session.client
    client.clear_logs()
    yield client

    # An error logged by the engine means the test left it unhappy even if every assertion passed.
    errors = client.errors()
    assert not errors, "the engine logged errors: " + "; ".join(f"{item['category']}: {item['message']}" for item in errors)


@pytest.fixture(scope="module")
def headless_session(backend: str, config: str, preset: str, project: str):
    """An engine without the editor, for testing the runtime only path."""
    launcher = LimeSession(
        project=project,
        config=config,
        preset=preset,
        backend=backend,
        width=TEST_WIDTH,
        height=TEST_HEIGHT,
        no_editor=True,
    )
    try:
        launcher.start()
    except LaunchError as error:
        pytest.skip(f"Cannot launch the engine: {error}")

    yield launcher
    launcher.stop()


@pytest.fixture
def headless_engine(headless_session) -> LimeClient:
    return headless_session.client


@pytest.fixture
def triangle_pass(engine: LimeClient) -> str:
    """Name of the project's reflected pass, restored to its original values afterwards.

    Restoring is what makes the shared engine safe: a test may change anything and the next one still
    starts from the documented defaults.
    """
    name = engine.find_pass()
    if name is None:
        pytest.skip("The project registers no pass with reflected settings")

    original = engine.get_pass_values(name)
    yield name
    engine.set_pass_values(name, original)


@pytest.fixture
def artifacts(tmp_path: Path) -> Path:
    """Per test directory for captures, which pytest keeps for the last few runs."""
    directory = tmp_path / "artifacts"
    directory.mkdir(parents=True, exist_ok=True)
    return directory
