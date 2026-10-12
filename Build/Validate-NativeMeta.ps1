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
function Get-RiftMetaPins([string]$Base, [string[]]$Paths) {
    @($Paths | Sort-Object -Unique | ForEach-Object {
        if (!(Test-Path -LiteralPath $_ -PathType Leaf)) { throw "Native Meta provenance input is missing: $_" }
        [ordered]@{path=[IO.Path]::GetRelativePath($Base,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length}
    })
}
function Get-RiftMetaSourcePaths {
    @(Get-ChildItem -LiteralPath (Join-Path $riftMetaProject 'Source'),(Join-Path $riftMetaProject 'Config') -File -Recurse | ForEach-Object FullName) +
        @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Assets/Data/card_manifest.json','Build/generate_card_manifest.py','Build/Validate-NativeMeta.ps1','Build/Tests/Audit-NativeMeta.py' | ForEach-Object { Join-Path $riftMetaRepo $_ })
}
function Get-RiftMetaRuntimePaths([string]$Project) {
    @(Get-ChildItem -LiteralPath (Join-Path $Project 'Binaries'),(Join-Path $Project 'Config'),(Join-Path $Project 'Content') -File -Recurse | ForEach-Object FullName) +
        @((Join-Path $Project 'RiftCrownArena.uproject'))
}
function Test-RiftMetaPinsEqual($Expected, $Actual) {
    ($Expected | ConvertTo-Json -Depth 5 -Compress) -eq ($Actual | ConvertTo-Json -Depth 5 -Compress)
}
$riftMetaSourcePins = @(Get-RiftMetaPins $riftMetaRepo (Get-RiftMetaSourcePaths))
$riftMetaOriginalRuntimePins = @(Get-RiftMetaPins $riftMetaProject (Get-RiftMetaRuntimePaths $riftMetaProject))
$riftMetaContext = [ordered]@{schema=1;requestedGames=$Games;startedUtc=[DateTime]::UtcNow.ToString('o');
    sourcePins=$riftMetaSourcePins;originalRuntimePins=$riftMetaOriginalRuntimePins;runtimeSnapshotPins=@();
    sourcePinsUnchangedAtCopy=$false;originalRuntimePinsUnchangedAtCopy=$false;runtimeSnapshotMatchesOriginal=$false;
    sourcePinsUnchanged=$false;runtimeSnapshotPinsUnchanged=$false;completed=$false;
    editorExecutable=$riftMetaEditor;editorExecutableSha256=(Get-FileHash -LiteralPath $riftMetaEditor -Algorithm SHA256).Hash.ToLowerInvariant();
    isolatedSaveRoot=$riftMetaSaves;isolatedEngineUserRoot=(Join-Path $riftMetaSaves 'EngineUser')}
$riftMetaContextPath = Join-Path $riftMetaRoot 'context.json'
function Write-RiftMetaContext {
    [IO.File]::WriteAllText($riftMetaContextPath,($riftMetaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
}
Write-RiftMetaContext
# Freeze the built runtime so renderer/build work can continue without DLL locks.
Copy-Item -LiteralPath (Join-Path $riftMetaProject 'RiftCrownArena.uproject') -Destination $riftMetaSnapshot
foreach ($riftMetaDirectory in @('Binaries','Config','Content')) {
    $riftMetaSource = Join-Path $riftMetaProject $riftMetaDirectory
    if (!(Test-Path -LiteralPath $riftMetaSource)) { throw "Native runtime input missing: $riftMetaDirectory" }
    Copy-Item -LiteralPath $riftMetaSource -Destination $riftMetaSnapshot -Recurse
}
$riftMetaModule = Join-Path $riftMetaSnapshot 'Binaries\Win64\UnrealEditor-RiftCrownArena.dll'
if (!(Test-Path -LiteralPath $riftMetaModule)) { throw 'Build the actual Editor runtime before validation.' }
if (!(Test-Path -LiteralPath (Join-Path $riftMetaSnapshot 'Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll') -PathType Leaf)) { throw 'Build the actual Editor-only module before validation.' }
$riftMetaSnapshotPins = @(Get-RiftMetaPins $riftMetaSnapshot (Get-RiftMetaRuntimePaths $riftMetaSnapshot))
$riftMetaContext.runtimeSnapshotPins = $riftMetaSnapshotPins
$riftMetaContext.sourcePinsUnchangedAtCopy = Test-RiftMetaPinsEqual $riftMetaSourcePins @(Get-RiftMetaPins $riftMetaRepo (Get-RiftMetaSourcePaths))
$riftMetaContext.originalRuntimePinsUnchangedAtCopy = Test-RiftMetaPinsEqual $riftMetaOriginalRuntimePins @(Get-RiftMetaPins $riftMetaProject (Get-RiftMetaRuntimePaths $riftMetaProject))
$riftMetaContext.runtimeSnapshotMatchesOriginal = Test-RiftMetaPinsEqual $riftMetaOriginalRuntimePins $riftMetaSnapshotPins
$riftMetaContext.runtimeSha256 = (Get-FileHash -LiteralPath $riftMetaModule -Algorithm SHA256).Hash.ToLowerInvariant()
# Preserve the historical field while pinning the complete source tree separately.
$riftMetaContext.simulationSources = @($riftMetaSourcePins | Where-Object { $_.path -match '^Unreal/RiftCrownArena/Source/RiftCrownArena/(Private|Public)/Simulation/' })
Write-RiftMetaContext
if (!$riftMetaContext.sourcePinsUnchangedAtCopy -or !$riftMetaContext.originalRuntimePinsUnchangedAtCopy -or !$riftMetaContext.runtimeSnapshotMatchesOriginal) {
    throw 'Source/runtime inputs changed while the native Meta project was copied; rebuild and use a fresh QA run.'
}
$riftMetaLog = Join-Path $riftMetaRoot 'engine.log'
& $riftMetaEditor (Join-Path $riftMetaSnapshot 'RiftCrownArena.uproject') /Game/Rift/Maps/Arena '-game' "-RiftMetaValidate=$Games" "-RiftSaveRoot=$riftMetaSaves" "-UserDir=$(Join-Path $riftMetaSaves 'EngineUser')" '-unattended' '-NullRHI' '-nosplash' '-NoSound' '-nop4' "-abslog=$riftMetaLog" *> (Join-Path $riftMetaRoot 'commandlet-output.log')
$riftMetaContext.commandletExitCode = $LASTEXITCODE
$riftMetaContext.completedUtc = [DateTime]::UtcNow.ToString('o')
try {
    $riftMetaContext.sourcePinsUnchanged = Test-RiftMetaPinsEqual $riftMetaSourcePins @(Get-RiftMetaPins $riftMetaRepo (Get-RiftMetaSourcePaths))
    $riftMetaContext.runtimeSnapshotPinsUnchanged = Test-RiftMetaPinsEqual $riftMetaSnapshotPins @(Get-RiftMetaPins $riftMetaSnapshot (Get-RiftMetaRuntimePaths $riftMetaSnapshot))
    $riftMetaContext.editorExecutableUnchanged = (Get-FileHash -LiteralPath $riftMetaEditor -Algorithm SHA256).Hash.ToLowerInvariant() -eq $riftMetaContext.editorExecutableSha256
} catch {
    $riftMetaContext.provenanceError = $_.Exception.Message
}
Write-RiftMetaContext
if ($riftMetaContext.commandletExitCode -ne 0) { throw "Native Meta process failed with exit $($riftMetaContext.commandletExitCode). Inspect $riftMetaRoot" }
if (!$riftMetaContext.sourcePinsUnchanged -or !$riftMetaContext.runtimeSnapshotPinsUnchanged -or !$riftMetaContext.editorExecutableUnchanged) {
    throw 'Original source or cloned runtime inputs changed while native Meta ran; rebuild and rerun with a fresh QA name.'
}
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
$riftMetaContext.completed = $true
Write-RiftMetaContext
$riftMetaResult = [ordered]@{passed=$true;games=$Games;dataset=$riftMetaDataset.FullName;datasetSha256=(Get-FileHash -LiteralPath $riftMetaDataset.FullName -Algorithm SHA256).Hash.ToLowerInvariant();export=$riftMetaExport;runtimeSha256=$riftMetaContext.runtimeSha256;completedUtc=[DateTime]::UtcNow.ToString('o');
    sourcePinsUnchanged=$riftMetaContext.sourcePinsUnchanged;runtimeSnapshotPinsUnchanged=$riftMetaContext.runtimeSnapshotPinsUnchanged;
    provenanceContext=$riftMetaContextPath;provenanceContextSha256=(Get-FileHash -LiteralPath $riftMetaContextPath -Algorithm SHA256).Hash.ToLowerInvariant();
    independentAggregateAudit='Run the maintained independent native Meta audit separately.'}
[IO.File]::WriteAllText((Join-Path $riftMetaRoot 'run.json'),($riftMetaResult | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output ($riftMetaResult | ConvertTo-Json -Depth 12)
