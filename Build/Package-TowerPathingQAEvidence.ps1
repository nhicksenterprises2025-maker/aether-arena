param(
    [string]$Executable='Artifacts/Game-1.3.3/Windows/RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe',
    [string[]]$CaptureNames=@('tower133-initial','tower133-after5'),
    [string]$PortableReport='Artifacts/QA/TowerPathing/portable-verification.json',
    [string]$BeforeWitness='Artifacts/QA/TowerPathing/before-fix-witness.json',
    [string]$MetaRoot='Artifacts/QA/tower133-meta100',
    [string]$MetaAudit='Docs/QA/tower-pathing-meta-100.json',
    [string]$VisualReview='Artifacts/QA/tower133-visual-review.json',
    [string[]]$ReleaseDocuments=@('Docs/PATCH_NOTES_1.3.3.md')
)
# Current navigation acceptance; earlier witnesses and audio are history.
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this evidence packager with PowerShell 7.'}
$Version='1.3.3';$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$summaryRelative='Docs/QA/tower-pathing-1.3.3.json'
$summaryPath=Join-Path $modelRepo $summaryRelative
$zipPath=Join-Path $modelRepo 'Artifacts/Release/RiftCrownArena-TowerPathing-QAEvidence-1.3.3.zip'
$sumPath=Join-Path $modelRepo 'Artifacts/Release/SHA256SUMS.txt'
if((Test-Path -LiteralPath $summaryPath) -or (Test-Path -LiteralPath $zipPath)){throw 'The immutable tower-pathing evidence already exists.'}
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
   $release.manifestSha256 -ne $modelPinIndex['Artifacts/Release/update-manifest.json'].sha256){throw 'Final Windows release, manifest and actual native pathing-test executable differ.'}
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
if($integration.failed -ne 0 -or $integration.notRun -ne 0 -or @($integration.tests).Count -lt 16 -or
   @($integration.tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0}).Count -ne 0 -or
   @($integration.tests.fullTestPath|Sort-Object -Unique).Count -ne $integration.tests.Count -or $context.version -ne $Version -or
   $context.allScenariosObserved -ne $true -or $context.commandletExitCode -ne 0 -or $context.wrapperExitCode -ne 0){throw 'The complete current native suite must pass.'}
foreach($required in @('Rift.Integration.Typography','Rift.Integration.BattleInputRouting','Rift.Integration.SpellCastReplay','Rift.Integration.TowerPathing')){
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


$core=@('Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftAI.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCombat.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftDeckAnalysis.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftPathfinding.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftSimulation.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftSimulation.h')
$portable=Read-ModelInput $PortableReport
# Final producer fields are accepted only once its real execution is recorded.
if($portable.schema -ne 'rift.portable.tower-pathing.qa.v1' -or $portable.version -ne $Version -or $portable.passed -ne $true -or $portable.exitCode -ne 0 -or
   $portable.scenarioCount -ne 47 -or $portable.paidTowerFixtures -ne 84 -or $portable.paidGroundMembers -ne 96 -or $portable.paidBoundaryFixtures -ne 98 -or
   $portable.paidBoundaryGroundMembers -ne 112 -or $portable.totalPaidGroundFixtures -ne 182 -or $portable.totalPaidGroundMembers -ne 208 -or
   $portable.continuousPerimeterPaths -ne 768 -or $portable.aiSoakMatches -ne 21 -or $portable.sourcePinsUnchanged -ne $true -or @($portable.newScenarios).Count -ne 6){throw 'Fresh post-fix portable simulation regressions must pass.'}
$portableLog=Pin-ModelInput $portable.log.path
if($portableLog.sha256 -ne $portable.log.sha256 -or $portableLog.bytes -ne $portable.log.bytes){throw 'The portable execution log changed after its observed pass.'}
$portableText=Get-Content -LiteralPath (Resolve-ModelInput $portableLog.path) -Raw
if($portableText -notmatch ('Native authoritative simulation: '+$portable.scenarioCount+' scenarios passed\.') -or $portableText -notmatch 'SOAK 21 complete matches'){throw 'Current portable regressions and full-match soak did not complete.'}
if(@([regex]::Matches($portableText,'(?m)^PASS .+\r?$')).Count -ne 47){throw 'The final portable log does not contain all forty-seven actual passing scenarios.'}
$portableExecutable=Get-HoverIdentity $portable.executable.path;Assert-HoverIdentity $portable.executable
foreach($scenario in $portable.newScenarios){if($portableText -notmatch ('(?m)^PASS '+[regex]::Escape($scenario)+'\r?$')){throw 'A new portable tower-pathing regression lacks its actual PASS record.'}}
$preSource=Read-ModelInput $portable.preRunSourcePinRecord.path
$prePin=Pin-ModelInput $portable.preRunSourcePinRecord.path
if($prePin.sha256 -ne $portable.preRunSourcePinRecord.sha256 -or $prePin.bytes -ne $portable.preRunSourcePinRecord.bytes -or
   @($preSource).Count -ne 8 -or @($portable.sourcePins).Count -ne 8 -or
   (Get-Item -LiteralPath (Resolve-ModelInput $prePin.path)).LastWriteTimeUtc -ge (Get-Item -LiteralPath (Resolve-ModelInput $portableExecutable.path)).LastWriteTimeUtc){throw 'The portable execution lacks its exact eight-source record captured before compilation.'}
$portableSourcePins=@($portable.sourcePins|ForEach-Object {Get-HoverIdentity $_.path})
foreach($path in $core+@('Build/Tests/RiftSimulationTests.cpp','Build/Tests/Run-NativeSimulationTests.ps1')){
    if(@($portableSourcePins|Where-Object path -eq $path).Count -ne 1){throw "Current portable execution lacks its source: $path"}
}
foreach($pin in $portable.sourcePins){
    Assert-HoverIdentity $pin;$null=Pin-ModelInput $pin.path
    $before=@($preSource|Where-Object path -eq $pin.path)
    if($before.Count -ne 1 -or $before[0].sha256 -ne $pin.sha256 -or $before[0].bytes -ne $pin.bytes){throw 'A portable authored source changed between its recorded pre-run and post-run pins.'}
}
$history=Read-ModelInput $BeforeWitness
if($history.schema -ne 'rift.tower-pathing.before-fix.v1' -or $history.historical -ne $true -or $history.version -ne '1.3.2' -or
   $history.baselineCommit -ne 'e27e14fd9c631a6027cfde6826c8ff8407cd7b7f' -or $history.expectedStallsReproduced -ne $true -or
   $history.sourcePinsUnchanged -ne $true -or $history.caseCount -ne 6 -or @($history.cases).Count -ne 6 -or $history.durationSeconds -ne 5 -or
   (($history.sourcePins.repositoryPath|Sort-Object)-join '|') -ne (($core|Sort-Object)-join '|')){throw 'The six baseline stalls lack their source-bound development-history witness.'}
foreach($pin in $history.sourcePins){
    Assert-HoverIdentity $pin;$null=Pin-ModelInput $pin.path
    if($pin.baselineCommit -ne $history.baselineCommit -or $pin.gitBlob -notmatch '^[a-f0-9]{40}$'){throw 'Historical source lacks its exact immutable baseline Git identity.'}
    $blob=(& git -C $modelRepo rev-parse ($history.baselineCommit+':'+$pin.repositoryPath)).Trim()
    if($LASTEXITCODE -ne 0 -or $blob -ne $pin.gitBlob){throw 'Historical source Git blob differs from the recorded baseline commit.'}
    $bytes=[IO.File]::ReadAllBytes((Resolve-ModelInput $pin.path));$header=[Text.Encoding]::ASCII.GetBytes("blob $($bytes.Length)`0")
    $combined=[byte[]]::new($header.Length+$bytes.Length);$header.CopyTo($combined,0);$bytes.CopyTo($combined,$header.Length)
    if([Convert]::ToHexString([Security.Cryptography.SHA1]::HashData($combined)).ToLowerInvariant() -ne $blob){throw 'Reconstructed historical source bytes differ from the actual Git blob.'}
}
foreach($pin in @($history.source,$history.results)){Assert-HoverIdentity $pin;$null=Pin-ModelInput $pin.path}
Assert-HoverIdentity $history.executable
$witnessText=Get-Content -LiteralPath (Resolve-ModelInput $history.results.path) -Raw
$caseKeys=[Collections.Generic.HashSet[string]]::new()
foreach($case in $history.cases){
    $key=$case.team+'|'+$case.startX+','+$case.startZ
    if(!$caseKeys.Add($key) -or $case.team -notin @('player','enemy') -or $case.paid -ne $true -or $case.seconds -ne 5 -or $case.delta -ne 0 -or
       $case.afterX -ne $case.startX -or $case.afterZ -ne $case.startZ -or $case.pathPoints -ne 0){throw 'A historical witness does not record an actual distinct paid five-second stall.'}
    $row=$case.team+' paid=1 start='+$case.startX+','+$case.startZ+' after5='+$case.afterX+','+$case.afterZ+' delta=0 target='+$case.targetId+' path=0'
    if(!$witnessText.Contains($row)){throw 'The normalized baseline stall differs from its actual witness console output.'}
}
foreach($key in @('player|0.5,17.5','player|0.5,16.5','player|8.5,13.5','enemy|0.5,-17.5','enemy|0.5,-16.5','enemy|8.5,-13.5')){
    if(!$caseKeys.Contains($key)){throw 'The historical witness does not contain all six original mirrored Core/Guard stalls.'}
}
$baselineRejection=$history.currentRegressionAgainstBaseline
if($baselineRejection.expectedExitCode -ne 1 -or $baselineRejection.observedExitCode -ne 1 -or $baselineRejection.rejected -ne $true -or
   $baselineRejection.precedingOriginalScenariosPassed -ne 30 -or $baselineRejection.failure -ne 'ground movement clears entire own-structure segment'){throw 'The new regression was not shown to reject the original navigation bug.'}
foreach($pin in @($baselineRejection.source,$baselineRejection.log)){Assert-HoverIdentity $pin;$null=Pin-ModelInput $pin.path}
Assert-HoverIdentity $baselineRejection.executable
if((Get-Content -LiteralPath (Resolve-ModelInput $baselineRejection.log.path) -Raw) -notmatch 'FAIL:?\s+ground movement clears entire own-structure segment'){throw 'Actual historical regression output lacks its expected clearance failure.'}

$captureSourcePins=[Collections.Generic.List[object]]::new();$captures=[Collections.Generic.List[object]]::new()
$physicalReview=Read-ModelInput $VisualReview
if($physicalReview.schema -ne 1 -or $physicalReview.version -ne $Version -or $physicalReview.passed -ne $true -or
   [string]::IsNullOrWhiteSpace($physicalReview.method) -or @($physicalReview.frames).Count -ne 2 -or
   (($physicalReview.frames.name|Sort-Object)-join '|') -ne 'tower133-after5|tower133-initial'){throw 'Physical review of both exact current Shipping tower-pathing frames is required.'}
$sourcePaths=@(Get-ChildItem -LiteralPath (Join-Path $modelRepo 'Unreal/RiftCrownArena/Source'),(Join-Path $modelRepo 'Unreal/RiftCrownArena/Config') -File -Recurse|
    Where-Object Extension -in @('.cpp','.h','.cs','.ini')|ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,$_.FullName).Replace('\','/')})+
    @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Build/Capture-Unreal.ps1')
if($CaptureNames.Count -ne 2 -or (($CaptureNames|Sort-Object)-join '|') -ne 'tower133-after5|tower133-initial'){throw 'Both explicit current initial and five-second Shipping frames are required.'}
foreach($name in $CaptureNames){
    $root='Artifacts/QA/Visual/'+$name
    $run=Read-ModelInput ($root+'.json');$state=Read-ModelInput ($root+'.state.json');$png=Pin-ModelInput ($root+'.png')
    $age=$(if($name -eq 'tower133-initial'){0}else{5})
    if($run.passed -ne $true -or $run.version -ne $Version -or $run.name -ne $name -or $run.editor -ne $false -or $run.scenario -ne 'tower_pathing' -or $run.page -ne 'Battle' -or
       $run.executableSha256 -ne $native.sha256 -or $run.executableUnchanged -ne $true -or $run.sourcePinsUnchanged -ne $true -or $run.towerPathingPassed -ne $true -or
       $run.captured -ne $true -or $run.stateCaptured -ne $true -or $run.timedOut -ne $false -or $run.exitCode -ne 0 -or @($run.errors).Count -ne 0 -or
       $run.width -ne 1920 -or $run.height -ne 1080 -or $run.actualWidth -ne 1920 -or $run.actualHeight -ne 1080 -or $run.resolutionMatches -ne $true -or
       $run.cameraFramingRequired -ne $true -or $run.cameraFramingPassed -ne $true -or $run.modelEnvelopeAvailable -ne $true -or $run.modelEnvelopePassed -ne $true -or
       $run.allowExternalInput -ne $false -or $state.captureInput.active -ne $true -or $state.captureInput.consuming -ne $true -or $state.speed -ne 0 -or $run.pathingAge -ne $age){throw 'Current actual Shipping tower-pathing capture/framing/source execution did not pass.'}
    $started=Get-ModelUtcTimestamp $run.startedUTC;$finished=Get-ModelUtcTimestamp $run.finishedUTC
    if($run.processId -ne $run.diagnosticVerification.processId -or $started -ne (Get-ModelUtcTimestamp $run.diagnosticVerification.startedUTC) -or
       $finished -ne (Get-ModelUtcTimestamp $run.diagnosticVerification.finishedUTC)){throw 'Tower capture process timestamps differ from its native diagnostics.'}
    foreach($pin in @($modelPinIndex[$root+'.state.json'],$png)){
        $written=(Get-Item -LiteralPath (Resolve-ModelInput $pin.path)).LastWriteTimeUtc
        if($written -lt $started -or $written -gt $finished){throw 'Actual tower capture state/PNG was not freshly written during its observed run.'}
    }
    $review=@($physicalReview.frames|Where-Object name -eq $name)
    if($review.Count -ne 1 -or $review[0].png.path -ne $png.path -or $review[0].png.sha256 -ne $png.sha256 -or $review[0].png.bytes -ne $png.bytes -or
       $review[0].run -ne $root+'.json' -or $review[0].state -ne $root+'.state.json' -or $review[0].nativeSha256 -ne $native.sha256 -or
       $review[0].physicallyReviewed -ne $true -or $review[0].readableArenaAndCenteredHand -ne $true -or [math]::Abs($review[0].age-$age) -gt .00001 -or
       ($age -eq 5 -and $review[0].allEightMembersProgressAfterFiveSeconds -ne $true) -or
       (Get-ModelUtcTimestamp $physicalReview.reviewedAtUtc) -lt $finished){throw 'Physical inspection does not pin this actual final frame, readable arena/hand, and required five-second progress.'}
    if((($run.sourcePins.path|Sort-Object)-join '|') -ne (($sourcePaths|Sort-Object)-join '|')){throw 'The live Shipping fixture did not pin its complete current source/configuration/capture wrapper.'}
    foreach($pin in $run.sourcePins){Assert-HoverIdentity $pin;$captureSourcePins.Add($pin);$null=Pin-ModelInput $pin.path}
    $pathing=$state.towerPathing
    if($pathing.schemaVersion -ne 1 -or $pathing.route -ne 'ordinary Match Spawn / fixed steps / live arena presentation' -or $pathing.passed -ne $true -or
       $pathing.ordinarySpawnOnly -ne $true -or $pathing.deploymentCount -ne 6 -or $pathing.expectedUnitCount -ne 8 -or $pathing.actualUnitCount -ne 8 -or
       @($pathing.units).Count -ne 8 -or @($pathing.units.id|Sort-Object -Unique).Count -ne 8 -or @($pathing.towers).Count -ne 6 -or
       @($pathing.towers.id|Sort-Object -Unique).Count -ne 6 -or $pathing.towerNavigationPadding -ne .22 -or $pathing.spawnTowerClearancePassed -ne $true -or
       $pathing.allStepTowerClearancePassed -ne $true -or $pathing.allContinuousSegmentTowerClearancePassed -ne $true -or
       $pathing.requestedAge -ne $age -or [math]::Abs($pathing.sampledAge-$age) -gt .00001 -or
       $pathing.fixedSteps -ne $age*60 -or $pathing.progressRequired -ne ($age -ge 2)){throw 'Actual tower fixture lacks its complete ordinary-spawn/clearance/age/member proof.'}
    foreach($team in @('player','enemy')){
        if(@($pathing.towers|Where-Object team -eq $team).Count -ne 3 -or @($pathing.units|Where-Object team -eq $team).Count -ne 4){throw 'The tower fixture must mirror all three towers and four actual members for both teams.'}
    }
    foreach($unit in $pathing.units){
        $tower=@($pathing.towers|Where-Object {$_.id -eq $unit.deploymentTowerId -and $_.team -eq $unit.team})
        if($tower.Count -ne 1 -or $unit.present -ne $true -or $unit.alive -ne $true -or $unit.radius -le 0 -or
           !$unit.PSObject.Properties['minimumSegmentTowerClearance'] -or $unit.minimumSegmentTowerClearance -lt -.000001 -or
           $unit.spawnTowerClearance -lt -.000001 -or $unit.minimumStepTowerClearance -lt -.000001){throw 'A deployed member is missing, dead or overlaps a tower footprint along its actual path.'}
        $spawnMinimum=[double]::PositiveInfinity;$currentMinimum=[double]::PositiveInfinity
        foreach($obstacle in $pathing.towers){
            $sum=$unit.radius+$obstacle.radius+.22
            $spawnMinimum=[math]::Min($spawnMinimum,[math]::Sqrt([math]::Pow($unit.spawnX-$obstacle.x,2)+[math]::Pow($unit.spawnZ-$obstacle.z,2))-$sum)
            $currentMinimum=[math]::Min($currentMinimum,[math]::Sqrt([math]::Pow($unit.x-$obstacle.x,2)+[math]::Pow($unit.z-$obstacle.z,2))-$sum)
        }
        $dx=$unit.x-$unit.spawnX;$dz=$unit.z-$unit.spawnZ;$distance=[math]::Sqrt($dx*$dx+$dz*$dz);$forward=$(if($unit.team -eq 'player'){-$dz}else{$dz})
        if([math]::Abs($spawnMinimum-$unit.spawnTowerClearance) -gt .000001 -or $spawnMinimum -lt -.000001 -or $currentMinimum -lt -.000001 -or
           [math]::Abs($distance-$unit.distanceFromSpawn) -gt .000001 -or [math]::Abs($forward-$unit.progressTowardRiver) -gt .000001){throw 'Actual unit coordinates do not agree with their reported tower clearance/progress.'}
        if($age -eq 0){if($distance -gt .000001){throw 'The initial fixture must show the actual corrected spawn positions before progression.'}}
        elseif($pathing.progressPassed -ne $true -or $unit.progressPassed -ne $true -or $distance -le .5 -or $forward -le .25){throw 'Every actual member must advance away from its own tower toward the river after five seconds.'}
    }
    $diagnostics=Assert-ModelShippingDiagnostics $run 'Current Shipping tower capture'
    $progress=$(if($age -eq 0){0}else{1})
    $marker="Actual tower pathing capture: 6 deployments, 8 members, $($age*60) fixed steps, spawnClear=1 stepClear=1 segmentClear=1 progress=$progress passed=1"
    if(@($diagnostics.records|Where-Object message -eq $marker).Count -ne 1){throw 'Actual structured native diagnostics do not certify this captured fixture age/clearance/progress.'}
    $captures.Add([pscustomobject][ordered]@{name=$name;passed=$true;png=$png;state=$root+'.state.json';report=$root+'.json';processId=$run.processId;towerPathing=$pathing})
}

$metaSourcePins=[Collections.Generic.List[object]]::new()
if($MetaRoot -ne 'Artifacts/QA/tower133-meta100'){throw 'Only the explicit fresh one-hundred-match navigation cohort can be inherited.'}
$metaContext=Read-ModelInput ($MetaRoot+'/context.json');$metaRun=Read-ModelInput ($MetaRoot+'/run.json')
$meta=Read-ModelInput $MetaAudit;$metaLog=Pin-ModelInput ($MetaRoot+'/engine.log')
foreach($path in @($metaRun.dataset,$metaRun.export)){
    $resolved=Resolve-ModelInput $path;$relative=[IO.Path]::GetRelativePath($modelRepo,$resolved).Replace('\','/')
    if($relative -notmatch '^Artifacts/QA/tower133-meta100/UserData/(Meta/[A-F0-9]{32}\.json|meta-validation\.json)$'){throw 'Only the two actual synthetic cohort data/export files may enter the evidence archive.'}
    $null=$modelSyntheticInputs.Add($relative)
}
$data=Read-ModelInput $metaRun.dataset;$export=Read-ModelInput $metaRun.export
$datasetPin=Pin-ModelInput $metaRun.dataset
$metaStarted=Get-ModelUtcTimestamp $metaContext.startedUtc;$metaFinished=Get-ModelUtcTimestamp $metaRun.completedUtc
$runtime=@($context.runtimeModules|Where-Object path -eq 'Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll')
if($metaContext.schema -ne 1 -or $metaContext.requestedGames -ne 100 -or $metaRun.passed -ne $true -or $metaRun.games -ne 100 -or
   $runtime.Count -ne 1 -or $metaContext.runtimeSha256 -ne $runtime[0].sha256 -or $metaRun.runtimeSha256 -ne $runtime[0].sha256 -or
   $metaRun.datasetSha256 -ne $datasetPin.sha256 -or $metaStarted -ge $metaFinished -or $data.games -ne 100 -or $data.economyChecks -ne 100 -or
   $data.invalid -ne 0 -or $data.economyInvalid -ne 0 -or $data.rulesSnapshot.navigationRevision -ne 2 -or $data.telemetryRevision -ne 3 -or
   $meta.completeObserved -ne $true -or $meta.observedGames -ne 100 -or $meta.target -ne 100 -or $meta.attempts -ne 100 -or
   $meta.failedChecks -ne 0 -or @($meta.errors).Count -ne 0 -or $meta.finalExportValidated -ne $true -or $meta.fingerprint -ne $data.fingerprint){throw 'The actual fresh navigation-revision-two cohort and independent complete audit must agree.'}
$metaSnapshot=Get-HoverIdentity ($MetaRoot+'/RuntimeProject/Binaries/Win64/UnrealEditor-RiftCrownArena.dll')
if($metaSnapshot.sha256 -ne $runtime[0].sha256){throw 'The cohort runtime snapshot differs from the current built/tested native module.'}
if((($metaContext.simulationSources.path|Sort-Object)-join '|') -ne (($core|Sort-Object)-join '|')){throw 'The fresh Meta cohort must freeze all six authoritative sources.'}
foreach($pin in $metaContext.simulationSources){
    Assert-HoverIdentity $pin;$metaSourcePins.Add($pin);$null=Pin-ModelInput $pin.path
    if(@($context.sourceHashes|Where-Object {$_.path -eq $pin.path -and $_.sha256 -eq $pin.sha256}).Count -ne 1){throw 'The cohort source differs from the current complete native-suite context.'}
}
$metaText=Get-Content -LiteralPath (Resolve-ModelInput $metaLog.path) -Raw
if($metaText -notmatch 'Native Meta validation completed: 100 actual games\.' -or $metaText -match '(?im)(^|\])\s*\S+:\s*(Error|Fatal):'){throw 'The fresh native Meta process lacks actual completion or contains an engine error.'}
foreach($pin in @($datasetPin,$modelPinIndex[[IO.Path]::GetRelativePath($modelRepo,(Resolve-ModelInput $metaRun.export)).Replace('\','/')],$metaLog)){
    $written=(Get-Item -LiteralPath (Resolve-ModelInput $pin.path)).LastWriteTimeUtc
    if($written -lt $metaStarted -or $written -gt $metaFinished){throw 'The fresh synthetic cohort outputs were not written within their observed execution.'}
}
if((Get-ModelUtcTimestamp $meta.auditedAtUtc) -lt $metaFinished){throw 'The independent cohort audit precedes actual native completion.'}
$auditEvidence=$meta.evidence
if($auditEvidence.schema -ne 2 -or $auditEvidence.version -ne $Version -or $auditEvidence.navigationRevision -ne 2 -or
   $auditEvidence.inputsUnchanged -ne $true -or $auditEvidence.exitCode -ne 0 -or $auditEvidence.runtimeSha256 -ne $runtime[0].sha256 -or
   (Resolve-ModelInput $meta.dataset) -ne (Resolve-ModelInput $metaRun.dataset)){throw 'The independent navigation audit lacks current unchanged launch-time input/runtime provenance.'}
$expectedMetaInputs=@('Build/Tests/Audit-NativeMeta.py',$datasetPin.path,
    ([IO.Path]::GetRelativePath($modelRepo,(Resolve-ModelInput $metaRun.export)).Replace('\','/')),
    ($MetaRoot+'/context.json'),($MetaRoot+'/run.json'),($MetaRoot+'/engine.log'))
$observedMetaInputs=@($auditEvidence.inputs|ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,(Resolve-ModelInput $_.path)).Replace('\','/')})
if((($observedMetaInputs|Sort-Object)-join '|') -ne (($expectedMetaInputs|Sort-Object)-join '|')){throw 'The independent audit did not freeze the exact auditor/cohort/export/runtime context/log inputs.'}
foreach($pin in $auditEvidence.inputs){
    Assert-HoverIdentity $pin;$current=Pin-ModelInput $pin.path
    if($pin.bytes -ne $current.bytes){throw 'An independent audit input length changed after actual execution.'}
}
$auditor=Pin-ModelInput $auditEvidence.auditor.path
if($auditor.path -ne 'Build/Tests/Audit-NativeMeta.py' -or $auditor.sha256 -ne $auditEvidence.auditor.sha256 -or $auditor.bytes -ne $auditEvidence.auditor.bytes){throw 'The current independent auditor source identity changed.'}
if((($auditEvidence.simulationSources.path|Sort-Object)-join '|') -ne (($core|Sort-Object)-join '|')){throw 'The independent current Meta audit lacks all six authoritative source identities.'}
foreach($pin in $auditEvidence.simulationSources){Assert-HoverIdentity $pin}
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

foreach($path in $ReleaseDocuments+@('Build/Capture-Unreal.ps1','Build/Validate-NativeMeta.ps1','Build/Tests/Audit-NativeMeta.py','Build/Tests/RiftSimulationTests.cpp',
    'Build/Test-Unreal.ps1','Build/Curate-UnrealQA.ps1','Build/ShippingDiagnostics.ps1','Build/Test-LauncherPlay.ps1','Installer/Test-Installer.ps1')){$null=Pin-ModelInput $path}
$failedRunHistory='Docs/QA/tower-pathing-first-failed-native-1.3.3.json'
if(Test-Path -LiteralPath (Join-Path $modelRepo $failedRunHistory)){$null=Pin-ModelInput $failedRunHistory}
$audioHistory=Read-ModelInput 'Docs/QA/audio-hotfix-1.3.2.json'
if($audioHistory.passed -ne $true -or $audioHistory.version -ne '1.3.2' -or @($audioHistory.audioRuns).Count -ne 2 -or
   @($audioHistory.audioRuns|Where-Object {$_.passed -ne $true -or @($_.checks).Count -ne 61}).Count){throw 'Historical prior audio acceptance is missing.'}
foreach($path in @('Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Presentation/RiftBattleAudioSubsystem.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation/RiftBattleAudioSubsystem.h')){
    $old=@($audioHistory.inputs|Where-Object path -eq $path)
    if($old.Count -ne 1){throw 'The historical audio packet lacks its unchanged runtime source identity.'}
    Assert-HoverIdentity $old[0]
}
$inputs=@($modelInputs|Sort-Object path -Unique)
if(($inputs|Measure-Object bytes -Sum).Sum -gt 30MB){throw 'The compact tower-pathing evidence exceeds its 30 MiB budget.'}
$zipName=[IO.Path]::GetFileName($zipPath)
if(@(Get-Content -LiteralPath $sumPath|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'A frozen pathing evidence checksum row already exists.'}
$summary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Ordinary deployments and continuous tower clearance, forward progress, saved replay, complete native regression, new one-hundred-match navigation cohort and Windows delivery';
    nativeExecutable=$native;integrationTests=$integration.tests.Count;portableScenarios=$portable.scenarioCount;portableExecutable=$portableExecutable;portableProvenance=$portable.provenance;
    paidGroundFixtures=182;paidGroundMembers=208;continuousPerimeterPaths=768;captures=$captures.ToArray();physicalVisualReview=$VisualReview;
    meta=[ordered]@{navigationRevision=2;games=100;fingerprint=$meta.fingerprint;audit=$MetaAudit;runtime=$metaSnapshot;allSixAuthoritativeSourcesMatch=$true;newCohortObserved=$true};
    beforeFix=[ordered]@{scope='Development history only; six actual observations of units stuck on the previous navigation implementation';report=$BeforeWitness;baselineCommit=$history.baselineCommit;cases=$history.cases;newRegressionRejectsOldImplementation=$true};
    windowsChecks=$installer.checkCount;windowsDelivery=[ordered]@{manifest='Artifacts/Release/update-manifest.json';gameArchive=$gameArchive;installer=$msi;launcher=$launcher};inputs=$inputs;
    historicalAudio=[ordered]@{version='1.3.2';checksPerRun=61;runtimeSourceUnchanged=$true;newAudioRun=$false};newModelArtAuditRun=$false;
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
    if($archive.Entries.Count -ne $inventory.Count -or @($archive.Entries.FullName|Sort-Object -Unique).Count -ne $inventory.Count){throw 'Unexpected tower-pathing evidence inventory after reopening.'}
    foreach($pin in $inventory){
        $entry=$archive.GetEntry($pin.entry);if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Tower-pathing evidence entry size mismatch after reopening.'}
        $stream=$entry.Open();try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($hash -ne $pin.sha256){throw 'Tower-pathing evidence entry hash mismatch after reopening.'}
    }
}finally{$archive.Dispose()}
foreach($pin in @($context.sourceHashes)+@($context.runtimeModules)+@($captureSourcePins.ToArray())+@($metaSourcePins.ToArray())+@($portable.sourcePins)+
    @($history.sourcePins)+@($payload.ToArray())+@($native,$msi,$gameArchive,$launcher,$portableExecutable,$metaSnapshot,$history.executable,$baselineRejection.executable)){Assert-HoverIdentity $pin}
if((Test-Path -LiteralPath $summaryPath) -or (Test-Path -LiteralPath $zipPath)){throw 'Frozen tower-pathing outputs appeared during acceptance; the verified temporary ZIP is retained.'}
Move-Item -LiteralPath $temporary -Destination $zipPath;[IO.File]::WriteAllBytes($summaryPath,$summaryBytes)
$zipHash=(Get-FileHash -LiteralPath $zipPath).Hash.ToLowerInvariant();$sumLines=@(Get-Content -LiteralPath $sumPath)
if(@($sumLines|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'The tower-pathing checksum row appeared during packaging.'}
$sumLines+=($zipHash+'  '+$zipName);[IO.File]::WriteAllLines($sumPath,$sumLines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($inventory.Count) verified tower-pathing evidence entries: $zipHash"


