param(
    [Parameter(Mandatory)][int]$GameProcessId,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [int]$IntervalSeconds = 5,
    [ValidateSet('C:\Users\T-GAMER\rpcs3\bin\rpcs3.exe','C:\Users\T-GAMER\rpcs3\bin\rpcs3-baseline-0dec46b1.exe','C:\Users\T-GAMER\rpcs3\bin\rpcs3-offloader-diag-F26AE709.exe','C:\Users\T-GAMER\rpcs3\bin\rpcs3-semaddr-5D2A5E2A.exe','C:\Users\T-GAMER\rpcs3\bin\rpcs3-turbo1-D42491E3.exe','C:\Users\T-GAMER\rpcs3\bin\rpcs3-turbo2-2BA0BD54.exe')]
    [string]$ExpectedExecutablePath = 'C:\Users\T-GAMER\rpcs3\bin\rpcs3.exe'
)
$ErrorActionPreference = 'Stop'
$expectedExe = $ExpectedExecutablePath
$logPath = 'C:\Users\T-GAMER\rpcs3\bin\portable\log\RPCS3.log'
$zone = [TimeZoneInfo]::FindSystemTimeZoneById('E. South America Standard Time')
$logicalProcessors = [Environment]::ProcessorCount
$encoding = [Text.UTF8Encoding]::new($false)
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
$allowedRoot = 'C:\Users\T-GAMER\rpcs3-dev-tools\monitoring\'
if (-not $outputPath.StartsWith($allowedRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Diretorio de coleta fora do projeto.' }
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$statusPath = Join-Path $outputPath 'status.json'
$csvPath = Join-Path $outputPath 'samples.csv'
$eventsPath = Join-Path $outputPath 'events.jsonl'
$initialProcess = Get-Process -Id $GameProcessId
if ($initialProcess.Path -ne $expectedExe) { throw 'Executavel inesperado.' }
$startTicks = $initialProcess.StartTime.ToUniversalTime().Ticks
$startedUtc = [DateTimeOffset]::UtcNow
$lastCpu = $initialProcess.TotalProcessorTime.TotalSeconds
$lastTime = [DateTimeOffset]::UtcNow
$logOffset = 0L
if (Test-Path -LiteralPath $logPath) { $logOffset = [Math]::Max(0L,(Get-Item -LiteralPath $logPath).Length - 262144L) }
$logCarry = ''
$severityTotals = @{ F=0; E=0; W=0 }
$compileTotals = @{ SPU=0; RSX=0; PPU=0 }
$recentEvents = [Collections.Generic.List[object]]::new()
$recentSamples = [Collections.Generic.List[object]]::new()
$sampleCount = 0
$validFpsCount = 0
$fpsSum = 0.0
$fpsMin = [double]::PositiveInfinity
$fpsMax = 0.0
$lastGameSeen = [DateTimeOffset]::UtcNow
$lastGpuRead = [DateTimeOffset]::MinValue
$gpu3d = $null; $gpuCompute = $null; $vramMb = $null; $sharedGpuMb = $null; $systemCpu = $null
$logReadError = $null
$monitorState = 'running'
$stopReason = $null

function Write-Status {
    param([object]$LastSample)
    $validRecent = @($recentSamples | Where-Object { $null -ne $_.fps })
    $recentAverage = if ($validRecent.Count) { [Math]::Round(($validRecent | Measure-Object fps -Average).Average,2) } else { $null }
    $recentMinimum = if ($validRecent.Count) { ($validRecent | Measure-Object fps -Minimum).Minimum } else { $null }
    $recentMaximum = if ($validRecent.Count) { ($validRecent | Measure-Object fps -Maximum).Maximum } else { $null }
    $payload = [ordered]@{
        state=$monitorState; stop_reason=$stopReason; process_id=$GameProcessId
        process_started_utc=$initialProcess.StartTime.ToUniversalTime().ToString('o')
        collection_started_utc=$startedUtc.ToString('o'); updated_utc=[DateTimeOffset]::UtcNow.ToString('o')
        interval_seconds=$IntervalSeconds; logical_processors=$logicalProcessors
        samples=$sampleCount; last_sample=$LastSample
        fps=[ordered]@{source='RPCS3 window title'; sampled_mean=if($validFpsCount){[Math]::Round($fpsSum/$validFpsCount,2)}else{$null}; sampled_min=if($validFpsCount){$fpsMin}else{$null}; sampled_max=if($validFpsCount){$fpsMax}else{$null}; recent_mean=$recentAverage; recent_min=$recentMinimum; recent_max=$recentMaximum; recent_window_seconds=300; note='Amostras de FPS do titulo, nao sao frame times individuais nem 1% low.'}
        log=[ordered]@{path=$logPath; offset_bytes=$logOffset; read_error=$logReadError; severity_totals=$severityTotals; successful_compiles=$compileTotals; recent_events=@($recentEvents); note='Contagens desde o inicio da coleta, incluindo cauda inicial de 256KB. Erros E isolados nao comprovam falha.'}
        csv=$csvPath; events=$eventsPath; recent_samples=@($recentSamples)
    }
    $temporaryPath = $statusPath + '.tmp'
    [IO.File]::WriteAllText($temporaryPath,($payload | ConvertTo-Json -Depth 9),$encoding)
    [IO.File]::Move($temporaryPath,$statusPath,$true)
}

try {
    while ($true) {
        $cycleClock = [Diagnostics.Stopwatch]::StartNew()
        $now = [DateTimeOffset]::UtcNow
        $gameProcess = Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue
        if (-not $gameProcess) { $monitorState='stopped'; $stopReason='process_exited'; break }
        if ($gameProcess.Path -ne $expectedExe -or $gameProcess.StartTime.ToUniversalTime().Ticks -ne $startTicks) { $monitorState='stopped'; $stopReason='process_changed'; break }
        if (Test-Path -LiteralPath (Join-Path $outputPath 'stop.requested')) { $monitorState='stopped'; $stopReason='user_stop_request'; break }
        $gameProcess.Refresh()
        $title = $gameProcess.MainWindowTitle
        $fps = $null
        if ($title -match 'FPS:\s*([0-9]+(?:[.,][0-9]+)?)' -and $title.Contains('[BCUS98125]')) {
            $fps = [double]::Parse($Matches[1].Replace(',','.'),[Globalization.CultureInfo]::InvariantCulture)
            $lastGameSeen = $now
        }
        $elapsed = [Math]::Max(0.1,($now-$lastTime).TotalSeconds)
        $cpuNow = $gameProcess.TotalProcessorTime.TotalSeconds
        $processCpu = [Math]::Round([Math]::Max(0,100*($cpuNow-$lastCpu)/$elapsed/$logicalProcessors),2)
        $lastCpu=$cpuNow; $lastTime=$now
        if (($now-$lastGpuRead).TotalSeconds -ge 15) {
            try {
                $engines = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -Filter "Name LIKE 'pid_${GameProcessId}_%'" -OperationTimeoutSec 3)
                $threeD = @($engines | Where-Object Name -match 'engtype_3D$')
                $compute = @($engines | Where-Object Name -match 'engtype_Compute')
                $gpu3d = if($threeD.Count){[Math]::Round(($threeD | Measure-Object UtilizationPercentage -Maximum).Maximum,2)}else{$null}
                $gpuCompute = if($compute.Count){[Math]::Round(($compute | Measure-Object UtilizationPercentage -Maximum).Maximum,2)}else{$null}
                $gpuMemory = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -Filter "Name LIKE 'pid_${GameProcessId}_%'" -OperationTimeoutSec 3)
                $vramMb = if($gpuMemory.Count){[Math]::Round(($gpuMemory | Measure-Object DedicatedUsage -Sum).Sum/1MB,1)}else{$null}
                $sharedGpuMb = if($gpuMemory.Count){[Math]::Round(($gpuMemory | Measure-Object SharedUsage -Sum).Sum/1MB,1)}else{$null}
                $systemCpu=(Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'" -OperationTimeoutSec 3).PercentProcessorTime
            } catch { $gpu3d=$null; $gpuCompute=$null; $vramMb=$null; $sharedGpuMb=$null; $systemCpu=$null }
            $lastGpuRead=$now
        }
        $deltaSeverity=@{F=0;E=0;W=0}; $deltaCompile=@{SPU=0;RSX=0;PPU=0}
        $stream=$null
        try {
            $stream=[IO.File]::Open($logPath,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            if($stream.Length -lt $logOffset){$logOffset=0L;$logCarry=''}
            [void]$stream.Seek($logOffset,[IO.SeekOrigin]::Begin)
            $readLength=[int][Math]::Min(4194304L,[Math]::Max(0L,$stream.Length-$logOffset))
            if($readLength -gt 0){
                $buffer=[byte[]]::new($readLength);$bytesRead=$stream.Read($buffer,0,$readLength);$logOffset+=$bytesRead
                $logText=$logCarry+$encoding.GetString($buffer,0,$bytesRead)
                $lastNewline=$logText.LastIndexOf("`n")
                if($lastNewline -ge 0){
                    $complete=$logText.Substring(0,$lastNewline);$logCarry=$logText.Substring($lastNewline+1)
                    foreach($line in ($complete -split "`n")){
                        if($line -match '^\W*([FEW])\s+\d+:\d+'){
                            $severity=$Matches[1];$severityTotals[$severity]++;$deltaSeverity[$severity]++
                            if($severity -eq 'F' -or $severity -eq 'E'){
                                $event=[pscustomobject]@{seen_utc=$now.ToString('o');severity=$severity;critical=($severity -eq 'F' -or $line -match 'VK_ERROR_DEVICE_LOST|Access violation|Unhandled exception|Deadlock detected');message=$line.Substring(0,[Math]::Min(1500,$line.Length)).Trim()}
                                $recentEvents.Add($event);while($recentEvents.Count -gt 30){$recentEvents.RemoveAt(0)}
                                if(-not (Test-Path -LiteralPath $eventsPath) -or (Get-Item -LiteralPath $eventsPath).Length -lt 10485760){[IO.File]::AppendAllText($eventsPath,(($event | ConvertTo-Json -Compress)+"`n"),$encoding)}
                            }
                        }
                        if($line -match 'SPU: New SPU block compiled successfully'){$compileTotals.SPU++;$deltaCompile.SPU++}
                        if($line -match 'RSX: Program compiled successfully'){$compileTotals.RSX++;$deltaCompile.RSX++}
                        if($line -match 'PPU:.*compiled successfully'){$compileTotals.PPU++;$deltaCompile.PPU++}
                    }
                }else{$logCarry=$logText}
            }
            $logReadError=$null
        }catch{$logReadError=$_.Exception.Message}finally{if($stream){$stream.Dispose()}}
        $sample=[pscustomobject][ordered]@{
            time_utc=$now.ToString('o');time_sao_paulo=([TimeZoneInfo]::ConvertTime($now,$zone)).ToString('o');fps=$fps
            cpu_process_percent=$processCpu;cpu_system_percent=$systemCpu;ram_working_set_mb=[Math]::Round($gameProcess.WorkingSet64/1MB,1)
            private_memory_mb=[Math]::Round($gameProcess.PrivateMemorySize64/1MB,1);gpu_3d_percent=$gpu3d;gpu_compute_percent=$gpuCompute
            gpu_dedicated_mb=$vramMb;gpu_shared_mb=$sharedGpuMb;responding=$gameProcess.Responding
            log_new_fatal=$deltaSeverity.F;log_new_error=$deltaSeverity.E;log_new_warning=$deltaSeverity.W
            compiled_spu=$deltaCompile.SPU;compiled_rsx=$deltaCompile.RSX;compiled_ppu=$deltaCompile.PPU;log_offset_bytes=$logOffset
        }
        $sample | Export-Csv -LiteralPath $csvPath -Append -NoTypeInformation -Encoding utf8
        $sampleCount++;$recentSamples.Add($sample);while($recentSamples.Count -gt 60){$recentSamples.RemoveAt(0)}
        if($null -ne $fps){$validFpsCount++;$fpsSum+=$fps;$fpsMin=[Math]::Min($fpsMin,$fps);$fpsMax=[Math]::Max($fpsMax,$fps)}
        Write-Status $sample
        $remaining=[Math]::Max(100,1000*$IntervalSeconds-$cycleClock.ElapsedMilliseconds)
        Start-Sleep -Milliseconds ([int]$remaining)
    }
} catch { $monitorState='failed';$stopReason=$_.Exception.Message }
Write-Status $(if($recentSamples.Count){$recentSamples[$recentSamples.Count-1]}else{$null})
