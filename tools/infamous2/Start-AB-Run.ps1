# A/B do Offloader: inicia uma rodada limpa (sem diagnosticos) e os coletores.
# Uso: .\Start-AB-Run.ps1 -Variant baseline|offloader -Run 1
param(
    [Parameter(Mandatory)][ValidateSet('baseline','offloader','semaddr','turbo','turbo2','turbo3','rsxwait','viewfast')][string]$Variant,
    [Parameter(Mandatory)][ValidateRange(1,20)][int]$Run,
    [int]$ThreadProfileSeconds = 14400,
    # Config completa (global + custom) passada com --config; o rotulo entra no nome da pasta.
    [string]$ConfigOverride,
    [string]$Label,
    # Liga RPCS3_RSX_DIAGNOSTICS=1 (linhas RSX_DIAG); nao usar em rodadas de benchmark.
    [switch]$RsxDiagnostics,
    # Mascara de afinidade do processo inteiro (ex.: 0xFFF = so nucleos P no i5-14400F).
    [long]$AffinityMask = 0,
    # Variaveis extras so para o processo do jogo, ex.: @{ RPCS3_INFAMOUS2_RELAXED = 'postfx,light' }
    [hashtable]$ExtraEnv = @{}
)
$ErrorActionPreference = 'Stop'
$devRoot = 'C:\Users\T-GAMER\rpcs3-dev-tools'
$binRoot = 'C:\Users\T-GAMER\rpcs3\bin'
$python = 'C:\Users\T-GAMER\AppData\Local\Programs\Python\Python313\python.exe'
$pwsh = 'C:\Users\T-GAMER\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe'
$iso = 'C:\Users\T-GAMER\OneDrive\Desktop\inFamous 2 (USA) (EnFrEsPt) (v02.00)\inFamous 2 (USA) (En,Fr,Es,Pt) (v02.00).iso'
$variants = @{
    baseline  = @{ exe = "$binRoot\rpcs3-baseline-0dec46b1.exe"; sha = 'D54E1221526B66CD07CDCB2EFA786A5D7B32BF4EA4732B0124257CFBCD5B52A7' }
    offloader = @{ exe = "$binRoot\rpcs3-offloader-diag-F26AE709.exe"; sha = 'F26AE7098D3011B922F253F1EB31A8A4204B11A1BC341CC8CB5EFD0F4F95EAA5' }
    # Offloader + espera de semaforo RSX por endereco no RSX_DIAG (build 24/10 14:24).
    semaddr   = @{ exe = "$binRoot\rpcs3-semaddr-5D2A5E2A.exe"; sha = '5D2A5E2A2CE6742A5A1826632E4C43434E394BB6ED1BFCCB94B8EB5DDA4B635D' }
    # + precisao seletiva (RPCS3_INFAMOUS2_RELAXED) e spin do Offloader (RPCS3_OFFLOAD_SPIN_US), build 24/10 ~15:20.
    turbo     = @{ exe = "$binRoot\rpcs3-turbo1-D42491E3.exe"; sha = 'D42491E36A33DD510CF95908EA88DD19894986FA09414635D59A268D36F3DF58' }
    # turbo + grupos edgepost/vis/spudraw na precisao seletiva (24/10 ~15:25).
    turbo2    = @{ exe = "$binRoot\rpcs3-turbo2-2BA0BD54.exe"; sha = '2BA0BD54D4071D71D1BBE99D0E53CA484D6BB557250CF918601F47E46553D529' }
    # + afinidade hibrida (RPCS3_HYBRID_AFFINITY) e grupos mesh/anim/fx/sim na precisao seletiva (24/10 ~15:45).
    turbo3    = @{ exe = "$binRoot\rpcs3-turbo3-A2CCEA61.exe";                   sha = 'A2CCEA61152D8F16C4DE55E1F187FBE774BD61BE28768DC79434717C8C7AF727' }
    rsxwait   = @{ exe = "$binRoot\rpcs3-rsx-flush-wait-v1.exe"; sha = '898B32E7EE923AF6B24F6708CD6CC67B23585B97D2ED79E1BBEC8ECCCDD6775B' }
    viewfast  = @{ exe = "$binRoot\rpcs3.exe"; sha = '532E806D413727DB05BC24D648A5B75D2DA15D9B51D2F195B964B56AFE1D9DCA' }
}
$exe = $variants[$Variant].exe
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $variants[$Variant].sha) { throw "Hash inesperado: $exe" }
if (@(Get-Process -Name 'rpcs3*' -ErrorAction SilentlyContinue).Count) { throw 'Feche o RPCS3 antes de iniciar outra rodada.' }
foreach ($tool in $python, $pwsh, $iso) { if (-not (Test-Path -LiteralPath $tool)) { throw "Nao encontrado: $tool" } }

# Sessoes ETW do PresentMon cujo jogo ja fechou impedem a nova captura de receber frames.
$presentMon = 'C:\Program Files (x86)\RivaTuner Statistics Server\Plugins\Client\PresentMonDataProvider\PresentMon-2.3.1-x64.exe'
foreach ($line in (logman query -ets)) {
    if ($line -match '^(CodexInfamousLive(\d+)_\d+)\s') {
        if (-not (Get-Process -Id ([int]$Matches[2]) -ErrorAction SilentlyContinue)) {
            & $presentMon --session_name $Matches[1] --terminate_existing_session 2>&1 | Out-Null
        }
    }
}

$runName = if ($Label) { "$Variant-$Label" } else { $Variant }
$outputDir = Join-Path $devRoot ("monitoring\ab-{0}-run{1}-{2}" -f $runName, $Run, (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $outputDir | Out-Null

# Benchmark limpo: nenhuma variavel de diagnostico herdada pelo processo do jogo.
$diagVars = 'RPCS3_RSX_DIAGNOSTICS','RPCS3_INFAMOUS2_LIGHT_PROFILE','RPCS3_INFAMOUS2_CAPTURE','RPCS3_INFAMOUS2_RELAXED','RPCS3_OFFLOAD_SPIN_US','RPCS3_HYBRID_AFFINITY','RPCS3_INFAMOUS2_RSX_FLUSH_WAIT'
foreach ($name in $diagVars) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
if ($RsxDiagnostics) { [Environment]::SetEnvironmentVariable('RPCS3_RSX_DIAGNOSTICS', '1', 'Process') }
foreach ($key in $ExtraEnv.Keys) { [Environment]::SetEnvironmentVariable($key, [string]$ExtraEnv[$key], 'Process') }

$gameArgs = @('"' + $iso + '"')
$configSha = $null
if ($ConfigOverride) {
    if (-not (Test-Path -LiteralPath $ConfigOverride -PathType Leaf)) { throw "Config nao encontrada: $ConfigOverride" }
    $configSha = (Get-FileHash -LiteralPath $ConfigOverride -Algorithm SHA256).Hash
    $gameArgs = @('--config', ('"' + $ConfigOverride + '"')) + $gameArgs
}
$game = Start-Process -FilePath $exe -ArgumentList $gameArgs -WorkingDirectory $binRoot -PassThru
if ($AffinityMask) {
    # SetProcessAffinityMask vale para todas as threads, inclusive as ja criadas.
    $game.ProcessorAffinity = [IntPtr]$AffinityMask
    $game.Refresh()
    if ($game.ProcessorAffinity.ToInt64() -ne $AffinityMask) { throw 'Afinidade nao aplicada.' }
}
$collector = Start-Process -FilePath $pwsh -WindowStyle Hidden -ArgumentList @(
    '-NoLogo','-NoProfile','-File',('"' + (Join-Path $devRoot 'Watch-Infamous2.ps1') + '"'),
    '-GameProcessId',$game.Id,'-OutputDirectory',('"' + $outputDir + '"'),'-ExpectedExecutablePath',('"' + $exe + '"')
) -RedirectStandardOutput (Join-Path $outputDir 'collector.stdout.log') -RedirectStandardError (Join-Path $outputDir 'collector.stderr.log') -PassThru
$frames = Start-Process -FilePath $python -WindowStyle Hidden -ArgumentList @(
    '-X','utf8',('"' + (Join-Path $devRoot 'Capture-Frametimes.py') + '"'),
    '--pid',$game.Id,'--output-directory',('"' + $outputDir + '"')
) -RedirectStandardOutput (Join-Path $outputDir 'frames.stdout.log') -RedirectStandardError (Join-Path $outputDir 'frames.stderr.log') -PassThru
$threads = Start-Process -FilePath $python -WindowStyle Hidden -ArgumentList @(
    '-X','utf8',('"' + (Join-Path $devRoot 'Profile-Rpcs3Threads.py') + '"'),
    '--pid',$game.Id,'--output',('"' + (Join-Path $outputDir 'threads.json') + '"'),'--seconds',$ThreadProfileSeconds,'--interval',2
) -RedirectStandardOutput (Join-Path $outputDir 'threads.stdout.log') -RedirectStandardError (Join-Path $outputDir 'threads.stderr.log') -PassThru

$session = [ordered]@{
    variant = $Variant; run = $Run; executable = $exe; sha256 = $variants[$Variant].sha
    game_process_id = $game.Id; started = (Get-Date).ToString('o'); diagnostics_env = $(if ($RsxDiagnostics) { 'RPCS3_RSX_DIAGNOSTICS=1' } else { 'none' })
    config_override = $ConfigOverride; config_sha256 = $configSha; label = $Label
    affinity_mask = $(if ($AffinityMask) { '0x{0:X}' -f $AffinityMask } else { 'default' })
    extra_env = $ExtraEnv
    collector_process_id = $collector.Id; frame_collector_process_id = $frames.Id; thread_profiler_process_id = $threads.Id
    output_directory = $outputDir; route_windows = @()
}
$session | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputDir 'session.json') -Encoding utf8
[pscustomobject]$session
