"""Checks the editor panels this project expects, and captures the full window."""

from __future__ import annotations

from pathlib import Path

OUTPUT_DIR = Path("Saved") / "Automation"

# Panels the engine provides plus the one this project registers.
EXPECTED_PANELS = ("Console", "Inspector", "Stats", "Viewport", "Triangle")


def run(engine) -> dict:
    panels = {panel["name"]: panel for panel in engine.list_panels()}

    missing = [name for name in EXPECTED_PANELS if name not in panels]
    assert not missing, f"missing panel(s): {missing}; found {sorted(panels)}"

    # Toggling has to be observable, otherwise the visibility flag is not actually wired up.
    engine.show_panel("Console", visible=False)
    assert not engine.panel_visible("Console"), "the Console panel did not hide"

    engine.wait_frames(2)
    without_console = engine.screenshot()

    engine.show_panel("Console", visible=True)
    assert engine.panel_visible("Console"), "the Console panel did not come back"

    engine.wait_frames(2)
    with_console = engine.screenshot()
    assert without_console != with_console, "hiding a panel did not change the window"

    destination = engine.save_screenshot(OUTPUT_DIR / "editor-layout.png")

    assert not engine.errors(), f"the engine logged errors: {engine.errors()}"
    return {"artifacts": [destination], "panels": sorted(panels)}
