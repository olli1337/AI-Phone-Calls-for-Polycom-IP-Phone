#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$AppDirectory,
    [string]$DataDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'AIPhone'),
    [string]$Message = 'Hello! Your AI agent is calling. Time for a coffee break. Goodbye.',
    [string]$Caller = 'MY AGENT'
)
$ErrorActionPreference = 'Stop'

# Use an extracted package and a private folder prepared with Setup-Speech.ps1 and config.json.
$app = (Resolve-Path -LiteralPath $AppDirectory).Path
$data = (Resolve-Path -LiteralPath $DataDirectory).Path
$base = Join-Path $data ("custom-" + [guid]::NewGuid().ToString('N'))

# Keep each message and recording under a new name in the private data folder.
$Message | Set-Content -LiteralPath "$base.txt" -Encoding utf8

& "$data\tts-env\Scripts\python.exe" "$app\scripts\synthesize.py" `
    --model "$data\voices\en_US-ljspeech-medium.onnx" `
    --text-file "$base.txt" --output "$base.wav" --end-beep

if ($LASTEXITCODE -ne 0) { throw 'Speech generation failed.' }

# Ring the configured phone and play the message after manual answer.
& "$app\aiphone.exe" call --config "$data\config.json" `
    --audio "$base.wav" --caller $Caller --json
exit $LASTEXITCODE
