"""Verifies the triangle renders and its reflected settings take effect.

Scripts receive a connected engine and assert against it. A raised AssertionError fails the script;
returning a dict with an "artifacts" list reports the files that were written.
"""

from __future__ import annotations

from pathlib import Path

# Screenshots go next to the engine's other output rather than into the source tree.
OUTPUT_DIR = Path("Saved") / "Automation"


def run(engine) -> dict:
    artifacts = []

    info = engine.engine_info()
    assert info["editorEnabled"], "this script expects the editor to be running"
    assert info["offscreenRendering"], "the scene should render into the viewport target"

    triangle = engine.find_pass()
    assert triangle is not None, "the project's triangle pass is not registered"

    fields = {field["name"]: field for field in engine.describe_pass(triangle)}
    assert "RotationSpeed" in fields, f"RotationSpeed is missing from {sorted(fields)}"
    assert "Tint" in fields, f"Tint is missing from {sorted(fields)}"

    original = engine.get_pass_values(triangle)

    # Pausing makes the output deterministic, which is what allows comparing captures at all.
    applied = engine.set_pass_values(triangle, {"bPaused": True, "RotationSpeed": 0.0})
    assert applied["bPaused"] is True, "bPaused was not applied"

    engine.wait_frames(2)
    paused_first = engine.screenshot(source="viewport")
    engine.wait_frames(2)
    paused_second = engine.screenshot(source="viewport")
    assert paused_first == paused_second, "the paused triangle still changed between frames"

    # A colour change has to be visible in the render, not just readable back.
    engine.set_pass_values(triangle, {"Tint": [0.2, 1.0, 0.35, 1.0]})
    engine.wait_frames(2)
    tinted = engine.screenshot(source="viewport")
    assert tinted != paused_first, "changing Tint did not change the rendered image"

    destination = engine.save_screenshot(OUTPUT_DIR / "triangle-tinted.png", source="viewport")
    artifacts.append(destination)

    # Restored so the engine is left as it was found, which matters when attaching to a live session.
    engine.set_pass_values(triangle, original)

    assert not engine.errors(), f"the engine logged errors: {engine.errors()}"
    return {"artifacts": artifacts, "pass": triangle}
