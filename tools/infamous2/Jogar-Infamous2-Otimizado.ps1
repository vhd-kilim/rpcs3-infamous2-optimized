# Abre o inFAMOUS 2 com a Turbo v3 + busca direta de view (primeira rodada positiva), sem diagnosticos nem coletores.
# Faz backup verificado dos saves antes. Nao altera a config normal do RPCS3.
# Uso: powershell -ExecutionPolicy Bypass -File C:\Users\T-GAMER\rpcs3-dev-tools\Jogar-Infamous2-Otimizado.ps1
$ErrorActionPreference = 'Stop'
$binRoot = 'C:\Users\T-GAMER\rpcs3\bin'
$exe = "$binRoot\rpcs3-view-lookup-v1.exe"
$sha = '532E806D413727DB05BC24D648A5B75D2DA15D9B51D2F195B964B56AFE1D9DCA'
$config = 'C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\config-tests\turbo-20261024\turbo3.yml'
$iso = 'C:\Users\T-GAMER\OneDrive\Desktop\inFamous 2 (USA) (EnFrEsPt) (v02.00)\inFamous 2 (USA) (En,Fr,Es,Pt) (v02.00).iso'

if (@(Get-Process -Name 'rpcs3*' -ErrorAction SilentlyContinue).Count) { throw 'O RPCS3 ja esta aberto.' }
if (-not (Test-Path -LiteralPath $exe)) { Copy-Item "$binRoot\rpcs3.exe" $exe }
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne $sha) { throw "Executavel inesperado: $exe" }

$backup = "C:\Users\T-GAMER\rpcs3-dev-tools\backups\saves-jogo-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
& 'C:\Users\T-GAMER\rpcs3-dev-tools\experiments\infamous2-analysis\tools\Backup-Rpcs3Saves.ps1' -HddRoot "$binRoot\portable\dev_hdd0" -DestinationDirectory $backup | Out-Null

foreach ($name in 'RPCS3_RSX_DIAGNOSTICS','RPCS3_INFAMOUS2_LIGHT_PROFILE','RPCS3_INFAMOUS2_CAPTURE') { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }
[Environment]::SetEnvironmentVariable('RPCS3_INFAMOUS2_RELAXED', 'postfx,light,edgepost,vis,spudraw,mesh,fx', 'Process')
[Environment]::SetEnvironmentVariable('RPCS3_OFFLOAD_SPIN_US', '50', 'Process')
[Environment]::SetEnvironmentVariable('RPCS3_HYBRID_AFFINITY', '1', 'Process')
[Environment]::SetEnvironmentVariable('RPCS3_INFAMOUS2_RSX_FLUSH_WAIT', '0', 'Process')

Start-Process -FilePath $exe -ArgumentList @('--config', ('"' + $config + '"'), ('"' + $iso + '"')) -WorkingDirectory $binRoot | Out-Null
"inFAMOUS 2 iniciado (Turbo v3 + view lookup). Backup dos saves: $backup"
