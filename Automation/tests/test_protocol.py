"""Protocol level tests: transport, routing and error reporting.

These cover the HTTP layer rather than engine behaviour, so a change to the protocol is caught here
instead of surfacing as a confusing failure in a higher level test.
"""

from __future__ import annotations

import json
import urllib.error
import urllib.request

import pytest

from lime_automation import CommandError, LimeClient, TransportError
from lime_automation import PROTOCOL_VERSION


def test_identity_document(engine: LimeClient) -> None:
    info = engine.info()

    assert info["server"] == "LimeEngine"
    assert info["protocol"] == PROTOCOL_VERSION, "the client and engine disagree on the protocol version"
    assert info["port"] > 0
    assert info["commandCount"] > 0


def test_command_catalogue(engine: LimeClient) -> None:
    commands = engine.commands()
    names = {command["name"] for command in commands}

    # A representative command from each group, so a whole group going missing is caught.
    for expected in ("ping", "engine.info", "log.tail", "pass.list", "settings.get", "screenshot.capture"):
        assert expected in names, f"{expected} is not registered; found {sorted(names)}"

    assert all(command["description"] for command in commands), "every command needs a description"

    # Transport-only commands must not appear, or the catalogue stops describing what to call.
    assert "screenshot.encode" not in names, "a hidden command leaked into the catalogue"


def test_ping_reports_frames(engine: LimeClient) -> None:
    first = engine.ping()
    assert first["pong"] is True

    # Commands run between frames, so a second call always lands on a later frame.
    engine.wait_frames(1)
    assert engine.ping()["frame"] > first["frame"]


def test_command_by_path(engine: LimeClient) -> None:
    """POST /command/<name> is the form usable from curl, so it has to work without a body."""
    request = urllib.request.Request(f"{engine.url}/command/engine.info", data=b"", method="POST")
    with urllib.request.urlopen(request, timeout=10) as response:
        payload = json.loads(response.read())

    assert payload["ok"] is True
    assert "backend" in payload["result"]


def test_unknown_command_is_rejected(engine: LimeClient) -> None:
    with pytest.raises(CommandError) as error:
        engine.call("does.not.exist")

    assert "Unknown command" in str(error.value)
    assert error.value.command == "does.not.exist"


def test_try_call_reports_failure_without_raising(engine: LimeClient) -> None:
    reply = engine.try_call("does.not.exist")

    assert reply.ok is False
    assert "Unknown command" in reply.error


def test_malformed_json_is_a_client_error(engine: LimeClient) -> None:
    """A bad body is a transport level mistake, so it must use an HTTP status rather than ok=false."""
    request = urllib.request.Request(
        f"{engine.url}/command",
        data=b"{not json",
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    with pytest.raises(urllib.error.HTTPError) as error:
        urllib.request.urlopen(request, timeout=10)

    assert error.value.code == 400
    payload = json.loads(error.value.read())
    assert payload["ok"] is False
    assert "Malformed JSON" in payload["error"]


def test_unknown_route_returns_json(engine: LimeClient) -> None:
    """Even a 404 has to be JSON, otherwise a client cannot report what went wrong."""
    with pytest.raises(urllib.error.HTTPError) as error:
        urllib.request.urlopen(f"{engine.url}/nope", timeout=10)

    assert error.value.code == 404
    payload = json.loads(error.value.read())
    assert payload["ok"] is False


def test_batch_preserves_order_and_isolates_failures(engine: LimeClient) -> None:
    replies = engine.batch([
        "ping",
        ("engine.info", {}),
        ("does.not.exist", {}),
        "engine.stats",
    ])

    assert len(replies) == 4
    assert replies[0].ok and replies[1].ok
    # A failure in the middle must not discard the results around it.
    assert not replies[2].ok
    assert replies[3].ok, "a command after a failed one was skipped"
    assert "frame" in replies[3].result


def test_unreachable_engine_raises_transport_error() -> None:
    # Port 1 is reserved and never listening, so this exercises the connection failure path.
    client = LimeClient("http://127.0.0.1:1", timeout=2.0)

    assert client.is_alive() is False
    with pytest.raises(TransportError):
        client.ping()
