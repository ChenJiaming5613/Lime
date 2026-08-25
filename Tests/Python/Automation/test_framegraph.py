"""The frame graph inspector: loading a graph, reading it back and saving it.

Drives the panel through automation commands rather than by clicking, because the properties worth
asserting — that a file becomes the graph it describes, and that saving reproduces it — are about the data
and not about the widget.

Uses the default project: the frame graph is part of the pipeline description and has nothing to do with
which scene is loaded, so these tests do not need the sample assets.
"""

from __future__ import annotations

import pytest

from lime_automation import CommandError, LimeClient

# Ships in Engine/Content/FrameGraph and is copied next to the executable by the build.
EXAMPLE = "DeferredExample.json"


@pytest.fixture
def framegraph(engine: LimeClient) -> LimeClient:
    """An engine whose frame graph panel is available, with the log cleared for this test.

    Asserts a clean log afterwards, which is how an unnoticed error surfaces. A test that provokes an
    error on purpose clears the log itself before returning.
    """
    try:
        engine.call("framegraph.info")
    except CommandError as error:
        pytest.skip(f"the frame graph panel is not in this build: {error}")

    engine.clear_logs()
    yield engine

    errors = engine.errors()
    assert not errors, "the engine logged errors: " + "; ".join(f"{item['category']}: {item['message']}" for item in errors)


class TestPanelRegistration:
    def test_the_panel_is_registered_and_starts_hidden(self, framegraph: LimeClient) -> None:
        """Hidden by default because it inspects the pipeline rather than the scene.

        Registration is worth asserting on its own: a panel living in a static library is only linked in
        because EditorLayer names its type, and losing that is a silent failure.
        """
        panels = framegraph.call("panel.list")["panels"]
        entry = next((item for item in panels if item["name"] == "Frame Graph"), None)

        assert entry is not None, f"Frame Graph missing from {[item['name'] for item in panels]}"
        assert entry["visible"] is False
        assert entry["dockSlot"] == "Center"

    def test_showing_the_panel_keeps_the_engine_running(self, framegraph: LimeClient) -> None:
        """The canvas draws on the frame it becomes visible, which is where a bad id or a null context
        would take the process down."""
        framegraph.call("panel.show", panel="Frame Graph", visible=True)

        first = framegraph.ping()["frame"]
        framegraph.wait_frames(5)
        assert framegraph.ping()["frame"] > first

        framegraph.call("panel.show", panel="Frame Graph", visible=False)


class TestLoading:
    def test_the_example_graph_loads_as_described(self, framegraph: LimeClient) -> None:
        """The shipped example and the built-in pass types are edited independently, so this also catches
        the two drifting apart."""
        info = framegraph.call("framegraph.load", path=EXAMPLE)

        assert info["passCount"] == 6
        assert info["edgeCount"] == 6
        assert info["errorCount"] == 0
        assert info["valid"] is True
        assert info["graphOutputs"] == ["ToneMap.ldr"]

        names = {pass_entry["name"] for pass_entry in info["passes"]}
        assert names == {"ShadowCaster", "DepthPrepass", "ForwardLit", "Bloom", "ToneMap", "Present"}

    def test_both_edge_kinds_survive_loading(self, framegraph: LimeClient) -> None:
        """A data edge carries a resource and an execution edge only orders passes; collapsing the two
        would either invent a resource or lose one."""
        info = framegraph.call("framegraph.load", path=EXAMPLE)

        kinds = [edge["kind"] for edge in info["edges"]]
        assert kinds.count("data") == 5
        assert kinds.count("execution") == 1

        execution = next(edge for edge in info["edges"] if edge["kind"] == "execution")
        # Names a pass on each end, with no resource part.
        assert "." not in execution["from"]
        assert "." not in execution["to"]

    def test_info_reports_the_graph_without_loading(self, framegraph: LimeClient) -> None:
        """Reading state must not be a mutation, so a script can poll it."""
        framegraph.call("framegraph.load", path=EXAMPLE)
        first = framegraph.call("framegraph.info")
        second = framegraph.call("framegraph.info")

        assert first["passes"] == second["passes"]
        assert first["edges"] == second["edges"]

    def test_a_missing_file_fails_without_disturbing_the_loaded_graph(self, framegraph: LimeClient) -> None:
        """A mistyped path should not replace a working graph with an empty one."""
        framegraph.call("framegraph.load", path=EXAMPLE)

        with pytest.raises(CommandError):
            framegraph.call("framegraph.load", path="NoSuchGraph.json")

        assert framegraph.call("framegraph.info")["passCount"] == 6

        # The engine logs an error for the failed load, which is correct: a load that did not happen is
        # worth recording. Cleared here so the fixture's clean log check still means something for the
        # tests that do not provoke one.
        framegraph.clear_logs()


class TestSaving:
    def test_a_saved_graph_loads_back_the_same(self, framegraph: LimeClient) -> None:
        """The round trip is the whole point: the file is the only record, so anything lost on save is
        lost for good."""
        original = framegraph.call("framegraph.load", path=EXAMPLE)
        framegraph.call("framegraph.save", path="RoundTrip.json")

        # Saved into the writable directory, so this reads it back from there rather than from Content.
        saved_path = framegraph.call("framegraph.save", path="RoundTrip.json")["path"]
        reloaded = framegraph.call("framegraph.load", path=saved_path)

        assert reloaded["passes"] == original["passes"]
        assert reloaded["edges"] == original["edges"]
        assert reloaded["graphOutputs"] == original["graphOutputs"]
        assert reloaded["errorCount"] == 0
