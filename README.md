# Graphic Driver Guard

[English](README.md) | [简体中文](README.zh-CN.md)

Graphic Driver Guard is a pre-render GPU check for standalone Unity players. It runs before Unity creates its graphics device and supports preflight checks for Vulkan, Direct3D 11, and/or Direct3D 12, for games that cannot offer another fallback.

## What It Checks

- A configurable render-API selection: any combination of Vulkan, Direct3D 11, and Direct3D 12.
- Vulkan Loader availability and physical-device enumeration.
- Physical-device API version. The default minimum is Vulkan 1.1.
- Direct3D 11 availability: whether a hardware adapter can create a D3D11 device, and the highest supported feature level (default minimum `11_0`; `9_1` through `11_1` are configurable).
- Direct3D 12 availability: whether a hardware adapter can create a D3D12 device, and the highest supported feature level (default minimum `11_0`; `11_1` / `12_0` / `12_1` are configurable).
- GPU vendor and device identity.
- UE5-style driver deny rules constrained by `RHIName` (`Vulkan` / `D3D11` / `D3D12`), adapter-name regex, and optional Vulkan `DeviceId`/`DriverId` selectors.
- A vendor-specific recommended driver version and download URL.

If a check fails, the native dialog shows the GPU, installed driver, Vulkan version (or D3D11 / D3D12 feature level), reason, recommended version, and an **Update driver** button. A deny-listed driver whose hardware still meets the required Vulkan API / D3D feature level also offers **Continue anyway**. Missing render-API support and levels below the minimum remain blocking. There is no render-API switch.

The dialog follows the operating-system language for Chinese, Japanese, and Korean. Every other system language uses English.

## UE5 Reference Model

The rule and message flow follows the public UE5 RHI startup model:

- `FGPUDriverInfo`-style vendor normalization and numeric version comparison.
- `DriverDenyList` entries with `<`, `<=`, `=`, `>=`, or `>` operators.
- `RHIName` (`Vulkan`, `D3D11`, or `D3D12`), `AdapterNameRegex`, `DeviceId`, and Vulkan `DriverId` selectors are evaluated before a rule can deny a driver.
- `MinimumDriverVersion` applies a vendor-wide threshold without requiring `DeviceId` selectors.
- `SuggestedDriverVersion` and vendor download URL fields.
- Early startup blocking equivalent to UE's `RHIDetectAndWarnOfBadDrivers` path.

The project does not copy Epic source code. It implements the same data model and startup behavior for Unity's player ABI.

## Single-Executable Integration

The Unity postprocessor patches a completed player in place:

1. `UnityPlayer.dll` / `UnityPlayer.so` from the Unity build is renamed to `UnityPlayer_.dll` / `UnityPlayer_.so`.
2. The same-name proxy is copied into its place.
3. The existing game executable is unchanged and still starts one process.
4. The proxy runs the preflight, then forwards to Unity's original player entry point.

Windows exports `UnityMain` and `UnityMain2`. Unity 6 Linux exports the actual `PlayerMain(int, char**)` symbol (`_Z10PlayerMainiPPc`), which the Linux proxy forwards.

## Unity Installation

This repository is a complete Unity package. It can be copied directly into a project's `Packages/com.fstgame.graphic-driver-guard` directory:

```text
YourGame/
  Packages/
    com.fstgame.graphic-driver-guard/
      package.json
      Editor/
      Native~/
```

When installed this way, no files need to be copied into `Assets/Plugins`. `Native~` is package data consumed by the build postprocessor; it is intentionally not imported as a runtime Unity plugin.

Alternatively, add this repository as a local/Git package in `Packages/manifest.json`:

```json
"com.fstgame.graphic-driver-guard": "https://github.com/Binaryinject/GraphicDriverGuard.git"
```

Tagged GitHub releases provide two ready-to-install archives:

- Import the `.unitypackage` through **Assets > Import Package > Custom Package**. It installs as an embedded package under `Packages/com.fstgame.graphic-driver-guard`.
- Install the `.tgz` through **Package Manager > Add package from tarball** to keep the package under `Packages`.

The release workflow builds and tests both native proxies before creating either archive. The `.unitypackage` is self-contained and requires no extra importer package. Both installation methods have the same build behavior.

Open **Project Settings > Player > Graphic Driver Guard** to edit the render-API selection (Vulkan / Direct3D 11 / Direct3D 12), the minimum Vulkan version, the minimum D3D11 / D3D12 feature level, per-platform minimum and recommended driver versions, vendor URLs, and optional deny-list rules. Disable **Enable Build Integration** to leave standalone builds untouched; when disabled, the postprocessor also removes proxy/configuration artifacts left by an earlier enabled build. The postprocessor applies to `StandaloneWindows64` and `StandaloneLinux64` builds. Direct3D 11 / 12 detection only runs on Windows.

The native proxy binaries are expected at:

```text
Native~/Windows/x86_64/UnityPlayer.dll
Native~/Linux/x86_64/UnityPlayer.so
```

## Native Build

Windows (Visual Studio 2022 Build Tools):

```powershell
./scripts/build-windows.ps1
```

Linux (GCC/Clang and CMake):

```bash
./scripts/build-linux.sh
```

Both scripts run the version/configuration tests. GitHub Actions builds and uploads both proxy artifacts.

The Unity smoke project uses **IL2CPP**, not Mono, and forces Vulkan. This matches the production build baseline. The Editor C# assembly is still compiled by Unity's editor compiler, but no Mono player is used as a runtime validation target.

## Configuration Format

The generated `DriverGuard.ini` is intentionally close to UE's INI style:

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

`RenderAPI` accepts a comma-separated list of `Vulkan`, `D3D11`, and/or `D3D12` and decides which render APIs are checked before startup; it defaults to Vulkan only. `MinimumD3D11FeatureLevel` is the minimum Direct3D 11 feature level (`9_1` through `11_1`) and defaults to `11_0`; `MinimumFeatureLevel` is the minimum Direct3D 12 feature level (`11_0`, `11_1`, `12_0`, `12_1`) and defaults to `11_0`.

The minimum driver version applies to every device from that vendor regardless of render API and does not emit a `DeviceId`. Drivers below it show the warning dialog; users can still continue when the required Vulkan API / D3D feature level is available. Keep minimum and optional deny-list values aligned with your compatibility validation and current vendor advisories.

## Platform Notes

- The probe dynamically loads `vulkan-1.dll` plus `d3d11.dll` / `d3d12.dll` / `dxgi.dll` on Windows and `libvulkan.so.1` on Linux, so the Unity Editor and project do not need Vulkan SDK headers or libraries.
- Linux GUI buttons use `zenity`, then `kdialog`, and finally log the reason to stderr when no desktop dialog helper is available.
- Windows uses Task Dialog and falls back to `MessageBox`; the proxy is statically linked to the MSVC runtime.
- The proxy must be built for the same architecture as the Unity player (`x86_64` in the supplied targets).

GraphicDriverGuard
