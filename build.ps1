<#
.SYNOPSIS
    Configures and builds Exp-LORer (CMake + Ninja + vcpkg) using a CMake preset.

.PARAMETER Preset
    Build preset: windows, release, or debug. Defaults to release.

.PARAMETER Reconfigure
    Forces a fresh CMake configure step even if a cache already exists.

.PARAMETER Target
    Optional CMake build target (defaults to building everything).
#>
param(
    [ValidateSet("windows", "release", "debug")]
    [string]$Preset = "release",
    [switch]$Reconfigure,
    [string]$Target
)

$ErrorActionPreference = "Stop"
$CMakePreset = switch ($Preset) {
    "windows" { "windows" }
    "release" { "Release" }
    "debug" { "DebugFast" }
}
$BinaryDir = Join-Path $PSScriptRoot "build\$CMakePreset"

# vcpkg manifest mode needs VCPKG_ROOT to locate the toolchain file.
if (-not $env:VCPKG_ROOT) {
    Write-Error "VCPKG_ROOT is not set. Set it to your vcpkg checkout, e.g. `$env:VCPKG_ROOT = 'C:\vcpkg'`."
    exit 1
}

# Ninja needs the MSVC toolchain (cl.exe) on PATH; import it from vswhere if missing.
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        Write-Error "cl.exe not found on PATH and vswhere.exe is missing. Run this from a Visual Studio Developer PowerShell."
        exit 1
    }
    $vsInstallPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsInstallPath) {
        Write-Error "Could not locate a Visual Studio installation with the C++ toolchain."
        exit 1
    }
    $vcvarsall = Join-Path $vsInstallPath "VC\Auxiliary\Build\vcvarsall.bat"
    $envDump = cmd.exe /c "`"$vcvarsall`" x64 && set" | Out-String
    foreach ($line in ($envDump -split "`r`n")) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

if ($Reconfigure -or -not (Test-Path (Join-Path $BinaryDir "CMakeCache.txt"))) {
    cmake --preset $CMakePreset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$buildArgs = @("--build", $BinaryDir)
if ($Target) { $buildArgs += @("--target", $Target) }

cmake @buildArgs
exit $LASTEXITCODE
