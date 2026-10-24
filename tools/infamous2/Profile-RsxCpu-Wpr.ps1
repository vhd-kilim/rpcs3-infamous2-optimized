# Bounded CPU sampling for one RPCS3 session. Run elevated only after user approval.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [Parameter(Mandatory)][string]$RunDirectory,
    [ValidateRange(15,120)][int]$Seconds = 90
)
$ErrorActionPreference = 'Stop'
$expected = 'C:\Users\T-GAMER\rpcs3\bin\rpcs3.exe'
$root = [IO.Path]::GetFullPath($RunDirectory)
$allowed = 'C:\Users\T-GAMER\rpcs3-dev-tools\monitoring\'
if (-not $root.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Unexpected collection directory.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'WPR CPU sampling requires elevation.' }
$game = Get-Process -Id $GameProcessId
if ($game.Path -ne $expected) { throw 'Unexpected target executable.' }
$instance = "CodexInfamousRsx$GameProcessId"
$statePath = Join-Path $root 'wpr-state.json'
$tracePath = Join-Path $root 'rsx-cpu.etl'
if (Test-Path -LiteralPath $tracePath) { throw 'Trace already exists.' }
$state = [ordered]@{state='ready'; process_id=$GameProcessId; instance=$instance; elevated=$true; duration_limit_s=$Seconds; ready_utc=[DateTime]::UtcNow.ToString('o'); trace=$tracePath}
function Publish-State {
    $state.updated_utc = [DateTime]::UtcNow.ToString('o')
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ($statePath+'.tmp') -Encoding utf8
    Move-Item -LiteralPath ($statePath+'.tmp') -Destination $statePath -Force
}
$started = $false
try {
    Publish-State
    $deadline = [DateTime]::UtcNow.AddMinutes(10)
    while (-not (Test-Path -LiteralPath (Join-Path $root 'wpr-start.request'))) {
        $game.Refresh()
        if ($game.HasExited) { throw 'Game ended before capture.' }
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Capture start was not requested within ten minutes.' }
        Start-Sleep -Milliseconds 250
    }
    $startLog = & "$env:SystemRoot\System32\wpr.exe" -start CPU -instancename $instance 2>&1
    $code = $LASTEXITCODE
    $startLog | Out-File -LiteralPath (Join-Path $root 'wpr-start.log') -Encoding utf8
    if ($code -ne 0) { throw "WPR start failed: $code" }
    $started = $true
    $state.state = 'recording'
    $state.started_utc = [DateTime]::UtcNow.ToString('o')
    Publish-State
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $game.Refresh()
        if ($game.HasExited -or (Test-Path -LiteralPath (Join-Path $root 'wpr-stop.request'))) { break }
        Start-Sleep -Milliseconds 250
    }
    $state.state = 'saving'
    $state.stop_requested_utc = [DateTime]::UtcNow.ToString('o')
    Publish-State
    $stopLog = & "$env:SystemRoot\System32\wpr.exe" -stop $tracePath -instancename $instance 2>&1
    $code = $LASTEXITCODE
    $stopLog | Out-File -LiteralPath (Join-Path $root 'wpr-stop.log') -Encoding utf8
    if ($code -ne 0) { throw "WPR stop failed: $code" }
    $started = $false
    if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) { throw 'WPR produced no trace.' }
    $state.state = 'complete'
    $state.bytes = (Get-Item -LiteralPath $tracePath).Length
    $state.completed_utc = [DateTime]::UtcNow.ToString('o')
    Publish-State
} catch {
    $state.state = 'failed'
    $state.error = $_.Exception.Message
    Publish-State
    throw
} finally {
    if ($started) {
        # Only cancel the recorder instance created by this helper.
        & "$env:SystemRoot\System32\wpr.exe" -cancel -instancename $instance 2>&1 | Out-File -LiteralPath (Join-Path $root 'wpr-cleanup.log') -Encoding utf8
    }
}
