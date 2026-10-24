# Reusable offline backup. No launch, cleanup, restore or deletion is performed.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$HddRoot,
    [Parameter(Mandatory)][string]$DestinationDirectory,
    [ValidatePattern('^[A-Z]{4}[0-9]{5}$')][string]$TitleId = 'BCUS98125'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (@(Get-Process -Name 'rpcs3*' -ErrorAction SilentlyContinue).Count) {
    throw 'Feche o RPCS3 antes de preservar os saves.'
}
$saveRoot = (Resolve-Path -LiteralPath $HddRoot).ProviderPath.TrimEnd('\','/')
$backupRoot = [IO.Path]::GetFullPath($DestinationDirectory).TrimEnd('\','/')
if ($backupRoot.Equals($saveRoot,[StringComparison]::OrdinalIgnoreCase) -or
    $backupRoot.StartsWith($saveRoot + '\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'O destino do backup deve estar fora de dev_hdd0.'
}
if (Test-Path -LiteralPath $backupRoot) { throw 'O destino do backup ja existe; use uma pasta nova.' }
if ((Get-Item -LiteralPath $saveRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'dev_hdd0 e um link; resolva e confira o local real antes do backup.'
}
$saveParents = [Collections.Generic.List[string]]::new()
$homeRoot = Join-Path $saveRoot 'home'
if (Test-Path -LiteralPath $homeRoot) {
    if ((Get-Item -LiteralPath $homeRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'home e um link.' }
    foreach ($userDir in Get-ChildItem -LiteralPath $homeRoot -Directory -Force) {
        if ($userDir.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Diretorio de usuario e um link.' }
        $candidate = Join-Path $userDir.FullName 'savedata'
        if (Test-Path -LiteralPath $candidate -PathType Container) { $saveParents.Add($candidate) }
    }
}
$legacy = Join-Path $saveRoot 'savedata'
if (Test-Path -LiteralPath $legacy -PathType Container) { $saveParents.Add($legacy) }
$roots = [Collections.Generic.List[string]]::new()
foreach ($saveParent in $saveParents) {
    if ((Get-Item -LiteralPath $saveParent).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'savedata e um link.' }
    foreach ($titleDir in Get-ChildItem -LiteralPath $saveParent -Directory -Force) {
        if ($titleDir.Name.StartsWith($TitleId,[StringComparison]::OrdinalIgnoreCase)) { $roots.Add($titleDir.FullName) }
    }
}
if (-not $roots.Count) { throw 'Nenhuma pasta de saves encontrada; nao liberar execucao.' }
$sourceFiles = [Collections.Generic.List[object]]::new()
$sourceDirs = [Collections.Generic.List[object]]::new()
foreach ($root in $roots) {
    $rootItem = Get-Item -LiteralPath $root
    if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Pasta de saves e um link.' }
    $sourceDirs.Add($rootItem)
    # Inspect one level at a time; never traverse a directory link before rejecting it.
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue($root)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Link encontrado dentro dos saves.' }
            if ($item.PSIsContainer) { $sourceDirs.Add($item); $pending.Enqueue($item.FullName) }
            else { $sourceFiles.Add($item) }
            if ($sourceFiles.Count + $sourceDirs.Count -gt 200000) { throw 'Inventario de saves excedeu limite de 200000 itens.' }
        }
    }
}
if (-not $sourceFiles.Count) { throw 'As pastas de saves estao vazias; nao liberar execucao.' }
New-Item -ItemType Directory -Path $backupRoot | Out-Null
foreach ($dir in $sourceDirs) {
    $relative = $dir.FullName.Substring($saveRoot.Length).TrimStart('\','/')
    $target = Join-Path $backupRoot $relative
    if (-not (Test-Path -LiteralPath $target)) { New-Item -ItemType Directory -Path $target | Out-Null }
}
$manifest = [Collections.Generic.List[object]]::new()
[long]$totalBytes = 0
foreach ($item in $sourceFiles) {
    $relative = $item.FullName.Substring($saveRoot.Length).TrimStart('\','/')
    $target = Join-Path $backupRoot $relative
    $before = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    Copy-Item -LiteralPath $item.FullName -Destination $target
    $after = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $copied = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($before -ne $after -or $before -ne $copied) { throw ('Backup nao corresponde aos bytes atuais: ' + $relative) }
    $length = (Get-Item -LiteralPath $item.FullName).Length
    if ($length -ne (Get-Item -LiteralPath $target).Length) { throw ('Tamanho divergente no backup: ' + $relative) }
    $totalBytes += $length
    $manifest.Add([ordered]@{ relative_path=$relative; bytes=$length; sha256=$before; source_after_sha256=$after; backup_sha256=$copied })
}
# Verify every original file again, so changing bytes block launch.
foreach ($item in $manifest) {
    if ((Get-FileHash -LiteralPath (Join-Path $saveRoot $item.relative_path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.sha256) {
        throw ('Save mudou durante o backup: ' + $item.relative_path)
    }
}
$currentPaths = [Collections.Generic.List[string]]::new()
foreach ($root in $roots) {
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue($root)
    while ($pending.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Link apareceu durante o backup.' }
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
            else { $currentPaths.Add($item.FullName.Substring($saveRoot.Length).TrimStart('\','/')) }
            if ($currentPaths.Count -gt 200000) { throw 'Inventario final excedeu limite.' }
        }
    }
}
if (@(Compare-Object -ReferenceObject @($manifest | ForEach-Object { $_.relative_path }) -DifferenceObject $currentPaths.ToArray()).Count) {
    throw 'A lista de arquivos mudou durante o backup.'
}
$summary = [ordered]@{
    verified=$true; created=[DateTimeOffset]::Now.ToString('o'); source_hdd_root=$saveRoot
    backup_directory=$backupRoot; files=$manifest.Count; bytes=$totalBytes
    title_id=$TitleId; scope='Title-prefix savedata folders, all users; original directory structure and empty subdirectories preserved'
    manifest=(Join-Path $backupRoot 'manifest.json'); entries=$manifest.ToArray()
}
$summary | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $summary.manifest -Encoding utf8
[pscustomobject]@{ verified=$true; manifest=$summary.manifest; files=$manifest.Count; bytes=$totalBytes; source_hdd_root=$saveRoot; backup_directory=$backupRoot }
