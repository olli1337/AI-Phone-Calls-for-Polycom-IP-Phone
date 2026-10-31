#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Python = 'python',
    [string]$Destination = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'AIPhone')
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$environmentPath = Join-Path $Destination 'tts-env'
$pythonExe = Join-Path $environmentPath 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $pythonExe)) {
    & $Python -m venv $environmentPath
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the optional speech environment.' }
}
& $pythonExe -m pip install -r (Join-Path $projectRoot 'dependencies/tts-tested-lock.txt')
if ($LASTEXITCODE -ne 0) { throw 'Optional speech dependency installation failed.' }

$voicePath = Join-Path $Destination 'voices'
New-Item -ItemType Directory -Path $voicePath -Force | Out-Null
$voiceBase = 'https://huggingface.co/rhasspy/piper-voices/resolve/b4e358ffcb232591bf73a540e70f7de528e4174a/en/en_US/ljspeech/medium/'
$voiceFiles = @{
    'en_US-ljspeech-medium.onnx' = '6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22'
    'en_US-ljspeech-medium.onnx.json' = '141d612cc0a95ed7efc1ca936b845c2364967f2e9217c5dbfcf69fc4d6c65860'
}
foreach ($fileName in $voiceFiles.Keys) {
    $target = Join-Path $voicePath $fileName
    if (-not (Test-Path -LiteralPath $target)) {
        Invoke-WebRequest -Uri ($voiceBase + $fileName) -OutFile $target -TimeoutSec 180
    }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $voiceFiles[$fileName]) {
        throw "Voice checksum mismatch: $fileName. Remove that cached file before retrying."
    }
}
Invoke-WebRequest -Uri ($voiceBase + 'MODEL_CARD') -OutFile (Join-Path $voicePath 'MODEL_CARD') -TimeoutSec 30
Write-Host "Offline Piper and voice are ready in $Destination. No call has been placed."
