#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path $env:LOCALAPPDATA 'aiphone-build'),
    [switch]$Bootstrap,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if ($Bootstrap) { & (Join-Path $PSScriptRoot 'Bootstrap.ps1') -BuildRoot $BuildRoot }
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$bash = Join-Path $BuildRoot 'msys64/usr/bin/bash.exe'
if (-not (Test-Path -LiteralPath $bash)) {
    throw 'Run scripts/Build.ps1 -Bootstrap to download and verify the OSS toolchain and sources.'
}
$env:AIPHONE_PROJECT_DIR = $projectRoot
$env:AIPHONE_BUILD_ROOT = $BuildRoot
$env:AIPHONE_SKIP_TESTS = if ($SkipTests) { '1' } else { '0' }
$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
& $bash -lc 'exec bash "$(cygpath -u "$AIPHONE_PROJECT_DIR")/scripts/build-native.sh"'
if ($LASTEXITCODE -ne 0) { throw 'Native build or tests failed.' }
