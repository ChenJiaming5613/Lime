"""Screenshots, which are how a rendering change is actually verified.

Captures come back as PNG bytes over HTTP, so these tests also cover the property that makes them
trustworthy: the image reflects the frame that was rendered after the last command, and is unaffected
by anything happening on the host's screen.
"""

from __future__ import annotations

import struct
from pathlib import Path

import pytest

from lime_automation import CommandError, LimeClient

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_png_size(data: bytes) -> tuple[int, int]:
    """Parses width and height from the IHDR chunk, without an image library."""
    assert data.startswith(PNG_SIGNATURE), "not a PNG"
    # 8 byte signature, 4 byte length, 4 byte "IHDR", then two big endian uint32.
    width, height = struct.unpack(">II", data[16:24])
    return width, height


def test_backbuffer_capture_is_a_valid_png(engine: LimeClient) -> None:
    engine.wait_frames(2)
    image = engine.screenshot()

    assert image.startswith(PNG_SIGNATURE)
    width, height = read_png_size(image)

    info = engine.engine_info()
    assert width == info["backBufferWidth"]
    assert height == info["backBufferHeight"]


def test_viewport_capture_matches_the_viewport_size(engine: LimeClient) -> None:
    info = engine.engine_info()
    if not info.get("offscreenRendering"):
        pytest.skip("the viewport target only exists with the editor active")

    engine.wait_frames(2)
    image = engine.screenshot(source="viewport")
    width, height = read_png_size(image)

    assert width == info["viewportWidth"]
    assert height == info["viewportHeight"]


def test_capture_contains_rendered_content(engine: LimeClient) -> None:
    """A blank frame compresses far smaller than one with content, which is enough of a signal."""
    engine.wait_frames(2)
    image = engine.screenshot()

    assert len(image) > 5000, f"the capture is only {len(image)} bytes, which suggests an empty frame"


def test_paused_scene_captures_are_identical(engine: LimeClient, triangle_pass: str) -> None:
    """Determinism is what makes byte comparison usable as an assertion at all."""
    engine.set_pass_values(triangle_pass, {"bPaused": True, "RotationSpeed": 0.0})
    engine.wait_frames(3)

    first = engine.screenshot(source="viewport")
    engine.wait_frames(3)
    second = engine.screenshot(source="viewport")

    assert first == second, "a paused scene produced different images"


def test_setting_a_value_changes_the_render(engine: LimeClient, triangle_pass: str) -> None:
    """The point of the whole facility: a scripted change has to be visible in the output."""
    engine.set_pass_values(triangle_pass, {"bPaused": True, "RotationSpeed": 0.0, "Tint": [1.0, 1.0, 1.0, 1.0]})
    engine.wait_frames(3)
    white = engine.screenshot(source="viewport")

    engine.set_pass_values(triangle_pass, {"Tint": [0.0, 1.0, 0.0, 1.0]})
    engine.wait_frames(3)
    green = engine.screenshot(source="viewport")

    assert white != green, "changing Tint did not change the rendered image"


def test_save_screenshot_writes_locally(engine: LimeClient, artifacts: Path) -> None:
    engine.wait_frames(2)
    destination = engine.save_screenshot(artifacts / "window.png")

    assert destination.exists()
    assert destination.read_bytes().startswith(PNG_SIGNATURE)


def test_unknown_source_is_rejected(engine: LimeClient) -> None:
    with pytest.raises(CommandError) as error:
        engine.screenshot(source="hologram")

    assert "Unknown source" in str(error.value)


def test_engine_side_capture_still_works(engine: LimeClient) -> None:
    """The file writing path is kept for cases where the image is wanted on the engine's machine."""
    path = engine.screenshot_to_engine_disk("pytest-engine-side.png")

    assert path.endswith(".png")
    assert "Screenshots" in path
