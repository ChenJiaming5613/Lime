# LimeEngine

A layered Windows x64 rendering engine skeleton built on NVRHI, with both Direct3D 12 and Vulkan
backends behind a single HLSL shader source tree.

Chinese task-oriented tutorials live in [Docs/zh](Docs/zh/README.md): creating a project, writing
render passes and editor panels, and using the automation facility. This file is the full reference.

## Requirements

| Tool | Version | Notes |
| --- | --- | --- |
| Visual Studio 2022 | 17.x with the C++ workload | MSVC toolset, C++20 |
| CMake | >= 3.24 | Ninja Multi-Config is the primary generator |
| Vulkan SDK | >= 1.3 | Required for the Vulkan backend and for a DXC that can emit SPIR-V |
| Windows SDK | >= 10.0.20348.0 | `DirectX-Headers` is vendored, so no 22621 requirement |

The DXC shipped with the Windows SDK cannot generate SPIR-V. `ShaderMake` is configured to use
`%VULKAN_SDK%\Bin\dxc.exe`, which handles both DXIL and SPIR-V.

## Getting started

The build needs the MSVC environment on PATH. `Scripts/Build.ps1` loads it automatically and also
works around Visual Studio installs whose default toolset version file points at a toolset that is
not actually installed.

```powershell
git clone --recurse-submodules <repo-url> LimeEngine
cd LimeEngine
# If the clone was not recursive:
./Scripts/SetupSubmodules.ps1

./Scripts/Build.ps1                       # configure + build Debug
./Scripts/Build.ps1 -Config Release
./Projects/HelloTriangle/Binaries/Debug/HelloTriangle.exe
```

Inside an x64 Native Tools prompt the presets can be used directly:

```powershell
cmake --preset ninja
cmake --build --preset ninja-debug
```

For Visual Studio debugging use the `vs2022` preset instead.

## Running HelloTriangle

Configuration comes from `Projects/HelloTriangle/ProjectSettings.json`. The command line overrides
it, so switching backends never means editing the file.

```powershell
HelloTriangle.exe                    # backend from ProjectSettings.json
HelloTriangle.exe --rhi=vulkan
HelloTriangle.exe --rhi=d3d12
HelloTriangle.exe --no-editor        # runtime only, no editor UI
HelloTriangle.exe --width=1280 --height=720
HelloTriangle.exe --no-vsync
HelloTriangle.exe --validation=off   # also: debugOnly (default), on
HelloTriangle.exe --project=<path to a ProjectSettings.json>
HelloTriangle.exe --automation-port=0   # automation on, OS assigned port; also --no-automation
```

A rotating triangle is drawn with the editor docked on top: the scene lives in the `Viewport` panel,
`Console` at the bottom, `Stats` and `Inspector` on the right, and the project's own `Triangle` panel
on the left. The layout is saved to `Saved/EditorLayout.ini` next to the executable; delete it or use
`Window > Reset layout` to restore the default arrangement.

With `--no-editor` the scene renders straight into the swap chain and no offscreen target is created,
so the runtime path stays free of editor cost.

## Build output layout

A project is self contained: everything built from its sources stays inside its own directory, which
is also its working directory at run time.

```
Projects/HelloTriangle/
  Source/ Shaders/ Content/ Automation/   Authored, tracked in git
  ProjectSettings.json
  Binaries/<Config>/                      Everything this project needs to run
    HelloTriangle.exe
    ProjectSettings.json
    EditorSettings.json
    Shaders/Engine/         Engine shaders, copied from Staging
    Shaders/HelloTriangle/  Project shaders, compiled here directly
    Content/
    Saved/                  Logs, layout, screenshots, automation endpoints
  Intermediate/                           Never shipped, safe to delete
    Build/                  CMakeFiles, object files, per config Ninja fragments
    Lib/<Config>/           Import libraries and the compiler pdb

Build/<preset>/                           Engine level outputs only
  Bin/<Config>/             LimeTests.exe, ShaderMake.exe
  Staging/<Config>/Shaders/Engine/  Engine shaders, built once and shared
  Lib/<Config>/             Engine static libraries
```

Both `Binaries/` and `Intermediate/` are git ignored, so a project directory can be copied or moved
without carrying build state along.

The engine resolves `Shaders`, `Content`, `Saved` and `ProjectSettings.json` relative to the
executable, so the per project directory is what keeps two projects from interfering: sharing one
would let the later build overwrite the earlier project's settings file, which then sends it looking
for shaders under the wrong name and its passes fail to initialize.

Engine shaders are compiled once into `Staging` and copied in, so adding a project does not multiply
the shader build cost.

## Writing a project

A project owns render passes, editor panels, shaders and automation scripts. It never defines `main`,
never lists files in CMake and never calls into the engine to register anything.

```
Projects/<Name>/
  CMakeLists.txt        One call to lime_add_project()
  ProjectSettings.json  Name, window, RHI and automation configuration
  EditorSettings.json   Editor appearance. Optional, defaults apply when absent
  Source/               Render passes and editor panels, globbed by CMake
  Shaders/              HLSL plus a ShaderMake .cfg
  Automation/           Python test scripts, discovered by name
  Content/              Optional assets, copied next to the executable
  Binaries/             Build output, generated and git ignored
  Intermediate/         Build state, generated and git ignored
```

`CMakeLists.txt` is one line:

```cmake
lime_add_project()
```

`ProjectSettings.json` supplies the name, window and RHI configuration:

```json
{
  "version": 1,
  "name": "HelloTriangle",
  "window": { "title": "LimeEngine - HelloTriangle", "width": 1600, "height": 900 },
  "rhi": { "backend": "d3d12", "vsync": true, "backBufferCount": 3, "validation": "debugOnly" },
  "editor": { "enabled": true },
  "automation": { "enabled": true, "port": 5613 }
}
```

CMake reads the same file at configure time, so the project name cannot drift between the build and
the runtime.

The editor can edit this file: `Window > Project Settings` opens a panel that writes back to the copy
in the project source tree, and refreshes the copy beside the executable so a restart picks the values
up without rebuilding. Saving is a load-modify-save, so comments, key order and any keys the engine
does not recognise are preserved.

**Nothing here is applied to a running session.** Every setting is consumed once during startup, when
the window and device are created, so the panel is a JSON editor for the *next* launch and states that
once at the top rather than marking individual fields. Keeping the running configuration immutable is
what lets anything reading it — the panel, `settings.get`, the Stats panel — report what the engine is
actually using rather than a value that was requested but could not take effect.

`name` is intentionally not editable, because CMake derives the target and the shader output directory
from it.

`EditorSettings.json` sits beside it and is optional, since the built-in defaults are a complete
configuration:

```json
{
  "version": 1,
  "appearance": {
    "fontSize": 16,
    "theme": "dark",
    "accentColor": [0.56, 0.83, 0.35]
  }
}
```

`fontSize` is clamped to 8..48, `theme` is `dark`, `light` or `classic`, and `accentColor` tints check
marks, slider grabs and the selected tab. It is a separate file because nothing outside the editor
reads it: a build with `LIME_BUILD_EDITOR` off would otherwise carry configuration it can never apply.
For the same reason `FEditorSettings` lives in `Engine/Source/Editor` rather than beside
`FProjectSettings`.

`Window > Editor Settings` edits it and follows the same restart rule. The font size has to: it is
baked into the atlas at startup, which is also why the size is applied through `FontSizeBase` rather
than by scaling an atlas built at another size. Holding the colours to the same rule keeps what is on
screen consistent with the file it was loaded from.

### A render pass

Declare the tunables once and the editor generates the controls from them:

```cpp
struct FTriangleSettings
{
    bool bPaused = false;
    float RotationSpeed = 1.0f;
};

LIME_REFLECT(HelloTriangle::FTriangleSettings)
{
    LIME_PROPERTY(bPaused,       Lime::FProp("Pause rotation"));
    LIME_PROPERTY(RotationSpeed, Lime::FProp("Speed").Range(-6.0f, 6.0f));
}

class FTrianglePass final : public Lime::TRenderPass<FTrianglePass>
{
public:
    static constexpr Lime::ERenderPassPriority Priority = Lime::ERenderPassPriority::Scene;

    const char* GetName() const override { return "Triangle"; }
    bool Initialize(Lime::FRenderer& Renderer) override;
    void Shutdown() override;
    // Runs before the targets are cleared, for renderer wide state such as the clear colour.
    void OnBeginFrame(Lime::FRenderer& Renderer, const Lime::FFrameContext& Context) override;
    void Render(const Lime::FFrameContext& Context) override;

    // Opting in makes the settings appear in the generic Inspector panel.
    Lime::FReflectedRef GetReflectedSettings() override { return Lime::MakeReflectedRef(Settings); }

private:
    FTriangleSettings Settings;
};
```

```cpp
// Last line of TrianglePass.cpp
LIME_REGISTER_RENDER_PASS(HelloTriangle::FTrianglePass);
```

Passes are instantiated by the engine once the device exists. `Priority` decides two things: the draw
order, and which render stage the pass belongs to (see Render stages below). A pass renders against
whatever `FFrameContext` hands it, so it does not need to know whether the editor is active.

### An editor panel

Only needed for UI that reflection cannot express; simple tunables already show up in `Inspector`.

```cpp
class FTrianglePanel final : public Lime::IEditorPanel
{
public:
    const char* GetName() const override { return "Triangle"; }
    Lime::EEditorDockSlot GetDefaultDockSlot() const override { return Lime::EEditorDockSlot::Left; }
    void OnDrawUI(const Lime::FEditorContext& Context) override;
};
```

```cpp
// Last line of TrianglePanel.cpp
LIME_REGISTER_EDITOR_PANEL(HelloTriangle::FTrianglePanel);
```

A panel reaches a pass by type, so neither has to know when the other is created:

```cpp
FTrianglePass* Pass = Context.FindPass<FTrianglePass>();
if (Pass == nullptr) { /* degrade gracefully */ }
```

The declared dock slot drives the default layout and the `Window` menu entry, so a new panel needs
no engine change. A project panel whose name matches a built-in one replaces it.

### Optional lifetime hooks

Rarely needed; passes normally update themselves.

```cpp
class FMyApp final : public Lime::ILimeApplication
{
    bool OnStartup(Lime::FEngine& Engine) override;
    void OnUpdate(float DeltaSeconds) override;
    void OnShutdown() override;
};

LIME_IMPLEMENT_APPLICATION(FMyApp)
```

Self registration depends on static initializers running, which is why `lime_add_project` compiles
project sources, and the engine's `LaunchMain.cpp`, straight into the executable. Moving them into a
static library would let the linker discard the object files whose only content is a registration.

## Tests

```powershell
ctest --test-dir Build/ninja -C Debug --output-on-failure
```

Two suites. Catch2 covers logic that can be verified without a GPU: the math library, the log ring
buffer, the shader name mapping that has to match what ShaderMake writes to disk, render pass ordering
and stage assignment, viewport resize decisions, JSON reading, writing and key order preservation, the
project settings value round trip, the reflection layer, the automation reflection bridge and
automation script discovery.

Everything that needs a device is covered by the Python suite instead, which drives a real engine over
the automation server; see Automation below. Both are registered with ctest, so the command above runs
them together.

Rendering can also be checked by hand, since the output is what matters:

```powershell
./Scripts/Capture.ps1                                  # Debug, D3D12, editor enabled
./Scripts/Capture.ps1 -Backend vulkan
./Scripts/Capture.ps1 -NoEditor                        # scene straight to the swap chain
./Scripts/Capture.ps1 -Config Release -KeepLayout
```

Images and the captured log land in `Build/Screenshots`. The script resets the saved dock layout by
default, so newly added panels are actually visible, and reports any warnings or errors the run
logged.

## Automation

A running engine serves its state over HTTP on the loopback interface, which lets an external script
drive it and inspect the result. Debug builds enable it by default; `LIME_BUILD_AUTOMATION=OFF` removes
the module outright.

`Programs/lime_automation/` holds a standalone Python package. The client needs only the standard
library, so it runs from a checkout with no install step; the pytest suite, in `Tests/Python/`, needs
`Programs/lime_automation/requirements.txt`.

```powershell
./Scripts/Automation.ps1 test                          # pytest suite, D3D12
./Scripts/Automation.ps1 test -Backend d3d12,vulkan    # both backends
./Scripts/Automation.ps1 list                          # a project's scripts
./Scripts/Automation.ps1 run triangle                  # launch an engine and run one script
./Scripts/Automation.ps1 run triangle -Attach          # use a running engine instead
./Scripts/Automation.ps1 info -Port 5613               # describe one engine

./Programs/lime_automation/shell.ps1                   # interactive REPL, attaching if it can
./Programs/lime_automation/shell.ps1 -Port 5613        # or a specific engine
```

A client finds an engine through the endpoint file it publishes under `Saved/Automation/`, falling
back to handshaking a port range when no file points at a live one. Both confirm the candidate with
`GET /`, so a file left behind by a killed process is never mistaken for a running engine.

The suite is registered with ctest, one test per backend, and skipped with a status line when pytest
is absent:

```powershell
ctest --test-dir Build/ninja -C Debug -L automation
```

### Writing a script

A project keeps its scripts in `Automation/` beside `Source/` and `Shaders/`. Each file exposes one
`run(engine)`; there is nothing to register. A raised `AssertionError` fails the script, and returning
`{"artifacts": [...]}` reports the files it wrote.

```python
"""Verifies the triangle renders and its settings take effect."""

def run(engine) -> dict:
    triangle = engine.find_pass()
    engine.set_pass_values(triangle, {"bPaused": True, "RotationSpeed": 0.0})

    engine.wait_frames(2)
    before = engine.screenshot(source="viewport")

    engine.set_pass_values(triangle, {"Tint": [0.2, 1.0, 0.35, 1.0]})
    engine.wait_frames(2)
    assert engine.screenshot(source="viewport") != before, "Tint did not change the image"

    return {"artifacts": [engine.save_screenshot("tinted.png", source="viewport")]}
```

The engine enumerates these files through `script.list` but never runs them: it embeds no interpreter,
so execution belongs to the launcher. That keeps a Python dependency out of the engine while still
letting one command reproduce a scenario.

### Using the library directly

```python
from lime_automation import LimeSession, find_engine

# Launch a dedicated engine and shut it down afterwards.
with LimeSession(backend="vulkan", width=1280, height=720) as engine:
    engine.set_pass_values("Triangle", {"bPaused": True})
    engine.save_screenshot("shot.png", source="viewport")

# Or attach to one that is already running, leaving its window open.
engine = find_engine().connect()
```

### Protocol

JSON over HTTP/1.1. Any language with an HTTP client can drive the engine:

```
POST /command          {"command": "pass.set", "params": {...}}  -> {"ok": true, "result": {...}}
POST /command/<name>   {...params...}                            -> same, name taken from the path
POST /batch            [{"command": ...}, ...]                   -> one reply per entry, in order
GET  /                                                           -> identity, protocol version, frame
GET  /commands                                                   -> the command catalogue
GET  /screenshot?source=viewport                                 -> image/png bytes
```

```powershell
curl -X POST http://127.0.0.1:5613/command/engine.info
curl -o shot.png "http://127.0.0.1:5613/screenshot?source=viewport"
```

A rejected command is a `200` with `ok=false`, because the request itself was well formed; malformed
bodies and unknown routes use `4xx`. Batches keep going past a failure, so one bad entry does not
discard the results around it.

| Command | Purpose |
| --- | --- |
| `help`, `ping` | Command list; readiness probe returning the current frame |
| `engine.info`, `engine.stats`, `engine.quit` | Backend and adapter, frame timing, graceful exit |
| `log.tail`, `log.clear` | Read the in-memory log, filtered by level and category |
| `pass.list`, `pass.describe`, `pass.get`, `pass.set` | Enumerate passes and read or write their reflected settings |
| `panel.list`, `panel.show`, `panel.resetLayout` | Panel visibility and dock layout |
| `settings.get`, `settings.set`, `settings.save` | Read the running configuration, edit the pending file, write it back |
| `screenshot.capture` | Writes a PNG on the engine's own machine |
| `script.list` | Automation scripts the running project ships |
| `uitest.list`, `uitest.run`, `uitest.status`, `uitest.abort` | Drive UI tests that click and drag real widgets |

### UI automation

`panel.show` flips a visibility flag. A UI test clicks the actual menu item and drags the actual
slider, which is what catches a menu entry wired to the wrong panel or a widget whose value never
reaches the code behind it. This is [Dear ImGui Test
Engine](https://github.com/ocornut/imgui_test_engine), which locates widgets by path and injects real
input events, so a test never hardcodes screen coordinates.

```python
status = engine.run_ui_tests()                       # every registered test
status = engine.run_ui_tests(filter="HelloTriangle") # or a subset
assert not engine.failed_ui_tests(status)
```

A run spans many frames, so `uitest.run` only queues the work and returns; the client polls
`uitest.status`. Waiting inside the engine would stop the very frame loop the tests need in order to
advance, which is a deadlock rather than a slow reply. `run_ui_tests` wraps the polling and takes its
own timeout, separate from the per command one.

A project registers tests from its own sources, with no engine change:

```cpp
namespace MyGame::UITests
{
    void TestDragSpeed(ImGuiTestContext* Ctx)
    {
        Ctx->SetRef("//LimeEditorDockHost");
        Ctx->MenuCheck("Window/Inspector");
        // Panels sharing a dock slot are tabs of one node, and only the selected tab can be
        // hovered, so the window has to be focused before its widgets can be driven.
        Ctx->WindowFocus("//Inspector");

        // The Inspector wraps each pass in PushID, so ** steps over the generated id.
        Ctx->ItemDragWithDelta("//Inspector/**/Speed", ImVec2(-30.0f, 0.0f));
        IM_CHECK_NE(Ctx->ItemReadAsFloat("//Inspector/**/Speed"), 1.0f);
    }
}

LIME_REGISTER_UI_TEST("MyGame", "drag_speed", &MyGame::UITests::TestDragSpeed);
```

The static macro works in a project because its sources are compiled into the executable. Engine
side tests cannot use it, for the same reason automation commands cannot: a static library lets the
linker drop an object file that only registers something.

`Window > UI Tests` opens the test engine's own window, where a test can be run by hand and watched.
Tests run at full speed by default; the window can slow them down to make each action visible.

Built with the editor and removed by `LIME_BUILD_IMGUI_TEST_ENGINE=OFF`, which also drops the
`uitest.*` commands from the catalogue.

> **Licensing.** The `imgui_test_engine/` directory is **not MIT**. It is free for individuals,
> education, open source and small companies; larger companies need a paid license. See
> `ThirdParty/ImGuiTestEngine/imgui_test_engine/LICENSE.txt` before shipping commercially. The rest
> of that repository is MIT.

### Why this is reliable

Commands run on the main thread between frames, so a handler sees consistent state and a change is
visible in the frame that same tick produces. Screenshots read back the GPU texture rather than
grabbing the screen, so they are unaffected by window occlusion, z-order and display scaling, and work
while the window is in the background. Together those two properties make byte comparison of captures
a usable assertion: pause a pass and two consecutive captures are identical.

Reflection keeps it generic. A pass that declares `LIME_REFLECT` becomes scriptable with no automation
code of its own, and `pass.describe` reports each field's type, range and tooltip so a script can
discover valid values instead of hardcoding them. Writes to unknown, read-only or mistyped fields are
rejected with a message naming the field.

Networking comes from cpp-httplib, so the module contains no platform specific socket code.

### Discovery and concurrency

`port=0`, the default for `LimeSession`, asks the OS for a free port, which is what lets several
engines run at once. Each publishes `Saved/Automation/<pid>.json` plus a shared
`AutomationEndpoint.json` inside its own project directory, so `find_engine()` can attach to a session
started by hand; `discover_engines()` walks every project directory and probes each candidate, so a
file left behind by a crash is filtered out. The listener binds `127.0.0.1` only, since the commands
expose full engine state.

Adding a command takes one registration, and a project can add its own the same way:

```cpp
LIME_REGISTER_AUTOMATION_COMMAND("scene.reset", "Returns the scene to its initial state",
                                 [](FAutomationInvocation& Invocation) { /* ... */ });
```

## Architecture

Strictly one-directional layering, one static library per layer:

```
LimeCore      Types, logging, assertions, math, JSON, reflection. No graphics dependencies.
LimePlatform  Window, input, timing, path resolution (GLFW).
LimeRHI       IDeviceManager plus the D3D12 and Vulkan implementations, shader loading (NVRHI).
LimeRenderer  Frame orchestration, the render pass registry, the viewport target, the ImGui backend.
LimeEditor    Editor layer, dock space, panel registry, built-in panels. Optional.
LimeRuntime   FEngine and project settings.
LimeLaunch    main(). Compiled into each executable so registrars are never discarded.
```

Projects depend only on the engine; the engine never references a project. Passes and panels travel
in the other direction through the two registries, which the engine drains at a defined point during
startup.

NVRHI does not provide device or swap chain management, so `IDeviceManager` is implemented per
backend. The ImGui renderer is written against NVRHI rather than using `imgui_impl_dx12` /
`imgui_impl_vulkan`, so both backends share one drawing path; only `imgui_impl_glfw` is reused for
platform input.

## Render stages

A frame runs in two stages, split at `ERenderPassPriority::EditorUI`:

| Stage | Passes | Target with editor | Target without editor |
| --- | --- | --- | --- |
| Scene | priority < `EditorUI` | `FViewportTarget` (offscreen) | back buffer |
| EditorUI | priority >= `EditorUI` | back buffer | back buffer |

The stage is named `EditorUI` rather than `UI` because in-world UI belongs in the scene image: an
in-game HUD or a world space widget should be composited into the viewport texture, so it renders at
`Overlay` and stays on the scene side. Only the editor chrome has to bypass the scene target.

The two stages are submitted as separate command lists, because the editor samples the texture the
scene stage wrote. `--no-editor` never creates the offscreen target at all, so the runtime path is
exactly what it was before the viewport existed.

The viewport panel only learns its size while the UI is being built, which is after the scene has
already been rendered. The requested size is therefore applied at the start of the next frame, so
dragging the panel stretches the image for one frame before the target matches again. When the target
is recreated, the ImGui binding set is rebuilt through `FRenderer::SetViewportResizedDelegate` while
the `ImTextureID` handed to the panel stays the same.

`IRenderPass::OnBeginFrame` exists for state that has to be set before the targets are cleared, such
as the clear colour. Doing that from `Render` would apply one frame late.

## Reflection

EnTT's meta system backs both the inspector UI and settings serialization, so a settings struct is
described exactly once. `LIME_REFLECT` / `LIME_PROPERTY` wrap the EnTT calls, which keeps project code
independent of the EnTT version.

Two constraints are baked into the wrapper:

- Fields are registered with `entt::as_ref_t`. The default as-value policy returns a copy, so widgets
  and the JSON loader would write into a temporary; nothing would fail to compile.
- `custom<FPropertyMeta>()` carries the display name, range and widget hint. It overwrites rather
  than accumulates, so the metadata is built in one shot.

EnTT is pinned to v3.16.0 because the meta entry point changed between releases (`entt::meta<T>()`
before 3.15, `entt::meta_factory<T>{}` from 3.15 on).

## Repository layout

```
CMake/          Build modules (compiler options, target helpers, shaders, projects)
Engine/
  Shaders/      Built-in HLSL sources plus the ShaderMake config
  Content/      Built-in assets such as textures
  Source/
    Runtime/    Core, Platform, RHI, Renderer, Automation, Engine, Launch
    Editor/     Editor layer, registry and built-in panels
Projects/       One directory per application, each with a ProjectSettings.json
Automation/     Python client library and the pytest suite
Docs/zh/        Chinese tutorials
Tests/          Catch2 unit tests
ThirdParty/     Submodules
Scripts/        Submodule setup, build, formatting, static analysis, capture, automation
```

Headers and sources live side by side; there is no separate `include/` tree.

## Shaders

Shader trees are described by ShaderMake configs, one line per shader permutation set:

```
Triangle/Triangle.hlsl -T vs -E MainVS
Triangle/Triangle.hlsl -T ps -E MainPS
Blur/Blur.hlsl         -T cs -E MainCS -D RADIUS={1,2,4}
```

Every entry is compiled twice, once into `DXIL/` and once into `SPIRV/`. ShaderMake handles
permutation expansion, include dependency tracking and incremental builds.

`Shaders/` holds one subdirectory per owner, and each of those holds the backend directories:

```
Shaders/Engine/DXIL/ImGui/ImGui_MainVS.dxil
Shaders/Engine/SPIRV/ImGui/ImGui_MainVS.spirv
Shaders/HelloTriangle/DXIL/Triangle/Triangle_MainVS.dxil
```

Engine shaders are compiled once into `Build/<preset>/Staging/<Config>/Shaders/Engine` and copied into
each project's directory; a project's own are compiled straight into `Shaders/<ProjectName>/`. Giving
the engine set its own subdirectory rather than the `Shaders` root keeps both owners symmetrical, so
they resolve through the same search root mechanism and a project named `Engine` cannot collide with
the built-in shaders. The engine registers the project root with higher precedence, so a project can
override a built-in shader by using the same relative path. Project shaders can still
`#include "Common.hlsli"` from the engine include directory.

Output names follow `<relative path>[_<Entry> when the entry is not "main"]`. A shader that declares
defines is packed into a blob and looked up at runtime through `ShaderMake::FindPermutationInBlob`;
a shader without defines is plain bytecode. `RHI/ShaderKey.h` mirrors that rule and is covered by
tests so the two sides cannot drift apart.

Two cross-backend details worth knowing:

- Vulkan register shifts are defined once in the root `CMakeLists.txt` (`LIME_VK_SHIFT_*`) and
  passed both to DXC and to `nvrhi::VulkanBindingOffsets`. They must stay in sync or descriptor
  bindings break silently.
- Constant buffer matrices are declared `row_major` in HLSL rather than relying on a global packing
  flag, because DXC's SPIR-V backend ignores `-Zpr`. NVRHI already flips the Vulkan viewport, so
  `-fvk-invert-y` must not be added.

## Code style

Unreal Engine conventions: `F` for classes and structs, `I` for interfaces, `E` for enums, `T` for
templates, PascalCase members and functions, `b` prefix for booleans. Enforced by `.clang-format`
and `.clang-tidy`.

```powershell
./Scripts/FormatCode.ps1
./Scripts/FormatCode.ps1 -Check
./Scripts/RunClangTidy.ps1
```

## Build options

| Option | Default | Description |
| --- | --- | --- |
| `LIME_BUILD_EDITOR` | `ON` | Build the editor module |
| `LIME_BUILD_TESTS` | `ON` | Build Catch2 tests |
| `LIME_BUILD_AUTOMATION` | `ON` | Build the automation server used by external scripts |
| `LIME_ENABLE_RHI_D3D12` | `ON` | Enable the Direct3D 12 backend |
| `LIME_ENABLE_RHI_VULKAN` | `ON` | Enable the Vulkan backend |

## Roadmap

- glTF scene loading through the vendored tinygltf
- Depth buffer and camera controls in the viewport
- Scene graph, material system and a render graph
