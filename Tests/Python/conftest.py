"""Root configuration for the Python test suites.

Command line options are registered here rather than in a suite's own conftest because pytest parses
the command line before descending into subdirectories: an option declared deeper would be rejected
as unrecognized when passed on the command line.

The path injection lives here for the same reason, so every suite under this directory can import the
client library without installing it.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Tests/Python -> repository root -> Programs, which holds the lime_automation package.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Programs"))

from lime_automation.paths import DEFAULT_CONFIG, DEFAULT_PRESET, DEFAULT_PROJECT  # noqa: E402


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--backend", action="append", default=[], choices=["d3d12", "vulkan"],
                     help="Backend to test; repeat for several. Defaults to d3d12.")
    parser.addoption("--config", default=DEFAULT_CONFIG, help="Build configuration to test")
    parser.addoption("--preset", default=DEFAULT_PRESET, help="CMake preset whose Build directory to use")
    parser.addoption("--project", default=DEFAULT_PROJECT, help="Project executable to run")


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "editor: requires the editor UI to be running")
    config.addinivalue_line("markers", "slow: launches an extra engine process")
