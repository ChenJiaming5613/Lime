"""The render graph inspector: loading a graph, reading it back and saving it.

Drives the panel through automation commands rather than by clicking, because the properties worth
asserting — that a file becomes the graph it describes, and that saving reproduces it — are about the data
and not about the widget.

Uses the default project: the render graph is part of the pipeline description and has nothing to do with
which scene is loaded, so these tests do not need the sample assets.
"""

from __future__ import annotations

import pytest

from lime_automation import CommandError, LimeClient

# Ships in Engine/Content/RenderGraph and is copied next to the executable by the build.
EXAMPLE = "DeferredExample.json"


@pytest.fixture
def rendergraph(engine: LimeClient) -> LimeClient:
    """An engine whose render graph panel is available, with the log cleared for this test.

    Asserts a clean log afterwards, which is how an unnoticed error surfaces. A test that provokes an
    error on purpose clears the log itself before returning.
    """
    try:
        engine.call("rendergraph.info")
    except CommandError as error:
        pytest.skip(f"the render graph panel is not in this build: {error}")

    engine.clear_logs()
    yield engine

    errors = engine.errors()
    assert not errors, "the engine logged errors: " + "; ".join(f"{item['category']}: {item['message']}" for item in errors)


class TestPanelRegistration:
    def test_the_panel_is_registered_and_starts_hidden(self, rendergraph: LimeClient) -> None:
        """Hidden by default because it inspects the pipeline rather than the scene.

        Registration is worth asserting on its own: a panel living in a static library is only linked in
        because EditorLayer names its type, and losing that is a silent failure.
        """
        panels = rendergraph.call("panel.list")["panels"]
        entry = next((item for item in panels if item["name"] == "Render Graph"), None)

        assert entry is not None, f"Render Graph missing from {[item['name'] for item in panels]}"
        assert entry["visible"] is False
        assert entry["dockSlot"] == "Center"

    def test_showing_the_panel_keeps_the_engine_running(self, rendergraph: LimeClient) -> None:
        """The canvas draws on the frame it becomes visible, which is where a bad id or a null context
        would take the process down."""
        rendergraph.call("panel.show", panel="Render Graph", visible=True)

        first = rendergraph.ping()["frame"]
        rendergraph.wait_frames(5)
        assert rendergraph.ping()["frame"] > first

        rendergraph.call("panel.show", panel="Render Graph", visible=False)


class TestLoading:
    def test_the_example_graph_loads_as_described(self, rendergraph: LimeClient) -> None:
        """The shipped example and the built-in pass types are edited independently, so this also catches
        the two drifting apart."""
        info = rendergraph.call("rendergraph.load", path=EXAMPLE)

        assert info["passCount"] == 6
        assert info["edgeCount"] == 5
        assert info["errorCount"] == 0
        assert info["valid"] is True
        assert info["graphOutputs"] == ["ToneMap.ldr"]

        names = {pass_entry["name"] for pass_entry in info["passes"]}
        assert names == {"ShadowCaster", "DepthPrepass", "ForwardLit", "Bloom", "ToneMap", "Present"}

    def test_every_edge_names_a_resource_on_both_ends(self, rendergraph: LimeClient) -> None:
        """An edge runs from an output to an input, so both ends carry a resource. An endpoint naming only
        a pass is malformed and the loader is expected to have refused it."""
        info = rendergraph.call("rendergraph.load", path=EXAMPLE)

        for edge in info["edges"]:
            assert "." in edge["from"], f"{edge} has no resource on its producing end"
            assert "." in edge["to"], f"{edge} has no resource on its consuming end"

        # The shape the file describes, so a reordering of the edges cannot pass unnoticed.
        assert {(edge["from"], edge["to"]) for edge in info["edges"]} == {
            ("ShadowCaster.depth", "ForwardLit.shadowDepth"),
            ("DepthPrepass.depth", "ForwardLit.sceneDepth"),
            ("ForwardLit.color", "Bloom.input"),
            ("Bloom.output", "ToneMap.hdr"),
            ("ToneMap.ldr", "Present.image"),
        }

    def test_info_reports_the_graph_without_loading(self, rendergraph: LimeClient) -> None:
        """Reading state must not be a mutation, so a script can poll it."""
        rendergraph.call("rendergraph.load", path=EXAMPLE)
        first = rendergraph.call("rendergraph.info")
        second = rendergraph.call("rendergraph.info")

        assert first["passes"] == second["passes"]
        assert first["edges"] == second["edges"]

    def test_a_missing_file_fails_without_disturbing_the_loaded_graph(self, rendergraph: LimeClient) -> None:
        """A mistyped path should not replace a working graph with an empty one."""
        rendergraph.call("rendergraph.load", path=EXAMPLE)

        with pytest.raises(CommandError):
            rendergraph.call("rendergraph.load", path="NoSuchGraph.json")

        assert rendergraph.call("rendergraph.info")["passCount"] == 6

        # The engine logs an error for the failed load, which is correct: a load that did not happen is
        # worth recording. Cleared here so the fixture's clean log check still means something for the
        # tests that do not provoke one.
        rendergraph.clear_logs()


class TestSaving:
    def test_a_saved_graph_loads_back_the_same(self, rendergraph: LimeClient) -> None:
        """The round trip is the whole point: the file is the only record, so anything lost on save is
        lost for good."""
        original = rendergraph.call("rendergraph.load", path=EXAMPLE)
        rendergraph.call("rendergraph.save", path="RoundTrip.json")

        # Saved into the writable directory, so this reads it back from there rather than from Content.
        saved_path = rendergraph.call("rendergraph.save", path="RoundTrip.json")["path"]
        reloaded = rendergraph.call("rendergraph.load", path=saved_path)

        assert reloaded["passes"] == original["passes"]
        assert reloaded["edges"] == original["edges"]
        assert reloaded["graphOutputs"] == original["graphOutputs"]
        assert reloaded["errorCount"] == 0
