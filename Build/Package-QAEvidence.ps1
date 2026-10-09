param(
    [string]$Version = '1.1.0',
    [string]$StageRoot = '',
    [switch]$Finalize,
    [string]$ShippingAudioRoot = '',
    [string]$ShippingVFXReport = '',
    [string]$EditorAudioRoot = 'Artifacts/QA/Audio/native-audio-postmix64',
    [string]$PresentationReview = 'Docs/QA/presentation-review.json',
    [string[]]$ShippingCaptureNames = @(),
    [string[]]$PerformanceReports = @(),
    [string[]]$PerformanceLogs = @(),
    [string]$InstallerReport = 'Artifacts/QA/installer-tests.json',
    [string]$ReleaseVerification = 'Artifacts/Release/windows-release-verification.json'
)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Run this evidence packager with PowerShell 7.' }
if ($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') { throw 'Use a numeric release version.' }
$qaPolishRelease = [version]$Version -ge [version]'1.1.0'
$qaRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Assert-QANoLinks([string]$Path) {
    $qaLinkPath = [IO.Path]::GetFullPath($Path)
    while ($qaLinkPath -and $qaLinkPath.StartsWith($qaRepo,[StringComparison]::OrdinalIgnoreCase)) {
        if ((Test-Path -LiteralPath $qaLinkPath) -and
            ((Get-Item -LiteralPath $qaLinkPath).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Evidence paths must not contain file or directory links.'
        }
        if ($qaLinkPath -eq $qaRepo) { break }
        $qaLinkPath = [IO.Path]::GetDirectoryName($qaLinkPath)
    }
}
$qaRelease = Join-Path $qaRepo 'Artifacts/Release'
Assert-QANoLinks $qaRelease
New-Item -ItemType Directory -Path $qaRelease -Force | Out-Null
if (!$StageRoot) { $StageRoot = Join-Path $qaRelease ('QAEvidence-stage-' + [Guid]::NewGuid().ToString('N')) }
elseif (![IO.Path]::IsPathRooted($StageRoot)) { $StageRoot = Join-Path $qaRepo $StageRoot }
$qaStage = [IO.Path]::GetFullPath($StageRoot)
if (!$qaStage.StartsWith($qaRelease + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($qaStage) -notmatch '^QAEvidence-stage-[a-f0-9]{32}$') {
    throw 'Evidence staging must be a uniquely named QAEvidence-stage-* directory inside Artifacts/Release.'
}
$qaMarker = Join-Path $qaStage '.qa-evidence-stage.json'
Assert-QANoLinks $qaStage
if (Test-Path -LiteralPath $qaStage) {
    if (!(Test-Path -LiteralPath $qaMarker -PathType Leaf)) { throw 'Refusing an unowned staging directory.' }
    $qaPrevious = Get-Content -LiteralPath $qaMarker -Raw | ConvertFrom-Json
    if ($qaPrevious.schema -ne 1 -or $qaPrevious.version -ne $Version) { throw 'The existing evidence stage belongs to another version or schema.' }
} else {
    New-Item -ItemType Directory -Path $qaStage | Out-Null
    [IO.File]::WriteAllText($qaMarker,([ordered]@{schema=1;version=$Version;createdUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
}
$qaSelections = [Collections.Generic.List[object]]::new()
function Resolve-QASource([string]$Path) {
    $qaResolved = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $qaRepo $Path }))
    if (!$qaResolved.StartsWith($qaRepo + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Evidence inputs must remain inside the repository workspace.'
    }
    if (!(Test-Path -LiteralPath $qaResolved -PathType Leaf)) { throw "Selected evidence is missing: $Path" }
    $qaRelative = [IO.Path]::GetRelativePath($qaRepo,$qaResolved).Replace('\','/')
    if ($qaRelative -match '(^|/)(AutomationFixtures|Replays|Saves|Backups|EngineUserData|Saved)(/|$)' -or
        [IO.Path]::GetFileName($qaResolved) -match '^(player_save|ue_save)\.json(\.|$)') {
        throw 'Player profiles, saves, replays and private fixtures are excluded from evidence packaging.'
    }
    Assert-QANoLinks $qaResolved
    return $qaResolved
}
function Read-QAJson([string]$Path) {
    return Get-Content -LiteralPath (Resolve-QASource $Path) -Raw | ConvertFrom-Json -Depth 100
}
function Assert-QAFrostProof([object]$State) {
    $qaFrostClips = @($State.niagara.unitAnimations | Where-Object assetId -eq 'frost_fang')
    $qaFrostPuffs = @($State.niagara.components | Where-Object system -eq '/Game/Rift/VFX/NS_RiftFrost.NS_RiftFrost')
    if (!$State.niagara.breathSmoke -or $State.niagara.frostBreathPuffs -ne 2 -or
        $State.niagara.totalParticles -ne 16 -or $qaFrostClips.Count -ne 2 -or $qaFrostPuffs.Count -ne 2) {
        throw 'The actual Frost Breath binding/particle proof is incomplete.'
    }
    foreach ($qaFrostClip in $qaFrostClips) {
        if ($qaFrostClip.clip -ne 'Breath' -or
            $qaFrostClip.animationAsset -ne '/Game/Rift/Characters/frost_fang/Animations/AN_frost_fang_Breath.AN_frost_fang_Breath' -or
            ![double]::IsFinite($qaFrostClip.positionSeconds) -or $qaFrostClip.positionSeconds -le 0) {
            throw 'Frost Fang did not select and advance the actual imported Breath clip.'
        }
    }
    foreach ($qaFrostPuff in $qaFrostPuffs) {
        if (!$qaFrostPuff.ready -or !$qaFrostPuff.valid -or !$qaFrostPuff.active -or $qaFrostPuff.complete -or
            ($qaFrostPuff.emitters | Measure-Object particles -Sum).Sum -ne 8) {
            throw 'The Frost Breath mouth puff did not retain its bounded live particle recipe.'
        }
    }
}
function Assert-QAShippingBinary([string]$Executable,[object]$Manifest,[object]$NativeFile) {
    $qaExecutablePath = Resolve-QASource $Executable
    $qaExecutableEntries = @($Manifest.files | Where-Object {
        $_.path -match '\.exe$' -and $qaExecutablePath.EndsWith(('\' + $_.path.Replace('/','\')),[StringComparison]::OrdinalIgnoreCase)
    })
    if ($qaExecutableEntries.Count -ne 1 -or
        $qaExecutableEntries[0].path -notin @('RiftCrownArena.exe',$NativeFile.path)) {
        throw 'The requested Shipping executable is not the manifest bootstrap or native game.'
    }
    $qaExecutableHash = (Get-FileHash -LiteralPath $qaExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($qaExecutableHash -ne $qaExecutableEntries[0].sha256) { throw 'The requested Shipping executable differs from its manifest bytes.' }
    $qaPayloadRoot = $qaExecutablePath.Substring(0,$qaExecutablePath.Length - $qaExecutableEntries[0].path.Length)
    $qaNativePath = Resolve-QASource (Join-Path $qaPayloadRoot $NativeFile.path)
    $qaNativeHash = (Get-FileHash -LiteralPath $qaNativePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($qaNativeHash -ne $NativeFile.sha256) { throw 'The requested Shipping package contains another native game executable.' }
    return [ordered]@{requestedSha256=$qaExecutableHash;requestedManifestEntry=$qaExecutableEntries[0].path;nativeSha256=$qaNativeHash}
}
function Select-QAEvidence([string]$Source,[string]$Entry,[string]$Kind = 'copy') {
    if ($Entry -match '(^|/)\.\.(/|$)|^[A-Za-z]:|^/' -or $Entry.Contains('\')) { throw 'Invalid archive entry.' }
    $qaSelections.Add([ordered]@{source=(Resolve-QASource $Source);entry=$Entry;kind=$Kind})
}
function Sanitize-QAJson([string]$Text) {
    $qaEscapedRoot = $qaRepo.Replace('\','\\')
    $qaForwardRoot = $qaRepo.Replace('\','/')
    $Text = $Text.Replace($qaEscapedRoot + '\\','').Replace($qaForwardRoot + '/','').Replace($qaRepo + '\','')
    $Text = $Text.Replace($qaEscapedRoot,'.').Replace($qaForwardRoot,'.').Replace($qaRepo,'.')
    if ($env:USERPROFILE) {
        $Text = $Text.Replace($env:USERPROFILE.Replace('\','\\'),'%USERPROFILE%').Replace($env:USERPROFILE.Replace('\','/'),'%USERPROFILE%')
    }
    $Text = [regex]::Replace($Text,'("deviceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation"')
    $Text = [regex]::Replace($Text,'("instanceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation instance"')
    $null = $Text | ConvertFrom-Json -Depth 100
    return $Text
}

$qaMetaRoot = 'Artifacts/QA/native-meta10000-final-core'
$qaDataset = $qaMetaRoot + '/UserData/Meta/78EF7F0B46E9905DF6EE9F84810BD043.json'
$qaContext = Read-QAJson ($qaMetaRoot + '/context.json')
$qaValidity = Read-QAJson 'Docs/QA/native-meta-final-validity.json'
if (!$qaValidity.passed -or !$qaValidity.completeObserved -or $qaValidity.observedGames -ne 10000 -or
    $qaValidity.failedChecks -ne 0 -or (Get-FileHash -LiteralPath (Resolve-QASource $qaDataset) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qaValidity.evidence.datasetSha256) {
    throw 'The selected final cohort does not match its completed independent validity report.'
}
foreach ($qaSource in $qaContext.simulationSources) {
    if ((Get-FileHash -LiteralPath (Resolve-QASource $qaSource.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qaSource.sha256) {
        throw 'The production simulation source changed after the final cohort was frozen.'
    }
}
Select-QAEvidence $qaDataset 'Meta/final-dataset.json'
Select-QAEvidence ($qaMetaRoot + '/UserData/meta-validation.json') 'Meta/final-export.json'
Select-QAEvidence ($qaMetaRoot + '/context.json') 'Meta/runtime-context.json' 'json'
Select-QAEvidence ($qaMetaRoot + '/run.json') 'Meta/completion.json' 'json'
Select-QAEvidence ($qaMetaRoot + '/engine.log') 'Meta/completion-excerpt.log' 'meta-log-excerpt'
Select-QAEvidence 'Docs/QA/native-meta-final-validity.json' 'Meta/validity.json' 'json'
Select-QAEvidence 'Docs/QA/native-meta-final-comparison.json' 'Meta/historical-comparison.json' 'json'
Select-QAEvidence 'Docs/QA/bank-normalization-witness.json' 'Meta/bank-normalization-witness.json' 'json'
Select-QAEvidence 'Artifacts/QA/NativeMeta10000Revision3/Meta/FE17A39B4A6DB64F585824A4C16D43F8.json' 'Meta/historical-telemetry3-dataset.json'

$qaPortableOutput = Get-Content -LiteralPath (Resolve-QASource 'Build/NativeTests/latest-results.txt') -Raw
if ($qaPortableOutput -notmatch 'Native authoritative simulation: 36 scenarios passed\.' -or
    $qaPortableOutput -notmatch 'SOAK 21 complete matches') { throw 'The portable production-core result is incomplete.' }
Select-QAEvidence 'Build/NativeTests/latest-results.txt' 'Native/portable-results.txt'
$qaIntegration = Read-QAJson 'Docs/QA/native-integration.json'
if (($qaPolishRelease -and (!$qaIntegration.curatedEvidence.allScenariosObserved -or $qaIntegration.curatedEvidence.schema -ne 2 -or $qaIntegration.curatedEvidence.version -ne $Version)) -or
    (!$qaPolishRelease -and !$qaIntegration.curatedEvidence.allNineScenariosObserved) -or
    $qaIntegration.curatedEvidence.sourceReport -notmatch '^Build/Automation/\d{8}-\d{6}/Report/index\.json$' -or
    $qaIntegration.curatedEvidence.commandletLog -notmatch '^Build/Automation/\d{8}-\d{6}/UnrealIntegration\.log$' -or
    $qaIntegration.curatedEvidence.wrapperExitCode -ne 0 -or $qaIntegration.curatedEvidence.commandletExitCode -ne 0 -or
    $qaIntegration.curatedEvidence.sourceReportSha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaIntegration.curatedEvidence.sourceReport) -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'Curated integration evidence does not match the completed named release suite.'
}
$qaFinalReport = Read-QAJson $qaIntegration.curatedEvidence.sourceReport
$qaRequiredTests = @('Rift.Integration.CardData','Rift.Integration.ConnectedUI','Rift.Integration.FullLengthReplay',
    'Rift.Integration.PausedResultAndReplayEvents','Rift.Integration.ProfilePersistence','Rift.Integration.ReplayTimeline',
    'Rift.Integration.SnapshotRoundtrip','Rift.Meta.AggregationEconomy','Rift.Meta.WorkerPauseAndRecovery')
if ($qaPolishRelease) { $qaRequiredTests += @('Rift.Integration.BattleInputRouting','Rift.Integration.Presentation') }
$qaActualTests = @($qaFinalReport.tests)
if ($qaFinalReport.failed -ne 0 -or $qaFinalReport.notRun -ne 0 -or $qaFinalReport.inProcess -ne 0 -or
    $qaActualTests.Count -lt $qaRequiredTests.Count -or ($qaFinalReport.succeeded + $qaFinalReport.succeededWithWarnings) -ne $qaActualTests.Count -or
    @($qaActualTests | Where-Object { $_.state -ne 'Success' -or $_.errors -ne 0 }).Count -ne 0 -or
    @($qaActualTests.fullTestPath | Sort-Object -Unique).Count -ne $qaActualTests.Count) { throw 'The complete named release automation did not pass.' }
foreach ($qaRequiredTest in $qaRequiredTests) { if ($qaRequiredTest -notin $qaActualTests.fullTestPath) { throw "A required release test is absent: $qaRequiredTest" } }
$qaAutomationCount = $qaActualTests.Count
if ($qaPolishRelease) {
    $qaIntegrationContext = Read-QAJson $qaIntegration.curatedEvidence.context
    if ($qaIntegrationContext.schema -ne 2 -or !$qaIntegrationContext.completed -or !$qaIntegrationContext.sourcesUnchanged -or !$qaIntegrationContext.runtimeModulesUnchanged -or
        $qaIntegrationContext.wrapperExitCode -ne 0 -or $qaIntegrationContext.commandletExitCode -ne 0 -or !$qaIntegrationContext.isolation.automationSandbox -or
        $qaIntegrationContext.report -ne $qaIntegration.curatedEvidence.sourceReport -or $qaIntegrationContext.sourceReportSha256 -ne $qaIntegration.curatedEvidence.sourceReportSha256 -or
        $qaIntegration.curatedEvidence.observedTestCount -ne $qaAutomationCount -or
        $qaIntegration.curatedEvidence.contextSha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaIntegration.curatedEvidence.context) -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'The release suite lacks matching successful launch-time source/module provenance.'
    }
    foreach ($qaModule in $qaIntegrationContext.runtimeModules) {
        if ($qaModule.sha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaModule.path) -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'An Editor module changed after the release automation.' }
    }
    Select-QAEvidence $qaIntegration.curatedEvidence.context 'Native/automation-context.json' 'json'
}
Select-QAEvidence $qaIntegration.curatedEvidence.sourceReport 'Native/ue-release-tests.json' 'json'
$qaDPIReportPath = 'Build/Automation/20261008-161916/Report/index.json'
$qaDPIReport = Read-QAJson $qaDPIReportPath
if ($qaDPIReport.failed -ne 0 -or $qaDPIReport.notRun -ne 0 -or
    ($qaDPIReport.succeeded + $qaDPIReport.succeededWithWarnings) -ne 1) { throw 'The historical focused UI-DPI correction test did not pass.' }
Select-QAEvidence $qaDPIReportPath 'Native/connected-ui-dpi.json' 'json'
foreach ($qaSource in $qaIntegration.curatedEvidence.sourceHashes) {
    if ($qaSource.sha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaSource.path) -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'A production source changed after the completed release suite.'
    }
}
if ($qaPolishRelease) {
    $qaHashPaths = @($qaIntegration.curatedEvidence.sourceHashes.path)
    $qaRequiredSourcePaths = @(
        Get-ChildItem -LiteralPath (Join-Path $qaRepo 'Unreal/RiftCrownArena/Source'),(Join-Path $qaRepo 'Unreal/RiftCrownArena/Config') -File -Recurse |
            Where-Object Extension -in @('.cpp','.h','.cs','.ini') | ForEach-Object { [IO.Path]::GetRelativePath($qaRepo,$_.FullName).Replace('\','/') }
    ) + @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Build/Test-Unreal.ps1','Build/generate_audio.py',
        'Assets/Source/Audio/audio_manifest.json','Assets/Source/Audio/Palette/sources.json')
    $qaRequiredSourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $qaRepo 'Assets/Source/Audio') -File | Where-Object Extension -eq '.wav' | ForEach-Object { [IO.Path]::GetRelativePath($qaRepo,$_.FullName).Replace('\','/') })
    foreach ($qaSourcePath in $qaRequiredSourcePaths) {
        if ($qaSourcePath -notin $qaHashPaths) { throw "Launch-time source hash is missing for a current module, UI, input or audio source: $qaSourcePath" }
        $qaCuratedHash = @($qaIntegration.curatedEvidence.sourceHashes | Where-Object path -eq $qaSourcePath)
        $qaLaunchHash = @($qaIntegrationContext.sourceHashes | Where-Object path -eq $qaSourcePath)
        if ($qaCuratedHash.Count -ne 1 -or $qaLaunchHash.Count -ne 1 -or $qaCuratedHash[0].sha256 -ne $qaLaunchHash[0].sha256) { throw 'Curated source hashes differ from the actual automation launch.' }
    }
}
Select-QAEvidence 'Docs/QA/native-integration.json' 'Native/curated-integration.json' 'json'
Select-QAEvidence $qaIntegration.curatedEvidence.commandletLog 'Native/commandlet-completion-excerpt.log' 'integration-log-excerpt'
foreach ($qaSourcePath in @('Build/Tests/RiftSimulationTests.cpp','Build/Tests/Run-NativeSimulationTests.ps1',
    'Build/Tests/Audit-NativeMeta.py','Build/Tests/RiftBankNormalizationWitness.cpp','Build/Tests/Compare-BankNormalization.ps1',
    'Build/Tests/README.md','Build/Validate-NativeMeta.ps1','Build/Test-Unreal.ps1','Build/Test-UnrealVFX.ps1','Build/Test-UnrealAudio.ps1',
    'Build/Measure-Unreal.ps1','Build/Capture-Unreal.ps1','Build/Package-QAEvidence.ps1','Build/Curate-UnrealQA.ps1',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Tests/RiftIntegrationTests.cpp') + @($qaContext.simulationSources.path)) {
    Select-QAEvidence $qaSourcePath ('Source/' + $qaSourcePath)
}
if ($qaPolishRelease) {
    foreach ($qaNewSource in @('Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Tests/RiftBattleInputTests.cpp',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Tests/RiftPresentationTests.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftBattleHUD.cpp',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftCollectionUI.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftFieldManual.cpp',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftReplayUI.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftTrainingPanel.cpp',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftUIPrimitives.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/RiftUIPrimitives.h',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftUIWidget.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/RiftUIWidget.h',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftGameMode.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/RiftGameMode.h',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftMatchSubsystem.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/RiftMatchSubsystem.h',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Presentation/RiftBattleAudioSubsystem.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation/RiftBattleAudioSubsystem.h',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Presentation/RiftArenaPresentation.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation/RiftArenaPresentation.h',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Presentation/RiftBattleOverlay.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation/RiftBattleOverlay.h',
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Presentation/RiftTrainingOverlay.cpp','Unreal/RiftCrownArena/Config/DefaultGame.ini',
        'Build/generate_audio.py','Build/fetch_audio_sources.py','Build/import_unreal_audio.py','Build/verify_audio_masters.py','Build/requirements-audio.txt',
        'Assets/Source/Audio/audio_manifest.json','Assets/Source/Audio/Palette/sources.json')) { Select-QAEvidence $qaNewSource ('Source/' + $qaNewSource) }
}
foreach ($qaDoc in @('Docs/NativeMetaQA.md','Docs/NativeSimulationQA.md','Docs/NativePersistenceReplay.md','Docs/QA/README.md')) {
    Select-QAEvidence $qaDoc ('Documentation/' + [IO.Path]::GetFileName($qaDoc))
}
function Select-QAMixedAudioEvidence([string]$Root,[object]$Report,[string]$Label) {
    $qaRequiredMixChecks = @('masterMixRegisteredWithLiveDevice','limiterDetectsIsolatedPeaksWithoutAttackSmoothing',
        'actualMixedPCMHasHeadroom','limiterOverloadPCMDoesNotClip')
    if (@($Report.checks).Count -lt 49 -or @($qaRequiredMixChecks | Where-Object { $_ -notin $Report.checks.name }).Count -ne 0) {
        throw 'Current audio evidence must exercise the registered limiter and actual mixed PCM.'
    }
    $qaRecordingMeasurements = [Collections.Generic.List[object]]::new()
    foreach ($qaRecording in @(@{field='normalMixedOutput';file='mixed-output.wav'},@{field='limiterOverloadOutput';file='limiter-overload.wav'})) {
        $qaRawMix = $Report.($qaRecording.field)
        if (!$qaRawMix -or !$qaRawMix.wavSaved -or $qaRawMix.wavFile -ne $qaRecording.file -or
            $qaRawMix.captureStage -ne 'Live MasterMix output after the submix effect chain; before PCM encoding' -or
            ![double]::IsFinite($qaRawMix.peak) -or $qaRawMix.peak -le .0001 -or $qaRawMix.peak -ge .95 -or
            ![double]::IsFinite($qaRawMix.rms) -or $qaRawMix.rms -le .00001 -or $qaRawMix.rms -gt $qaRawMix.peak -or
            $qaRawMix.clippedFloatSamples -ne 0 -or $qaRawMix.channels -ne 2 -or
            $qaRawMix.sampleRate -lt 8000 -or $qaRawMix.sampleRate -gt 192000 -or
            $qaRawMix.samples -le $qaRawMix.channels*$qaRawMix.sampleRate*.15 -or
            $qaRawMix.samples % $qaRawMix.channels -ne 0 -or
            ![double]::IsFinite($qaRawMix.seconds) -or
            [math]::Abs($qaRawMix.seconds - $qaRawMix.samples/($qaRawMix.channels*$qaRawMix.sampleRate)) -gt .00001) {
            throw 'A real mixer recording failed the raw floating-point headroom/content assertions.'
        }
        # Verify encoded bytes independently. PCM16 cannot establish whether the
        # upstream float signal clipped, so retain both measurements separately.
        $qaWavePath = Resolve-QASource (Join-Path $Root $qaRecording.file)
        $qaWaveBytes = [IO.File]::ReadAllBytes($qaWavePath)
        if ($qaWaveBytes.Length -lt 44 -or [Text.Encoding]::ASCII.GetString($qaWaveBytes,0,4) -ne 'RIFF' -or
            [Text.Encoding]::ASCII.GetString($qaWaveBytes,8,4) -ne 'WAVE' -or
            [BitConverter]::ToUInt32($qaWaveBytes,4) + 8 -ne $qaWaveBytes.Length) { throw 'A mixed-output WAV has an invalid RIFF container.' }
        $qaFormatOffset = -1; $qaDataOffset = -1; $qaDataBytes = 0; $qaChunkOffset = 12
        while ($qaChunkOffset + 8 -le $qaWaveBytes.Length) {
            $qaChunkName = [Text.Encoding]::ASCII.GetString($qaWaveBytes,$qaChunkOffset,4)
            $qaChunkBytes = [long][BitConverter]::ToUInt32($qaWaveBytes,$qaChunkOffset+4)
            if ($qaChunkOffset + 8 + $qaChunkBytes -gt $qaWaveBytes.Length) { throw 'A mixed-output WAV chunk is truncated.' }
            if ($qaChunkName -eq 'fmt ') {
                if ($qaFormatOffset -ne -1 -or $qaChunkBytes -lt 16) { throw 'A mixed-output WAV format chunk is invalid.' }
                $qaFormatOffset = $qaChunkOffset + 8
            } elseif ($qaChunkName -eq 'data') {
                if ($qaDataOffset -ne -1) { throw 'A mixed-output WAV has duplicate sample chunks.' }
                $qaDataOffset = $qaChunkOffset + 8; $qaDataBytes = $qaChunkBytes
            }
            $qaChunkOffset += 8 + $qaChunkBytes + ($qaChunkBytes % 2)
        }
        if ($qaFormatOffset -lt 0 -or $qaDataOffset -lt 0 -or $qaChunkOffset -ne $qaWaveBytes.Length -or
            [BitConverter]::ToUInt16($qaWaveBytes,$qaFormatOffset) -ne 1 -or
            [BitConverter]::ToUInt16($qaWaveBytes,$qaFormatOffset+2) -ne $qaRawMix.channels -or
            [BitConverter]::ToUInt32($qaWaveBytes,$qaFormatOffset+4) -ne $qaRawMix.sampleRate -or
            [BitConverter]::ToUInt16($qaWaveBytes,$qaFormatOffset+12) -ne $qaRawMix.channels*2 -or
            [BitConverter]::ToUInt16($qaWaveBytes,$qaFormatOffset+14) -ne 16 -or
            [BitConverter]::ToUInt32($qaWaveBytes,$qaFormatOffset+8) -ne $qaRawMix.sampleRate*$qaRawMix.channels*2 -or
            $qaDataBytes -ne $qaRawMix.samples*2) { throw 'A mixed-output PCM16 WAV differs from the captured sample layout.' }
        $qaEncodedPeak = [double]0; $qaEncodedEnergy = [double]0; $qaFullScaleSamples = 0
        for ($qaSampleOffset=$qaDataOffset; $qaSampleOffset -lt $qaDataOffset+$qaDataBytes; $qaSampleOffset+=2) {
            $qaIntegerSample = [BitConverter]::ToInt16($qaWaveBytes,$qaSampleOffset)
            if ($qaIntegerSample -eq -32768 -or $qaIntegerSample -eq 32767) { ++$qaFullScaleSamples }
            $qaEncodedSample = $qaIntegerSample / 32768.0
            $qaEncodedPeak = [math]::Max($qaEncodedPeak,[math]::Abs($qaEncodedSample))
            $qaEncodedEnergy += $qaEncodedSample*$qaEncodedSample
        }
        $qaEncodedRms = [math]::Sqrt($qaEncodedEnergy/$qaRawMix.samples)
        if ($qaFullScaleSamples -ne 0 -or [math]::Abs($qaEncodedPeak-$qaRawMix.peak) -gt 2.0/32768 -or
            [math]::Abs($qaEncodedRms-$qaRawMix.rms) -gt 2.0/32768) { throw 'Encoded WAV content differs from the raw mixed output beyond PCM16 quantization.' }
        $qaWaveHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($qaWaveBytes)).ToLowerInvariant()
        $qaRecordingMeasurements.Add([ordered]@{file=$qaRecording.file;rawFloatMeasurement=$qaRawMix;
            encodedWavMeasurement=[ordered]@{format='PCM16 little-endian';channels=$qaRawMix.channels;sampleRate=$qaRawMix.sampleRate;
                samples=$qaRawMix.samples;fileBytes=$qaWaveBytes.Length;dataBytes=$qaDataBytes;peak=$qaEncodedPeak;rms=$qaEncodedRms;
                fullScaleSamples=$qaFullScaleSamples;sha256=$qaWaveHash};passed=$true})
        Select-QAEvidence $qaWavePath ('Audio/' + $Label + '-' + $qaRecording.file)
    }
    $qaRecordingVerificationPath = Join-Path (Split-Path -Parent (Resolve-QASource (Join-Path $Root 'audio-smoke.json'))) 'mixed-recording-verification.json'
    $qaRecordingVerification = [ordered]@{schema=1;checks=@($Report.checks).Count;passed=$true;
        scope='Raw post-effect float assertions and separate encoded PCM16 byte verification; no human listening acceptance';
        recordings=$qaRecordingMeasurements.ToArray()}
    [IO.File]::WriteAllText($qaRecordingVerificationPath,($qaRecordingVerification | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
    Select-QAEvidence $qaRecordingVerificationPath ('Audio/' + $Label + '-recording-verification.json') 'json'
}
$qaEditorAudio = Read-QAJson (Join-Path $EditorAudioRoot 'audio-smoke.json')
$qaEditorRun = Read-QAJson (Join-Path $EditorAudioRoot 'run.json')
if ($qaPolishRelease -and $EditorAudioRoot -eq 'Artifacts/QA/Audio/native-audio-postmix64') { throw 'A polish release requires a fresh Editor audio report from its current audio bank.' }
if (!$qaEditorAudio.passed -or @($qaEditorAudio.checks | Where-Object { !$_.passed }).Count -ne 0 -or
    !$qaEditorRun.editor -or !$qaEditorRun.freshReport -or !$qaEditorRun.passed -or $qaEditorRun.timedOut -or
    $qaEditorRun.exitCode -ne 0 -or $qaEditorRun.audioDisabled -or @($qaEditorRun.errors).Count -ne 0) { throw 'Editor audio evidence did not pass.' }
if ($qaPolishRelease -and ($qaEditorAudio.loadedSounds -ne 41 -or $qaEditorAudio.loadedCombatVariations -ne 20 -or
    'all20CombatVariationsLoad' -notin $qaEditorAudio.checks.name)) { throw 'The fresh Editor audio run did not exercise the complete new combat sound bank.' }
Select-QAEvidence (Join-Path $EditorAudioRoot 'audio-smoke.json') 'Audio/editor.json' 'json'
Select-QAEvidence (Join-Path $EditorAudioRoot 'run.json') 'Audio/editor-run.json' 'json'
if ($qaPolishRelease) { Select-QAMixedAudioEvidence $EditorAudioRoot $qaEditorAudio 'editor' }
Select-QAEvidence 'Docs/QA/audio-master-waveform.json' $(if ($qaPolishRelease) { 'Audio/historical-v1.0-master-waveform.json' } else { 'Audio/master-waveform.json' }) 'json'
$qaRegistry = Read-QAJson 'Artifacts/QA/unreal_asset_audit.json'
$qaAudioManifest = Read-QAJson 'Assets/Source/Audio/audio_manifest.json'
$qaSoundEntries = @($qaAudioManifest.sounds.PSObject.Properties)
if ($qaPolishRelease) {
    $qaAudioPolish = Read-QAJson 'Docs/QA/audio-polish-1.1.0.json'
    if ($qaAudioPolish.manifestSha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource 'Assets/Source/Audio/audio_manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() -or
        @($qaAudioPolish.sounds.PSObject.Properties).Count -ne $qaSoundEntries.Count) { throw 'Current audio waveform measurements do not match the actual sound manifest.' }
    Select-QAEvidence 'Docs/QA/audio-polish-1.1.0.json' 'Audio/current-master-waveform.json' 'json'
    Select-QAEvidence 'Docs/AUDIO_DESIGN.md' 'Documentation/AUDIO_DESIGN.md'
}
foreach ($qaSoundEntry in $qaSoundEntries) {
    if ($qaSoundEntry.Value.sha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaSoundEntry.Value.file) -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'An audio master changed after its manifest was generated.' }
}
$qaExpectedAssetCount = 263 - 41 + $qaSoundEntries.Count
if ($qaRegistry.assetCount -ne $qaExpectedAssetCount -or $qaRegistry.classCounts.SoundWave -ne $qaSoundEntries.Count -or $qaRegistry.classCounts.RiftCardData -ne 14 -or
    $qaRegistry.classCounts.NiagaraSystem -ne 17 -or $qaRegistry.classCounts.PhysicsAsset -ne 11 -or
    @($qaRegistry.errors).Count -ne 0 -or @($qaRegistry.nativeValidation.errors).Count -ne 0 -or
    $qaRegistry.nativeValidation.cards.Count -ne 14 -or @($qaRegistry.assets | Where-Object { !$_.onDisk -or (!$_.loadable -and $_.class -ne 'World') }).Count -ne 0) {
    throw 'The final native asset registry audit is incomplete.'
}
Select-QAEvidence 'Artifacts/QA/unreal_asset_audit.json' 'Visual/asset-registry.json' 'json'
$qaEditorVFX = Read-QAJson 'Artifacts/QA/niagara-all17-final-verification.json'
if (!$qaEditorVFX.passed -or $qaEditorVFX.effectSystems -ne 17 -or $qaEditorVFX.persistentSystems -ne 7 -or
    $qaEditorVFX.immediateParticles -le 0 -or $qaEditorVFX.checks.Count -lt 78 -or @($qaEditorVFX.checks | Where-Object { !$_.passed }).Count -ne 0) {
    throw 'The corrected final 17-system particle verification did not pass.'
}
Select-QAEvidence 'Artifacts/QA/niagara-all17-final-verification.json' 'Visual/editor-particle-verification.json' 'json'
$qaPresentationReview = Read-QAJson $PresentationReview
$qaRequiredPresentationPages = @('Home','Battle','Loadout','Cards','CardDetail','Settings','Help','Replays','ReplayView','Analysis','Meta','PatchNotes')
if ($qaPresentationReview.sourceReview.sha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaPresentationReview.sourceReview.path) -Algorithm SHA256).Hash.ToLowerInvariant() -or
    $qaPresentationReview.captureCount -ne @($qaPresentationReview.captures).Count -or
    ($qaPolishRelease -and ($qaPresentationReview.version -ne $Version -or $qaPresentationReview.captureCount -lt $qaRequiredPresentationPages.Count)) -or
    (!$qaPolishRelease -and $qaPresentationReview.captureCount -ne 28) -or
    @($qaPresentationReview.captures | Where-Object stem -match '^profile-').Count -ne 0) {
    throw 'The curated presentation review is stale or includes a Profile page.'
}
$qaReviewedPages = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$qaReviewedCaptures = @{}
foreach ($qaReviewedCapture in $qaPresentationReview.captures) {
    if ($qaReviewedCapture.stem -notmatch '^[a-z0-9][a-z0-9_-]*$' -or $qaReviewedCaptures.ContainsKey($qaReviewedCapture.stem) -or
        [string]::IsNullOrWhiteSpace($qaReviewedCapture.observation)) { throw 'A reviewed capture lacks a unique valid stem or actual inspection notes.' }
    foreach ($qaReviewedFile in $qaReviewedCapture.files) {
        if ($qaReviewedFile.sha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaReviewedFile.path) -Algorithm SHA256).Hash.ToLowerInvariant()) {
            throw 'A reviewed renderer capture changed after its final inspection.'
        }
    }
    if ($qaPolishRelease) {
        $qaReviewedBase = 'Artifacts/QA/Visual/' + $qaReviewedCapture.stem
        $qaExpectedReviewedFiles = @(($qaReviewedBase + '.png'), ($qaReviewedBase + '.json'), ($qaReviewedBase + '.state.json'))
        if (@($qaReviewedCapture.files).Count -ne 3 -or @($qaReviewedCapture.files.path | Sort-Object -Unique).Count -ne 3) { throw 'A current visual review must pin the PNG, launch metadata and captured state exactly once.' }
        foreach ($qaExpectedFile in $qaExpectedReviewedFiles) { if ($qaExpectedFile -notin $qaReviewedCapture.files.path) { throw 'A current visual review does not identify its matching PNG/metadata/state triplet.' } }
        $qaReviewedMetadata = Read-QAJson ($qaReviewedBase + '.json')
        if (!$qaReviewedMetadata.captured -or !$qaReviewedMetadata.stateCaptured -or !$qaReviewedMetadata.resolutionMatches -or
            $qaReviewedMetadata.exitCode -ne 0 -or $qaReviewedMetadata.timedOut -or @($qaReviewedMetadata.errors).Count -ne 0 -or
            $qaReviewedMetadata.page -notin $qaRequiredPresentationPages -or
            $qaReviewedCapture.actualDimensions[0] -ne $qaReviewedMetadata.actualWidth -or $qaReviewedCapture.actualDimensions[1] -ne $qaReviewedMetadata.actualHeight) {
            throw 'The current visual review includes an incomplete capture, private Profile page or incorrect dimensions.'
        }
        $null = $qaReviewedPages.Add($qaReviewedMetadata.page)
        $qaReviewedCaptures.Add($qaReviewedCapture.stem,$qaReviewedMetadata)
    } else { $qaReviewedCaptures.Add($qaReviewedCapture.stem,$null) }
}
if ($qaPolishRelease) { foreach ($qaRequiredPage in $qaRequiredPresentationPages) { if (!$qaReviewedPages.Contains($qaRequiredPage)) { throw "A required current game page has not been visually reviewed: $qaRequiredPage" } } }
Select-QAEvidence $PresentationReview 'Visual/presentation-review.json' 'json'
Select-QAEvidence $qaPresentationReview.sourceReview.path 'Documentation/presentation-source-review.md'

# Explicit latest synthetic capture stems. Never enumerate/copy the QA or save tree.
$qaCaptureNames = @('home-final-1920x1080','cards-final-1920x1080','loadout-final-width-1920x1080',
    'settings-final-combo-1920x1080','meta-final-table-1920x1080','patch-notes-final-1920x1080',
    'battle-congestion-final-2d-1920x1080','battle-effects-final-normal-1920x1080',
    'battle-effects-final-quarter-1920x1080','battle-effects-final-quadruple-1920x1080','battle-effects-final-burst-1920x1080',
    'battle-effects-niagara-diagnostic-1920x1080',
    'placement-final-building-legal','placement-final-building-occupied','placement-final-building-river',
    'placement-final-opponent-pocket','placement-final-rear-corner','placement-final-spell-radius')
$qaCaptureNames += @('1280x720','1600x900','1920x1080','1920x1200','2560x1080','2560x1440','2560x1600','3440x1440','3840x2160' | ForEach-Object { 'battle-roster-final-camera-' + $_ })
$qaHistoricalCaptureNames = @('battle-effects-final-normal-1920x1080','battle-effects-final-quarter-1920x1080',
    'battle-effects-final-quadruple-1920x1080','battle-effects-final-burst-1920x1080','battle-effects-niagara-diagnostic-1920x1080')
$qaCaptureNames += @('niagara-all17-final-immediate','niagara-all17-final-lifecycle',
    'battle-effects-final-particles-1920x1080','battle-effects-final-particles-quarter-1920x1080','battle-effects-final-particles-quadruple-1920x1080',
    'frost-breath-final-1920x1080')
foreach ($qaCaptureName in $qaCaptureNames) {
    $qaCapturePath = 'Artifacts/QA/Visual/' + $qaCaptureName
    $qaCapture = Read-QAJson ($qaCapturePath + '.json')
    if (!$qaCapture.captured -or !$qaCapture.stateCaptured -or !$qaCapture.resolutionMatches -or
        $qaCapture.exitCode -ne 0 -or $qaCapture.timedOut -or @($qaCapture.errors).Count -ne 0) {
        throw "The selected actual renderer capture is incomplete: $qaCaptureName"
    }
    if ($qaCaptureName -eq 'battle-effects-niagara-diagnostic-1920x1080') {
        $qaHistoricalParticles = Read-QAJson ($qaCapturePath + '.state.json')
        if (!$qaHistoricalParticles.niagara -or $qaHistoricalParticles.niagara.totalParticles -ne 0) {
            throw 'The selected historical particle diagnostic no longer describes the observed zero-particle defect.'
        }
    }
    if ($qaCaptureName -eq 'frost-breath-final-1920x1080') {
        Assert-QAFrostProof (Read-QAJson ($qaCapturePath + '.state.json'))
    }
    # These effect captures predate the particle graph correction. Their gameplay
    # events were real, but the diagnostic proved zero rendered Niagara particles.
    $qaCaptureEntry = $(if ($qaHistoricalCaptureNames -contains $qaCaptureName) { 'Visual/Historical-BeforeParticleFix/' } elseif ($qaPolishRelease) { 'Visual/Historical-v1.0/' } else { 'Visual/' }) + $qaCaptureName
    Select-QAEvidence ($qaCapturePath + '.json') ($qaCaptureEntry + '.json') 'json'
    Select-QAEvidence ($qaCapturePath + '.state.json') ($qaCaptureEntry + '.state.json') 'json'
    Select-QAEvidence ($qaCapturePath + '.png') ($qaCaptureEntry + '.png')
}

if ($Finalize) {
    foreach ($qaDoc in @('Assets/PRESENTATION_ACCEPTANCE.md','Docs/RELEASE_QA.md','Docs/BUILD_AND_ARCHITECTURE.md','Docs/WINDOWS_RELEASE.md','Docs/LAUNCHER_QA.md')) {
        Select-QAEvidence $qaDoc ('Documentation/' + [IO.Path]::GetFileName($qaDoc))
    }
    if (!$ShippingAudioRoot -or !$ShippingVFXReport -or $PerformanceReports.Count -eq 0) { throw 'Finalize requires the completed actual Shipping audio, particle and performance report paths.' }
    $qaShippingAudio = Read-QAJson (Join-Path $ShippingAudioRoot 'audio-smoke.json')
    $qaShippingRun = Read-QAJson (Join-Path $ShippingAudioRoot 'run.json')
    if (!$qaShippingAudio.passed -or @($qaShippingAudio.checks | Where-Object { !$_.passed }).Count -ne 0 -or
        $qaShippingRun.editor -or !$qaShippingRun.passed -or !$qaShippingRun.freshReport -or $qaShippingRun.timedOut -or
        $qaShippingRun.exitCode -ne 0 -or $qaShippingRun.audioDisabled -or @($qaShippingRun.errors).Count -ne 0) {
        throw 'The selected actual Shipping audio run did not pass.'
    }
    if ($qaPolishRelease -and ($qaShippingAudio.loadedSounds -ne 41 -or $qaShippingAudio.loadedCombatVariations -ne 20 -or
        'all20CombatVariationsLoad' -notin $qaShippingAudio.checks.name)) { throw 'The final Shipping audio run did not load the complete new combat sound bank.' }
    Select-QAEvidence (Join-Path $ShippingAudioRoot 'audio-smoke.json') 'Audio/shipping.json' 'json'
    Select-QAEvidence (Join-Path $ShippingAudioRoot 'run.json') 'Audio/shipping-run.json' 'json'
    if ($qaPolishRelease) { Select-QAMixedAudioEvidence $ShippingAudioRoot $qaShippingAudio 'shipping' }
    $qaPerfIndex = 0
    $qaPerfRuns = [Collections.Generic.List[object]]::new()
    foreach ($qaPerformancePath in $PerformanceReports) {
        $qaPerformance = Read-QAJson $qaPerformancePath
        if ($qaPerformance.build -ne 'Shipping' -or $qaPerformance.frame.samples -lt 100 -or
            ($qaPerformance.metaRequested -and $qaPerformance.metaGamesDuringBattle -ne 0)) { throw 'The selected Shipping performance measurement is incomplete.' }
        $qaPerfEntry = 'Performance/' + (++$qaPerfIndex) + '-' + [IO.Path]::GetFileName((Split-Path -Parent $qaPerformancePath))
        $qaPerformanceRunPath = Join-Path (Split-Path -Parent $qaPerformancePath) 'run.json'
        $qaPerformanceRun = Read-QAJson $qaPerformanceRunPath
        if (!$qaPerformanceRun.passed -or $qaPerformanceRun.editor -or $qaPerformanceRun.exitCode -ne 0 -or
            $qaPerformanceRun.reportSha256 -ne (Get-FileHash -LiteralPath (Resolve-QASource $qaPerformancePath) -Algorithm SHA256).Hash.ToLowerInvariant()) {
            throw 'Shipping performance launch/exit/hash provenance does not match its measured report.'
        }
        $qaPerfRuns.Add($qaPerformanceRun)
        Select-QAEvidence $qaPerformancePath ($qaPerfEntry + '.json') 'json'
        Select-QAEvidence $qaPerformanceRunPath ($qaPerfEntry + '-run.json') 'json'
    }
    $qaPerfLogRoots = @($PerformanceReports | ForEach-Object {
        [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent (Resolve-QASource $_)) 'UserData/Logs'))
    })
    foreach ($qaPerformanceLog in $PerformanceLogs) {
        $qaLogPath = Resolve-QASource $qaPerformanceLog
        if ([IO.Path]::GetFileName($qaLogPath) -notmatch '^RiftGame-[0-9-]+\.log$' -or
            @($qaPerfLogRoots | Where-Object { $qaLogPath.StartsWith($_ + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) }).Count -ne 1) {
            throw 'Only explicit native logs from the selected isolated performance runs may be included.'
        }
        $qaLogRecords = @([IO.File]::ReadAllLines($qaLogPath) | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json -Depth 30 })
        $qaLogContext = @($qaLogRecords | Where-Object event -eq 'system_context')
        if ($qaLogContext.Count -ne 1 -or $qaLogContext[0].build -ne 'Shipping' -or
            $qaLogContext[0].cardRulesFingerprint -ne $qaValidity.fingerprint -or
            @($qaLogRecords | Where-Object level -in @('Error','Fatal')).Count -ne 0) {
            throw 'A selected native performance diagnostic log contains an error or incompatible context.'
        }
        Select-QAEvidence $qaPerformanceLog ('Performance/Logs/' + [IO.Path]::GetFileNameWithoutExtension($qaLogPath) + '.jsonl') 'jsonl'
    }
    $qaInstaller = Read-QAJson $InstallerReport
    $qaReleaseProof = Read-QAJson $ReleaseVerification
    if (!$qaInstaller.passed -or $qaInstaller.checkCount -lt 51 -or !$qaReleaseProof.passed -or
        $qaReleaseProof.version -ne $Version -or $qaInstaller.installedVersion -ne $Version -or
        $qaInstaller.installedArchiveSha256 -ne $qaReleaseProof.archiveSha256 -or
        $qaInstaller.installedLauncherSha256 -ne $qaReleaseProof.launcherSha256 -or
        $qaInstaller.installedGameFiles -ne $qaReleaseProof.gameFiles -or
        @($qaInstaller.msiSha256,$qaInstaller.upgradeMsiSha256) -notcontains $qaReleaseProof.installerSha256) { throw 'Final installed MSI evidence does not match the verified release.' }
    Select-QAEvidence $InstallerReport 'Windows/installer-lifecycle.json' 'json'
    Select-QAEvidence $ReleaseVerification 'Windows/release-verification.json' 'json'
    $qaReleaseManifest = Read-QAJson 'Artifacts/Release/update-manifest.json'
    if ($qaReleaseManifest.version -ne $Version -or $qaReleaseManifest.sha256 -ne $qaReleaseProof.archiveSha256 -or
        (Get-FileHash -LiteralPath (Resolve-QASource 'Artifacts/Release/update-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qaReleaseProof.manifestSha256) {
        throw 'The final release manifest differs from the verified release.'
    }
    $qaNativeExecutable = @($qaReleaseManifest.files | Where-Object path -match '(^|/)Binaries/Win64/RiftCrownArena-Win64-Shipping\.exe$')
    if ($qaNativeExecutable.Count -ne 1) { throw 'The release manifest does not identify one native Shipping executable.' }
    $qaAudioBinary = Assert-QAShippingBinary $qaShippingRun.executable $qaReleaseManifest $qaNativeExecutable[0]
    if ($qaAudioBinary.requestedManifestEntry -ne $qaNativeExecutable[0].path -or
        $qaShippingRun.executableSha256 -ne $qaNativeExecutable[0].sha256) {
        throw 'The audio report does not record launch-time hashing of the exact final native Shipping executable.'
    }
    foreach ($qaPerformanceRun in $qaPerfRuns) {
        $qaPerfBinary = Assert-QAShippingBinary $qaPerformanceRun.executable $qaReleaseManifest $qaNativeExecutable[0]
        if ($qaPerfBinary.requestedManifestEntry -ne $qaNativeExecutable[0].path -or
            $qaPerformanceRun.executableSha256 -ne $qaNativeExecutable[0].sha256) {
            throw 'The performance run does not record launch-time hashing of the exact final native Shipping executable.'
        }
    }
    Select-QAEvidence 'Artifacts/Release/update-manifest.json' 'Windows/update-manifest.json' 'json'
    $qaShippingVFX = Read-QAJson $ShippingVFXReport
    if (!$qaShippingVFX.passed -or $qaShippingVFX.effectSystems -ne 17 -or $qaShippingVFX.persistentSystems -ne 7 -or
        $qaShippingVFX.immediateParticles -le 0 -or $qaShippingVFX.checks.Count -lt 80 -or
        @($qaShippingVFX.checks | Where-Object { !$_.passed }).Count -ne 0) {
        throw 'The final manifest-hashed Shipping particle regression did not pass.'
    }
    $qaVFXBinary = Assert-QAShippingBinary $qaShippingVFX.executable $qaReleaseManifest $qaNativeExecutable[0]
    if ($qaVFXBinary.requestedManifestEntry -ne $qaNativeExecutable[0].path -or
        $qaShippingVFX.executableSha256 -ne $qaNativeExecutable[0].sha256) { throw 'The Shipping particle report used another requested executable.' }
    Select-QAEvidence $ShippingVFXReport 'Visual/shipping-particle-verification.json' 'json'
    $qaShippingCaptureSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $qaCurrentReviewedShippingPages = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $qaCurrentReviewedBattleRatios = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $qaCurrentFrostProof = $false
    foreach ($qaCaptureName in @(($qaShippingVFX.name + '-immediate'),($qaShippingVFX.name + '-lifecycle')) + $ShippingCaptureNames) {
        if (!$qaShippingCaptureSet.Add($qaCaptureName)) { continue }
        if ($qaCaptureName -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Invalid Shipping capture name.' }
        $qaCapturePath = 'Artifacts/QA/Visual/' + $qaCaptureName
        $qaCapture = Read-QAJson ($qaCapturePath + '.json')
        if ($qaCapture.editor -or !$qaCapture.captured -or !$qaCapture.stateCaptured -or !$qaCapture.resolutionMatches -or
            $qaCapture.exitCode -ne 0 -or $qaCapture.timedOut -or @($qaCapture.errors).Count -ne 0 -or
            $qaCapture.page -notin @('Home','Cards','CardDetail','Loadout','Settings','Meta','PatchNotes','Battle','Help','Replays','ReplayView','Analysis') -or
            $qaCapture.executableSha256 -ne $qaNativeExecutable[0].sha256 -or
            ($qaCapture.nativeExecutableSha256 -and $qaCapture.nativeExecutableSha256 -ne $qaNativeExecutable[0].sha256)) {
            throw 'The selected Shipping capture is incomplete, includes a Profile page or used another executable.'
        }
        $qaCaptureBinary = Assert-QAShippingBinary $qaCapture.executable $qaReleaseManifest $qaNativeExecutable[0]
        if ($qaCaptureBinary.requestedManifestEntry -ne $qaNativeExecutable[0].path) { throw 'The Shipping capture did not launch the exact native executable directly.' }
        $qaShippingState = Read-QAJson ($qaCapturePath + '.state.json')
        if ($qaPolishRelease -and ($qaCapture.allowExternalInput -or
            !$qaShippingState.captureInput.active -or !$qaShippingState.captureInput.consuming -or
            ($qaCapture.phaseFixture -and !$qaCapture.phaseFixturePassed))) {
            throw 'Current Shipping evidence requires isolated external input and a successful requested phase fixture.'
        }
        if ($qaCapture.breathSmoke -or $qaCaptureName.StartsWith('shipping-frost-breath-')) { Assert-QAFrostProof $qaShippingState; $qaCurrentFrostProof = $true }
        if ($qaPolishRelease -and $qaReviewedCaptures.ContainsKey($qaCaptureName)) {
            $qaReviewedMetadata = $qaReviewedCaptures[$qaCaptureName]
            if ($qaReviewedMetadata.editor -or $qaReviewedMetadata.executableSha256 -ne $qaNativeExecutable[0].sha256) { throw 'The current reviewed Shipping capture belongs to another executable.' }
            $null = $qaCurrentReviewedShippingPages.Add($qaCapture.page)
            if ($qaCapture.page -eq 'Battle') {
                $qaRatio = [double]$qaCapture.actualWidth / [double]$qaCapture.actualHeight
                if ([math]::Abs($qaRatio - (16.0/9.0)) -lt 0.015) { $null = $qaCurrentReviewedBattleRatios.Add('16:9') }
                if ([math]::Abs($qaRatio - (16.0/10.0)) -lt 0.015) { $null = $qaCurrentReviewedBattleRatios.Add('16:10') }
                if ($qaRatio -ge 2.3 -and $qaRatio -le 2.45) { $null = $qaCurrentReviewedBattleRatios.Add('21:9') }
            }
        }
        if ($qaCapture.scenario -eq 'effects' -and
            ($qaShippingState.elapsed -lt 3 -or $qaShippingState.events.aura -lt 1 -or $qaShippingState.events.stun -lt 1 -or
                $qaShippingState.events.slow -lt 1 -or $qaShippingState.events.attack -lt 1 -or $qaShippingState.hazards -lt 1 -or
                $qaShippingState.niagara.totalParticles -le 0)) { throw 'The Shipping combat showcase did not reach its actual special mechanics.' }
        Select-QAEvidence ($qaCapturePath + '.json') ('Visual/Shipping/' + $qaCaptureName + '.json') 'json'
        Select-QAEvidence ($qaCapturePath + '.state.json') ('Visual/Shipping/' + $qaCaptureName + '.state.json') 'json'
        Select-QAEvidence ($qaCapturePath + '.png') ('Visual/Shipping/' + $qaCaptureName + '.png')
    }
    if ($qaPolishRelease) {
        foreach ($qaRequiredPage in $qaRequiredPresentationPages) { if (!$qaCurrentReviewedShippingPages.Contains($qaRequiredPage)) { throw "The current native Shipping executable lacks a selected and reviewed game page: $qaRequiredPage" } }
        foreach ($qaRequiredRatio in @('16:9','16:10','21:9')) { if (!$qaCurrentReviewedBattleRatios.Contains($qaRequiredRatio)) { throw "The current native Shipping battle HUD lacks reviewed aspect-ratio coverage: $qaRequiredRatio" } }
        if (!$qaCurrentFrostProof) { throw 'The current Shipping executable lacks its selected actual Frost Breath animation/particle proof.' }
    }
}

$qaInventory = [Collections.Generic.List[object]]::new()
$qaSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($qaSelection in $qaSelections) {
    if (!$qaSeen.Add($qaSelection.entry)) { throw 'Duplicate evidence archive entry.' }
    $qaDestination = Join-Path $qaStage $qaSelection.entry
    Assert-QANoLinks $qaDestination
    New-Item -ItemType Directory -Path (Split-Path -Parent $qaDestination) -Force | Out-Null
    $qaOriginalHash = (Get-FileHash -LiteralPath $qaSelection.source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($qaSelection.kind -eq 'json') {
        [IO.File]::WriteAllText($qaDestination,(Sanitize-QAJson ([IO.File]::ReadAllText($qaSelection.source))),[Text.UTF8Encoding]::new($false))
    } elseif ($qaSelection.kind -eq 'jsonl') {
        $qaSanitizedLines = @([IO.File]::ReadAllLines($qaSelection.source) | Where-Object { $_.Trim() } | ForEach-Object { Sanitize-QAJson $_ })
        [IO.File]::WriteAllText($qaDestination,($qaSanitizedLines -join "`n") + "`n",[Text.UTF8Encoding]::new($false))
    } elseif ($qaSelection.kind -in @('meta-log-excerpt','integration-log-excerpt')) {
        $qaLogPattern = $(if ($qaSelection.kind -eq 'meta-log-excerpt') { 'Native Meta validation completed: 10000 actual games\.|FPlatformMisc::RequestExitWithStatus\(0, 0|LogExit: Exiting\.' }
            else { "Automation Test Queue Empty $qaAutomationCount tests performed\.|FPlatformMisc::RequestExitWithStatus\(1, 0|LogExit: Exiting\." })
        $qaLogExcerpt = @([IO.File]::ReadAllLines($qaSelection.source) | Where-Object { $_ -match $qaLogPattern }) -join "`n"
        $qaRequiredMarker = $(if ($qaSelection.kind -eq 'meta-log-excerpt') { 'Native Meta validation completed: 10000 actual games.' } else { "Automation Test Queue Empty $qaAutomationCount tests performed." })
        if (!$qaLogExcerpt.Contains($qaRequiredMarker)) { throw 'Selected original completion log marker is missing.' }
        $qaLogExcerpt = $qaLogExcerpt.Replace($qaRepo.Replace('\','/') + '/','').Replace($qaRepo + '\','')
        [IO.File]::WriteAllText($qaDestination,("# Selected original completion/shutdown lines; full source SHA-256: $qaOriginalHash`n" + $qaLogExcerpt + "`n"),[Text.UTF8Encoding]::new($false))
    } else { Copy-Item -LiteralPath $qaSelection.source -Destination $qaDestination -Force }
    if ((Get-FileHash -LiteralPath $qaSelection.source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qaOriginalHash) { throw 'Selected source evidence changed while staging.' }
    $qaInventory.Add([ordered]@{entry=$qaSelection.entry;source=[IO.Path]::GetRelativePath($qaRepo,$qaSelection.source).Replace('\','/');sourceSha256=$qaOriginalHash;
        size=(Get-Item -LiteralPath $qaDestination).Length;sha256=(Get-FileHash -LiteralPath $qaDestination -Algorithm SHA256).Hash.ToLowerInvariant();pathSanitized=($qaSelection.kind -ne 'copy')})
}
$qaSourceHashPath = Join-Path $qaStage 'Meta/source-hashes.json'
[IO.File]::WriteAllText($qaSourceHashPath,([ordered]@{runtimeSha256=$qaContext.runtimeSha256;simulationSources=$qaContext.simulationSources;allCurrentSourceHashesVerified=$true} | ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
$qaInventory.Add([ordered]@{entry='Meta/source-hashes.json';source='Generated from the frozen final context';size=(Get-Item -LiteralPath $qaSourceHashPath).Length;sha256=(Get-FileHash -LiteralPath $qaSourceHashPath -Algorithm SHA256).Hash.ToLowerInvariant()})
$qaReadme = @"
# Rift Crown Arena QA evidence

Version: $Version. Final Shipping/audio/performance/MSI reports included: $([bool]$Finalize).

This archive uses an explicit synthetic evidence whitelist. It excludes player_save.json, ue_save.json, Profile pages, player save/config folders, historical replay archives and private automation fixtures. JSON workspace paths and machine names are normalized; inventory.json retains the original and staged SHA-256 hashes. Raw synthetic cohort JSON remains byte-identical.

Meta/validity.json passes the final completed 10,000-match cohort. Meta/historical-comparison.json separately records real differences after the numerical bank correction; aggregate differences are not a changed-match count. Meta/bank-normalization-witness.json gives a controlled causal seed. Renderer capture metadata preserves actual review limitations. Visual/Historical-BeforeParticleFix contains real combat events and screenshots from before the particle graph correction; its diagnostic reported zero Niagara particles. Those files are historical failed particle coverage, not final VFX acceptance.

Portable core/test source is included under Source/ in its original relative layout. UE automation reports distinguish the final complete $qaAutomationCount-test release suite from the earlier focused UI-DPI correction test. Its launch-time context pins all current native/UI/input/audio sources and both Editor modules. Historical-v1.0 captures preserve the original release baseline; Visual/Shipping contains the current executable's captures. Audio reports distinguish waveform analysis, real Editor playback and actual Shipping playback. Performance JSON contains measured frame/CPU/GPU values, not a general hardware guarantee.

Audio recording-verification reports retain raw post-effect floating-point peak/RMS and clipping checks separately from encoded PCM16 WAV measurements and byte hashes. The normal burst and deliberate 12x overload WAVs are actual mixer recordings; no human listening acceptance is claimed.

Completion log files are selected original lines, explicitly labeled excerpts with the complete source log SHA-256. Reproduce final cohort validity with: python Source/Build/Tests/Audit-NativeMeta.py Meta/final-dataset.json --output audit-validity.json --log Meta/completion-excerpt.log --export Meta/final-export.json --require-complete. Add --compare Meta/historical-telemetry3-dataset.json for the separate strict comparison; its nonzero exit reflects documented aggregate differences.
"@
$qaReadmePath = Join-Path $qaStage 'README.md'
[IO.File]::WriteAllText($qaReadmePath,$qaReadme,[Text.UTF8Encoding]::new($false))
$qaInventory.Add([ordered]@{entry='README.md';source='Generated packaging scope';size=(Get-Item -LiteralPath $qaReadmePath).Length;sha256=(Get-FileHash -LiteralPath $qaReadmePath -Algorithm SHA256).Hash.ToLowerInvariant()})
$qaTotalBytes = [long]0
foreach ($qaFile in $qaInventory) { $qaTotalBytes += [long]$qaFile.size }
$qaManifest = [ordered]@{schema=1;version=$Version;stagedUtc=[DateTime]::UtcNow.ToString('o');finalized=[bool]$Finalize;files=$qaInventory.ToArray();fileCount=$qaInventory.Count;
    totalBytes=$qaTotalBytes;scope='Explicit synthetic QA selection; no private player profile/save/config/fixture tree'}
$qaManifestPath = Join-Path $qaStage 'inventory.json'
[IO.File]::WriteAllText($qaManifestPath,($qaManifest | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
if (!$Finalize) {
    Write-Output ([ordered]@{stage=$qaStage;files=$qaInventory.Count;bytes=$qaManifest.totalBytes;finalized=$false;zipCreated=$false;inventory=$qaManifestPath} | ConvertTo-Json)
    return
}

# Archive entries are exactly the verified whitelist, never a recursive QA-tree copy.
$qaAllowed = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($qaFile in $qaInventory) { $null = $qaAllowed.Add($qaFile.entry) }
$null = $qaAllowed.Add('inventory.json');$null = $qaAllowed.Add('.qa-evidence-stage.json')
foreach ($qaFile in Get-ChildItem -LiteralPath $qaStage -File -Recurse) {
    $qaEntry = [IO.Path]::GetRelativePath($qaStage,$qaFile.FullName).Replace('\','/')
    if (!$qaAllowed.Contains($qaEntry)) { throw 'Unexpected file in evidence stage; refusing to archive it.' }
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$qaArchiveName = 'RiftCrownArena-QAEvidence-Windows-x64-' + $Version + '.zip'
$qaArchive = Join-Path $qaRelease $qaArchiveName
$qaTemporary = $qaArchive + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
$qaZip = [IO.Compression.ZipFile]::Open($qaTemporary,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($qaFile in $qaInventory) { $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($qaZip,(Join-Path $qaStage $qaFile.entry),$qaFile.entry,[IO.Compression.CompressionLevel]::Optimal) }
    $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($qaZip,$qaManifestPath,'inventory.json',[IO.Compression.CompressionLevel]::Optimal)
} finally { $qaZip.Dispose() }
$qaZip = [IO.Compression.ZipFile]::OpenRead($qaTemporary)
try {
    if ($qaZip.Entries.Count -ne $qaInventory.Count + 1) { throw 'QA archive count differs from the whitelist.' }
    foreach ($qaFile in $qaInventory) {
        $qaEntry = $qaZip.GetEntry($qaFile.entry)
        if (!$qaEntry -or $qaEntry.Length -ne $qaFile.size) { throw 'A QA archive entry is missing or changed length.' }
        $qaStream = $qaEntry.Open()
        try { $qaDigest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($qaStream)).ToLowerInvariant() }
        finally { $qaStream.Dispose() }
        if ($qaDigest -ne $qaFile.sha256) { throw 'A QA archive entry changed its actual bytes.' }
    }
    $qaEntry = $qaZip.GetEntry('inventory.json')
    if (!$qaEntry -or $qaEntry.Length -ne (Get-Item -LiteralPath $qaManifestPath).Length) { throw 'The QA archive inventory is missing or truncated.' }
    $qaStream = $qaEntry.Open()
    try { $qaDigest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($qaStream)).ToLowerInvariant() }
    finally { $qaStream.Dispose() }
    if ($qaDigest -ne (Get-FileHash -LiteralPath $qaManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()) { throw 'The QA archive inventory changed its actual bytes.' }
} finally { $qaZip.Dispose() }
Move-Item -LiteralPath $qaTemporary -Destination $qaArchive -Force
$qaArchiveHash = (Get-FileHash -LiteralPath $qaArchive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $qaRelease 'QAEvidence-SHA256.txt'),($qaArchiveHash + '  ' + $qaArchiveName + "`n"),[Text.UTF8Encoding]::new($false))
Write-Output ([ordered]@{archive=$qaArchive;sha256=$qaArchiveHash;bytes=(Get-Item -LiteralPath $qaArchive).Length;files=$qaInventory.Count + 1;finalized=$true;zipVerified=$true} | ConvertTo-Json)
