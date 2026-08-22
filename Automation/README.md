# LimeEngine automation

Python client library and test suite for driving a running engine.

The engine serves JSON over HTTP on the loopback interface. This package wraps that protocol, manages
engine processes, and runs the automation scripts a project ships.

## Layout

```
lime_automation/
  client.py      LimeClient: the HTTP protocol and one method per command
  session.py     LimeSession: launches an engine and shuts it down
  discovery.py   Attaches to engines that are already running
  runner.py      Loads and executes a project's automation scripts
  paths.py       Locates the repository, build outputs and executables
  cli.py         python -m lime_automation
tests/           pytest suite, registered with ctest
```

## Requirements

The client uses only the standard library, so it runs from a checkout with no install step. The test
suite needs pytest:

```powershell
python -m pip install -r requirements.txt
```

## Running

From the repository root, through the wrapper:

```powershell
./Scripts/Automation.ps1 test                          # pytest suite, D3D12
./Scripts/Automation.ps1 test -Backend d3d12,vulkan    # both backends
./Scripts/Automation.ps1 list                          # a project's scripts
./Scripts/Automation.ps1 run triangle                  # launch an engine and run one script
./Scripts/Automation.ps1 shell                         # interactive REPL
```

Or directly from this directory:

```powershell
python -m pytest -q --backend vulkan
python -m pytest -q -k screenshot          # one area
python -m pytest -q -m "not slow"          # skip tests that launch a second engine
python -m lime_automation run triangle --attach
```

Through ctest, which is what a full `ctest` run uses:

```powershell
ctest --test-dir ../Build/ninja -C Debug -L automation
```

## Writing a test

Tests take the `engine` fixture, which is a connected `LimeClient`. One engine process is shared per
module and backend, so a test must leave the engine as it found it; `triangle_pass` restores the pass
values it was given for exactly that reason. The `engine` fixture also fails a test that caused the
engine to log an error, even when every assertion passed.

```python
def test_tint_changes_the_render(engine, triangle_pass):
    engine.set_pass_values(triangle_pass, {"bPaused": True})
    engine.wait_frames(2)
    before = engine.screenshot(source="viewport")

    engine.set_pass_values(triangle_pass, {"Tint": [0.0, 1.0, 0.0, 1.0]})
    engine.wait_frames(2)
    assert engine.screenshot(source="viewport") != before
```

Comparing PNG bytes works because a paused scene renders identically each frame and captures are read
back from the GPU rather than grabbed from the screen.

## Writing a project script

Scripts live in `Projects/<Name>/Automation/` and expose one `run(engine)`. They are for scenarios
worth reproducing by hand, where the pytest suite is for regression coverage.

```python
"""One line description, shown by the list command."""

def run(engine) -> dict:
    assert engine.engine_info()["editorEnabled"]
    return {"artifacts": [engine.save_screenshot("shot.png")]}
```

## Environment variables

| Variable | Effect |
| --- | --- |
| `LIME_ROOT` | Repository root, when it cannot be found by walking upwards |
| `LIME_BUILD_DIR` | CMake binary directory holding the executables; set by the ctest integration |
