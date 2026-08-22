"""Editor panels, project settings, logs and the runtime only path."""

from __future__ import annotations

import pytest

from lime_automation import CommandError, LimeClient

# Panels the engine itself provides; a project may add more but must not remove these.
BUILTIN_PANELS = ("Console", "Inspector", "Stats", "Viewport")


def test_builtin_panels_are_registered(engine: LimeClient) -> None:
    panels = {panel["name"] for panel in engine.list_panels()}

    for name in BUILTIN_PANELS:
        assert name in panels, f"{name} is missing; found {sorted(panels)}"


def test_panel_metadata_is_complete(engine: LimeClient) -> None:
    for panel in engine.list_panels():
        assert "visible" in panel
        # The dock slot is what drives the default layout, so it has to be reported.
        assert panel.get("dockSlot"), f"{panel['name']} reports no dock slot"


def test_panel_visibility_toggles(engine: LimeClient) -> None:
    original = engine.panel_visible("Console")
    try:
        engine.show_panel("Console", visible=False)
        assert engine.panel_visible("Console") is False

        engine.show_panel("Console", visible=True)
        assert engine.panel_visible("Console") is True
    finally:
        engine.show_panel("Console", visible=original)


def test_unknown_panel_is_rejected(engine: LimeClient) -> None:
    with pytest.raises(CommandError):
        engine.show_panel("NoSuchPanel", visible=True)


def test_settings_expose_the_running_configuration(engine: LimeClient) -> None:
    settings = engine.get_settings()

    assert settings["rhi"]["backend"] in {"d3d12", "vulkan"}
    assert settings["window"]["width"] > 0
    assert "automation" in settings, "the automation section should be part of the schema"


def test_settings_backend_matches_the_device(engine: LimeClient) -> None:
    """The reported settings must describe the engine that is actually running."""
    settings = engine.get_settings()
    info = engine.engine_info()

    assert settings["rhi"]["backend"].lower() == info["backend"].lower()


def test_editing_settings_leaves_the_session_untouched(engine: LimeClient) -> None:
    """Settings are consumed at startup, so an edit must not change the running configuration.

    This is the property that makes settings.get trustworthy: it always describes what the engine is
    actually using, never a value that was requested but could not take effect.
    """
    live_before = engine.get_settings()
    original_title = engine.get_pending_settings()["window"]["title"]

    try:
        pending = engine.set_settings({"window": {"title": "Automation Test", "width": 4242}})

        # The draft records the edit.
        assert pending["window"]["title"] == "Automation Test"
        assert pending["window"]["width"] == 4242

        # The session does not.
        assert engine.get_settings() == live_before
        assert engine.has_unsaved_settings()
    finally:
        engine.set_settings({
            "window": {"title": original_title, "width": live_before["window"]["width"]},
        })


def test_pending_settings_start_clean(engine: LimeClient) -> None:
    """Before anything is edited the draft must equal what is running.

    Otherwise saving without editing would silently rewrite the file with different values.
    """
    reply = engine.call("settings.get")

    assert reply["pending"] == reply["settings"]
    assert reply["dirty"] is False


def test_setting_reports_that_a_restart_is_needed(engine: LimeClient) -> None:
    """The reply has to say so, or a client cannot tell the edit is not live."""
    original = engine.get_pending_settings()["rhi"]["vsync"]
    try:
        reply = engine.call("settings.set", settings={"rhi": {"vsync": not original}})
        assert reply["restartRequired"] is True
    finally:
        engine.set_settings({"rhi": {"vsync": original}})


def test_invalid_setting_is_rejected_atomically(engine: LimeClient) -> None:
    """A rejected request must not apply the valid keys alongside the invalid one.

    The draft is what gets inspected here: checking the live values would pass trivially, since they
    never change either way.
    """
    before = engine.get_pending_settings()

    with pytest.raises(CommandError):
        engine.set_settings({"window": {"width": 1024}, "rhi": {"backBufferCount": 99}})

    after = engine.get_pending_settings()
    assert after == before, "a rejected request modified the draft"


def test_logs_are_readable_and_filterable(engine: LimeClient) -> None:
    entries = engine.logs(count=200)
    assert isinstance(entries, list)

    for entry in entries:
        assert {"time", "level", "category", "message"} <= entry.keys()

    # Filtering by level must not return anything below it.
    warnings = engine.logs(count=0, level="warning")
    assert all(entry["level"] in {"warning", "error", "critical"} for entry in warnings)


def test_log_clear_empties_the_buffer(engine: LimeClient) -> None:
    engine.clear_logs()
    # Reading the log is itself side effect free, so the buffer stays empty or nearly so.
    assert len(engine.logs(count=500)) < 10


def test_scripts_are_discoverable(engine: LimeClient) -> None:
    """The engine reports the scripts its project ships, which is what the launcher lists."""
    result = engine.call("script.list")

    assert "scripts" in result
    for script in result["scripts"]:
        assert script["name"]
        assert script["path"].endswith(".py")


@pytest.mark.slow
def test_runtime_only_path_degrades_gracefully(headless_engine: LimeClient) -> None:
    """Without the editor there is no viewport target and no panels, and both must fail clearly."""
    info = headless_engine.engine_info()
    assert info["editorEnabled"] is False
    assert info.get("offscreenRendering") is False, "no offscreen target should exist without the editor"

    # The back buffer is still capturable, which is the point of supporting this path.
    headless_engine.wait_frames(2)
    assert headless_engine.screenshot().startswith(b"\x89PNG")

    with pytest.raises(CommandError):
        headless_engine.screenshot(source="viewport")

    with pytest.raises(CommandError):
        headless_engine.list_panels()
