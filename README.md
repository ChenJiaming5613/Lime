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

```powershell
HelloTriangle.exe                    # default backend (D3D12)
HelloTriangle.exe --rhi=vulkan       # Vulkan backend
HelloTriangle.exe --rhi=d3d12
HelloTriangle.exe --no-editor        # runtime only, no editor UI
HelloTriangle.exe --width=1280 --height=720
HelloTriangle.exe --no-vsync
HelloTriangle.exe --no-validation    # skip the debug runtime and the nvrhi validation layer
```

A rotating triangle is drawn with the editor docked on top: `Console` at the bottom, `Inspector`
on the right, and a transparent central node so the scene stays visible. The layout is saved to
`Saved/EditorLayout.ini` next to the executable; delete it to restore the default arrangement.

## Tests

```powershell
ctest --test-dir Build/ninja -C Debug --output-on-failure
```

Coverage focuses on logic that can be verified without a GPU: the math library, the log ring
buffer, and the shader name mapping that has to stay in sync with what ShaderMake writes to disk.

## Architecture

Strictly one-directional layering, one static library per layer:

```
LimeCore      Types, logging, assertions, math. No graphics dependencies.
LimePlatform  Window, input, timing, path resolution (GLFW).
LimeRHI       IDeviceManager plus the D3D12 and Vulkan implementations, shader loading (NVRHI).
LimeRenderer  Frame orchestration, render passes, the NVRHI based ImGui backend.
LimeEditor    Editor layer, dock space, panels. Optional.
LimeRuntime   FEngine and ILimeApplication, the only interface a project implements.
```

NVRHI does not provide device or swap chain management, so `IDeviceManager` is implemented per
backend. The ImGui renderer is written against NVRHI rather than using `imgui_impl_dx12` /
`imgui_impl_vulkan`, so both backends share one drawing path; only `imgui_impl_glfw` is reused for
platform input.

## Repository layout

```
CMake/          Build modules (compiler options, target helpers, shader compilation)
Engine/
  Shaders/      Built-in HLSL sources plus the ShaderMake config
  Content/      Built-in assets such as textures
  Source/
    Runtime/    Core, Platform, RHI, Renderer, Engine
    Editor/     Editor layer and panels
Projects/       Sample and product applications
Tests/          Catch2 unit tests
ThirdParty/     Submodules
Scripts/        Submodule setup, formatting, static analysis
```

Headers and sources live side by side; there is no separate `include/` tree.

## Shaders

`Engine/Shaders/LimeShaders.cfg` is a ShaderMake config. One line per shader permutation set:

```
Triangle/Triangle.hlsl -T vs -E MainVS
Triangle/Triangle.hlsl -T ps -E MainPS
Blur/Blur.hlsl         -T cs -E MainCS -D RADIUS={1,2,4}
```

Every entry is compiled twice, once into `Shaders/DXIL` and once into `Shaders/SPIRV` next to the
executable. ShaderMake handles permutation expansion, include dependency tracking and incremental
builds.

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
| `LIME_ENABLE_RHI_D3D12` | `ON` | Enable the Direct3D 12 backend |
| `LIME_ENABLE_RHI_VULKAN` | `ON` | Enable the Vulkan backend |

## Roadmap

- glTF scene loading through the vendored tinygltf
- Viewport panel rendering into an offscreen target
- Scene graph, material system and a render graph
