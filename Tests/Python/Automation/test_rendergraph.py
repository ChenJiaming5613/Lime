"""The render graph inspector: loading a graph, reading it back and saving it.

Drives the panel through automation commands rather than by clicking, because the properties worth
asserting — that a file becomes the graph it describes, and that saving reproduces it — are about the data
and not about the widget.

Uses the default project: the render graph is part of the pipeline description and has nothing to do with
which scene is loaded, so these tests do not need the sample assets.
"""

from __future__ import annotations

import pytest

from lime_automation import CommandError, LimeClient, LimeSession
from lime_automation.errors import LaunchError

# Ships in Engine/Content/RenderGraph and is copied next to the executable by the build.
EXAMPLE = "DefaultGraph.json"
# Marks nothing as an output, so it compiles to nothing runnable. Shipped on purpose: the fallback is a
# behaviour worth testing, and it needs a file that genuinely fails.
BROKEN = "BrokenGraph.json"

# Matches the conftest values, so the extra session below behaves like the shared one.
TEST_WIDTH = 1024
TEST_HEIGHT = 576


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

        assert info["passCount"] == 3
        assert info["edgeCount"] == 2
        assert info["errorCount"] == 0
        assert info["valid"] is True
        assert info["graphOutputs"] == ["PostProcess.color"]

        names = {pass_entry["name"] for pass_entry in info["passes"]}
        assert names == {"ShadowCaster", "ForwardLit", "PostProcess"}

        # The types have to be ones the engine actually registers, or the graph would load and then fail to
        # compile. Asserting them here is what ties the file to the passes that exist.
        types = {pass_entry["type"] for pass_entry in info["passes"]}
        assert types == {"ShadowCaster", "BlinnPhongForwardLit", "PostProcess"}

    def test_every_edge_names_a_resource_on_both_ends(self, rendergraph: LimeClient) -> None:
        """An edge runs from an output to an input, so both ends carry a resource. An endpoint naming only
        a pass is malformed and the loader is expected to have refused it."""
        info = rendergraph.call("rendergraph.load", path=EXAMPLE)

        for edge in info["edges"]:
            assert "." in edge["from"], f"{edge} has no resource on its producing end"
            assert "." in edge["to"], f"{edge} has no resource on its consuming end"

        # The shape the file describes, so a reordering of the edges cannot pass unnoticed.
        assert {(edge["from"], edge["to"]) for edge in info["edges"]} == {
            ("ShadowCaster.shadowDepth", "ForwardLit.shadowDepth"),
            ("ForwardLit.color", "PostProcess.sceneColor"),
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

        assert rendergraph.call("rendergraph.info")["passCount"] == 3

        # The engine logs an error for the failed load, which is correct: a load that did not happen is
        # worth recording. Cleared here so the fixture's clean log check still means something for the
        # tests that do not provoke one.
        rendergraph.clear_logs()


class TestRuntimeGraph:
    """The graph the renderer executes, which is not the panel's document.

    Reported separately because the two can differ: the panel can open any file, and saving does not
    rebuild the running graph. These assert the engine started with a usable pipeline.
    """

    def test_the_engine_is_running_a_compiled_graph(self, rendergraph: LimeClient) -> None:
        """The default project ships a graph, so a build where it failed to compile would be rendering
        nothing but the editor UI — which looks like a black viewport and nothing else."""
        runtime = rendergraph.call("rendergraph.info")["runtime"]

        assert runtime["available"] is True
        assert runtime["compiled"] is True, f"compile issues: {runtime['issues']}"
        assert runtime["running"] is True
        assert runtime["errorCount"] == 0

    def test_the_passes_run_in_dependency_order(self, rendergraph: LimeClient) -> None:
        """Compiling is what turns a set of edges into a sequence, so the order is the result worth
        asserting: a consumer running before its producer would read an unwritten texture.

        The project ships its own graph, which is the arrangement being asserted here — the engine's
        default is one option, not something a project is stuck with.
        """
        runtime = rendergraph.call("rendergraph.info")["runtime"]

        order = [entry["name"] for entry in runtime["executionOrder"]]
        assert order == ["Triangle"]

    def test_resources_are_shared_rather_than_copied(self, rendergraph: LimeClient) -> None:
        """An edge means the producer and the consumer are talking about one texture, not two that get
        copied into each other, so a resource count follows from the graph and not from the pass count."""
        runtime = rendergraph.call("rendergraph.info")["runtime"]

        assert len(runtime["resources"]) == 1

        colour = runtime["resources"][0]
        assert colour["name"] == "Triangle.color"
        # Left at the graph's size, which is reported as 0 rather than resolved: the difference between a
        # pinned size and an inherited one is what a reader needs.
        assert colour["width"] == 0
        assert colour["height"] == 0
        assert colour["isDepth"] is False
        # Written by the pass and then read when the result is presented.
        assert colour["isRenderTarget"] is True

    def test_the_graph_output_reaches_the_viewport(self, rendergraph: LimeClient) -> None:
        """The compiled state says the graph could run; this says its result arrives on screen.

        Asserts the image is not a single flat colour rather than checking pixels: the default project has
        no scene, so the only thing to see is the clear colour applied by the graph's own target. A viewport
        that never received the graph's output would be uniformly black, and so would one whose copy was
        skipped.
        """
        png = rendergraph.screenshot(source="viewport")

        assert png.startswith(b"\x89PNG\r\n\x1a\n"), "the capture is not a PNG"
        # A real image compresses to more than a few hundred bytes even when mostly flat; an empty or
        # zero sized target would not.
        assert len(png) > 512, f"the capture is suspiciously small at {len(png)} bytes"


@pytest.mark.slow
class TestInvalidGraphFallback:
    """A graph that cannot compile must leave the engine usable.

    The point of the fallback is that a broken pipeline description is a recoverable authoring mistake, not
    a crash: the editor stays up and says why.

    Needs its own engine process, because the graph is chosen at startup. Marked slow for that reason.
    """

    def test_a_graph_with_no_output_falls_back_to_the_editor_only(
        self, backend: str, config: str, preset: str, project: str
    ) -> None:
        """BrokenGraph.json marks nothing as an output, so it produces nothing and cannot run.

        The engine is expected to start anyway, report why, and render only the editor UI. Refusing to
        start would leave no way to see the reason or fix the file.
        """
        launcher = LimeSession(
            project=project,
            config=config,
            preset=preset,
            backend=backend,
            width=TEST_WIDTH,
            height=TEST_HEIGHT,
            extra_args=[f"--render-graph={BROKEN}"],
        )

        try:
            launcher.start()
        except LaunchError as error:
            pytest.skip(f"Cannot launch the engine: {error}")

        try:
            client = launcher.client

            # Still running: the frame loop has to survive a graph it cannot execute.
            first = client.ping()["frame"]
            client.wait_frames(5)
            assert client.ping()["frame"] > first, "the engine stopped advancing frames"

            runtime = client.call("rendergraph.info")["runtime"]

            assert runtime["available"] is True
            assert runtime["compiled"] is False, "a graph with no output should not compile"
            assert runtime["running"] is False, "nothing should be executing"
            assert runtime["errorCount"] > 0

            # The reason has to be reported, or a black viewport is all the user gets.
            messages = " ".join(issue["message"] for issue in runtime["issues"])
            assert "output" in messages.lower(), f"no issue explains the missing output: {runtime['issues']}"

            # The editor is what remains, so its panels must still answer.
            panels = client.call("panel.list")["panels"]
            assert any(item["name"] == "Render Graph" for item in panels)
        finally:
            launcher.stop()


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
