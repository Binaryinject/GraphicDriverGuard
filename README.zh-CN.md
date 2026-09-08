# Graphic Driver Guard

[English](README.md) | [简体中文](README.zh-CN.md)

Graphic Driver Guard 是用于 Unity PC 独立播放器的渲染前 GPU 检测工具。它会在 Unity 创建图形设备之前运行，可为必须使用 Vulkan、Direct3D 11 或 Direct3D 12（不能提供其它后备方案）的游戏做启动前检测。

## 检测内容

- 可在配置中选择需要检测的渲染 API：Vulkan、Direct3D 11、Direct3D 12 的任意组合。
- Vulkan：Vulkan Loader 是否可用，以及能否枚举物理设备。
- Vulkan：物理设备公开的 Vulkan API 版本，默认最低要求为 Vulkan 1.1。
- Direct3D 11：是否具备可创建 D3D11 设备的硬件适配器，以及最高支持的 Feature Level（默认最低 `11_0`，可配置为 `9_1`～`11_1`）。
- Direct3D 12：是否具备可创建 D3D12 设备的硬件适配器，以及最高支持的 Feature Level（默认最低 `11_0`，可配置为 `11_1` / `12_0` / `12_1`）。
- GPU 厂商和设备标识。
- 参考 UE5 的驱动拒绝规则，支持 `RHIName`（`Vulkan` / `D3D11` / `D3D12`）、显卡名称正则、`DeviceId` 和 Vulkan `DriverId` 精确约束。
- 各厂商的建议驱动版本和官方下载地址。

检测失败时，原生弹窗会显示 GPU、已安装驱动、Vulkan 版本（或 D3D11 / D3D12 Feature Level）、失败原因、建议版本以及**更新驱动**按钮。命中驱动拒绝规则、但硬件仍满足所需的 Vulkan API / D3D Feature Level 时，还会提供**仍然继续**按钮。缺少所需的渲染 API 支持或低于最低要求时仍会阻止启动。弹窗不会提供切换渲染 API 的选项。

弹窗会根据操作系统语言自动选择中文、日文或韩文；除中日韩以外的所有系统语言统一使用英文。

## UE5 参考模型

规则和提示流程参考 UE5 公开的 RHI 启动模型：

- 类似 `FGPUDriverInfo` 的厂商版本归一化和数字版本比较。
- 支持 `<`、`<=`、`=`、`>=`、`>` 运算符的 `DriverDenyList` 条目。
- 只有 `RHIName`（`Vulkan`、`D3D11` 或 `D3D12`）、`AdapterNameRegex`、`DeviceId`、Vulkan `DriverId` 等选择器匹配后，规则才会拦截驱动。
- `MinimumDriverVersion` 设置厂商全局最低版本，不需要配置 `DeviceId` 选择器。
- `SuggestedDriverVersion` 和厂商驱动下载地址。
- 类似 UE `RHIDetectAndWarnOfBadDrivers` 流程的启动早期阻断。

本项目不复制 Epic 源代码，而是针对 Unity Player ABI 实现相同的数据模型和启动行为。

## 单一游戏可执行文件

Unity 构建后处理会直接修改已完成的 Player：

1. 将 Unity 构建生成的 `UnityPlayer.dll` / `UnityPlayer.so` 改名为 `UnityPlayer_.dll` / `UnityPlayer_.so`。
2. 将同名代理放到原来的 `UnityPlayer.dll` / `UnityPlayer.so` 位置。
3. 游戏主可执行文件保持不变，启动时仍然只有一个游戏进程。
4. 代理先执行检测，通过后再转发到 Unity 原始 Player 入口。

Windows 代理导出 `UnityMain` 和 `UnityMain2`。Unity 6 Linux 实际导出 `PlayerMain(int, char**)` 符号（`_Z10PlayerMainiPPc`），Linux 代理会转发该入口。

## Unity 安装

本仓库本身就是完整的 Unity Package，可以直接复制到项目的 `Packages/com.fstgame.graphic-driver-guard`：

```text
YourGame/
  Packages/
    com.fstgame.graphic-driver-guard/
      package.json
      Editor/
      Native~/
```

这种安装方式不需要向 `Assets/Plugins` 复制任何文件。`Native~` 是供构建后处理读取的包数据，不会作为普通 Unity Runtime Plugin 导入。

也可以在 `Packages/manifest.json` 中使用 Git 地址：

```json
"com.fstgame.graphic-driver-guard": "https://github.com/Binaryinject/GraphicDriverGuard.git"
```

带 Tag 的 GitHub Release 会同时提供两种可直接安装的压缩包：

- 在 Unity 中通过 **Assets > Import Package > Custom Package** 导入 `.unitypackage`，它会作为嵌入式包安装到 `Packages/com.fstgame.graphic-driver-guard`。
- 在 **Package Manager > Add package from tarball** 中选择 `.tgz`，包会继续位于 `Packages` 下。

Release 流程会先编译并测试 Windows、Linux 两个平台的原生代理，再生成这两种压缩包。`.unitypackage` 自带全部内容，用户不需要额外安装导入工具；两种安装方式的构建行为相同。

在 **Project Settings > Player > Graphic Driver Guard** 中可以编辑渲染 API 选择（Vulkan / Direct3D 11 / Direct3D 12）、最低 Vulkan 版本、最低 D3D11 / D3D12 Feature Level、各平台最低及建议驱动版本、厂商下载地址和可选拒绝列表。构建后处理适用于 `StandaloneWindows64` 和 `StandaloneLinux64`。Direct3D 11 / 12 检测仅在 Windows 上生效。

原生代理文件应位于：

```text
Native~/Windows/x86_64/UnityPlayer.dll
Native~/Linux/x86_64/UnityPlayer.so
```

## 原生构建

Windows，需要 Visual Studio 2022 Build Tools：

```powershell
./scripts/build-windows.ps1
```

Linux，需要 GCC/Clang 和 CMake：

```bash
./scripts/build-linux.sh
```

两个脚本都会运行版本和配置规则测试。GitHub Actions 会构建并上传两个平台的代理产物。

Unity 冒烟工程强制使用 **IL2CPP** 和 Vulkan，不使用 Mono Player，保持与正式包的构建基线一致。Editor C# 程序集仍由 Unity 编辑器编译器完成编译。

## 配置格式

生成的 `DriverGuard.ini` 特意采用接近 UE 的 INI 规则格式：

```ini
[Global]
MinimumVulkanVersion=1.1
RenderAPI=Vulkan,D3D11,D3D12
MinimumD3D11FeatureLevel=11_0
MinimumFeatureLevel=12_0

[GPU_NVIDIA]
MinimumDriverVersion=516.25
SuggestedDriverVersion=516.25
DownloadURL=https://www.nvidia.com/Download/index.aspx
```

`RenderAPI` 接受逗号分隔的 `Vulkan`、`D3D11` 和/或 `D3D12`，决定启动前检测哪些渲染 API；缺省时仅检测 Vulkan。`MinimumD3D11FeatureLevel` 是 Direct3D 11 的最低 Feature Level（`9_1`～`11_1`），缺省为 `11_0`；`MinimumFeatureLevel` 是 Direct3D 12 的最低 Feature Level（`11_0`、`11_1`、`12_0`、`12_1`），缺省为 `11_0`。

最低驱动版本作用于该厂商的所有设备（不分渲染 API），生成时不会写入 `DeviceId`。低于该版本会显示警告；满足所需 Vulkan API / D3D Feature Level 时玩家仍可选择继续运行。最低版本和可选拒绝列表应结合项目兼容性验证及厂商公告维护。

## 平台说明

- Windows 动态加载 `vulkan-1.dll` 以及 `d3d11.dll` / `d3d12.dll` / `dxgi.dll`，Linux 动态加载 `libvulkan.so.1`，因此 Unity Editor 和游戏工程不需要 Vulkan SDK 头文件或链接库。
- Linux 弹窗依次尝试 `zenity` 和 `kdialog`；如果桌面环境没有这些工具，失败原因会输出到 stderr。
- Windows 使用 Task Dialog，并以 `MessageBox` 作为后备；代理静态链接 MSVC Runtime。
- 代理架构必须与 Unity Player 一致，当前提供的产物目标为 `x86_64`。
