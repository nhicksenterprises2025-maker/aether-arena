param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(1,1000000)][int]$Games = 10000,
    [string]$Name = ''
)
$ErrorActionPreference = 'Stop'
$riftMetaRepo = Split-Path -Parent $PSScriptRoot
$riftMetaProject = Join-Path $riftMetaRepo 'Unreal\RiftCrownArena'
$riftMetaEditor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $riftMetaEditor)) { throw 'Installed Unreal commandlet not found.' }
if (!$Name) { $Name = 'native-meta-' + $Games + '-' + [Guid]::NewGuid().ToString('N') }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Use a lowercase alphanumeric QA name.' }
$riftMetaRoot = Join-Path $riftMetaRepo ('Artifacts\QA\' + $Name)
if (Test-Path -LiteralPath $riftMetaRoot) { throw 'Use a fresh QA name; existing evidence is preserved.' }
$riftMetaSnapshot = Join-Path $riftMetaRoot 'RuntimeProject'
$riftMetaSaves = Join-Path $riftMetaRoot 'UserData'
New-Item -ItemType Directory -Path $riftMetaSnapshot,$riftMetaSaves -Force | Out-Null
# Freeze the built runtime so renderer/build work can continue without DLL locks.
Copy-Item -LiteralPath (Join-Path $riftMetaProject 'RiftCrownArena.uproject') -Destination $riftMetaSnapshot
foreach ($riftMetaDirectory in @('Binaries','Config','Content')) {
    $riftMetaSource = Join-Path $riftMetaProject $riftMetaDirectory
    if (!(Test-Path -LiteralPath $riftMetaSource)) { throw "Native runtime input missing: $riftMetaDirectory" }
    Copy-Item -LiteralPath $riftMetaSource -Destination $riftMetaSnapshot -Recurse
}
$riftMetaModule = Join-Path $riftMetaSnapshot 'Binaries\Win64\UnrealEditor-RiftCrownArena.dll'
if (!(Test-Path -LiteralPath $riftMetaModule)) { throw 'Build the actual Editor runtime before validation.' }
$riftMetaSources = @(Get-ChildItem -LiteralPath @((Join-Path $riftMetaProject 'Source\RiftCrownArena\Private\Simulation'),(Join-Path $riftMetaProject 'Source\RiftCrownArena\Public\Simulation')) -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($riftMetaRepo,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$riftMetaContext = [ordered]@{schema=1;requestedGames=$Games;startedUtc=[DateTime]::UtcNow.ToString('o');runtimeSha256=(Get-FileHash -LiteralPath $riftMetaModule -Algorithm SHA256).Hash.ToLowerInvariant();simulationSources=$riftMetaSources;isolatedSaveRoot=$riftMetaSaves;isolatedEngineUserRoot=(Join-Path $riftMetaSaves 'EngineUser')}
[IO.File]::WriteAllText((Join-Path $riftMetaRoot 'context.json'),($riftMetaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
$riftMetaLog = Join-Path $riftMetaRoot 'engine.log'
& $riftMetaEditor (Join-Path $riftMetaSnapshot 'RiftCrownArena.uproject') /Game/Rift/Maps/Arena '-game' "-RiftMetaValidate=$Games" "-RiftSaveRoot=$riftMetaSaves" "-UserDir=$(Join-Path $riftMetaSaves 'EngineUser')" '-unattended' '-NullRHI' '-nosplash' '-NoSound' '-nop4' "-abslog=$riftMetaLog" *> (Join-Path $riftMetaRoot 'commandlet-output.log')
if ($LASTEXITCODE -ne 0) { throw "Native Meta process failed with exit $LASTEXITCODE. Inspect $riftMetaRoot" }
$riftMetaExport = Join-Path $riftMetaSaves 'meta-validation.json'
if (!(Test-Path -LiteralPath $riftMetaExport)) { throw 'Native process did not export its completed report.' }
$riftMetaDatasetFiles = @(Get-ChildItem -LiteralPath (Join-Path $riftMetaSaves 'Meta') -Filter '*.json' -File)
$riftMetaDataset = $null
foreach ($riftMetaFile in $riftMetaDatasetFiles) {
    $riftMetaCandidate = Get-Content -LiteralPath $riftMetaFile.FullName -Raw | ConvertFrom-Json -Depth 100
    if ($riftMetaCandidate.games -eq $Games) { $riftMetaDataset = $riftMetaFile; break }
}
if (!$riftMetaDataset) { throw 'Completed dataset does not contain the exact requested match count.' }
$riftMetaData = Get-Content -LiteralPath $riftMetaDataset.FullName -Raw | ConvertFrom-Json -Depth 100
if ($riftMetaData.invalid -ne 0 -or $riftMetaData.economyInvalid -ne 0) { throw 'Native matches reported invalid state or economy accounting.' }
$riftMetaResult = [ordered]@{passed=$true;games=$Games;dataset=$riftMetaDataset.FullName;datasetSha256=(Get-FileHash -LiteralPath $riftMetaDataset.FullName -Algorithm SHA256).Hash.ToLowerInvariant();export=$riftMetaExport;runtimeSha256=$riftMetaContext.runtimeSha256;completedUtc=[DateTime]::UtcNow.ToString('o');independentAggregateAudit='Run the maintained independent native Meta audit separately.'}
[IO.File]::WriteAllText((Join-Path $riftMetaRoot 'run.json'),($riftMetaResult | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output ($riftMetaResult | ConvertTo-Json -Depth 12)
