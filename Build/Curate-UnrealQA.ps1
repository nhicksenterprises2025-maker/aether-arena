param(
    [Parameter(Mandatory=$true)][string]$Report,
    [string]$Version = '1.1.0',
    [string]$Output = 'Docs/QA/native-integration.json'
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Run with PowerShell 7.' }
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'Use a numeric release version.' }
$riftCurateRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Resolve-RiftCuratePath([string]$Path) {
    $riftResolved = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $riftCurateRoot $Path }))
    if (!$riftResolved.StartsWith($riftCurateRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'QA report inputs and outputs must remain in the workspace.' }
    return $riftResolved
}
$riftReportPath = Resolve-RiftCuratePath $Report
$riftReportRelative = [IO.Path]::GetRelativePath($riftCurateRoot,$riftReportPath).Replace('\','/')
if ($riftReportRelative -notmatch '^Build/Automation/\d{8}-\d{6}/Report/index\.json$') { throw 'Choose a real timestamped Test-Unreal automation report.' }
$riftRunRoot = Split-Path -Parent (Split-Path -Parent $riftReportPath)
$riftContextPath = Join-Path $riftRunRoot 'context.json'
$riftReport = Get-Content -LiteralPath $riftReportPath -Raw | ConvertFrom-Json -Depth 100
$riftContext = Get-Content -LiteralPath $riftContextPath -Raw | ConvertFrom-Json -Depth 100
if ($riftContext.schema -ne 2 -or !$riftContext.completed -or !$riftContext.sourcesUnchanged -or !$riftContext.runtimeModulesUnchanged -or
    $riftContext.commandletExitCode -ne 0 -or $riftContext.wrapperExitCode -ne 0 -or $riftContext.report -ne $riftReportRelative -or
    !$riftContext.isolation.automationSandbox -or $riftContext.isolation.saveRoot -eq $riftContext.isolation.engineUserRoot -or
    $riftContext.sourceReportSha256 -ne (Get-FileHash -LiteralPath $riftReportPath -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'Automation lacks completed launch-time source/module hashes, isolated roots, or successful exit provenance.'
}
$riftRequiredTests = @('Rift.Integration.CardData','Rift.Integration.ConnectedUI','Rift.Integration.FullLengthReplay',
    'Rift.Integration.PausedResultAndReplayEvents','Rift.Integration.ProfilePersistence','Rift.Integration.ReplayTimeline',
    'Rift.Integration.SnapshotRoundtrip','Rift.Meta.AggregationEconomy','Rift.Meta.WorkerPauseAndRecovery')
if ([version]$Version -ge [version]'1.1.0') { $riftRequiredTests += @('Rift.Integration.BattleInputRouting','Rift.Integration.Presentation') }
if ([version]$Version -ge [version]'1.2.0') { $riftRequiredTests += @('Rift.Integration.UnitMotion','Rift.Integration.ProjectilePresentation') }
if ([version]$Version -ge [version]'1.2.1') { $riftRequiredTests += 'Rift.Integration.SpellCastReplay' }
if ([version]$Version -ge [version]'1.3.0') { $riftRequiredTests += 'Rift.Integration.Typography' }
if ([version]$Version -ge [version]'1.3.3') { $riftRequiredTests += 'Rift.Integration.TowerPathing' }
if ([version]$Version -ge [version]'1.3.5') { $riftRequiredTests += 'Rift.Integration.UnitCollision' }
$riftTests = @($riftReport.tests)
$riftNames = @($riftTests.fullTestPath)
if ($riftReport.failed -ne 0 -or $riftReport.notRun -ne 0 -or $riftReport.inProcess -ne 0 -or
    ($riftReport.succeeded + $riftReport.succeededWithWarnings) -ne $riftTests.Count -or $riftTests.Count -lt $riftRequiredTests.Count -or
    @($riftTests | Where-Object { $_.state -ne 'Success' -or $_.errors -ne 0 }).Count -ne 0 -or
    @($riftNames | Sort-Object -Unique).Count -ne $riftNames.Count) { throw 'The complete named automation suite has failures, unrun tests, duplicate names or inconsistent counts.' }
foreach ($riftName in $riftRequiredTests) { if ($riftName -notin $riftNames) { throw "Required automation scenario is missing: $riftName" } }
$riftRequiredSourcePaths = @(
    Get-ChildItem -LiteralPath (Join-Path $riftCurateRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $riftCurateRoot 'Unreal/RiftCrownArena/Config') -File -Recurse |
        Where-Object Extension -in @('.cpp','.h','.cs','.ini') | ForEach-Object { [IO.Path]::GetRelativePath($riftCurateRoot,$_.FullName).Replace('\','/') }
) + @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Build/Test-Unreal.ps1','Build/generate_audio.py',
    'Assets/Source/Audio/audio_manifest.json','Assets/Source/Audio/Palette/sources.json')
$riftRequiredSourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $riftCurateRoot 'Assets/Source/Audio') -File | Where-Object Extension -eq '.wav' | ForEach-Object { [IO.Path]::GetRelativePath($riftCurateRoot,$_.FullName).Replace('\','/') })
if (@($riftContext.sourceHashes.path | Sort-Object -Unique).Count -ne @($riftContext.sourceHashes).Count) { throw 'The launch-time source inventory contains duplicate paths.' }
foreach ($riftSourcePath in $riftRequiredSourcePaths) { if ($riftSourcePath -notin $riftContext.sourceHashes.path) { throw "A current source/audio file was not frozen at automation launch: $riftSourcePath" } }
$riftExpectedModules = @('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll')
if (@($riftContext.runtimeModules).Count -ne 2 -or @($riftContext.runtimeModules.path | Sort-Object -Unique).Count -ne 2) { throw 'The launch-time runtime inventory must identify both native Editor modules exactly once.' }
foreach ($riftModulePath in $riftExpectedModules) { if ($riftModulePath -notin $riftContext.runtimeModules.path) { throw "An actual runtime Editor module is not pinned: $riftModulePath" } }
foreach ($riftHash in @($riftContext.sourceHashes) + @($riftContext.runtimeModules)) {
    $riftHashPath = Resolve-RiftCuratePath $riftHash.path
    if ($riftHash.sha256 -ne (Get-FileHash -LiteralPath $riftHashPath -Algorithm SHA256).Hash.ToLowerInvariant()) { throw "Source/module changed since the successful run: $($riftHash.path). Rebuild and rerun." }
}
$riftLogPath = Resolve-RiftCuratePath $riftContext.commandletLog
$riftLog = Get-Content -LiteralPath $riftLogPath -Raw
if (!$riftLog.Contains("Automation Test Queue Empty $($riftTests.Count) tests performed.") -or
    $riftLog -notmatch 'FPlatformMisc::RequestExitWithStatus\(1, 0') { throw 'Commandlet log lacks the exact successful suite-completion/shutdown markers.' }
foreach ($riftDevice in $riftReport.devices) { $riftDevice.deviceName = 'QA workstation'; $riftDevice.instanceName = 'QA workstation instance' }
$riftPrimaryModule = @($riftContext.runtimeModules | Where-Object path -eq 'Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll')
if ($riftPrimaryModule.Count -ne 1) { throw 'Automation does not identify exactly one primary native Editor module.' }
$riftReport | Add-Member -MemberType NoteProperty -Name curatedEvidence -Value ([ordered]@{
    schema=2;version=$Version;sourceReport=$riftReportRelative;sourceReportSha256=$riftContext.sourceReportSha256;
    context=[IO.Path]::GetRelativePath($riftCurateRoot,$riftContextPath).Replace('\','/');contextSha256=(Get-FileHash -LiteralPath $riftContextPath -Algorithm SHA256).Hash.ToLowerInvariant();
    commandletLog=$riftContext.commandletLog;wrapperExitCode=0;commandletExitCode=0;allScenariosObserved=$true;observedTestCount=$riftTests.Count;requiredTests=$riftRequiredTests;
    runtimeModule=$riftPrimaryModule[0].path;runtimeModuleSha256=$riftPrimaryModule[0].sha256;runtimeModules=$riftContext.runtimeModules;
    isolation='Separate RiftSaveRoot and engine UserDir with RiftAutomationSandbox; source/module hashes frozen before launch and verified after exit.';
    expectedWarning='ProfilePersistence intentionally corrupts its isolated primary save and observes recovery from backup.';sourceHashes=$riftContext.sourceHashes
}) -Force
$riftJson = $riftReport | ConvertTo-Json -Depth 100
$riftJson = $riftJson.Replace($riftCurateRoot.Replace('\','\\') + '\\','').Replace($riftCurateRoot.Replace('\','/') + '/','')
if ($env:USERPROFILE) { $riftJson=$riftJson.Replace($env:USERPROFILE.Replace('\','\\'),'%USERPROFILE%').Replace($env:USERPROFILE.Replace('\','/'),'%USERPROFILE%') }
$null = $riftJson | ConvertFrom-Json -Depth 100
$riftOutputPath = Resolve-RiftCuratePath $Output
New-Item -ItemType Directory -Path (Split-Path -Parent $riftOutputPath) -Force | Out-Null
[IO.File]::WriteAllText($riftOutputPath,$riftJson,[Text.UTF8Encoding]::new($false))
Write-Output ([ordered]@{passed=$true;version=$Version;tests=$riftTests.Count;sourceHashes=@($riftContext.sourceHashes).Count;runtimeModules=@($riftContext.runtimeModules).Count;report=$Output;sourceReport=$riftReportRelative} | ConvertTo-Json)
