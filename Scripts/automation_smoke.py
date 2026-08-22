"""End to end automation smoke test.

Launches the engine, exercises every command group and verifies the observable effects. Screenshots
are written so a rendering change can be reviewed by eye afterwards.

Usage:
    python Scripts/automation_smoke.py
    python Scripts/automation_smoke.py --backend vulkan
    python Scripts/automation_smoke.py --backend d3d12 --backend vulkan
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from lime_automation import LimeAutomationError, LimeSession


class Checker:
    """Collects failures instead of stopping at the first one, so one run reports everything."""

    def __init__(self) -> None:
        self.failures: list[str] = []

    def check(self, condition: bool, description: str) -> bool:
        status = "PASS" if condition else "FAIL"
        print(f"  [{status}] {description}")
        if not condition:
            self.failures.append(description)
        return condition

    def note(self, message: str) -> None:
        print(f"  ....  {message}")


def resolve_triangle_pass(client) -> str | None:
    """Finds the project's pass among the registered ones.

    Passes are addressed by the name they report from GetName, not by their class name, so the lookup
    is by the pass that exposes reflected settings and is not the engine's own ImGui pass.
    """
    for entry in client.list_passes():
        if entry["name"] != "ImGui" and entry["hasSettings"]:
            return entry["name"]
    return None


def run_backend(backend: str, config: str) -> list[str]:
    print(f"\n=== {backend} ===")
    checker = Checker()

    with LimeSession(backend=backend, config=config, width=1280, height=720) as client:
        info = client.engine_info()
        checker.note(f"{info['backend']} on {info['adapter']}")
        checker.check(info["backend"].lower().startswith(backend[:2].lower()) or backend in info["backend"].lower(),
                      f"engine.info reports the requested backend ({info['backend']})")
        checker.check(info["editorEnabled"], "the editor is active")
        checker.check(info.get("offscreenRendering", False), "the scene renders into the viewport target")

        stats = client.engine_stats()
        checker.check(stats["frame"] > 0, f"frames are advancing (frame {stats['frame']})")

        commands = client.help()
        checker.check(len(commands) >= 12, f"help lists the command set ({len(commands)} commands)")

        # --- Render passes, driven entirely by reflection ---
        passes = client.list_passes()
        checker.note("passes: " + ", ".join(entry["name"] for entry in passes))
        checker.check(any(entry["name"] == "ImGui" for entry in passes), "the ImGui pass is registered")

        pass_name = resolve_triangle_pass(client)
        if not checker.check(pass_name is not None, "the project's triangle pass is registered"):
            return checker.failures

        fields = client.describe_pass(pass_name)
        field_names = {field["name"] for field in fields}
        checker.note("fields: " + ", ".join(sorted(field_names)))
        checker.check("RotationSpeed" in field_names, "RotationSpeed is discoverable through reflection")

        # Metadata has to survive the round trip, otherwise a client cannot know the valid range.
        speed_field = next((field for field in fields if field["name"] == "RotationSpeed"), None)
        if speed_field is not None:
            checker.check("min" in speed_field and "max" in speed_field,
                          f"RotationSpeed exposes its range ({speed_field.get('min')}..{speed_field.get('max')})")

        original = client.get_pass_values(pass_name)
        checker.note(f"original values: {original}")

        # Pausing makes the render deterministic, which is what allows a stable screenshot.
        applied = client.set_pass_values(pass_name, {"bPaused": True, "RotationSpeed": 0.0})
        checker.check(applied["bPaused"] is True, "bPaused was written and read back")
        checker.check(abs(applied["RotationSpeed"]) < 1e-6, "RotationSpeed was written and read back")

        applied = client.set_pass_values(pass_name, {"Tint": [0.2, 1.0, 0.35, 1.0]})
        checker.check(applied["Tint"][1] > 0.9, f"a vector field round trips ({applied['Tint']})")

        # An unknown field must be rejected rather than silently ignored.
        try:
            client.set_pass_values(pass_name, {"NoSuchField": 1.0})
            checker.check(False, "writing an unknown field is rejected")
        except LimeAutomationError as error:
            checker.check("Unknown field" in str(error), "writing an unknown field is rejected")

        # A type mismatch must be rejected too.
        try:
            client.set_pass_values(pass_name, {"RotationSpeed": "fast"})
            checker.check(False, "a type mismatch is rejected")
        except LimeAutomationError as error:
            checker.check("does not fit" in str(error), "a type mismatch is rejected")

        # --- Editor panels ---
        panels = client.list_panels()
        panel_names = {panel["name"] for panel in panels}
        checker.note("panels: " + ", ".join(sorted(panel_names)))
        checker.check("Console" in panel_names, "the built-in Console panel exists")
        checker.check("Inspector" in panel_names, "the built-in Inspector panel exists")
        checker.check(any(panel["name"] == "Triangle" for panel in panels), "the project's Triangle panel is registered")

        client.show_panel("Console", visible=False)
        hidden = next(panel for panel in client.list_panels() if panel["name"] == "Console")
        checker.check(hidden["visible"] is False, "a panel can be hidden")

        client.show_panel("Console", visible=True)
        shown = next(panel for panel in client.list_panels() if panel["name"] == "Console")
        checker.check(shown["visible"] is True, "a panel can be shown again")

        # --- Settings ---
        settings = client.get_settings()
        checker.check(settings["rhi"]["backend"] in {"d3d12", "vulkan"}, f"settings report the backend ({settings['rhi']['backend']})")

        updated = client.set_settings({"window": {"title": "LimeEngine Automation"}})
        checker.check(updated["window"]["title"] == "LimeEngine Automation", "the window title can be changed live")

        # An out of range value must be refused, leaving the rest untouched.
        try:
            client.set_settings({"rhi": {"backBufferCount": 99}})
            checker.check(False, "an out of range setting is rejected")
        except LimeAutomationError:
            checker.check(True, "an out of range setting is rejected")
        checker.check(client.get_settings()["rhi"]["backBufferCount"] == settings["rhi"]["backBufferCount"],
                      "a rejected setting leaves the previous value in place")

        # --- Logs ---
        entries = client.logs(count=200)
        checker.check(len(entries) > 0, f"the log buffer is readable ({len(entries)} entries)")

        # --- Screenshots ---
        client.wait_frames(3)

        window_shot = client.screenshot(f"smoke-{backend}-window.png")
        checker.check(Path(window_shot).exists(), f"the back buffer was captured ({Path(window_shot).name})")

        scene_shot = client.screenshot(f"smoke-{backend}-scene.png", source="viewport")
        checker.check(Path(scene_shot).exists(), f"the viewport was captured ({Path(scene_shot).name})")

        if Path(window_shot).exists():
            size = Path(window_shot).stat().st_size
            # A blank PNG of this size compresses to a few KB, so a larger file means real content.
            checker.check(size > 5000, f"the capture holds actual image data ({size // 1024} KB)")

        # Errors logged during the run are failures in their own right.
        errors = [entry for entry in client.logs(count=0, level="error")]
        for entry in errors:
            checker.note(f"logged error: {entry['category']}: {entry['message']}")
        checker.check(not errors, "no errors were logged")

        checker.note(f"screenshots in {Path(window_shot).parent}")

    return checker.failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--backend", action="append", choices=["d3d12", "vulkan"],
                        help="Backend to test; repeat for several. Defaults to d3d12.")
    parser.add_argument("--config", default="Debug", choices=["Debug", "Release", "RelWithDebInfo"])
    arguments = parser.parse_args()

    backends = arguments.backend or ["d3d12"]
    all_failures: dict[str, list[str]] = {}

    for backend in backends:
        try:
            failures = run_backend(backend, arguments.config)
        except LimeAutomationError as error:
            print(f"  [FAIL] the session could not run: {error}")
            failures = [f"session startup: {error}"]
        if failures:
            all_failures[backend] = failures

    print("\n=== summary ===")
    if not all_failures:
        print(f"All checks passed for: {', '.join(backends)}")
        return 0

    for backend, failures in all_failures.items():
        print(f"{backend}: {len(failures)} failure(s)")
        for failure in failures:
            print(f"  - {failure}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
