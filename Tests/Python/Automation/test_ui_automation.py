"""UI automation driven by the Dear ImGui test engine.

These differ from the panel tests in test_panels.py in an important way: panel.show flips a
visibility flag directly, while these click the actual menu item and drag the actual widget. That is
what lets them catch a menu entry wired to the wrong panel, or a slider whose value never reaches the
pass behind it.

The run is asynchronous because a UI test spans many frames. Waiting inside the engine would stop the
frame loop the tests depend on, so the client polls instead.
"""

from __future__ import annotations

import pytest

from lime_automation import LimeClient
from lime_automation.errors import CommandError

# A run drives real widgets across many frames, which is slower than a normal command test.
pytestmark = [pytest.mark.editor, pytest.mark.slow]


def test_builtin_ui_tests_are_registered(engine: LimeClient) -> None:
    """The engine's own tests must be present, or a green run would prove nothing."""
    tests = engine.list_ui_tests()
    names = {test["qualifiedName"] for test in tests}

    # Registered through an explicit call, since a static library drops object files that only
    # register things.
    assert "Editor/menu_toggles_panel" in names
    assert "Editor/drag_changes_slider_value" in names

    for test in tests:
        assert test["category"]
        assert test["name"]
        assert test["qualifiedName"] == f"{test['category']}/{test['name']}"


def test_project_ui_tests_are_registered(engine: LimeClient) -> None:
    """Project tests use the static macro, which only works because project sources are in the exe."""
    names = {test["qualifiedName"] for test in engine.list_ui_tests()}
    assert any(name.startswith("HelloTriangle/") for name in names), f"no project UI tests in {names}"


def test_clicking_and_dragging_widgets_passes(engine: LimeClient) -> None:
    """The whole suite, which is the actual point: clicks and drags reach the code behind them."""
    registered = len(engine.list_ui_tests())
    status = engine.run_ui_tests()

    assert status["running"] is False
    assert status["remaining"] == 0
    # Every registered test ran, so a suite that silently skipped some cannot pass here.
    assert status["tested"] == registered

    failed = engine.failed_ui_tests(status)
    assert not failed, f"UI tests failed: {', '.join(failed)}"
    assert status["succeeded"] == status["tested"]

    # A failing test logs an error, so the engine fixture's own error assertion would fire too. The
    # explicit check above reports which test failed, which the fixture cannot.
    engine.clear_logs()


def test_filter_selects_a_single_test(engine: LimeClient) -> None:
    """A filter must queue only what it matches.

    The counters describe every registered test, not just this run, because a test keeps the status
    of the last run it took part in. So the filtered test is identified by name instead.
    """
    status = engine.run_ui_tests(filter="Editor/menu_toggles_panel")

    assert status["running"] is False
    assert not engine.failed_ui_tests(status)

    by_name = {test["qualifiedName"]: test for test in status["tests"]}
    assert by_name["Editor/menu_toggles_panel"]["status"] == "success"
    assert by_name["Editor/menu_toggles_panel"]["durationSeconds"] > 0.0


def test_run_counter_distinguishes_consecutive_runs(engine: LimeClient) -> None:
    """Without this a client could not tell 'the previous run ended' from 'mine has not started'."""
    first = engine.run_ui_tests(filter="Editor/menu_opens_inspector")
    second = engine.run_ui_tests(filter="Editor/menu_opens_inspector")

    assert second["run"] == first["run"] + 1


def test_unmatched_filter_is_an_error(engine: LimeClient) -> None:
    """Silently running nothing would look exactly like a suite that passed."""
    with pytest.raises(CommandError) as error:
        engine.start_ui_tests(filter="no-such-test")

    assert "no-such-test" in str(error.value)


def test_status_is_readable_while_a_run_is_in_flight(engine: LimeClient) -> None:
    """Polling must not disturb the run.

    This is a regression test with a specific cause: the obvious implementation calls
    ImGuiTestEngine_GetResultSummary, which asserts that no test is running. Under the test engine's
    assert handler that aborts the process, and the modal dialog Windows opens would freeze the frame
    loop, so every later command would time out with an unrelated message.
    """
    engine.start_ui_tests()
    try:
        # Reachable while tests are still running, which is the case being covered.
        status = engine.ui_test_status()
        assert "running" in status
        assert "tests" in status
    finally:
        engine.abort_ui_tests()
        # Aborting fails the test that was interrupted, which is expected here.
        engine.run_ui_tests()
        engine.clear_logs()


def test_ui_tests_are_unavailable_without_the_editor(headless_engine: LimeClient) -> None:
    """A --no-editor session has no ImGui context, so the commands must explain rather than crash."""
    with pytest.raises(CommandError) as error:
        headless_engine.list_ui_tests()

    assert "editor" in str(error.value).lower()
