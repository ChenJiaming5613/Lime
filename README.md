# LimeEngine

A layered Windows x64 rendering engine skeleton built on NVRHI, with both Direct3D 12 and Vulkan
backends behind a single HLSL shader source tree.

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
./Build/ninja/Bin/Debug/HelloTriangle.exe
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

## Writing a project

A project owns render passes, editor panels and shaders. It never defines `main`, never lists files
in CMake and never calls into the engine to register anything.

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
  "editor": { "enabled": true, "persistPassSettings": false },
  "automation": { "enabled": true, "port": 8787 }
}
```

CMake reads the same file at configure time, so the project name cannot drift between the build and
the runtime.

The editor can edit this file: `Window > Project Settings` opens a panel that writes back to the copy
in the project source tree, and refreshes the copy beside the executable so a restart picks the values
up without rebuilding. Saving is a load-modify-save, so comments, key order and any keys the engine
does not recognise are preserved.

Most of these values are read once at startup, so the panel edits the configuration for the *next*
launch and marks those fields `(restart)`. The backend, buffer count and validation mode are fixed
when the device is created, and turning the editor off would remove the UI needed to turn it back on.
The window title is the exception and applies immediately. `name` is intentionally not editable,
because CMake derives the target and the shader output directory from it.

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

Coverage focuses on logic that can be verified without a GPU: the math library, the log ring buffer,
the shader name mapping that has to match what ShaderMake writes to disk, render pass ordering and
stage assignment, viewport resize decisions, JSON reading, writing and key order preservation, the
project settings value round trip, the reflection layer, and the automation reflection bridge.

Rendering itself is verified by capturing the window, since the output is what matters:

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

A running engine exposes its state on a loopback socket, which lets an external script drive it and
inspect the result. Debug builds enable it by default; `LIME_BUILD_AUTOMATION=OFF` removes the module
outright.

```powershell
python Scripts/automation_smoke.py                     # D3D12
python Scripts/automation_smoke.py --backend vulkan
python Scripts/automation_smoke.py --backend d3d12 --backend vulkan
```

`Scripts/lime_automation.py` is the client library. `LimeSession` launches the executable, waits for
it to answer and shuts it down again:

```python
from lime_automation import LimeSession

with LimeSession(backend="vulkan", width=1280, height=720) as engine:
    print(engine.engine_info()["adapter"])

    # Reflected pass settings are readable and writable by name.
    engine.set_pass_values("Triangle", {"bPaused": True, "Tint": [0.2, 1.0, 0.35, 1.0]})
    engine.show_panel("Console", visible=False)

    engine.wait_frames(2)
    engine.screenshot("triangle.png")               # whole window
    engine.screenshot("scene.png", source="viewport")  # scene without editor chrome
```

The protocol is newline delimited JSON, one request object per line, so any language can speak it:

```
-> {"id": 1, "command": "pass.set", "params": {"pass": "Triangle", "values": {"RotationSpeed": 0.0}}}
<- {"id": 1, "ok": true, "result": {...}}
```

| Command | Purpose |
| --- | --- |
| `help`, `ping` | Command list; readiness probe returning the current frame |
| `engine.info`, `engine.stats`, `engine.quit` | Backend and adapter, frame timing, graceful exit |
| `log.tail`, `log.clear` | Read the in-memory log, filtered by level and category |
| `pass.list`, `pass.describe`, `pass.get`, `pass.set` | Enumerate passes and read or write their reflected settings |
| `panel.list`, `panel.show`, `panel.resetLayout` | Panel visibility and dock layout |
| `settings.get`, `settings.set`, `settings.save` | Project settings, including writing `ProjectSettings.json` |
| `screenshot.capture` | PNG of the back buffer or the viewport target |

Two properties make this usable as a test harness. Commands run on the main thread between frames, so
a handler sees consistent state and a change is visible in the frame that same tick produces.
Screenshots read back the GPU texture instead of grabbing the screen, so they are unaffected by
window occlusion, z-order and display scaling, and work while the window is in the background.

Reflection is what keeps this generic: a pass that declares `LIME_REFLECT` becomes scriptable with no
automation code of its own, and `pass.describe` reports each field's type, range and tooltip so a
script can discover valid values rather than hardcode them. Writes to unknown, read-only or mistyped
fields are rejected with a message naming the field.

Passing `port=0` lets the OS assign a port, which is how several sessions run at once; the chosen
value is written to `AutomationPort.txt` next to the executable. The listener binds `127.0.0.1` only,
since the commands expose full engine state.

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
Tests/          Catch2 unit tests
ThirdParty/     Submodules
Scripts/        Submodule setup, build, formatting, static analysis, capture, Python automation
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

Engine shaders land in `Shaders/` next to the executable; a project's land in
`Shaders/<ProjectName>/`. The engine registers the project root with higher precedence, so a project
can override a built-in shader by using the same relative path. Project shaders can still
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
