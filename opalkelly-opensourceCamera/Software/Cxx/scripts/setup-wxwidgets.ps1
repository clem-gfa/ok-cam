# Copyright (c) 2026 Opal Kelly Incorporated
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

# setup-wxwidgets.ps1: fetch the prebuilt wxWidgets used by the okCameraApp (C++) Windows build.
#
# What it does: downloads the official prebuilt wxWidgets (the headers, plus the vc14x x64 Dev and
# ReleaseDLL packages) from the wxWidgets project's GitHub releases and extracts them into
# Software/Cxx/third_party/wxwidgets, which the CMake build then finds automatically. If 7-Zip is not
# already installed it also downloads the small portable 7zr.exe extractor from 7-zip.org. Nothing is
# installed system-wide and nothing is committed to the repo; that directory is .gitignored.
#
# Run it from Software/Cxx:
#   powershell -ExecutionPolicy Bypass -File scripts\setup-wxwidgets.ps1
#
# The -ExecutionPolicy Bypass applies only to this one invocation, so PowerShell will run this local
# script; it does not change your system's execution policy.
#
# Linux and macOS do not need this: install wxWidgets from the system package manager instead
# (apt install libwxgtk3.2-dev  /  brew install wxwidgets).

param(
    [string]$Version = "3.2.10",
    [string]$Dest    = (Join-Path $PSScriptRoot "..\third_party\wxwidgets")
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Dest = [IO.Path]::GetFullPath($Dest)
if (Test-Path (Join-Path $Dest "include\wx\version.h")) {
    Write-Host "wxWidgets already present at $Dest - nothing to do."
    exit 0
}
New-Item -ItemType Directory -Force $Dest | Out-Null
$dl = Join-Path $env:TEMP "wx-$Version-dl"
New-Item -ItemType Directory -Force $dl | Out-Null

# Locate a 7-Zip extractor (.7z is the only format the wx prebuilts ship in); fetch portable 7zr if absent.
$pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
$sevenZip = @("$env:ProgramFiles\7-Zip\7z.exe", "$pf86\7-Zip\7z.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $sevenZip) {
    $sevenZip = Join-Path $dl "7zr.exe"
    if (-not (Test-Path $sevenZip)) {
        Write-Host "7-Zip not found; downloading portable 7zr.exe ..."
        Invoke-WebRequest "https://www.7-zip.org/a/7zr.exe" -OutFile $sevenZip -UseBasicParsing
    }
}

$base = "https://github.com/wxWidgets/wxWidgets/releases/download/v$Version"
$assets = @(
    "wxWidgets-$Version-headers.7z",
    "wxMSW-${Version}_vc14x_x64_Dev.7z",
    "wxMSW-${Version}_vc14x_x64_ReleaseDLL.7z"
)
foreach ($a in $assets) {
    $out = Join-Path $dl $a
    if (-not (Test-Path $out)) {
        Write-Host "Downloading $a ..."
        Invoke-WebRequest "$base/$a" -OutFile $out -UseBasicParsing
    }
    & $sevenZip x $out "-o$Dest" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "extract failed: $a" }
}

if (-not (Test-Path (Join-Path $Dest "include\wx\version.h"))) { throw "wxWidgets headers missing after extract" }
if (-not (Test-Path (Join-Path $Dest "lib\vc14x_x64_dll\mswu\wx\setup.h"))) { throw "wxWidgets setup.h missing after extract" }
Write-Host "wxWidgets $Version ready at $Dest"
