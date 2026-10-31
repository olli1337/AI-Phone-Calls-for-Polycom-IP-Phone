#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path $env:LOCALAPPDATA 'aiphone-build')
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies/manifest.json') -Raw |
    ConvertFrom-Json
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
if ($BuildRoot.Contains(' ')) {
    throw 'The native PJSIP build cache path must not contain spaces. The project path may contain spaces.'
}
New-Item -ItemType Directory -Path $BuildRoot -Force | Out-Null

function Get-VerifiedSource($Source) {
    $destination = Join-Path $BuildRoot $Source.file
    if (-not (Test-Path -LiteralPath $destination)) {
        Write-Host "Downloading $($Source.file)"
        Invoke-WebRequest -Uri $Source.url -OutFile $destination -TimeoutSec 180
    }
    $actual = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Source.sha256) {
        throw "Checksum mismatch for $($Source.file). Remove only that cached file and try again."
    }
    return $destination
}

$baseArchive = Get-VerifiedSource $manifest.msys2_base
$bash = Join-Path $BuildRoot 'msys64/usr/bin/bash.exe'
if (-not (Test-Path -LiteralPath $bash)) {
    & tar -xf $baseArchive -C $BuildRoot
    if ($LASTEXITCODE -ne 0) { throw 'MSYS2 extraction failed.' }
}
foreach ($source in $manifest.sources) {
    $archive = Get-VerifiedSource $source
    if ($source.directory -and -not (Test-Path -LiteralPath (Join-Path $BuildRoot $source.directory))) {
        & tar -xf $archive -C $BuildRoot
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $($source.name)" }
    }
}

$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
# Core runtime updates can close the first shell. Always use a new shell afterwards.
& $bash -lc 'pacman -Syu --noconfirm'
if ($LASTEXITCODE -ne 0) { throw 'MSYS2 core update failed.' }
& $bash -lc 'pacman -Syu --noconfirm && pacman -S --needed --noconfirm make patch diffutils mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-clang-tools-extra'
if ($LASTEXITCODE -ne 0) { throw 'Toolchain installation failed.' }
Write-Host "Verified sources and toolchain available at $BuildRoot"
