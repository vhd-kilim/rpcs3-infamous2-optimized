# Fecha uma rodada: copia o RPCS3.log para a pasta e roda as analises de frametime/threads e de semaforos.
# Uso: .\Finish-AB-Run.ps1 -RunDirectory C:\...\monitoring\ab-...
param([Parameter(Mandatory)][string]$RunDirectory)
$ErrorActionPreference = 'Stop'
if (@(Get-Process -Name 'rpcs3*' -ErrorAction SilentlyContinue).Count) { throw 'Feche o RPCS3 antes de finalizar a rodada.' }
$python = 'C:\Users\T-GAMER\AppData\Local\Programs\Python\Python313\python.exe'
$devRoot = 'C:\Users\T-GAMER\rpcs3-dev-tools'
$log = Join-Path $RunDirectory 'RPCS3.log'
if (-not (Test-Path -LiteralPath $log)) { Copy-Item 'C:\Users\T-GAMER\rpcs3\bin\portable\log\RPCS3.log' $log }
$frames = & $python -X utf8 (Join-Path $devRoot 'Analyze-AB-Run.py') $RunDirectory | ConvertFrom-Json
$sems = $null
if (Select-String -LiteralPath $log -Pattern 'RSX_DIAG' -SimpleMatch -Quiet) {
    $sems = & $python -X utf8 (Join-Path $devRoot 'Analyze-RsxSemaphores.py') $RunDirectory | ConvertFrom-Json
}
$f = $frames.frames; $t = $frames.threads.mean_one_core_percent
$pick = { param($addr) if ($sems) { $sems.per_address | Where-Object address -eq $addr | Select-Object -First 1 } }
$s870 = & $pick '0x40300870'; $s890 = & $pick '0x40300890'
[pscustomobject][ordered]@{
    run = Split-Path $RunDirectory -Leaf
    fps = $f.fps_mean; low1 = $f.one_percent_low_fps; p99_ms = $f.p99_ms; over50 = $f.over_50ms
    sem_wait_pct = $(if ($sems) { $sems.semaphore_wait_pct_wall })
    wait870_ms_frame = $s870.wait_ms_per_frame; wait870_frames_pct = $s870.wait_pct_of_frames
    wait890_ms_frame = $s890.wait_ms_per_frame
    rsx_cpu = $t.'rsx::thread'; ppu_main_cpu = $t.'PPU[0x1000000] main_thread'
}
