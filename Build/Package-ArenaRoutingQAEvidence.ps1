param(
    [string]$Executable='Artifacts/Game-1.3.4/Windows/RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe',
    [string[]]$CaptureNames=@('arena134-initial','arena134-after5','arena134-left-player','arena134-right-player','arena134-left-enemy','arena134-right-enemy','arena134-720','arena134-meta-guide'),
    [string]$PortableReport='Artifacts/QA/ArenaRouting/portable-verification.json',
    [string]$BeforeWitness='Artifacts/QA/TowerPathing/before-fix-witness.json',
    [string]$MetaRoot='Artifacts/QA/arena134-meta100',
    [string]$MetaAudit='Docs/QA/arena-routing-meta-100.json',
    [string]$VisualReview='Artifacts/QA/arena134-visual-review.json',
    [string[]]$ReleaseDocuments=@('Docs/PATCH_NOTES_1.3.4.md')
)
# Current navigation acceptance; earlier witnesses and audio are history.
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this evidence packager with PowerShell 7.'}
$Version='1.3.4';$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$summaryRelative='Docs/QA/arena-routing-1.3.4.json'
$summaryPath=Join-Path $modelRepo $summaryRelative
$zipPath=Join-Path $modelRepo 'Artifacts/Release/RiftCrownArena-ArenaRouting-QAEvidence-1.3.4.zip'
$sumPath=Join-Path $modelRepo 'Artifacts/Release/SHA256SUMS.txt'
if((Test-Path -LiteralPath $summaryPath) -or (Test-Path -LiteralPath $zipPath)){throw 'The immutable arena/routing evidence already exists.'}
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
if($portable.schema -ne 'rift.portable.arena-routing.qa.v1' -or $portable.version -ne $Version -or $portable.passed -ne $true -or $portable.exitCode -ne 0 -or
   $portable.scenarioCount -ne 51 -or $portable.paidTowerFixtures -ne 84 -or $portable.paidGroundMembers -ne 96 -or $portable.paidBoundaryFixtures -ne 98 -or
   $portable.paidBoundaryGroundMembers -ne 112 -or $portable.clearancePaidFixtures -ne 182 -or $portable.clearancePaidMembers -ne 208 -or
   $portable.laneRouteFixtures -ne 84 -or $portable.laneRouteMembers -ne 96 -or $portable.totalPaidGroundFixtures -ne 266 -or $portable.totalPaidGroundMembers -ne 304 -or
   $portable.continuousPerimeterPaths -ne 768 -or $portable.oppositeTargetPaths -ne 28 -or $portable.aiSoakMatches -ne 21 -or
   $portable.sourcePinsUnchanged -ne $true -or @($portable.newScenarios).Count -ne 4 -or
   $portable.arena.width -ne 30 -or $portable.arena.height -ne 44 -or $portable.arena.halfWidth -ne 15 -or $portable.arena.halfHeight -ne 22 -or
   $portable.arena.coreDepth -ne 17.3 -or $portable.arena.guardDepth -ne 13.4 -or $portable.arena.guardX -ne 8.2 -or
   $portable.arena.pocketOuterX -ne 14.2 -or $portable.arena.pocketMaxDepth -ne 10.25){throw 'Fresh arena/routing portable simulation regressions must pass.'}
$portableLog=Pin-ModelInput $portable.log.path
if($portableLog.sha256 -ne $portable.log.sha256 -or $portableLog.bytes -ne $portable.log.bytes){throw 'The portable execution log changed after its observed pass.'}
$portableText=Get-Content -LiteralPath (Resolve-ModelInput $portableLog.path) -Raw
if($portableText -notmatch ('Native authoritative simulation: '+$portable.scenarioCount+' scenarios passed\.') -or $portableText -notmatch 'SOAK 21 complete matches'){throw 'Current portable regressions and full-match soak did not complete.'}
if(@([regex]::Matches($portableText,'(?m)^PASS .+\r?$')).Count -ne 51){throw 'The final portable log does not contain all fifty-one actual passing scenarios.'}
$portableExecutable=Get-HoverIdentity $portable.executable.path;Assert-HoverIdentity $portable.executable
$requiredPortableScenarios=@('expanded arena moves every tower back one tile and keeps full new edge placement',
    'all ground cards retain their bridge side and advance to Core through a destroyed lane',
    'continuous routes to an opposite target still use the current side bridge',
    'destroyed-lane Core advances stay deterministic across frame chunks')
if((($portable.newScenarios|Sort-Object)-join '|') -ne (($requiredPortableScenarios|Sort-Object)-join '|')){throw 'The current portable report omits an actual new arena/routing regression.'}
foreach($scenario in $requiredPortableScenarios){if($portableText -notmatch ('(?m)^PASS '+[regex]::Escape($scenario)+'\r?$')){throw 'A new portable arena/routing regression lacks its actual PASS record.'}}
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
# Preserve the old source-bound witness as evidence of the previous release,
# not a fresh execution against sources that intentionally changed in 1.3.4.
$towerHistory=Read-ModelInput 'Docs/QA/tower-pathing-1.3.3.json';$history=Read-ModelInput $BeforeWitness
$historicalPacket=Get-HoverIdentity 'Artifacts/Release/RiftCrownArena-TowerPathing-QAEvidence-1.3.3.zip'
$historicalRow=$historicalPacket.sha256+'  RiftCrownArena-TowerPathing-QAEvidence-1.3.3.zip'
$historicalSums=Pin-ModelInput 'Artifacts/QA/Release133History/SHA256SUMS.txt'
$oldWitness=@($towerHistory.inputs|Where-Object path -eq $BeforeWitness)
if($towerHistory.passed -ne $true -or $towerHistory.version -ne '1.3.3' -or $towerHistory.portableScenarios -ne 47 -or
   $towerHistory.integrationTests -lt 16 -or $history.historical -ne $true -or $history.version -ne '1.3.2' -or
   $history.caseCount -ne 6 -or $history.expectedStallsReproduced -ne $true -or $oldWitness.Count -ne 1 -or
   $oldWitness[0].sha256 -ne $modelPinIndex[$BeforeWitness].sha256 -or $oldWitness[0].bytes -ne $modelPinIndex[$BeforeWitness].bytes -or
   @((Get-Content -LiteralPath (Resolve-ModelInput $historicalSums.path))|Where-Object {$_ -eq $historicalRow}).Count -ne 1){throw 'The historical 1.3.3 packet and its original six-case witness must remain immutable.'}

$captureSourcePins=[Collections.Generic.List[object]]::new();$captures=[Collections.Generic.List[object]]::new()
$specs=[ordered]@{
    'arena134-initial'=@{age=0;route='clearance';team='both';width=1920;height=1080;page='Battle'}
    'arena134-after5'=@{age=5;route='clearance';team='both';width=1920;height=1080;page='Battle'}
    'arena134-left-player'=@{age=5;route='left_pocket';team='player';width=1920;height=1080;page='Battle'}
    'arena134-right-player'=@{age=5;route='right_pocket';team='player';width=1920;height=1080;page='Battle'}
    'arena134-left-enemy'=@{age=5;route='left_pocket';team='enemy';width=1920;height=1080;page='Battle'}
    'arena134-right-enemy'=@{age=5;route='right_pocket';team='enemy';width=1920;height=1080;page='Battle'}
    'arena134-720'=@{age=5;route='clearance';team='both';width=1280;height=720;page='Battle'}
    'arena134-meta-guide'=@{age=$null;route='';team='';width=1920;height=1080;page='MetaGuide'}
}
$physicalReview=Read-ModelInput $VisualReview
if($physicalReview.schema -ne 1 -or $physicalReview.version -ne $Version -or $physicalReview.passed -ne $true -or
   [string]::IsNullOrWhiteSpace($physicalReview.method) -or @($physicalReview.frames).Count -ne $specs.Count -or
   (($physicalReview.frames.name|Sort-Object)-join '|') -ne (($specs.Keys|Sort-Object)-join '|') -or
   $CaptureNames.Count -ne $specs.Count -or (($CaptureNames|Sort-Object)-join '|') -ne (($specs.Keys|Sort-Object)-join '|')){throw 'All eight exact current arena/routing/720p/Stats Guide frames need physical review.'}
$sourcePaths=@(Get-ChildItem -LiteralPath (Join-Path $modelRepo 'Unreal/RiftCrownArena/Source'),(Join-Path $modelRepo 'Unreal/RiftCrownArena/Config') -File -Recurse|
    Where-Object Extension -in @('.cpp','.h','.cs','.ini')|ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,$_.FullName).Replace('\','/')})+
    @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Build/Capture-Unreal.ps1')
foreach($name in $CaptureNames){
    $spec=$specs[$name];$root='Artifacts/QA/Visual/'+$name;$battle=$spec.page -eq 'Battle'
    $run=Read-ModelInput ($root+'.json');$state=Read-ModelInput ($root+'.state.json');$png=Pin-ModelInput ($root+'.png')
    if($run.passed -ne $true -or $run.version -ne $Version -or $run.name -ne $name -or $run.editor -ne $false -or $run.page -ne $spec.page -or
       $run.executableSha256 -ne $native.sha256 -or $run.executableUnchanged -ne $true -or $run.sourcePinsUnchanged -ne $true -or
       $run.captured -ne $true -or $run.stateCaptured -ne $true -or $run.timedOut -ne $false -or $run.exitCode -ne 0 -or @($run.errors).Count -ne 0 -or
       $run.width -ne $spec.width -or $run.height -ne $spec.height -or $run.actualWidth -ne $spec.width -or $run.actualHeight -ne $spec.height -or $run.resolutionMatches -ne $true -or
       $run.cameraFramingRequired -ne $battle -or $run.cameraFramingPassed -ne $true -or $run.allowExternalInput -ne $false -or
       $state.captureInput.active -ne $true -or $state.captureInput.consuming -ne $true){throw 'A current actual Shipping frame lacks clean exact-resolution/source/runtime acceptance.'}
    $started=Get-ModelUtcTimestamp $run.startedUTC;$finished=Get-ModelUtcTimestamp $run.finishedUTC
    if($run.processId -ne $run.diagnosticVerification.processId -or $started -ne (Get-ModelUtcTimestamp $run.diagnosticVerification.startedUTC) -or
       $finished -ne (Get-ModelUtcTimestamp $run.diagnosticVerification.finishedUTC)){throw 'Capture process timestamps differ from its native diagnostics.'}
    foreach($pin in @($modelPinIndex[$root+'.state.json'],$png)){
        $written=(Get-Item -LiteralPath (Resolve-ModelInput $pin.path)).LastWriteTimeUtc
        if($written -lt $started -or $written -gt $finished){throw 'An actual state/PNG was not freshly written during its observed run.'}
    }
    $review=@($physicalReview.frames|Where-Object name -eq $name)
    if($review.Count -ne 1 -or $review[0].png.path -ne $png.path -or $review[0].png.sha256 -ne $png.sha256 -or $review[0].png.bytes -ne $png.bytes -or
       $review[0].run -ne $root+'.json' -or $review[0].state -ne $root+'.state.json' -or $review[0].nativeSha256 -ne $native.sha256 -or
       $review[0].physicallyReviewed -ne $true -or (Get-ModelUtcTimestamp $physicalReview.reviewedAtUtc) -lt $finished -or
       ($battle -and $review[0].readableArenaAndCenteredHand -ne $true) -or (!$battle -and $review[0].statsGuideReadable -ne $true) -or
       ($battle -and [math]::Abs($review[0].age-$spec.age) -gt .00001) -or
       ($battle -and $spec.route -eq 'clearance' -and $spec.age -eq 5 -and $review[0].allEightMembersProgressAfterFiveSeconds -ne $true) -or
       ($battle -and $spec.route -ne 'clearance' -and $review[0].sameLaneCoreRouteReadable -ne $true)){throw 'Physical review does not pin the actual final frame and its required visible content.'}
    if((($run.sourcePins.path|Sort-Object)-join '|') -ne (($sourcePaths|Sort-Object)-join '|')){throw 'The Shipping capture did not pin its complete current source/configuration/wrapper.'}
    foreach($pin in $run.sourcePins){Assert-HoverIdentity $pin;$captureSourcePins.Add($pin);$null=Pin-ModelInput $pin.path}
    $diagnostics=Assert-ModelShippingDiagnostics $run 'Current Shipping arena capture'
    if(!$battle){
        if($run.scenario -ne '' -or $state.cameraFraming.activeBattleView -ne $false){throw 'The Stats Guide frame must show the actual menu page.'}
        $captures.Add([pscustomobject][ordered]@{name=$name;passed=$true;png=$png;state=$root+'.state.json';report=$root+'.json';processId=$run.processId;page=$spec.page})
        continue
    }
    $geometry=$state.arenaGeometry;$framing=$state.cameraFraming
    if($run.scenario -ne 'tower_pathing' -or $run.towerPathingPassed -ne $true -or $run.pathingAge -ne $spec.age -or $run.routeCase -ne $spec.route -or
       ($spec.team -ne 'both' -and $run.routeTeam -ne $spec.team) -or $state.speed -ne 0 -or $run.arenaGeometryRequired -ne $true -or $run.arenaGeometryPassed -ne $true -or
       $geometry.schemaVersion -ne 1 -or $geometry.passed -ne $true -or $geometry.floorPassed -ne $true -or $geometry.riverPassed -ne $true -or
       $geometry.bridgesPassed -ne $true -or $geometry.borderPassed -ne $true -or $geometry.widthTiles -ne 30 -or $geometry.heightTiles -ne 44 -or
       $geometry.playableFloorTiles -ne 1200 -or $geometry.expectedPlayableFloorTiles -ne 1200 -or $geometry.decorativeFloorTiles -ne 640 -or
       $geometry.expectedDecorativeFloorTiles -ne 640 -or $geometry.riverWidthTiles -ne 30 -or [math]::Abs($geometry.riverDepthTiles-3.3) -gt .000001 -or
       $geometry.borderStones -ne 104 -or @($geometry.bridges).Count -ne 2 -or
       $geometry.playableFloorCenters.minX -ne -14.5 -or $geometry.playableFloorCenters.maxX -ne 14.5 -or
       $geometry.playableFloorCenters.minZ -ne -21.5 -or $geometry.playableFloorCenters.maxZ -ne 21.5 -or
       $geometry.decorativeGroundCenters.minX -ne -19.5 -or $geometry.decorativeGroundCenters.maxX -ne 19.5 -or
       $geometry.decorativeGroundCenters.minZ -ne -24.5 -or $geometry.decorativeGroundCenters.maxZ -ne 24.5){throw 'Actual constructed arena geometry differs from the new thirty-by-forty-four board.'}
    if((($geometry.bridges.x|Sort-Object)-join '|') -ne '-7.2|7.2' -or @($geometry.bridges|Where-Object z -ne 0).Count){throw 'Actual rendered bridge centers differ from navigation.'}
    if($run.modelEnvelopeAvailable -ne $true -or $run.modelEnvelopePassed -ne $true -or $framing.projectedModelBoundsPassed -ne $true -or
       $framing.legalFieldWidthTiles -ne 30 -or $framing.legalFieldHeightTiles -ne 44 -or $framing.legalFieldHalfWidthTiles -ne 15 -or
       $framing.legalFieldHalfHeightTiles -ne 22 -or @($framing.legalFieldCorners).Count -ne 4 -or @($framing.modelEnvelope).Count -ne 16 -or
       @($framing.legalFieldCorners|Where-Object {$_.projected -ne $true -or $_.insideSafeArea -ne $true}).Count -or
       @($framing.modelEnvelope|Where-Object {$_.projected -ne $true -or $_.insideSafeArea -ne $true}).Count -or
       [math]::Abs($framing.legalFieldWidthPixels-30*$framing.tilePitchPixels) -gt .001){throw 'The expanded field or actual enlarged silhouettes extend beneath the unchanged HUD.'}
    $corners=@($framing.legalFieldCorners|ForEach-Object {$_.tileX.ToString()+','+$_.tileY.ToString()}|Sort-Object)
    if(($corners-join '|') -ne '-15,-22|-15,22|15,-22|15,22'){throw 'Camera legal-field samples do not cover all four new boundaries.'}
    $pathing=$state.towerPathing;$age=$spec.age
    if($pathing.passed -ne $true -or $pathing.routeCase -ne $spec.route -or $pathing.requestedAge -ne $age -or
       [math]::Abs($pathing.sampledAge-$age) -gt .00001 -or $pathing.fixedSteps -ne $age*60 -or
       $pathing.expectedUnitCount -lt 2 -or $pathing.actualUnitCount -ne $pathing.expectedUnitCount -or
       @($pathing.units).Count -ne $pathing.actualUnitCount -or @($pathing.units.id|Sort-Object -Unique).Count -ne $pathing.actualUnitCount -or
       @($pathing.towers).Count -ne 6 -or @($pathing.towers.id|Sort-Object -Unique).Count -ne 6 -or $pathing.towerNavigationPadding -ne .22 -or
       $pathing.spawnTowerClearancePassed -ne $true -or $pathing.allStepTowerClearancePassed -ne $true -or
       $pathing.allContinuousSegmentTowerClearancePassed -ne $true -or $pathing.progressRequired -ne ($age -ge 2)){throw 'The actual fixture lacks its complete clearance/age/member proof.'}
    foreach($team in @('player','enemy')){
        $side=$(if($team -eq 'player'){1}else{-1});$towers=@($pathing.towers|Where-Object team -eq $team)
        if($towers.Count -ne 3 -or @($towers|Where-Object {$_.kind -eq 'core' -and $_.x -eq 0 -and [math]::Abs($_.z-$side*17.3) -lt .000001}).Count -ne 1 -or
           @($towers|Where-Object {$_.kind -eq 'guard' -and [math]::Abs([math]::Abs($_.x)-8.2) -lt .000001 -and [math]::Abs($_.z-$side*13.4) -lt .000001}).Count -ne 2){throw 'Actual mirrored Core and Guard positions differ from the new arena.'}
    }
    if($spec.route -eq 'clearance'){
        if($pathing.schemaVersion -ne 1 -or $pathing.route -ne 'ordinary Match Spawn / fixed steps / live arena presentation' -or $pathing.ordinarySpawnOnly -ne $true -or
           $pathing.deploymentCount -ne 6 -or $pathing.expectedUnitCount -ne 8 -or
           @($pathing.units|Where-Object team -eq 'player').Count -ne 4 -or @($pathing.units|Where-Object team -eq 'enemy').Count -ne 4){throw 'The ordinary six-tower fixture must contain all eight actual mirrored members.'}
    }else{
        $lane=$(if($spec.route -eq 'left_pocket'){-1}else{1})
        $coreTarget=@($pathing.towers|Where-Object {$_.id -eq $pathing.expectedCoreId -and $_.kind -eq 'core' -and $_.team -ne $spec.team -and $_.dead -ne $true})
        $destroyedGuard=@($pathing.towers|Where-Object {$_.id -eq $pathing.destroyedGuardId -and $_.kind -eq 'guard' -and $_.lane -eq $lane -and $_.team -ne $spec.team -and $_.dead -eq $true})
        $oppositeGuard=@($pathing.towers|Where-Object {$_.id -eq $pathing.oppositeGuardId -and $_.kind -eq 'guard' -and $_.lane -eq -$lane -and $_.team -ne $spec.team -and $_.dead -ne $true})
        if($pathing.schemaVersion -ne 2 -or $pathing.route -ne 'SetTowerHP / paid Match Play / fixed steps / live arena presentation' -or $pathing.routeTeam -ne $spec.team -or
           $pathing.paidPlayOnly -ne $true -or $pathing.ordinarySpawnOnly -ne $false -or $pathing.deploymentCount -ne 2 -or $pathing.sameSideGuardDestroyed -ne $true -or
           $pathing.paidCostAndCyclePassed -ne $true -or $pathing.allCoreTargetsObserved -ne $true -or $pathing.coreTargetsRequired -ne $true -or
           $pathing.allBridgeHistoriesPassed -ne $true -or $pathing.crossingRequired -ne $true -or $pathing.intendedBridge -ne $lane -or
           $coreTarget.Count -ne 1 -or $destroyedGuard.Count -ne 1 -or $oppositeGuard.Count -ne 1 -or @($pathing.paidPlays).Count -ne 2 -or
           @($pathing.paidPlays.role|Sort-Object -Unique).Count -ne 2 -or (($pathing.paidPlays.role|Sort-Object)-join '|') -ne 'own_half|pocket' -or
           ($pathing.paidPlays|Measure-Object memberCount -Sum).Sum -ne $pathing.actualUnitCount){throw 'The paid route fixture lacks both roles, the destroyed same-lane Guard, surviving opposite Guard, and intended Core.'}
        foreach($play in $pathing.paidPlays){
            if($play.accepted -ne $true -or $play.handCycledOnce -ne $true -or $play.cost -le 0 -or $play.memberCount -lt 1 -or
               $play.aetherBefore -ne 10 -or $play.spentDelta -ne $play.cost -or $play.aetherAfter -ne (10-$play.cost) -or
               @($pathing.units|Where-Object {$_.role -eq $play.role -and $_.cardId -eq $play.cardId}).Count -ne $play.memberCount){throw 'An actual route play did not preserve its original paid cost, single hand cycle, and full member count.'}
        }
    }
    foreach($unit in $pathing.units){
        if($unit.present -ne $true -or $unit.radius -le 0 -or !$unit.PSObject.Properties['minimumSegmentTowerClearance'] -or
           $unit.minimumSegmentTowerClearance -lt -.000001 -or $unit.spawnTowerClearance -lt -.000001 -or $unit.minimumStepTowerClearance -lt -.000001){throw 'A member is missing or overlaps a live tower along an actual movement segment.'}
        $spawnMinimum=[double]::PositiveInfinity;$currentMinimum=[double]::PositiveInfinity
        foreach($obstacle in $pathing.towers|Where-Object dead -ne $true){
            $sum=$unit.radius+$obstacle.radius+.22
            $spawnMinimum=[math]::Min($spawnMinimum,[math]::Sqrt([math]::Pow($unit.spawnX-$obstacle.x,2)+[math]::Pow($unit.spawnZ-$obstacle.z,2))-$sum)
            $currentMinimum=[math]::Min($currentMinimum,[math]::Sqrt([math]::Pow($unit.x-$obstacle.x,2)+[math]::Pow($unit.z-$obstacle.z,2))-$sum)
        }
        $dx=$unit.x-$unit.spawnX;$dz=$unit.z-$unit.spawnZ;$distance=[math]::Sqrt($dx*$dx+$dz*$dz);$forward=$(if($unit.team -eq 'player'){-$dz}else{$dz})
        if([math]::Abs($spawnMinimum-$unit.spawnTowerClearance) -gt .000001 -or $spawnMinimum -lt -.000001 -or $currentMinimum -lt -.000001 -or
           [math]::Abs($distance-$unit.distanceFromSpawn) -gt .000001){throw 'Actual member coordinates disagree with the reported tower clearance or movement.'}
        if($spec.route -eq 'clearance'){
            if($unit.alive -ne $true -or [math]::Abs($forward-$unit.progressTowardRiver) -gt .000001 -or
               @($pathing.towers|Where-Object {$_.id -eq $unit.deploymentTowerId -and $_.team -eq $unit.team}).Count -ne 1){throw 'An ordinary spawn is not associated with its own live tower.'}
        }else{
            if($unit.team -ne $spec.team -or $unit.intendedBridge -ne $lane -or $unit.expectedCoreId -ne $pathing.expectedCoreId -or
               $unit.sawCoreTarget -ne $true -or ($unit.alive -eq $true -and ($unit.targetKind -ne 'core' -or $unit.targetId -ne $pathing.expectedCoreId)) -or
               $unit.bridgeHistoryPassed -ne $true -or [math]::Abs($forward-$unit.progressForward) -gt .000001 -or
               @($unit.bridgeHistory|Where-Object {$_.bridge -notin @(0,$lane)}).Count){throw 'A paid pocket member selected the opposite Guard, wrong Core, or wrong bridge.'}
            if($unit.role -eq 'own_half' -and ($unit.crossedRiver -ne $true -or
               @($unit.crossingHistory|Where-Object {$_.event -eq 'river_center' -and $_.bridge -eq $lane -and $_.x*$lane -gt 0}).Count -ne 1)){throw 'The own-half member did not actually cross its source-side river bridge.'}
        }
        if($age -eq 0){if($distance -gt .000001){throw 'The initial frame must show real corrected spawn positions before progression.'}}
        elseif($pathing.progressPassed -ne $true -or $unit.progressPassed -ne $true -or $distance -le .5 -or $forward -le .25){throw 'Every actual member must make forward progress after five seconds.'}
    }
    $marker=$(if($spec.route -eq 'clearance'){
        $progress=$(if($age -eq 0){0}else{1});"Actual tower pathing capture: 6 deployments, 8 members, $($age*60) fixed steps, spawnClear=1 stepClear=1 segmentClear=1 progress=$progress passed=1"
    }else{"Actual tower route capture: case=$($spec.route) team=$($spec.team) paid=2 members=$($pathing.actualUnitCount) steps=$($age*60) core=1 bridge=1 clearance=1 progress=1 passed=1"})
    if(@($diagnostics.records|Where-Object message -eq $marker).Count -ne 1){throw 'Actual structured native diagnostics lack this exact captured route/clearance/progress completion.'}
    $captures.Add([pscustomobject][ordered]@{name=$name;passed=$true;png=$png;state=$root+'.state.json';report=$root+'.json';processId=$run.processId;arenaGeometry=$geometry;towerPathing=$pathing;framing=$framing})
}
$metaSourcePins=[Collections.Generic.List[object]]::new()
if($MetaRoot -ne 'Artifacts/QA/arena134-meta100'){throw 'Only the explicit fresh one-hundred-match arena/routing cohort can be inherited.'}
$metaContext=Read-ModelInput ($MetaRoot+'/context.json');$metaRun=Read-ModelInput ($MetaRoot+'/run.json')
$meta=Read-ModelInput $MetaAudit;$metaLog=Pin-ModelInput ($MetaRoot+'/engine.log')
foreach($path in @($metaRun.dataset,$metaRun.export)){
    $resolved=Resolve-ModelInput $path;$relative=[IO.Path]::GetRelativePath($modelRepo,$resolved).Replace('\','/')
    if($relative -notmatch '^Artifacts/QA/arena134-meta100/UserData/(Meta/[A-F0-9]{32}\.json|meta-validation\.json)$'){throw 'Only the two actual synthetic cohort data/export files may enter the evidence archive.'}
    $null=$modelSyntheticInputs.Add($relative)
}
$data=Read-ModelInput $metaRun.dataset;$export=Read-ModelInput $metaRun.export
$datasetPin=Pin-ModelInput $metaRun.dataset
$metaStarted=Get-ModelUtcTimestamp $metaContext.startedUtc;$metaFinished=Get-ModelUtcTimestamp $metaRun.completedUtc
$runtime=@($context.runtimeModules|Where-Object path -eq 'Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll')
if($metaContext.schema -ne 1 -or $metaContext.requestedGames -ne 100 -or $metaRun.passed -ne $true -or $metaRun.games -ne 100 -or
   $runtime.Count -ne 1 -or $metaContext.runtimeSha256 -ne $runtime[0].sha256 -or $metaRun.runtimeSha256 -ne $runtime[0].sha256 -or
   $metaRun.datasetSha256 -ne $datasetPin.sha256 -or $metaStarted -ge $metaFinished -or $data.games -ne 100 -or $data.economyChecks -ne 100 -or
   $data.invalid -ne 0 -or $data.economyInvalid -ne 0 -or $data.rulesSnapshot.navigationRevision -ne 3 -or $data.telemetryRevision -ne 3 -or
   $data.rulesSnapshot.arenaWidth -ne 30 -or $data.rulesSnapshot.arenaHeight -ne 44 -or $data.rulesSnapshot.coreDepth -ne 17.3 -or
   $data.rulesSnapshot.guardDepth -ne 13.4 -or $data.rulesSnapshot.guardX -ne 8.2 -or $data.rulesSnapshot.pocketOuterX -ne 14.2 -or
   $data.rulesSnapshot.pocketMaxDepth -ne 10.25 -or $data.rulesSnapshot.navigation -notmatch 'source-side bridge commitment; same-lane Guard else Core$' -or
   $meta.completeObserved -ne $true -or $meta.observedGames -ne 100 -or $meta.target -ne 100 -or $meta.attempts -ne 100 -or
   $meta.failedChecks -ne 0 -or @($meta.errors).Count -ne 0 -or $meta.finalExportValidated -ne $true -or $meta.fingerprint -ne $data.fingerprint){throw 'The actual fresh navigation-revision-three cohort and independent complete audit must agree.'}
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
if($auditEvidence.schema -ne 2 -or $auditEvidence.version -ne $Version -or $auditEvidence.navigationRevision -ne 3 -or
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
foreach($failedRunHistory in @('Docs/QA/tower-pathing-first-failed-native-1.3.3.json','Docs/QA/arena-routing-first-failed-native-1.3.4.json')){
    if(Test-Path -LiteralPath (Join-Path $modelRepo $failedRunHistory)){$null=Pin-ModelInput $failedRunHistory}
}
$audioHistory=Read-ModelInput 'Docs/QA/audio-hotfix-1.3.2.json'
if($audioHistory.passed -ne $true -or $audioHistory.version -ne '1.3.2' -or @($audioHistory.audioRuns).Count -ne 2 -or
   @($audioHistory.audioRuns|Where-Object {$_.passed -ne $true -or @($_.checks).Count -ne 61}).Count){throw 'Historical prior audio acceptance is missing.'}
foreach($path in @('Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Presentation/RiftBattleAudioSubsystem.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation/RiftBattleAudioSubsystem.h')){
    $old=@($audioHistory.inputs|Where-Object path -eq $path)
    if($old.Count -ne 1){throw 'The historical audio packet lacks its unchanged runtime source identity.'}
    Assert-HoverIdentity $old[0]
}
$inputs=@($modelInputs|Sort-Object path -Unique)
if(($inputs|Measure-Object bytes -Sum).Sum -gt 60MB){throw 'The compact arena/routing evidence exceeds its 60 MiB budget.'}
$zipName=[IO.Path]::GetFileName($zipPath)
if(@(Get-Content -LiteralPath $sumPath|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'A frozen pathing evidence checksum row already exists.'}
$summary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Expanded30x44 arena, shifted towers, same-source-side bridge commitment and destroyed-lane Core targeting, paid drops, continuous clearance, forward progress, saved replay, complete native regression, Stats Guide, fresh100-match cohort and Windows delivery';
    nativeExecutable=$native;integrationTests=$integration.tests.Count;portableScenarios=$portable.scenarioCount;portableExecutable=$portableExecutable;portableProvenance=$portable.provenance;
    arena=$portable.arena;paidGroundFixtures=$portable.totalPaidGroundFixtures;paidGroundMembers=$portable.totalPaidGroundMembers;
    laneRouteFixtures=$portable.laneRouteFixtures;laneRouteMembers=$portable.laneRouteMembers;continuousPerimeterPaths=768;oppositeTargetPaths=28;
    nativePaidGroundFixtures=238;nativePaidGroundMembers=272;captures=$captures.ToArray();physicalVisualReview=$VisualReview;
    meta=[ordered]@{navigationRevision=3;games=100;fingerprint=$meta.fingerprint;audit=$MetaAudit;runtime=$metaSnapshot;allSixAuthoritativeSourcesMatch=$true;newCohortObserved=$true};
    beforeFix=[ordered]@{scope='Preserved development history only; original six stalls and baseline rejection remain bound to the immutable 1.3.3 packet';version='1.3.3';packet=$historicalPacket;checksumFile=$historicalSums;report=$BeforeWitness;baselineCommit=$history.baselineCommit;cases=$history.cases;newWitnessRun=$false};
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
    if($archive.Entries.Count -ne $inventory.Count -or @($archive.Entries.FullName|Sort-Object -Unique).Count -ne $inventory.Count){throw 'Unexpected arena/routing evidence inventory after reopening.'}
    foreach($pin in $inventory){
        $entry=$archive.GetEntry($pin.entry);if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Arena/routing evidence entry size mismatch after reopening.'}
        $stream=$entry.Open();try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($hash -ne $pin.sha256){throw 'Arena/routing evidence entry hash mismatch after reopening.'}
    }
}finally{$archive.Dispose()}
foreach($pin in @($context.sourceHashes)+@($context.runtimeModules)+@($captureSourcePins.ToArray())+@($metaSourcePins.ToArray())+@($portable.sourcePins)+
    @($payload.ToArray())+@($native,$msi,$gameArchive,$launcher,$portableExecutable,$metaSnapshot,$historicalPacket)){Assert-HoverIdentity $pin}
if((Test-Path -LiteralPath $summaryPath) -or (Test-Path -LiteralPath $zipPath)){throw 'Frozen arena/routing outputs appeared during acceptance; the verified temporary ZIP is retained.'}
Move-Item -LiteralPath $temporary -Destination $zipPath;[IO.File]::WriteAllBytes($summaryPath,$summaryBytes)
$zipHash=(Get-FileHash -LiteralPath $zipPath).Hash.ToLowerInvariant();$sumLines=@(Get-Content -LiteralPath $sumPath)
if(@($sumLines|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'The arena/routing checksum row appeared during packaging.'}
$sumLines+=($zipHash+'  '+$zipName);[IO.File]::WriteAllLines($sumPath,$sumLines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($inventory.Count) verified arena/routing evidence entries: $zipHash"


