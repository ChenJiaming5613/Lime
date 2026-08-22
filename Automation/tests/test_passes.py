"""Reflection driven pass control.

This is the mechanism that makes the engine scriptable at all: a pass that declares LIME_REFLECT
becomes readable and writable by name with no automation code of its own.
"""

from __future__ import annotations

import pytest

from lime_automation import CommandError, LimeClient


def test_passes_are_enumerable(engine: LimeClient) -> None:
    passes = engine.list_passes()
    assert passes, "no render passes are registered"

    names = {entry["name"] for entry in passes}
    assert "ImGui" in names, "the engine's own ImGui pass should be registered"

    for entry in passes:
        assert "priority" in entry
        assert "hasSettings" in entry


def test_describe_reports_types_and_metadata(engine: LimeClient, triangle_pass: str) -> None:
    fields = {field["name"]: field for field in engine.describe_pass(triangle_pass)}
    assert fields, f"{triangle_pass} exposes no reflected fields"

    speed = fields.get("RotationSpeed")
    assert speed is not None, f"RotationSpeed is missing from {sorted(fields)}"
    assert speed["type"] == "float"
    # The range is what lets a client pick a valid value instead of hardcoding one.
    assert "min" in speed and "max" in speed
    assert speed["min"] < speed["max"]

    tint = fields.get("Tint")
    assert tint is not None
    assert tint["type"] == "vector4"
    assert len(tint["value"]) == 4


def test_values_round_trip(engine: LimeClient, triangle_pass: str) -> None:
    applied = engine.set_pass_values(triangle_pass, {"bPaused": True, "RotationSpeed": 0.0})

    assert applied["bPaused"] is True
    assert applied["RotationSpeed"] == pytest.approx(0.0)

    # Read back through a separate call, so the reply is not just echoing the request.
    values = engine.get_pass_values(triangle_pass)
    assert values["bPaused"] is True
    assert values["RotationSpeed"] == pytest.approx(0.0)


def test_vector_round_trip(engine: LimeClient, triangle_pass: str) -> None:
    applied = engine.set_pass_values(triangle_pass, {"Tint": [0.25, 0.5, 0.75, 1.0]})

    assert applied["Tint"] == pytest.approx([0.25, 0.5, 0.75, 1.0])


def test_partial_write_leaves_other_fields_alone(engine: LimeClient, triangle_pass: str) -> None:
    before = engine.get_pass_values(triangle_pass)
    engine.set_pass_values(triangle_pass, {"RotationSpeed": 0.5})
    after = engine.get_pass_values(triangle_pass)

    assert after["RotationSpeed"] == pytest.approx(0.5)
    for name, value in before.items():
        if name != "RotationSpeed":
            assert after[name] == value, f"{name} changed unexpectedly"


def test_unknown_field_is_rejected(engine: LimeClient, triangle_pass: str) -> None:
    with pytest.raises(CommandError) as error:
        engine.set_pass_values(triangle_pass, {"NoSuchField": 1.0})

    assert "Unknown field" in str(error.value)


def test_type_mismatch_is_rejected(engine: LimeClient, triangle_pass: str) -> None:
    before = engine.get_pass_values(triangle_pass)

    with pytest.raises(CommandError) as error:
        engine.set_pass_values(triangle_pass, {"RotationSpeed": "fast"})

    assert "does not fit" in str(error.value)
    assert engine.get_pass_values(triangle_pass)["RotationSpeed"] == before["RotationSpeed"]


def test_unknown_pass_is_rejected(engine: LimeClient) -> None:
    with pytest.raises(CommandError):
        engine.get_pass_values("NoSuchPass")


def test_pass_without_settings_is_reported_clearly(engine: LimeClient) -> None:
    """Describing a pass with no reflected settings is refused with a message that says why.

    Failing rather than returning an empty list is the better contract here: a client asking for a
    pass's fields has almost certainly picked the wrong pass, and silence would hide that.
    """
    for entry in engine.list_passes():
        if not entry["hasSettings"]:
            with pytest.raises(CommandError) as error:
                engine.describe_pass(entry["name"])
            assert "no reflected settings" in str(error.value)
            return
    pytest.skip("every registered pass exposes settings")
