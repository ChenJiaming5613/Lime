"""The glTF scene: loading, the hierarchy it produces and the camera that views it.

Runs against GltfViewer rather than the default project, because that is the one configured with a
scene, and it is also the project that proves a settings file alone is enough to render something.

Every test skips rather than fails when the sample assets are absent: fetching them is a developer's
choice made with Scripts/FetchSampleAssets.ps1, and a fresh checkout must still get a green run.
"""

from __future__ import annotations

import math
import struct
from pathlib import Path

import pytest

from lime_automation import CommandError, LimeClient, LimeSession
from lime_automation.errors import LaunchError

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

# Matches the conftest values, so a capture does not depend on the host's display size.
TEST_WIDTH = 1024
TEST_HEIGHT = 576


@pytest.fixture(scope="module")
def scene_session(backend: str, config: str, preset: str):
    """One GltfViewer process per backend, shared by the module.

    Deliberately not the `session` fixture: that one honours --project, and these tests need the
    project whose settings file names a scene.
    """
    launcher = LimeSession(
        project="GltfViewer",
        config=config,
        preset=preset,
        backend=backend,
        width=TEST_WIDTH,
        height=TEST_HEIGHT,
    )
    try:
        launcher.start()
    except LaunchError as error:
        pytest.skip(f"Cannot launch GltfViewer: {error}")

    yield launcher
    launcher.stop()


@pytest.fixture
def scene(scene_session) -> LimeClient:
    """A client whose scene is known to be loaded, with the log cleared for this test."""
    client = scene_session.client

    info = client.call("scene.info")
    if not info.get("loaded"):
        pytest.skip("the glTF sample assets are not present; run Scripts/FetchSampleAssets.ps1")

    client.clear_logs()
    yield client

    errors = client.errors()
    assert not errors, "the engine logged errors: " + "; ".join(f"{item['category']}: {item['message']}" for item in errors)


@pytest.fixture
def camera(scene: LimeClient):
    """Restores the camera afterwards, so a test that moves it cannot affect the next one.

    Yaw and pitch are recovered from the forward vector rather than read back directly: the camera
    reports its orientation as a direction, which is the form that matters to a viewer, and deriving the
    angles here keeps the command surface from having to expose both.
    """
    original = scene.call("scene.camera.get")
    forward = original["forward"]
    yaw = math.degrees(math.atan2(forward[0], forward[2]))
    pitch = math.degrees(-math.asin(max(-1.0, min(1.0, forward[1]))))

    yield scene

    scene.call("scene.camera.set", position=original["position"], yaw=yaw, pitch=pitch)


def read_png_size(data: bytes) -> tuple[int, int]:
    assert data.startswith(PNG_SIGNATURE), "not a PNG"
    width, height = struct.unpack(">II", data[16:24])
    return width, height


def flatten(node: dict) -> list[dict]:
    """Depth first list of a scene.list subtree, so a test can search it without recursing itself."""
    result = [node]
    for child in node.get("children", []):
        result.extend(flatten(child))
    return result


def all_nodes(client: LimeClient) -> list[dict]:
    listing = client.call("scene.list")
    nodes: list[dict] = []
    for root in listing["roots"]:
        nodes.extend(flatten(root))
    return nodes


class TestSceneLoading:
    def test_the_configured_scene_is_loaded(self, scene: LimeClient) -> None:
        info = scene.call("scene.info")

        assert info["loaded"]
        assert info["path"].lower().endswith((".gltf", ".glb"))

    def test_statistics_describe_real_geometry(self, scene: LimeClient) -> None:
        info = scene.call("scene.info")

        assert info["entities"] > 0
        assert info["meshEntities"] > 0
        # A model with no triangles would still report as loaded, so this is the assertion that proves
        # the importer produced usable geometry rather than an empty shell.
        assert info["triangles"] > 0

    def test_a_default_light_is_present(self, scene: LimeClient) -> None:
        """glTF carries no light of its own, so the scene builder has to supply one."""
        assert any(node.get("light") for node in all_nodes(scene))

    def test_loading_logged_no_errors(self, scene: LimeClient) -> None:
        """A path that resolved and parsed cleanly must leave the log free of errors."""
        assert not scene.errors()


class TestSceneHierarchy:
    def test_every_node_is_named(self, scene: LimeClient) -> None:
        """An unnamed node would leave a blank row in the hierarchy panel, unusable for navigation."""
        for node in all_nodes(scene):
            assert node.get("name"), f"node {node.get('id')} has no name"

    def test_mesh_nodes_report_triangles(self, scene: LimeClient) -> None:
        mesh_nodes = [node for node in all_nodes(scene) if "mesh" in node]

        assert mesh_nodes, "the scene has no mesh nodes"
        for node in mesh_nodes:
            assert node["triangles"] > 0

    def test_triangle_counts_agree_with_the_summary(self, scene: LimeClient) -> None:
        """Two independent paths compute this, so a mismatch means one of them is wrong."""
        listed = sum(node.get("triangles", 0) for node in all_nodes(scene))

        assert listed == scene.call("scene.info")["triangles"]

    def test_every_node_has_a_world_position(self, scene: LimeClient) -> None:
        """World matrices are rebuilt every frame, so none may be left unset."""
        for node in all_nodes(scene):
            position = node.get("worldPosition")
            assert position is not None and len(position) == 3


class TestSelection:
    def test_selecting_by_name_succeeds(self, scene: LimeClient) -> None:
        target = next(node for node in all_nodes(scene) if "mesh" in node)

        result = scene.call("scene.select", name=target["name"])

        assert result["selected"] == target["name"]
        assert result["id"] == target["id"]

    def test_clearing_the_selection_works(self, scene: LimeClient) -> None:
        scene.call("scene.select", name=all_nodes(scene)[0]["name"])
        result = scene.call("scene.select")

        assert result["selected"] is None

    def test_an_unknown_name_is_rejected(self, scene: LimeClient) -> None:
        with pytest.raises(CommandError) as error:
            scene.call("scene.select", name="no such entity")

        assert "No entity named" in str(error.value)


class TestCamera:
    def test_the_camera_frames_the_model(self, scene: LimeClient) -> None:
        """Framing is derived from the world bounds, so the model must be within the clip range."""
        state = scene.call("scene.camera.get")

        assert state["projection"] == "perspective"
        assert state["farPlane"] > state["nearPlane"] > 0.0
        # The camera is placed back from the model, so it cannot be sitting at the origin.
        assert any(abs(value) > 1e-3 for value in state["position"])

    def test_setting_the_pose_moves_the_camera(self, camera: LimeClient) -> None:
        camera.call("scene.camera.set", position=[1.0, 2.0, -6.0], yaw=0.0, pitch=0.0)
        state = camera.call("scene.camera.get")

        assert state["position"] == pytest.approx([1.0, 2.0, -6.0], abs=1e-4)
        # Yaw and pitch of zero look straight down +Z in this left handed system.
        assert state["forward"] == pytest.approx([0.0, 0.0, 1.0], abs=1e-4)

    def test_a_partial_pose_leaves_the_rest_alone(self, camera: LimeClient) -> None:
        """Setting only the angles is what makes "turn to look at this" a one line call."""
        before = camera.call("scene.camera.get")["position"]
        camera.call("scene.camera.set", yaw=90.0)
        after = camera.call("scene.camera.get")

        assert after["position"] == pytest.approx(before, abs=1e-4)
        assert after["forward"] == pytest.approx([1.0, 0.0, 0.0], abs=1e-3)

    def test_pitch_is_clamped_short_of_vertical(self, camera: LimeClient) -> None:
        """At exactly 90 degrees the view rolls unpredictably, so the camera has to refuse to get there."""
        camera.call("scene.camera.set", yaw=0.0, pitch=200.0)
        forward = camera.call("scene.camera.get")["forward"]

        # Straight down would leave no horizontal component at all. The clamp is 89 degrees, so what
        # remains is small but must be strictly non zero: that is the difference between a defined view
        # basis and a degenerate one.
        horizontal = (forward[0] ** 2 + forward[2] ** 2) ** 0.5
        assert horizontal > 1e-4, "the camera reached vertical, where the view basis is undefined"
        assert forward[1] < -0.9, "a large positive pitch should still be looking downwards"

    def test_a_malformed_position_is_rejected(self, camera: LimeClient) -> None:
        with pytest.raises(CommandError) as error:
            camera.call("scene.camera.set", position=[1.0, 2.0])

        assert "three numbers" in str(error.value)


class TestSceneRendering:
    def test_the_scene_renders_content(self, camera: LimeClient) -> None:
        """A blank frame compresses far smaller than one with a lit model in it.

        Uses the camera fixture so the model is framed by the engine's own startup placement and restored
        afterwards; a test that had moved the camera away would otherwise leave this one looking at
        nothing.
        """
        camera.wait_frames(3)
        image = camera.screenshot(source="viewport")

        assert image.startswith(PNG_SIGNATURE)
        assert len(image) > 5000, f"the capture is only {len(image)} bytes, which suggests an empty frame"

    def test_a_still_camera_renders_identical_frames(self, camera: LimeClient) -> None:
        """Determinism is what makes byte comparison usable as an assertion at all."""
        camera.call("scene.camera.set", position=[0.0, 0.0, -4.0], yaw=0.0, pitch=0.0)
        camera.wait_frames(3)

        first = camera.screenshot(source="viewport")
        camera.wait_frames(3)
        second = camera.screenshot(source="viewport")

        assert first == second, "a still camera produced different images"

    def test_moving_the_camera_changes_the_image(self, camera: LimeClient) -> None:
        camera.call("scene.camera.set", position=[0.0, 0.0, -4.0], yaw=0.0, pitch=0.0)
        camera.wait_frames(3)
        front = camera.screenshot(source="viewport")

        camera.call("scene.camera.set", position=[0.0, 0.0, 4.0], yaw=180.0, pitch=0.0)
        camera.wait_frames(3)
        back = camera.screenshot(source="viewport")

        assert front != back, "moving the camera did not change the rendered image"

    def test_disabling_the_pass_changes_the_image(self, camera: LimeClient) -> None:
        """Confirms the built-in pass is what draws the scene, not something else."""
        # Five frames rather than two or three: a pass setting is applied on the next frame, and the
        # capture itself is deferred until one has been rendered, so a shorter wait can sample the frame
        # before the change landed and compare an image against itself.
        camera.wait_frames(5)
        lit = camera.screenshot(source="viewport")

        camera.set_pass_values("BlinnPhongForward", {"bEnabled": False})
        try:
            camera.wait_frames(5)
            unlit = camera.screenshot(source="viewport")
        finally:
            camera.set_pass_values("BlinnPhongForward", {"bEnabled": True})

        assert lit != unlit
        # The disabled pass draws nothing, so the frame is near empty and compresses far smaller.
        assert len(unlit) < len(lit)

    def test_lighting_settings_affect_the_image(self, camera: LimeClient) -> None:
        original = camera.get_pass_values("BlinnPhongForward")
        try:
            camera.set_pass_values("BlinnPhongForward", {"LightIntensity": 0.0, "AmbientStrength": 0.0})
            camera.wait_frames(5)
            dark = camera.screenshot(source="viewport")

            camera.set_pass_values("BlinnPhongForward", {"LightIntensity": 5.0, "AmbientStrength": 0.5})
            camera.wait_frames(5)
            bright = camera.screenshot(source="viewport")
        finally:
            camera.set_pass_values("BlinnPhongForward", original)

        assert dark != bright

    def test_the_capture_matches_the_viewport_size(self, scene: LimeClient) -> None:
        info = scene.engine_info()
        if not info.get("offscreenRendering"):
            pytest.skip("the viewport target only exists with the editor active")

        scene.wait_frames(2)
        width, height = read_png_size(scene.screenshot(source="viewport"))

        assert (width, height) == (info["viewportWidth"], info["viewportHeight"])


class TestCodeFreeProject:
    """GltfViewer is the proof that a project needs no C++ of its own.

    Asserted from the file system rather than from the engine, because the property being protected is a
    build time one: the moment someone adds a source file here, the "engine features only" path stops
    being exercised and could rot without anyone noticing.
    """

    PROJECT_DIR = Path(__file__).resolve().parents[3] / "Projects" / "GltfViewer"

    def test_the_project_has_no_sources(self) -> None:
        assert not (self.PROJECT_DIR / "Source").exists(), (
            "GltfViewer gained a Source directory, so the code-free path is no longer covered"
        )

    def test_the_project_has_no_shaders(self) -> None:
        """The pass that draws the scene is built in, so its shader comes from the engine too."""
        assert not (self.PROJECT_DIR / "Shaders").exists()

    def test_only_the_build_script_and_settings_are_authored(self) -> None:
        # Intermediate and Binaries are build outputs and are git ignored, so they do not count.
        authored = {
            entry.name
            for entry in self.PROJECT_DIR.iterdir()
            if entry.name not in ("Intermediate", "Binaries")
        }

        assert authored == {"CMakeLists.txt", "ProjectSettings.json"}, (
            f"unexpected files in GltfViewer: {sorted(authored)}"
        )

    def test_the_build_script_is_a_single_call(self) -> None:
        """A project's CMakeLists is one call with no arguments; everything else is derived."""
        lines = [
            line.strip()
            for line in (self.PROJECT_DIR / "CMakeLists.txt").read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.strip().startswith("#")
        ]

        assert lines == ["lime_add_project()"]

    def test_it_still_builds_and_runs(self, scene: LimeClient) -> None:
        """The whole point: no code, yet a window, a device and a rendered scene."""
        info = scene.engine_info()

        assert info["project"] == "GltfViewer"
        assert scene.call("scene.info")["triangles"] > 0

    def test_the_scene_is_drawn_by_a_builtin_pass(self, scene: LimeClient) -> None:
        """A project with no code cannot register a pass, so the engine has to provide one.

        Also confirms the explicit registration function works: this pass lives in a static library,
        where the linker is free to discard an object file whose only content is a self-registration.
        """
        names = [item["name"] for item in scene.list_passes()]

        assert "BlinnPhongForward" in names


class TestHierarchyPanel:
    def test_the_panel_is_registered(self, scene: LimeClient) -> None:
        names = [panel["name"] for panel in scene.call("panel.list")["panels"]]

        assert "Scene Hierarchy" in names

    def test_the_panel_is_docked_away_from_the_inspector(self, scene: LimeClient) -> None:
        """Panels sharing a slot become tabs, and only the selected tab can be hovered or clicked.

        Keeping these two apart is what lets UI automation reach both without activating a tab first.
        """
        panels = {panel["name"]: panel for panel in scene.call("panel.list")["panels"]}

        assert panels["Scene Hierarchy"]["dockSlot"] != panels["Inspector"]["dockSlot"]
