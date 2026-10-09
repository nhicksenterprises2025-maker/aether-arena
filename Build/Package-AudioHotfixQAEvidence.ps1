param(
    [string]$Executable='Artifacts/Game-1.3.2/Windows/RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe',
    [string]$EditorAudioName='water132-editor-audio',
    [string]$ShippingAudioName='water132-shipping-audio',
    [string[]]$ReleaseDocuments=@('Docs/PATCH_NOTES_1.3.2.md')
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this evidence packager with PowerShell 7.'}
$Version='1.3.2';$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$summaryRelative='Docs/QA/audio-hotfix-1.3.2.json'
$summaryPath=Join-Path $modelRepo $summaryRelative
$zipPath=Join-Path $modelRepo 'Artifacts/Release/RiftCrownArena-Audio-QAEvidence-1.3.2.zip'
$sumPath=Join-Path $modelRepo 'Artifacts/Release/SHA256SUMS.txt'
if((Test-Path -LiteralPath $summaryPath) -or (Test-Path -LiteralPath $zipPath)){throw 'The immutable audio hotfix evidence already exists.'}
$modelInputs=[Collections.Generic.List[object]]::new();$modelPinIndex=@{}
$modelSyntheticInputs=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
# Load only maintained validators and the sanitizer/writer, never their
# top-level acceptance, previous-version requirements or packaging actions.
foreach($library in @(
    @{file='Build/Package-ModelInputQAEvidence.ps1';names=@('Resolve-ModelInput','Pin-ModelInput','Read-ModelInput','Get-ModelUtcTimestamp','Assert-ModelShippingDiagnostics','Assert-ModelRecord','Assert-ModelChecks')},
    @{file='Build/Package-HoverQAEvidence.ps1';names=@('Get-HoverIdentity','Assert-HoverIdentity','Convert-HoverSanitized','Write-HoverEntry')}
)){
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $modelRepo $library.file),[ref]$tokens,[ref]$errors)
    if($errors.Count){throw 'A maintained evidence validator does not parse.'}
    foreach($name in $library.names){
        $definitions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
        if($definitions.Count -ne 1){throw "Missing maintained evidence validator: $name"}
        Invoke-Expression $definitions[0].Extent.Text
    }
    $null=Pin-ModelInput $library.file
}
$null=Pin-ModelInput $PSCommandPath
$native=Get-HoverIdentity $Executable
$manifest=Read-ModelInput 'Artifacts/Release/update-manifest.json'
$release=Read-ModelInput 'Artifacts/Release/windows-release-verification.json'
$entry=@($manifest.files|Where-Object path -match 'Binaries/Win64/RiftCrownArena-Win64-Shipping.exe$')
if($release.passed -ne $true -or $release.version -ne $Version -or $release.tag -ne ('v'+$Version) -or $manifest.version -ne $Version -or
   $entry.Count -ne 1 -or $entry[0].sha256 -ne $native.sha256 -or $entry[0].size -ne $native.bytes -or
   !$native.path.EndsWith('/'+$entry[0].path,[StringComparison]::OrdinalIgnoreCase) -or
   $release.manifestSha256 -ne $modelPinIndex['Artifacts/Release/update-manifest.json'].sha256){throw 'Final Windows release, manifest and actual native audio-test executable differ.'}
$packageRoot=$native.path.Substring(0,$native.path.Length-$entry[0].path.Length)
$payload=[Collections.Generic.List[object]]::new()
foreach($file in $manifest.files){
    if($file.path -match '(^|/)\.\.(/|$)|^[A-Za-z]:|^[/\\]' -or $file.sha256 -notmatch '^[a-f0-9]{64}$' -or $file.size -le 0){throw 'Invalid final Windows payload record.'}
    $pin=Get-HoverIdentity ($packageRoot+$file.path)
    if($pin.sha256 -ne $file.sha256 -or $pin.bytes -ne $file.size){throw 'Actual final payload differs from its manifest.'}
    $payload.Add($pin)
}
if(@($manifest.files.path|Sort-Object -Unique).Count -ne $manifest.files.Count -or $release.gameFiles -ne $manifest.files.Count){throw 'Final Windows payload inventory differs.'}

$integration=Read-ModelInput 'Docs/QA/native-integration.json';$context=$integration.curatedEvidence
if($integration.failed -ne 0 -or $integration.notRun -ne 0 -or @($integration.tests).Count -lt 15 -or
   @($integration.tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0}).Count -ne 0 -or
   @($integration.tests.fullTestPath|Sort-Object -Unique).Count -ne $integration.tests.Count -or $context.version -ne $Version -or
   $context.allScenariosObserved -ne $true -or $context.commandletExitCode -ne 0 -or $context.wrapperExitCode -ne 0){throw 'The complete current native suite must pass.'}
foreach($required in @('Rift.Integration.Typography','Rift.Integration.BattleInputRouting','Rift.Integration.SpellCastReplay')){
    if(@($integration.tests|Where-Object fullTestPath -eq $required).Count -ne 1){throw "Current complete-suite test is missing: $required"}
}
foreach($pin in @($context.sourceHashes)+@($context.runtimeModules)){Assert-HoverIdentity $pin}
$suite=Read-ModelInput $context.sourceReport;$suiteContext=Read-ModelInput $context.context
$suiteLog=Pin-ModelInput $context.commandletLog
if($modelPinIndex[$context.sourceReport].sha256 -ne $context.sourceReportSha256 -or $modelPinIndex[$context.context].sha256 -ne $context.contextSha256 -or
   $suite.failed -ne 0 -or $suite.notRun -ne 0 -or $suite.inProcess -ne 0 -or @($suite.tests).Count -ne $integration.tests.Count -or
   $suiteContext.completed -ne $true -or $suiteContext.sourcesUnchanged -ne $true -or $suiteContext.runtimeModulesUnchanged -ne $true -or
   $suiteContext.commandletExitCode -ne 0 -or $suiteContext.wrapperExitCode -ne 0){throw 'Native suite lacks its exact completed raw report and context.'}
$suiteText=Get-Content -LiteralPath (Resolve-ModelInput $suiteLog.path) -Raw
if(!$suiteText.Contains("Automation Test Queue Empty $($integration.tests.Count) tests performed.") -or
   $suiteText -notmatch 'FPlatformMisc::RequestExitWithStatus\(1, 0' -or $suiteText -match '(?im)(^|\])\s*\S+:\s*(Error|Fatal):'){throw 'The actual complete-suite engine log lacks clean completion.'}

# Retain the established music/UI/combat/settings/limiter assertions. Only the
# former river-presence assertions and two-loop budget are intentionally replaced.
$requiredAudioChecks=@('liveAudioDevice','profileAvailable','settingsInterfaceAvailable','allVoicesHaveLimiterRoute','masterMixRegisteredWithLiveDevice',
    'stereoPeakLimiterBelowFullScale','limiterDetectsIsolatedPeaksWithoutAttackSmoothing','actualSettingsAudioSliders','all41ImportedSoundsLoad','all20CombatVariationsLoad',
    'masterCallback','musicCallback','sfxCallback','uiCallback','musicPlaying','musicMasterProduct','musicLoopConfigured','musicStreamed','effectPlaying','uiPlaying',
    'effectMasterProduct','uiMasterProduct','musicIndependentMute','musicMuteKeepsUI','sfxIndependentMute','sfxMuteKeepsUI','uiIndependentMute','uiMuteKeepsSFX',
    'masterMutesMusic','masterMutesSFX','masterMutesUI','mutedPlayCreatesNoVoice','combinedUIAndSFXVoiceLimit','sixUIVoicesReservedUnderCombatLoad',
    'sameTickSwarmCueAggregated','importantEventReplacesLowerPriorityVoice','crowdedEffectMixHasHeadroom','settingsRestored','postMixDeferredAcrossGameTicks',
    'musicSurvivesBoundedBurst','musicRetainsPhysicalVoice','actualMixedPCMHasHeadroom','limiterOverloadPCMDoesNotClip','postMixSettingsRestored')
$requiredAudioChecks+=@('physicalBudgetIncludes32OneShotsAndMusic','riverVoiceInspectionDetectsSilentControl','noRiverVoiceInHome','battleLifecycleSubsystemAvailable',
    'actualBattleActiveForWaterCheck','noRiverVoiceInBattle','noRiverVoiceAfterRepeatedWorldBinding','noRiverVoiceAfterReturningHome','musicPreservedAcrossPagesAndWorldBinding',
    'noRiverVoiceInSettings','noRiverVoiceAfterDeferredBurst','noRiverVoiceDuringLimiterOverload','quietMusicMuteUsesActualSettingsSlider','quietMusicSliderMuteApplied',
    'quietSceneSettledAcrossGameTicks','noRiverVoiceInQuietScene','quietLiveMixerHasNoConstantSound')
if($requiredAudioChecks.Count -ne 61 -or @($requiredAudioChecks|Sort-Object -Unique).Count -ne 61){throw 'The native audio acceptance inventory must contain sixty-one unique checks.'}
$sourcePaths=@(Get-ChildItem -LiteralPath (Join-Path $modelRepo 'Unreal/RiftCrownArena/Source'),(Join-Path $modelRepo 'Unreal/RiftCrownArena/Config') -File -Recurse|
    Where-Object Extension -in @('.cpp','.h','.cs','.ini')|ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,$_.FullName).Replace('\','/')})+
    @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Build/Test-UnrealAudio.ps1','Assets/Source/Audio/audio_manifest.json')
$sourcePaths+=@(Get-ChildItem -LiteralPath (Join-Path $modelRepo 'Assets/Source/Audio') -File|Where-Object Extension -eq '.wav'|
    ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,$_.FullName).Replace('\','/')})
$audioRuns=[Collections.Generic.List[object]]::new();$audioSourcePins=[Collections.Generic.List[object]]::new()
$editorIdentity=$null
foreach($selection in @(@{name=$EditorAudioName;editor=$true},@{name=$ShippingAudioName;editor=$false})){
    if($selection.name -notmatch '^water132-[a-z0-9-]+$'){throw 'Choose explicit current audio hotfix runs.'}
    $root='Artifacts/QA/Audio/'+$selection.name+'/'
    $run=Read-ModelInput ($root+'run.json');$report=Read-ModelInput ($root+'audio-smoke.json')
    if($run.checkCount -ne 61 -or $run.reportSha256 -ne $modelPinIndex[$root+'audio-smoke.json'].sha256){throw 'Native audio report bytes differ from the completed process report.'}
    $started=Get-ModelUtcTimestamp $run.startedUtc;$finished=Get-ModelUtcTimestamp $run.finishedUtc
    if($run.passed -ne $true -or $run.version -ne $Version -or $run.editor -ne $selection.editor -or $run.audioDisabled -ne $false -or
       $run.freshReport -ne $true -or $run.timedOut -ne $false -or $run.exitCode -ne 0 -or @($run.errors).Count -ne 0 -or $run.processId -le 0 -or
       $run.sourcesUnchanged -ne $true -or $run.executableUnchanged -ne $true -or $started -ge $finished -or
       [IO.Path]::GetFullPath($run.report) -ne (Resolve-ModelInput ($root+'audio-smoke.json'))){throw 'Audio run lacks its actual enabled, fresh, clean process/source execution.'}
    $written=(Get-Item -LiteralPath (Resolve-ModelInput ($root+'audio-smoke.json'))).LastWriteTimeUtc
    if($written -lt $started -or $written -gt $finished){throw 'The native audio report was not freshly written during its actual run.'}
    Assert-ModelChecks $report $requiredAudioChecks.Count 'Live settings, music, UI, combat and postmix audio'
    if($report.checks.Count -ne 61 -or @($report.checks.name|Sort-Object -Unique).Count -ne 61 -or $report.loadedSounds -ne 41 -or
       $report.loadedCombatVariations -ne 20 -or $report.maximumOneShotVoices -ne 32){throw 'Audio checks or retained authored sound/voice inventory are incomplete.'}
    foreach($name in $requiredAudioChecks){if(@($report.checks|Where-Object name -eq $name).Count -ne 1){throw "An original live audio assertion was lost: $name"}}
    $river=$report.waterAmbienceLifecycle
    if($river.cue -ne 'SFX_river_ambience' -or $river.inspection -ne 'Live UAudioComponent instances in the active world, matching river SoundWave and IsPlaying' -or
       $river.controlPlayingVoices -ne 1 -or $report.quietTailSettleGameSeconds -lt .75){throw 'Actual river inspection lacks its live positive control or quiet limiter-tail settlement.'}
    foreach($field in @('homePlayingVoices','battlePlayingVoices','reboundPlayingVoices','returnedHomePlayingVoices','settingsPlayingVoices','burstPlayingVoices','overloadPlayingVoices','quietPlayingVoices')){
        if(!$river.PSObject.Properties[$field] -or $river.$field -ne 0){throw "The real river component scan must find zero live river voices: $field"}
    }
    foreach($output in @($report.normalMixedOutput,$report.limiterOverloadOutput)){
        if(!$output -or $output.silenceExpected -ne $false -or $output.peak -le .0001 -or $output.peak -ge .95 -or $output.rms -le .00001){throw 'Music/UI/combat output or limiter stress output is missing its actual bounded nonquiet mixer PCM.'}
    }
    $quiet=$report.quietMixedOutput
    if(!$quiet -or $quiet.silenceExpected -ne $true -or $quiet.peak -lt 0 -or $quiet.peak -gt .00001 -or $quiet.rms -lt 0 -or $quiet.rms -gt .000001){throw 'Actual settled quiet live MasterMix output still contains constant sound.'}
    $expectedSources=@($sourcePaths)
    if($selection.editor){$expectedSources+=@($context.runtimeModules.path)}
    if((($run.sourceHashes.path|Sort-Object)-join '|') -ne (($expectedSources|Sort-Object)-join '|')){
        throw 'Actual audio run did not pin its complete current source, configuration, wrapper, authored sound bank and applicable runtime inventory.'
    }
    if(@($run.sourceHashes.path|Sort-Object -Unique).Count -ne @($run.sourceHashes).Count){throw 'Audio launch-time source inventory contains duplicate paths.'}
    foreach($pin in $run.sourceHashes){
        Assert-HoverIdentity $pin;$audioSourcePins.Add($pin)
        if($pin.path -match '\.dll$'){
            if(!$selection.editor -or @($context.runtimeModules|Where-Object {$_.path -eq $pin.path -and $_.sha256 -eq $pin.sha256}).Count -ne 1){throw 'Audio launch-time runtime differs from the current complete-suite module.'}
        }elseif($pin.path -match '\.(cpp|h|cs|ini|uproject|ps1|json)$'){$null=Pin-ModelInput $pin.path}
    }
    if($selection.editor){
        $editor=[IO.Path]::GetFullPath($run.executable)
        if([IO.Path]::GetFileName($editor) -notin @('UnrealEditor-Cmd.exe','UnrealEditor.exe') -or !(Test-Path -LiteralPath $editor -PathType Leaf) -or
           (Get-FileHash -LiteralPath $editor).Hash.ToLowerInvariant() -ne $run.executableSha256 -or $run.diagnosticSource -ne 'editor-engine-log' -or
           @($run.sourceHashes|Where-Object path -match '\.dll$').Count -ne $context.runtimeModules.Count){throw 'Actual enabled Editor audio executable/runtime identities are missing.'}
        $engine=Pin-ModelInput $run.engineLog
        if($engine.path -ne $root+'engine.log'){throw 'Editor audio requires its actual run engine.log.'}
        $logItem=Get-Item -LiteralPath (Resolve-ModelInput $engine.path)
        $text=Get-Content -LiteralPath $logItem.FullName -Raw
        if($logItem.LastWriteTimeUtc -lt $started -or $logItem.LastWriteTimeUtc -gt $finished -or $text -notmatch 'Native audio smoke passed:' -or
           $text -notmatch 'FPlatformMisc::RequestExitWithStatus\(0, 0' -or $text -notmatch 'LogExit: Exiting\.' -or $text -match '(?im)(^|\])\s*\S+:\s*(Error|Fatal):|Fatal error|Unhandled Exception|Assertion failed'){throw 'Editor audio engine log is stale, lacks clean native completion, or contains an engine error.'}
        $processIdentity=[ordered]@{name=[IO.Path]::GetFileName($editor);sha256=$run.executableSha256;bytes=(Get-Item -LiteralPath $editor).Length}
        $editorIdentity=[pscustomobject]@{path=$editor;sha256=$processIdentity.sha256;bytes=$processIdentity.bytes}
    }else{
        if($run.executableSha256 -ne $native.sha256 -or [IO.Path]::GetFullPath($run.executable) -ne (Resolve-ModelInput $native.path)){throw 'Shipping audio did not execute the exact final Windows PE.'}
        $diagnostics=Assert-ModelShippingDiagnostics $run 'Current native audio hotfix'
        if($run.diagnosticVerification.processId -ne $run.processId -or (Get-ModelUtcTimestamp $run.diagnosticVerification.startedUTC) -ne $started -or
           (Get-ModelUtcTimestamp $run.diagnosticVerification.finishedUTC) -ne $finished -or
           @($diagnostics.records|Where-Object {$_.message -match '^Native audio smoke passed: '}).Count -ne 1){throw 'Shipping native diagnostic process context/completion differs from its audio run.'}
        $processIdentity=$native
    }
    foreach($output in @($report.normalMixedOutput,$report.limiterOverloadOutput,$report.quietMixedOutput)){
        if(!$output -or $output.channels -le 0 -or $output.sampleRate -le 0 -or $output.samples -le $output.channels*$output.sampleRate*.15 -or $output.seconds -le .15 -or
           $output.clippedFloatSamples -ne 0 -or $output.wavSaved -ne $true -or [IO.Path]::GetFileName($output.wavFile) -ne $output.wavFile -or $output.wavFile -notmatch '\.wav$'){throw 'Actual live mixer PCM and its saved WAV are missing.'}
        if($output.captureStage -ne 'Live MasterMix output after the submix effect chain; before PCM encoding'){throw 'Audio evidence must measure the real final MasterMix output.'}
        $wav=Pin-ModelInput ($root+$output.wavFile)
        $bytes=[IO.File]::ReadAllBytes((Resolve-ModelInput $wav.path))
        if($bytes.Length -le 44 -or [Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'RIFF' -or [Text.Encoding]::ASCII.GetString($bytes,8,4) -ne 'WAVE'){throw 'A measured native mixer output is not an actual WAV.'}
        $mtime=(Get-Item -LiteralPath (Resolve-ModelInput $wav.path)).LastWriteTimeUtc
        if($mtime -lt $started -or $mtime -gt $finished){throw 'A measured native mixer WAV was not freshly written during this run.'}
    }
    $audioRuns.Add([pscustomobject][ordered]@{name=$selection.name;editor=$selection.editor;version=$Version;passed=$true;processId=$run.processId;startedUtc=$run.startedUtc;finishedUtc=$run.finishedUtc;
        executable=$processIdentity;run=$root+'run.json';report=$root+'audio-smoke.json';checks=$report.checks;waterAmbienceLifecycle=$river;quietTailSettleGameSeconds=$report.quietTailSettleGameSeconds;
        normalMixedOutput=$report.normalMixedOutput;limiterOverloadOutput=$report.limiterOverloadOutput;quietMixedOutput=$report.quietMixedOutput})
}

$play=Read-ModelInput 'Artifacts/QA/launcher-play-smoke.json';$build=Read-ModelInput 'Artifacts/QA/installer-build.json';$installer=Read-ModelInput 'Artifacts/QA/installer-tests.json'
$runtime=Read-ModelInput 'Artifacts/QA/app-local-runtime.json'
$msi=Get-HoverIdentity ('Artifacts/Release/'+$release.installer);$gameArchive=Get-HoverIdentity ('Artifacts/Release/'+$release.archive);$launcher=Get-HoverIdentity ('Artifacts/Release/'+$release.launcher)
if($play.passed -ne $true -or $play.installed -ne $Version -or $play.nativeSha256 -ne $native.sha256 -or $play.verifiedGameFiles -ne $manifest.files.Count -or
   $play.legacySavePreserved -ne $true -or $play.launcherSha256 -ne $launcher.sha256 -or $play.nativeExitCode -ne 0 -or $play.bootstrapExitCode -ne 0 -or
   $build.passed -ne $true -or $build.version -ne $Version -or $build.sha256 -ne $msi.sha256 -or $build.size -ne $msi.bytes -or $build.gameArchiveSha256 -ne $gameArchive.sha256 -or
   $installer.passed -ne $true -or $installer.installedVersion -ne $Version -or $installer.upgradeMsiSha256 -ne $msi.sha256 -or $installer.installedArchiveSha256 -ne $gameArchive.sha256 -or
   $installer.installedGameFiles -ne $manifest.files.Count -or $installer.checkCount -lt 51 -or @($installer.checks).Count -ne $installer.checkCount -or $installer.failure -or
   $runtime.passed -ne $true -or $release.installerChecks -lt 51 -or $release.installerSha256 -ne $msi.sha256 -or $release.archiveSha256 -ne $gameArchive.sha256 -or
   $manifest.sha256 -ne $gameArchive.sha256 -or $manifest.size -ne $gameArchive.bytes -or $release.launcherSha256 -ne $launcher.sha256){throw 'Final native package, real WPF Play and fifty-one-check MSI lifecycle identities must agree.'}
if(@($play.contexts|Where-Object {$_.event -eq 'system_context' -and $_.gameVersion -eq $Version -and $_.build -eq 'Shipping' -and $_.processId -eq $play.nativeProcessId}).Count -lt 1){throw 'Actual WPF Play lacks its current observed Shipping context.'}
foreach($path in $ReleaseDocuments+@('Build/Test-UnrealAudio.ps1','Build/ShippingDiagnostics.ps1','Build/Test-Unreal.ps1','Build/Curate-UnrealQA.ps1','Build/Test-LauncherPlay.ps1','Installer/Test-Installer.ps1')){$null=Pin-ModelInput $path}
$inputs=@($modelInputs|Sort-Object path -Unique)
if(($inputs|Measure-Object bytes -Sum).Sum -gt 30MB){throw 'Compact audio hotfix evidence exceeds its 30 MiB budget.'}
$zipName=[IO.Path]::GetFileName($zipPath)
if(@(Get-Content -LiteralPath $sumPath|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'A frozen audio evidence checksum row already exists.'}
$summary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Actual enabled Editor and Shipping audio settings/voice/limiter checks, live river-loop absence lifecycle and quiet mixer output';
    nativeExecutable=$native;integrationTests=$integration.tests.Count;audioRuns=$audioRuns.ToArray();windowsChecks=$installer.checkCount;
    windowsDelivery=[ordered]@{manifest='Artifacts/Release/update-manifest.json';gameArchive=$gameArchive;installer=$msi;launcher=$launcher};inputs=$inputs;
    historicalAcceptanceScope='Existing 1.3.1 hover/model evidence stays historical and unchanged. No new model/art audit, card-drag rerun or balance cohort is claimed.';
    publicDelivery='Pending publication. Anonymous download and published update checks are separate postpublication evidence.'}
$summaryText=Convert-HoverSanitized ($summary|ConvertTo-Json -Depth 100);$null=$summaryText|ConvertFrom-Json -Depth 100
$summaryBytes=[Text.UTF8Encoding]::new($false).GetBytes($summaryText)
$temporary=$zipPath+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
$archive=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create);$inventory=[Collections.Generic.List[object]]::new()
try{
    foreach($pin in $inputs){
        Assert-HoverIdentity $pin;$source=Resolve-ModelInput $pin.path;$sanitized=[IO.Path]::GetExtension($source) -in @('.json','.log')
        if($sanitized){$text=Convert-HoverSanitized (Get-Content -LiteralPath $source -Raw);if([IO.Path]::GetExtension($source) -eq '.json'){$null=$text|ConvertFrom-Json -Depth 100};$bytes=[Text.UTF8Encoding]::new($false).GetBytes($text)}
        else{$bytes=[IO.File]::ReadAllBytes($source)}
        $entry=Write-HoverEntry $pin.path $bytes;$entry|Add-Member sourceSha256 $pin.sha256;$entry|Add-Member sourceBytes $pin.bytes;$entry|Add-Member sanitized $sanitized;$inventory.Add($entry)
    }
    $inventory.Add((Write-HoverEntry $summaryRelative $summaryBytes))
    $inventoryBytes=[Text.UTF8Encoding]::new($false).GetBytes(([ordered]@{schema=1;version=$Version;entries=$inventory.ToArray()}|ConvertTo-Json -Depth 20))
    $inventory.Add((Write-HoverEntry 'evidence-inventory.json' $inventoryBytes))
}finally{$archive.Dispose()}
$archive=[IO.Compression.ZipFile]::OpenRead($temporary)
try{
    if($archive.Entries.Count -ne $inventory.Count -or @($archive.Entries.FullName|Sort-Object -Unique).Count -ne $inventory.Count){throw 'Unexpected audio hotfix evidence inventory after reopening.'}
    foreach($pin in $inventory){
        $entry=$archive.GetEntry($pin.entry);if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Audio evidence entry size mismatch after reopening.'}
        $stream=$entry.Open();try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($hash -ne $pin.sha256){throw 'Audio evidence entry hash mismatch after reopening.'}
    }
}finally{$archive.Dispose()}
foreach($pin in @($context.sourceHashes)+@($context.runtimeModules)+@($audioSourcePins.ToArray())+@($payload.ToArray())+@($native,$msi,$gameArchive,$launcher)){Assert-HoverIdentity $pin}
if(!$editorIdentity -or (Get-FileHash -LiteralPath $editorIdentity.path).Hash.ToLowerInvariant() -ne $editorIdentity.sha256 -or
   (Get-Item -LiteralPath $editorIdentity.path).Length -ne $editorIdentity.bytes){throw 'The actual Editor executable changed during audio evidence acceptance.'}
if((Test-Path -LiteralPath $summaryPath) -or (Test-Path -LiteralPath $zipPath)){throw 'Frozen audio outputs appeared during acceptance; the verified temporary ZIP is retained.'}
Move-Item -LiteralPath $temporary -Destination $zipPath;[IO.File]::WriteAllBytes($summaryPath,$summaryBytes)
$zipHash=(Get-FileHash -LiteralPath $zipPath).Hash.ToLowerInvariant();$sumLines=@(Get-Content -LiteralPath $sumPath)
if(@($sumLines|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'The audio checksum row appeared during packaging.'}
$sumLines+=($zipHash+'  '+$zipName);[IO.File]::WriteAllLines($sumPath,$sumLines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($inventory.Count) verified audio hotfix evidence entries: $zipHash"
