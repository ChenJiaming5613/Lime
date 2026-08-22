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

```powershell
git clone --recurse-submodules <repo-url> LimeEngine
cd LimeEngine
# If the clone was not recursive:
./Scripts/SetupSubmodules.ps1

cmake --preset ninja
cmake --build --preset ninja-debug
./Build/ninja/Bin/Debug/HelloTriangle.exe
```

For Visual Studio debugging use the `vs2022` preset instead.

## Running HelloTriangle

```powershell
HelloTriangle.exe                 # default backend (D3D12)
HelloTriangle.exe --rhi=vulkan    # Vulkan backend
HelloTriangle.exe --rhi=d3d12
HelloTriangle.exe --no-editor     # runtime only, no editor UI
```

## Tests

```powershell
ctest --preset ninja-debug
```

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

The build compiles every entry twice, once to `Shaders/DXIL` and once to `Shaders/SPIRV`, next to
the executable. Vulkan register shifts are defined once in the root `CMakeLists.txt`
(`LIME_VK_SHIFT_*`) and passed both to DXC and to `nvrhi::VulkanBindingOffsets`; the two must stay
in sync or descriptor bindings break.

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
