#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path $env:LOCALAPPDATA 'aiphone-build')
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$projectName = 'AI-Phone-Calls-for-Polycom-IP-Phone'
$packageName = "$projectName-0.1.0-win-x64"
$destination = Join-Path $projectRoot "dist/$packageName"
$executable = Join-Path $projectRoot 'build/aiphone.exe'
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build and test the executable first.' }
New-Item -ItemType Directory -Force -Path $destination | Out-Null

# Remove only unmodified, manifest-owned copies left by older package recipes.
$staleRootDocs = @(Get-ChildItem -LiteralPath $destination -File -Filter '*.md' |
    Where-Object Name -ne 'README.md')
if ($staleRootDocs.Count) {
    $previousManifest = Get-Content -LiteralPath (Join-Path $destination 'PACKAGE-MANIFEST.json') -Raw |
        ConvertFrom-Json
    if ($previousManifest.schema_version -ne 1 -or $previousManifest.version -ne '0.1.0' -or
        $previousManifest.target -ne 'Windows 11 x64') {
        throw 'Cannot verify ownership of the existing package documents.'
    }
    foreach ($file in $staleRootDocs) {
        $entry = @($previousManifest.files | Where-Object file -eq $file.Name)
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $entry.Count -ne 1 -or
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry[0].sha256) {
            throw "Refusing to remove an unverified or modified package document: $($file.Name)"
        }
    }
    foreach ($file in $staleRootDocs) { [IO.File]::Delete($file.FullName) }
}

function Copy-PublicFile([string]$RelativePath) {
    $source = Join-Path $projectRoot $RelativePath
    $target = Join-Path $destination $RelativePath
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}

Copy-Item -LiteralPath $executable -Destination (Join-Path $destination 'aiphone.exe') -Force
$nativeBin = Join-Path $BuildRoot 'msys64/ucrt64/bin'
& (Join-Path $nativeBin 'strip.exe') --strip-unneeded (Join-Path $destination 'aiphone.exe')
if ($LASTEXITCODE -ne 0) { throw 'Release stripping failed.' }
foreach ($file in @('README.md', 'LICENSE',
                     'scripts/synthesize.py', 'scripts/Setup-Speech.ps1',
                     'dependencies/tts-requirements.txt', 'dependencies/tts-tested-lock.txt')) {
    Copy-PublicFile $file
}
foreach ($folder in @('docs', 'examples')) {
    Get-ChildItem -LiteralPath (Join-Path $projectRoot $folder) -File | ForEach-Object {
        Copy-PublicFile ([IO.Path]::GetRelativePath($projectRoot, $_.FullName))
    }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'config/config.example.json') `
    -Destination (Join-Path $destination 'config.example.json') -Force
$tone = Join-Path $destination 'test-tone.wav'
if (-not (Test-Path -LiteralPath $tone)) {
    & python (Join-Path $PSScriptRoot 'generate_tone.py') --output $tone
    if ($LASTEXITCODE -ne 0) { throw 'Test tone generation failed.' }
}

$licenses = Join-Path $destination 'licenses'
New-Item -ItemType Directory -Force -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildRoot 'pjproject-2.17/COPYING') -Destination (Join-Path $licenses 'PJSIP-COPYING') -Force
Copy-Item -LiteralPath (Join-Path $BuildRoot 'CLI11-2.5.0/LICENSE') -Destination (Join-Path $licenses 'CLI11-LICENSE') -Force
Copy-Item -LiteralPath (Join-Path $BuildRoot 'json-3.12.0/LICENSE.MIT') -Destination (Join-Path $licenses 'json-LICENSE.MIT') -Force
Copy-Item -LiteralPath (Join-Path $BuildRoot 'Catch2-3.8.1/LICENSE.txt') -Destination (Join-Path $licenses 'Catch2-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $BuildRoot 'dr_wav.h') -Destination (Join-Path $licenses 'dr_wav.h') -Force
New-Item -ItemType Directory -Force -Path (Join-Path $licenses 'gcc') | Out-Null
foreach ($file in @('COPYING3', 'COPYING.RUNTIME')) {
    Copy-Item -LiteralPath (Join-Path $BuildRoot "msys64/ucrt64/share/licenses/gcc/$file") `
        -Destination (Join-Path $licenses "gcc/$file") -Force
}
Copy-Item -LiteralPath (Join-Path $BuildRoot 'msys64/ucrt64/share/licenses/libwinpthread/COPYING') `
    -Destination (Join-Path $licenses 'winpthreads-COPYING') -Force
# Static MinGW-w64 runtime code has notices separate from GCC and winpthreads.
Copy-Item -LiteralPath (Join-Path $BuildRoot 'msys64/ucrt64/share/licenses/crt/COPYING.MinGW-w64-runtime.txt') `
    -Destination (Join-Path $licenses 'COPYING.MinGW-w64-runtime.txt') -Force

# An explicit source allowlist excludes ignored build data, active config, audio,
# package caches, captures and local secrets even when no Git checkout is present.
$sourceFiles = @()
foreach ($folder in @('src', 'tests', 'scripts', 'dependencies', 'docs', 'examples', '.github')) {
    $folderPath = Join-Path $projectRoot $folder
    if (Test-Path -LiteralPath $folderPath) {
        $sourceFiles += Get-ChildItem -LiteralPath $folderPath -File -Recurse |
            Where-Object { $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and
                           $_.Extension -notin @('.log', '.wav', '.mp3', '.pyc', '.pcap', '.pcapng') }
    }
}
$rootSources = @('README.md', 'LICENSE',
                  'CMakeLists.txt', '.editorconfig', '.clang-format', '.gitattributes', '.gitignore')
foreach ($file in $rootSources) { $sourceFiles += Get-Item -LiteralPath (Join-Path $projectRoot $file) }
$sourceFiles += Get-Item -LiteralPath (Join-Path $projectRoot 'config/config.example.json')
$sourceArchiveName = "$projectName-source.zip"
$sourceArchive = Join-Path $destination $sourceArchiveName
$sourceStream = [IO.File]::Open($sourceArchive, [IO.FileMode]::Create)
$archive = [IO.Compression.ZipArchive]::new($sourceStream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in ($sourceFiles | Sort-Object FullName -Unique)) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Source symlinks are not packaged.' }
        $relative = [IO.Path]::GetRelativePath($projectRoot, $file.FullName).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName,
            "$projectName/$relative", [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
    $manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'dependencies/manifest.json') -Raw | ConvertFrom-Json
    foreach ($source in $manifest.sources) {
        $cached = Join-Path $BuildRoot $source.file
        if ((Get-FileHash -LiteralPath $cached -Algorithm SHA256).Hash.ToLowerInvariant() -ne $source.sha256) {
            throw "Upstream source changed: $($source.file)"
        }
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $cached,
            "upstream/$($source.file)", [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $archive.Dispose()
    $sourceStream.Dispose()
}

$imports = & (Join-Path $nativeBin 'objdump.exe') -p (Join-Path $destination 'aiphone.exe') |
    Select-String 'DLL Name:' | ForEach-Object { ($_.Line -split 'DLL Name:')[1].Trim() }
$unexpected = $imports | Where-Object { $_ -notmatch '^(KERNEL32|WS2_32|ADVAPI32|SHELL32|ole32|msvcrt|api-ms-win-crt-[a-z0-9-]+)\.dll$' }
if ($unexpected) { throw "Unexpected non-system runtime dependency: $unexpected" }
$fileHashes = Get-ChildItem -LiteralPath $destination -File -Recurse |
    Where-Object { $_.Name -ne 'PACKAGE-MANIFEST.json' } | ForEach-Object {
        [ordered]@{file=[IO.Path]::GetRelativePath($destination,$_.FullName).Replace('\','/');
                   sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
[ordered]@{
    schema_version=1; version='0.1.0'; target='Windows 11 x64'; windows_imports=@($imports);
    source_archive=$sourceArchiveName; files=@($fileHashes)
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'PACKAGE-MANIFEST.json') -Encoding utf8

$zip = Join-Path $projectRoot "dist/$packageName.zip"
if (Test-Path -LiteralPath $zip) { [IO.File]::Delete($zip) }
[IO.Compression.ZipFile]::CreateFromDirectory($destination, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $packageName.zip" | Set-Content -LiteralPath ($zip + '.sha256') -Encoding utf8
Write-Host "Created $zip"
Write-Host "SHA256 $hash"
