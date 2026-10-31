[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$Config,
    [Parameter(Mandatory)][string]$Audio,
    [string]$Caller = 'MY AGENT'
)
$ErrorActionPreference = 'Stop'
& $Executable call --config $Config --audio $Audio --caller $Caller --json
exit $LASTEXITCODE
