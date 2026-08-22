# LimeEngine 中文文档

面向项目开发者的教程。仓库根目录的 [README.md](../../README.md) 是英文的完整参考，这里是按任务组织的上手指引。

## 目录

| 文档 | 内容 |
| --- | --- |
| [01-创建项目.md](01-创建项目.md) | 从零建立一个新项目，理解引擎与项目的职责边界 |
| [02-自定义Pass.md](02-自定义Pass.md) | 编写渲染 Pass、写 Shader、用反射暴露可调参数 |
| [03-自定义面板.md](03-自定义面板.md) | 编写编辑器面板、停靠布局、与 Pass 通信 |
| [04-自动化脚本.md](04-自动化脚本.md) | 编写自动化脚本与 pytest 测试 |
| [05-自动化能力参考.md](05-自动化能力参考.md) | 全部命令、HTTP 协议、Python API 速查 |

## 阅读顺序

第一次接触，按 01 → 02 → 03 顺序读，能得到一个可运行、有自定义渲染和 UI 的项目。

想写自动化测试，读 04；查具体命令参数，直接看 05。

## 核心设计

三件事贯穿全部文档，先了解会省很多困惑：

**引擎接管 `main`。** 项目不写入口函数、不在 CMake 里列文件、不主动调用引擎去注册任何东西。你只写 Pass 类和 Panel 类，用一个宏声明它存在。

**反射是唯一的信息源。** 一个设置结构体用 `LIME_REFLECT` 描述一次，编辑器的控件、JSON 存档、自动化的读写就都有了 —— 不需要为这三件事分别写代码。

**自动化是一等能力。** 引擎内置 HTTP 服务，外部脚本可以读写任意 Pass 的参数、开关面板、截图。这既是测试手段，也是开发时的调试手段。

## 环境要求

| 工具 | 版本 | 说明 |
| --- | --- | --- |
| Visual Studio 2022 | 17.x，含 C++ 工作负载 | MSVC 工具链，C++20 |
| CMake | 3.24 或更高 | 预设与 `string(JSON)` 需要 |
| Vulkan SDK | 1.3 或更高 | 提供能输出 SPIR-V 的 DXC |
| Python | 3.10 或更高 | 仅自动化需要 |

```powershell
git clone --recurse-submodules <仓库地址> LimeEngine
cd LimeEngine
./Scripts/SetupSubmodules.ps1     # 若克隆时没带 --recurse-submodules
./Scripts/Build.ps1               # 配置 + 构建 Debug
./Build/ninja/Bin/Debug/HelloTriangle.exe
```

## 常用命令

```powershell
./Scripts/Build.ps1 -Config Release      # 构建
./Scripts/FormatCode.ps1                 # 格式化（-Check 只检查不改）
./Scripts/RunClangTidy.ps1               # 静态分析
ctest --test-dir Build/ninja -C Debug    # 全部测试（C++ 与 Python）
./Scripts/Automation.ps1 test            # 只跑 Python 自动化套件
```
