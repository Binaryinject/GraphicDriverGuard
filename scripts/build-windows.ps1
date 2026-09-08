[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}

$visualStudio = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) {
    throw "Visual Studio C++ Build Tools were not found."
}

$toolset = Get-ChildItem (Join-Path $visualStudio "VC\Tools\MSVC") -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$compiler = Join-Path $toolset.FullName "bin\Hostx64\x64\cl.exe"
$windowsKits = "${env:ProgramFiles(x86)}\Windows Kits\10"
$sdk = Get-ChildItem (Join-Path $windowsKits "Include") -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "um\Windows.h") } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) {
    throw "Windows 10/11 SDK was not found."
}

$env:INCLUDE = @(
    (Join-Path $toolset.FullName "include"),
    (Join-Path $sdk.FullName "ucrt"),
    (Join-Path $sdk.FullName "shared"),
    (Join-Path $sdk.FullName "um"),
    (Join-Path $sdk.FullName "winrt")
) -join ";"
$sdkLib = Join-Path (Join-Path $windowsKits "Lib") $sdk.Name
$env:LIB = @(
    (Join-Path $toolset.FullName "lib\x64"),
    (Join-Path $sdkLib "ucrt\x64"),
    (Join-Path $sdkLib "um\x64")
) -join ";"

$buildDirectory = Join-Path $repoRoot "build\windows-x86_64"
$outputDirectory = Join-Path $repoRoot "Native~\Windows\x86_64"
New-Item -ItemType Directory -Force $buildDirectory, $outputDirectory | Out-Null

$optimization = if ($Configuration -eq "Release") { "/O2" } else { "/Od" }
$common = @(
    "/nologo", "/std:c++17", "/EHsc", "/utf-8", "/MT", "/W4", $optimization,
    "/I$repoRoot\native\include"
)
$coreSources = @(
    "$repoRoot\native\src\config.cpp",
    "$repoRoot\native\src\driver_version.cpp",
    "$repoRoot\native\src\driver_version_windows.cpp",
    "$repoRoot\native\src\dx11_probe.cpp",
    "$repoRoot\native\src\dx12_probe.cpp",
    "$repoRoot\native\src\localization.cpp",
    "$repoRoot\native\src\preflight.cpp",
    "$repoRoot\native\src\vulkan_probe.cpp"
)

Push-Location $buildDirectory
try {
    & $compiler @common @coreSources `
        "$repoRoot\tests\main.cpp" `
        "$repoRoot\tests\config_tests.cpp" `
        "$repoRoot\tests\driver_version_tests.cpp" `
        "/I$repoRoot\tests" "/Fe:$buildDirectory\uvdg_tests.exe" `
        "/link" "advapi32.lib" "user32.lib"
    if ($LASTEXITCODE -ne 0) { throw "Native tests failed to compile." }

    & "$buildDirectory\uvdg_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "Native tests failed." }

    & $compiler @common "/LD" @coreSources `
        "$repoRoot\native\src\dialog_windows.cpp" `
        "$repoRoot\native\src\proxy_windows.cpp" `
        "/Fe:$outputDirectory\UnityPlayer.dll" `
        "/link" "/DEF:$repoRoot\native\windows\UnityPlayerProxy.def" `
        "advapi32.lib" "comctl32.lib" "shell32.lib" "user32.lib"
    if ($LASTEXITCODE -ne 0) { throw "UnityPlayer proxy failed to compile." }
}
finally {
    Pop-Location
}

Write-Host "Built $outputDirectory\UnityPlayer.dll"
