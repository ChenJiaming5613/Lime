"""Engine discovery: endpoint files, the handshake and the port scan.

These cover how a client finds an engine, which is what the interactive shell depends on. They matter
because both failure modes here are confusing in practice: a stale endpoint file makes a client
connect to nothing, and a slow probe makes the shell look like it hung.
"""

from __future__ import annotations

import pytest

from lime_automation import LimeClient, discover_engines, find_engine, handshake, scan_ports
from lime_automation.discovery import DEFAULT_PORT
from lime_automation.errors import EngineNotFoundError


def _port_of(engine: LimeClient) -> int:
    return int(engine.info()["port"])


def test_handshake_identifies_a_running_engine(engine: LimeClient) -> None:
    endpoint = handshake(_port_of(engine))

    assert endpoint is not None
    assert endpoint.protocol > 0
    assert endpoint.project, "the handshake must report which project answered"


def test_handshake_rejects_a_port_with_nothing_on_it() -> None:
    """A closed port must return None rather than raise, so a scan can keep going."""
    # Far from the engine's range, and unlikely to be in use.
    assert handshake(1, timeout=0.2) is None


def test_scan_finds_the_running_engine(engine: LimeClient) -> None:
    port = _port_of(engine)
    found = scan_ports(start=port, count=1)

    assert [item.port for item in found] == [port]


def test_scan_is_quick_when_nothing_answers() -> None:
    """Probes must run concurrently.

    Sequentially this would take count * timeout seconds. That is what made an interactive shell
    appear to hang, so the bound is asserted rather than assumed.
    """
    import time

    start = time.monotonic()
    # A range chosen to be closed, so every probe has to time out.
    found = scan_ports(start=1, count=8, timeout=0.2)
    elapsed = time.monotonic() - start

    assert found == []
    assert elapsed < 2.0, f"probing 8 closed ports took {elapsed:.1f}s; they are not running concurrently"


def test_find_engine_accepts_an_explicit_port(engine: LimeClient) -> None:
    endpoint = find_engine(port=_port_of(engine))

    assert endpoint.port == _port_of(engine)
    # A scan cannot know the pid, and saying so is better than reporting a wrong one.
    assert endpoint.discovered_by_scan
    assert "port scan" in endpoint.describe()


def test_find_engine_reports_an_unusable_port(engine: LimeClient) -> None:
    with pytest.raises(EngineNotFoundError) as error:
        find_engine(port=1)

    assert "1" in str(error.value)


def test_find_engine_rejects_a_project_mismatch(engine: LimeClient) -> None:
    """Connecting to the wrong project silently would be worse than failing."""
    with pytest.raises(EngineNotFoundError) as error:
        find_engine(port=_port_of(engine), project="NotTheRunningProject")

    assert "NotTheRunningProject" in str(error.value)


def test_discovery_finds_the_running_engine(engine: LimeClient) -> None:
    engines = discover_engines()
    ports = {item.port for item in engines}

    assert _port_of(engine) in ports


def test_discovered_endpoints_are_alive(engine: LimeClient) -> None:
    """Verification is what keeps files left behind by a killed process out of the results."""
    for endpoint in discover_engines():
        assert endpoint.connect(timeout=2.0).is_alive()


def test_default_port_matches_the_engine_default(engine: LimeClient) -> None:
    """The scan starts at the port the engine uses unless told otherwise."""
    assert DEFAULT_PORT == 5613
