# LimeEngine automation

Python client library for driving a running engine.

The engine serves JSON over HTTP on the loopback interface. This package wraps that protocol, manages
engine processes, and runs the automation scripts a project ships. It serves two consumers: the
interactive shell, and the scripts a project keeps under `Projects/<Name>/Automation/`.

## Layout

```
Programs/lime_automation/
  client.py      LimeClient: the HTTP protocol and one method per command
  session.py     LimeSession: launches an engine and shuts it down
  discovery.py   Finds engines that are already running
  runner.py      Loads and executes a project's automation scripts
  paths.py       Locates the repository, project outputs and executables
  cli.py         python -m lime_automation
  shell.ps1      One click entry point into the interactive shell

Tests/Python/    pytest suites, registered with ctest
```

The package sits under `Programs/` rather than beside the tests because it is a tool in its own
right: the suites are one consumer of it, not its owner.

## Requirements

The client uses only the standard library, so it runs from a checkout with no install step. The test
suite needs pytest:

```powershell
python -m pip install -r Programs/lime_automation/requirements.txt
```

## The interactive shell

```powershell
./Programs/lime_automation/shell.ps1              # attach to a running engine, or launch one
./Programs/lime_automation/shell.ps1 -Port 5613   # a specific engine
./Programs/lime_automation/shell.ps1 -Launch      # always start a new one
```

`engine` is a connected client:

```python
>>> engine.engine_info()
>>> engine.list_ui_tests()
>>> engine.run_ui_tests(filter="HelloTriangle")
```

## Finding an engine

Two mechanisms, tried in order:

1. **Endpoint files.** The engine writes one per process under
   `Projects/<Name>/Binaries/<Config>/Saved/Automation/<pid>.json`. This is the fast path, and the
   only one that knows which process a port belongs to.
2. **Port scan.** When no file points at a live engine, a range starting at 5613 is handshaked. This
   reaches an engine started from a build tree this checkout knows nothing about.

Both confirm a candidate with `GET /`, so a file left behind by a killed process, or an unrelated
service holding the port, is never mistaken for an engine. Endpoint files accumulate when a process
is killed rather than closed, so the probes run concurrently; sequentially they made the shell appear
to hang for tens of seconds.

```python
from lime_automation import find_engine, handshake, scan_ports

find_engine()                 # newest running engine
find_engine(port=5613)        # a specific one, skipping discovery
handshake(5613)               # None when nothing answers
scan_ports(start=5613, count=16)
```

## Running the tests

From the repository root:

```powershell
./Scripts/Automation.ps1 test                          # pytest suite, D3D12
./Scripts/Automation.ps1 test -Backend d3d12,vulkan    # both backends
./Scripts/Automation.ps1 list                          # a project's scripts
./Scripts/Automation.ps1 run triangle                  # launch an engine and run one script
./Scripts/Automation.ps1 run triangle -Attach          # use a running engine
./Scripts/Automation.ps1 info -Port 5613               # describe one engine
```

Or directly from `Tests/Python`:

```powershell
python -m pytest -q --backend vulkan
python -m pytest -q -k screenshot          # one area
python -m pytest -q -m "not slow"          # skip tests that launch a second engine
```

Through ctest, which is what a full run uses:

```powershell
ctest --test-dir Build/ninja -C Debug -L automation
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
| `PYTHONPATH` | Must include `Programs/` unless the package is installed. Both wrapper scripts set it. |
| `LIME_ROOT` | Repository root, when it cannot be found by walking upwards |
| `LIME_BUILD_DIR` | Old shared CMake binary directory, used only by the legacy executable fallback |

Project executables are resolved from `Projects/<Name>/Binaries/<Config>/`, so neither of the last two
is needed for a normal build.
