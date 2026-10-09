param(
    [string]$Executable='Artifacts/Game-1.3.5/Windows/RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe',
    [string[]]$CaptureNames=@('collision135-final-initial','collision135-final-crowd5','collision135-final-bridge12','collision135-final-contact5','collision135-final-air5','collision135-final-720'),
    [string]$PortableReport='Artifacts/QA/UnitCollision/portable-verification.json',
    [string]$MetaRoot='Artifacts/QA/collision135-meta100-final',
    [string]$MetaAudit='Docs/QA/unit-collision-meta-100.json',
    [string]$VisualReview='Artifacts/QA/collision135-final-visual-review.json',
    [string]$PerformanceName='collision135-stress-final',
    [string[]]$ReleaseDocuments=@('Docs/PATCH_NOTES_1.3.5.md')
)
# Current navigation acceptance; earlier witnesses and audio are history.
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this evidence packager with PowerShell 7.'}
$Version='1.3.5';$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$summaryRelative='Docs/QA/unit-collision-1.3.5.json'
$summaryPath=Join-Path $modelRepo $summaryRelative
$zipPath=Join-Path $modelRepo 'Artifacts/Release/RiftCrownArena-UnitCollision-QAEvidence-1.3.5.zip'
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
if($integration.failed -ne 0 -or $integration.notRun -ne 0 -or @($integration.tests).Count -lt 17 -or
   @($integration.tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0}).Count -ne 0 -or
   @($integration.tests.fullTestPath|Sort-Object -Unique).Count -ne $integration.tests.Count -or $context.version -ne $Version -or
   $context.allScenariosObserved -ne $true -or $context.commandletExitCode -ne 0 -or $context.wrapperExitCode -ne 0){throw 'The complete current native suite must pass.'}
foreach($required in @('Rift.Integration.Typography','Rift.Integration.BattleInputRouting','Rift.Integration.SpellCastReplay','Rift.Integration.TowerPathing','Rift.Integration.UnitCollision')){
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
$requiredCollisionScenarios=@(
    'every physical paid card separates same-tile members and existing bodies on both teams',
    'ground and air occupy distinct collision layers while spells have no body',
    'opposing paid melee swarms make contact without overlap or crossing through one another',
    'opposing structure-only runners pass on their selected bridge without tunneling or permanent deadlock',
    'dense paid mixed ground crowd queues at bridges and makes bounded forward progress',
    'collision crowd remains deterministic across fixed-step frame chunks',
    'stationary paid ranged attackers remain solid while friendly melee routes past',
    'real Raven aura stun keeps the stopped body solid to an unstunned paid follower',
    'full physical deployment capacity rejects paid cards atomically without consuming IDs',
    'paid three-building pocket releases every ground member through the selected bridge'
)
if($portable.schema -ne 'rift.portable.unit-collision.qa.v1' -or $portable.version -ne $Version -or $portable.passed -ne $true -or $portable.exitCode -ne 0 -or
   $portable.scenarioCount -lt 61 -or $portable.aiSoakMatches -ne 21 -or $portable.sourcePinsUnchanged -ne $true -or
   $portable.physicalRosterCards -ne 11 -or $portable.spellBodies -ne 0 -or @($portable.scenarios).Count -ne $portable.scenarioCount -or
   @($portable.scenarios|Sort-Object -Unique).Count -ne $portable.scenarioCount -or $portable.collisionSkinTiles -ne .02 -or
   $portable.collisionSummary.endpointPairChecks -le 10000 -or $portable.collisionSummary.sweptPairChecks -le 10000 -or
   $portable.collisionSummary.minimumBodyGap -lt .02-1e-8 -or $portable.collisionSummary.minimumSweptGap -lt .02-1e-8 -or $portable.structureSkinTiles -ne .22 -or
   @($portable.denseBridgeQueues).Count -ne 4 -or @($portable.denseBridgeQueues|Where-Object {$_.members -ne 24 -or $_.crossed -lt 18}).Count -or
   @($portable.fullCapacityFixtures).Count -ne 2 -or @($portable.fullCapacityFixtures|Where-Object {$_.buildings -lt 300 -or $_.troops -le 0 -or $_.rejectedSandboxRequests -le 0 -or $_.paidRejected -ne 1 -or $_.airFollowup -ne 1}).Count -or
   $portable.buildingPocketSpeedToleranceTiles -ne 1e-8 -or @($portable.paidBuildingPockets).Count -ne 4 -or
   @($portable.paidBuildingPockets|Where-Object {$_.team -notin @(0,1) -or $_.lane -notin @(-1,1) -or $_.plays -ne 16 -or $_.members -ne 31 -or $_.buildings -ne 3 -or $_.ground -ne 11 -or $_.crossed -ne 11 -or $_.livingBuildings -ne 3 -or $_.buildingLifetimeSeconds -ne 25 -or $_.seconds -ne $(if($_.team -eq 0){12}else{15}) -or ($_.team -eq 0 -and $_.crossedAt12 -ne 11) -or ($_.team -eq 0 -and $_.lane -eq 1 -and $_.adjustedBuildingDrops -ne 0)}).Count -or
   @($portable.paidBuildingPockets|ForEach-Object {[string]$_.team+':'+[string]$_.lane}|Sort-Object -Unique).Count -ne 4){throw 'Fresh complete portable unit collision regressions and full-match soak must pass.'}
if(($portable.buildingPocketDeck -join '|') -ne 'ironclad|twin_blades|boulderback|archer_tower|sky_manta|vampire_bats|storm_raven|frost_fang' -or
   ($portable.buildingPocketPaidSlots -join ',') -ne '1,2,2,3,0,1,0,1,0,1,2,3,2,3,2,3' -or @($portable.buildingRequestedTileAdjustments).Count -ne 4 -or
   @($portable.buildingRequestedTileAdjustments|Where-Object {$_.team -ne 1 -or $_.lane -notin @(-1,1) -or $_.play -notin @(9,15) -or ($_.requestedX -eq $_.legalX -and $_.requestedZ -eq $_.legalZ)}).Count -or
   @($portable.buildingRequestedTileAdjustments|ForEach-Object {[string]$_.team+':'+[string]$_.lane+':'+[string]$_.play}|Sort-Object -Unique).Count -ne 4){throw 'The mirrored paid building-pocket fixture lacks its actual original deck/slot sequence and lawful enemy placement adjustments.'}
$portableLog=Pin-ModelInput $portable.log.path
if($portableLog.sha256 -ne $portable.log.sha256 -or $portableLog.bytes -ne $portable.log.bytes){throw 'The portable execution log changed after its observed pass.'}
$portableText=Get-Content -LiteralPath (Resolve-ModelInput $portableLog.path) -Raw
if($portableText -notmatch ('Native authoritative simulation: '+$portable.scenarioCount+' scenarios passed\.') -or $portableText -notmatch 'SOAK 21 complete matches' -or
   @([regex]::Matches($portableText,'(?m)^PASS .+\r?$')).Count -ne $portable.scenarioCount){throw 'The current portable log lacks its complete actual PASS inventory and full-match soak.'}
foreach($scenario in $portable.scenarios){if($portableText -notmatch ('(?m)^PASS '+[regex]::Escape($scenario)+'\r?$')){throw 'A portable collision regression lacks its actual PASS record.'}}
foreach($scenario in $requiredCollisionScenarios){if($scenario -notin $portable.scenarios){throw 'The complete portable inventory lacks a required new collision scenario.'}}
$portableExecutable=Get-HoverIdentity $portable.executable.path;Assert-HoverIdentity $portable.executable
$preSource=Read-ModelInput $portable.preRunSourcePinRecord.path;$prePin=Pin-ModelInput $portable.preRunSourcePinRecord.path
if($prePin.sha256 -ne $portable.preRunSourcePinRecord.sha256 -or $prePin.bytes -ne $portable.preRunSourcePinRecord.bytes -or
   @($preSource).Count -ne 10 -or @($portable.sourcePins).Count -ne 10 -or
   (Get-Item -LiteralPath (Resolve-ModelInput $prePin.path)).LastWriteTimeUtc -ge (Get-Item -LiteralPath (Resolve-ModelInput $portableExecutable.path)).LastWriteTimeUtc){throw 'The portable execution lacks its exact ten-source record captured before compilation.'}
$portableSourcePins=@($portable.sourcePins|ForEach-Object {Get-HoverIdentity $_.path})
foreach($path in $core+@('Build/Tests/RiftSimulationTests.cpp','Build/Tests/Run-NativeSimulationTests.ps1','Build/Tests/Verify-UnitCollision.ps1','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Tests/RiftUnitCollisionTests.cpp')){
    if(@($portableSourcePins|Where-Object path -eq $path).Count -ne 1){throw "Current portable execution lacks its source: $path"}
}
foreach($pin in $portable.sourcePins){
    Assert-HoverIdentity $pin;$null=Pin-ModelInput $pin.path
    $before=@($preSource|Where-Object path -eq $pin.path)
    if($before.Count -ne 1 -or $before[0].sha256 -ne $pin.sha256 -or $before[0].bytes -ne $pin.bytes){throw 'A portable authored source changed between recorded pre-run and post-run pins.'}
}
$history=Read-ModelInput 'Docs/QA/arena-routing-1.3.4.json'
$historicalPacket=Get-HoverIdentity 'Artifacts/Release/RiftCrownArena-ArenaRouting-QAEvidence-1.3.4.zip'
$historicalSums=Pin-ModelInput 'Artifacts/QA/Release134History/SHA256SUMS.txt'
$historicalRow=$historicalPacket.sha256+'  RiftCrownArena-ArenaRouting-QAEvidence-1.3.4.zip'
if($history.version -ne '1.3.4' -or $history.passed -ne $true -or
   @((Get-Content -LiteralPath (Resolve-ModelInput $historicalSums.path))|Where-Object {$_ -eq $historicalRow}).Count -ne 1){throw 'The immutable prior arena/routing packet must remain bound to its preserved checksum.'}

function Assert-CollisionSamples($collision){
    $previous=@{};$pairs=@{};$lastBodies=@{};$farCrossings=@{};$centerCrossings=@{};$minimum=[double]::PositiveInfinity;$minimumSegment=[double]::PositiveInfinity;$crossLayer=0;$observations=0
    $expectedStep=0;$startTime=$collision.samples[0].time
    foreach($sample in $collision.samples){
        if($sample.step -ne $expectedStep -or [math]::Abs($sample.time-$startTime-$expectedStep/60.0) -gt .000001 -or
           @($sample.bodies.id|Sort-Object -Unique).Count -ne @($sample.bodies).Count){throw 'Collision body samples are incomplete, duplicated or not fixed sixty-hertz steps.'}
        $current=@{};foreach($body in $sample.bodies){
            if($body.radius -le 0 -or $body.kind -notin @(0,1,2,3) -or ($body.kind -ne 0 -and $body.flying)){throw 'Collision sample has an invalid body radius or physical layer.'}
            $key=[string]$body.id;$current[$key]=$body;$lastBodies[$key]=$body
            if($body.kind -eq 0 -and !$body.flying){
                if($body.team -notin @('player','enemy')){throw 'A ground body lacks its actual team side.'}
                $side=if($body.team -eq 'player'){1}else{-1}
                if($body.z*$side -le 0){$centerCrossings[$key]=$true}
                if(!$farCrossings.ContainsKey($key) -and $body.z*$side -lt -1.93){$farCrossings[$key]=@{step=$sample.step;time=$sample.time;x=$body.x;z=$body.z}}
            }
            if($body.kind -ne 0 -and $previous.ContainsKey($key) -and ([math]::Abs($body.x-$previous[$key].x) -gt .000001 -or [math]::Abs($body.z-$previous[$key].z) -gt .000001)){throw 'A stationary structure was displaced by crowd navigation.'}
        }
        $bodies=@($sample.bodies)
        for($i=0;$i -lt $bodies.Count;$i++){for($j=$i+1;$j -lt $bodies.Count;$j++){
            $a=$bodies[$i];$b=$bodies[$j];$airA=$a.kind -eq 0 -and $a.flying;$airB=$b.kind -eq 0 -and $b.flying
            $padding=if(!$airA -and !$airB -and ($a.kind -ne 0 -or $b.kind -ne 0)){.22}else{.02}
            $dx=$a.x-$b.x;$dz=$a.z-$b.z;$gap=[math]::Sqrt($dx*$dx+$dz*$dz)-$a.radius-$b.radius-$padding
            if($airA -ne $airB){if($gap -lt 0){$crossLayer++};continue}
            if($gap -lt -1e-8){throw 'Independent fixed-step coordinates reveal same-layer body penetration.'}
            $key=[string]$a.id+':'+[string]$b.id
            if(!$pairs.ContainsKey($key)){$pairs[$key]=@{minimumGap=$gap;minimumSegmentGap=$gap;currentGap=$gap;observations=0;layer=$(if($airA){'air'}else{'ground'});padding=$padding;opponents=$a.team -ne $b.team;stationaryBody=$a.kind -ne 0 -or $b.kind -ne 0}}
            $pair=$pairs[$key];$pair.minimumGap=[math]::Min($pair.minimumGap,$gap);$pair.currentGap=$gap;$pair.observations++;$observations++;$minimum=[math]::Min($minimum,$gap)
            $aKey=[string]$a.id;$bKey=[string]$b.id
            if($previous.ContainsKey($aKey) -and $previous.ContainsKey($bKey)){
                $x=$previous[$aKey].x-$previous[$bKey].x;$z=$previous[$aKey].z-$previous[$bKey].z;$vx=$dx-$x;$vz=$dz-$z;$length=$vx*$vx+$vz*$vz
                $t=if($length -gt 0){[math]::Clamp(-($x*$vx+$z*$vz)/$length,0.0,1.0)}else{0.0}
                $sx=$x+$t*$vx;$sz=$z+$t*$vz;$segment=[math]::Sqrt($sx*$sx+$sz*$sz)-$a.radius-$b.radius-$padding
                if($segment -lt -1e-8){throw 'Independent relative swept coordinates reveal same-layer penetration between simulation steps.'}
                $pair.minimumSegmentGap=[math]::Min($pair.minimumSegmentGap,$segment);$minimumSegment=[math]::Min($minimumSegment,$segment)
            }
        }}
        $previous=$current;$expectedStep++
    }
    if($expectedStep -ne $collision.fixedSteps+1 -or $pairs.Count -ne @($collision.pairs).Count -or $crossLayer -ne $collision.crossLayerOverlapObservations -or
       [math]::Abs($minimum-$collision.minimumGap) -gt .000001 -or [math]::Abs($(if($collision.fixedSteps){$minimumSegment}else{$minimum})-$collision.minimumSegmentGap) -gt .000001){throw 'Independent complete sample inventory and minima differ from the native report.'}
    foreach($pair in $collision.pairs){
        $key=[string]$pair.a+':'+[string]$pair.b;if(!$pairs.ContainsKey($key)){throw 'A native pair has no actual sampled bodies.'};$actual=$pairs[$key]
        if($actual.layer -ne $pair.layer -or $actual.padding -ne $pair.padding -or $actual.opponents -ne $pair.opponents -or $actual.stationaryBody -ne $pair.stationaryBody -or
           $actual.observations -ne $pair.observations -or [math]::Abs($actual.minimumGap-$pair.minimumGap) -gt .000001 -or
           [math]::Abs($actual.minimumSegmentGap-$pair.minimumSegmentGap) -gt .000001 -or [math]::Abs($actual.currentGap-$pair.currentGap) -gt .000001){throw 'Independent pair classification or actual measured clearance differs from the native report.'}
    }
    $initial=@{};foreach($body in $collision.samples[0].bodies){$initial[[string]$body.id]=$body}
    foreach($unit in $collision.units){
        $key=[string]$unit.id;if(!$initial.ContainsKey($key) -or !$lastBodies.ContainsKey($key)){throw 'An accepted card member is absent from the actual initial/live samples.'}
        $spawn=$initial[$key];$last=$lastBodies[$key];$dx=$unit.x-$unit.spawnX;$dz=$unit.z-$unit.spawnZ;$distance=[math]::Sqrt($dx*$dx+$dz*$dz)
        if($unit.radius -ne $spawn.radius -or $unit.flying -ne $spawn.flying -or $unit.kind -ne $spawn.kind -or
           [math]::Abs($spawn.x-$unit.spawnX) -gt .000001 -or [math]::Abs($spawn.z-$unit.spawnZ) -gt .000001 -or
           [math]::Abs($last.x-$unit.x) -gt .000001 -or [math]::Abs($last.z-$unit.z) -gt .000001 -or
           [math]::Abs($distance-$unit.distanceFromSpawn) -gt .000001 -or ($unit.present -ne $true -and $unit.deathObserved -ne $true)){throw 'A card member lacks matching actual spawn, final movement or observed death evidence.'}
        if($unit.kind -eq 0 -and !$unit.flying){
            $center=$centerCrossings.ContainsKey($key);$far=$farCrossings.ContainsKey($key);$events=@($unit.farBankCrossingHistory)
            if($unit.crossedRiver -ne $center -or $unit.crossedFarBank -ne $far -or $events.Count -ne $(if($far){1}else{0})){throw 'A ground member crossing flag lacks its actual fixed-step coordinates and lifetime history.'}
            if($far){$actual=$farCrossings[$key];$event=$events[0];if($event.step -ne $actual.step -or [math]::Abs($event.time-$actual.time) -gt .000001 -or [math]::Abs($event.x-$actual.x) -gt .000001 -or [math]::Abs($event.z-$actual.z) -gt .000001 -or $event.bridge -ne $unit.intendedBridge -or $event.liveBridge -notin @(0,$unit.intendedBridge)){throw 'A far-bank event differs from its first actual observed crossing through the selected lane.'}}
        }
    }
    if($collision.farBankDepth -ne 1.93 -or $collision.groundMembersFarBankCrossed -ne $farCrossings.Count -or $collision.groundMembersCrossed -ne $centerCrossings.Count){throw 'Actual complete ground crossing counts differ from the native report.'}
    return [ordered]@{passed=$true;sampleCount=$expectedStep;sameLayerPairCount=$pairs.Count;sameLayerObservations=$observations;crossLayerOverlapObservations=$crossLayer;minimumGap=$minimum;minimumSegmentGap=$(if($collision.fixedSteps){$minimumSegment}else{$minimum});groundMembersFarBankCrossed=$farCrossings.Count}
}

$captureSourcePins=[Collections.Generic.List[object]]::new();$captures=[Collections.Generic.List[object]]::new()
$specs=[ordered]@{
    'collision135-final-initial'=@{age=0;case='crowd';width=1920;height=1080}
    'collision135-final-crowd5'=@{age=5;case='crowd';width=1920;height=1080}
    'collision135-final-bridge12'=@{age=12;case='crowd';width=1920;height=1080}
    'collision135-final-contact5'=@{age=5;case='contact';width=1920;height=1080}
    'collision135-final-air5'=@{age=5;case='layers';width=1920;height=1080}
    'collision135-final-720'=@{age=5;case='crowd';width=1280;height=720}
}
$physicalReview=Read-ModelInput $VisualReview
if($physicalReview.schema -ne 1 -or $physicalReview.version -ne $Version -or $physicalReview.passed -ne $true -or
   [string]::IsNullOrWhiteSpace($physicalReview.method) -or @($physicalReview.frames).Count -ne $specs.Count -or
   (($physicalReview.frames.name|Sort-Object)-join '|') -ne (($specs.Keys|Sort-Object)-join '|') -or
   $CaptureNames.Count -ne $specs.Count -or (($CaptureNames|Sort-Object)-join '|') -ne (($specs.Keys|Sort-Object)-join '|')){throw 'All six actual current crowd/contact/air/720p frames need physical review.'}
$sourcePaths=@(Get-ChildItem -LiteralPath (Join-Path $modelRepo 'Unreal/RiftCrownArena/Source'),(Join-Path $modelRepo 'Unreal/RiftCrownArena/Config') -File -Recurse|
    Where-Object Extension -in @('.cpp','.h','.cs','.ini')|ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,$_.FullName).Replace('\','/')})+
    @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Build/Capture-Unreal.ps1')
foreach($name in $CaptureNames){
    $spec=$specs[$name];$root='Artifacts/QA/Visual/'+$name
    $run=Read-ModelInput ($root+'.json');$state=Read-ModelInput ($root+'.state.json');$png=Pin-ModelInput ($root+'.png')
    if($run.passed -ne $true -or $run.version -ne $Version -or $run.name -ne $name -or $run.editor -ne $false -or $run.page -ne 'Battle' -or $run.scenario -ne 'unit_collision' -or
       $run.executableSha256 -ne $native.sha256 -or $run.executableUnchanged -ne $true -or $run.sourcePinsUnchanged -ne $true -or
       $run.captured -ne $true -or $run.stateCaptured -ne $true -or $run.timedOut -ne $false -or $run.exitCode -ne 0 -or @($run.errors).Count -ne 0 -or
       $run.width -ne $spec.width -or $run.height -ne $spec.height -or $run.actualWidth -ne $spec.width -or $run.actualHeight -ne $spec.height -or $run.resolutionMatches -ne $true -or
       $run.cameraFramingRequired -ne $true -or $run.cameraFramingPassed -ne $true -or $run.allowExternalInput -ne $false -or
       $run.unitCollisionPassed -ne $true -or $run.collisionCase -ne $spec.case -or $run.collisionAge -ne $spec.age -or
       $state.captureInput.active -ne $true -or $state.captureInput.consuming -ne $true -or $state.speed -ne 0){throw 'An actual Shipping collision frame lacks clean exact-resolution/source/runtime acceptance.'}
    $started=Get-ModelUtcTimestamp $run.startedUTC;$finished=Get-ModelUtcTimestamp $run.finishedUTC
    if($run.processId -ne $run.diagnosticVerification.processId -or $started -ne (Get-ModelUtcTimestamp $run.diagnosticVerification.startedUTC) -or
       $finished -ne (Get-ModelUtcTimestamp $run.diagnosticVerification.finishedUTC)){throw 'Capture process timestamps differ from native diagnostics.'}
    foreach($pin in @($modelPinIndex[$root+'.state.json'],$png)){
        $written=(Get-Item -LiteralPath (Resolve-ModelInput $pin.path)).LastWriteTimeUtc
        if($written -lt $started -or $written -gt $finished){throw 'An actual state/PNG was not freshly written during its observed run.'}
    }
    $review=@($physicalReview.frames|Where-Object name -eq $name)
    if($review.Count -ne 1 -or $review[0].png.path -ne $png.path -or $review[0].png.sha256 -ne $png.sha256 -or $review[0].png.bytes -ne $png.bytes -or
       $review[0].run -ne $root+'.json' -or $review[0].state -ne $root+'.state.json' -or $review[0].nativeSha256 -ne $native.sha256 -or
       $review[0].physicallyReviewed -ne $true -or $review[0].crowdsAndLayersReadable -ne $true -or $review[0].centeredHandPreserved -ne $true -or
       $review[0].age -ne $spec.age -or $review[0].case -ne $spec.case -or (Get-ModelUtcTimestamp $physicalReview.reviewedAtUtc) -lt $finished){throw 'Physical review does not pin the actual final frame and its required visible content.'}
    if((($run.sourcePins.path|Sort-Object)-join '|') -ne (($sourcePaths|Sort-Object)-join '|')){throw 'The Shipping capture did not pin its complete current source/configuration/wrapper.'}
    foreach($pin in $run.sourcePins){Assert-HoverIdentity $pin;$captureSourcePins.Add($pin);$null=Pin-ModelInput $pin.path}
    $diagnostics=Assert-ModelShippingDiagnostics $run 'Current Shipping unit collision capture'
    $geometry=$state.arenaGeometry;$framing=$state.cameraFraming
    if($run.arenaGeometryPassed -ne $true -or $geometry.passed -ne $true -or $geometry.widthTiles -ne 30 -or $geometry.heightTiles -ne 44 -or
       $geometry.playableFloorTiles -ne 1200 -or $geometry.decorativeFloorTiles -ne 640 -or $geometry.borderStones -ne 104 -or
       $run.modelEnvelopeAvailable -ne $true -or $run.modelEnvelopePassed -ne $true -or $framing.projectedModelBoundsPassed -ne $true -or
       @($framing.legalFieldCorners).Count -ne 4 -or @($framing.modelEnvelope).Count -ne 16 -or
       @($framing.legalFieldCorners|Where-Object {$_.projected -ne $true -or $_.insideSafeArea -ne $true}).Count -or
       @($framing.modelEnvelope|Where-Object {$_.projected -ne $true -or $_.insideSafeArea -ne $true}).Count){throw 'The preserved arena or enlarged models extend beneath the unchanged HUD.'}
    $collision=$state.unitCollision
    if($collision.schemaVersion -ne 1 -or $collision.passed -ne $true -or $collision.case -ne $spec.case -or $collision.requestedAge -ne $spec.age -or
       [math]::Abs($collision.sampledAge-$spec.age) -gt .00001 -or $collision.fixedSteps -ne $spec.age*60 -or $collision.paidPlayOnly -ne $true -or
       $collision.paidCostAndCyclePassed -ne $true -or $collision.actualUnitCount -ne $collision.expectedUnitCount -or
       @($collision.units).Count -ne $collision.actualUnitCount -or @($collision.units.id|Sort-Object -Unique).Count -ne $collision.actualUnitCount -or
       $collision.initialMemberCount -ne $collision.actualUnitCount -or $collision.initialGroundMemberCount -ne $collision.groundMembers -or
       $collision.allMembersSeen -ne $true -or $collision.captureSeenMemberCount -ne $collision.actualUnitCount -or
       (($collision.captureSeenIds|Sort-Object)-join '|') -ne (($collision.units.id|Sort-Object)-join '|') -or
       $collision.allMembersAccounted -ne $true -or $collision.spawnClearancePassed -ne $true -or $collision.allFixedStepClearancePassed -ne $true -or
       $collision.allRelativeMovementSegmentClearancePassed -ne $true -or $collision.allBridgeHistoriesPassed -ne $true -or $collision.stationaryBuildingsPassed -ne $true -or
       $collision.layerCoveragePassed -ne $true -or $collision.collisionSkin -ne .02 -or $collision.structurePadding -ne .22 -or
       @($collision.paidPlays).Count -ne $collision.deploymentCount -or ($collision.paidPlays|Measure-Object memberCount -Sum).Sum -ne $collision.actualUnitCount -or
       @($collision.paidPlays|Where-Object {$_.accepted -ne $true -or $_.handCycledOnce -ne $true -or $_.spentDelta -ne $_.cost -or $_.aetherBefore -ne 10 -or $_.aetherAfter -ne (10-$_.cost)}).Count){throw 'An actual collision fixture lacks its complete paid member/age/layer/clearance proof.'}
    if($spec.age -ge 2 -and ($collision.progressPassed -ne $true -or $collision.membersProgressed -le 0)){throw 'The live crowd failed to make actual forward navigation progress.'}
    if($spec.case -eq 'contact' -and $collision.enemyContactObserved -ne $true){throw 'The opposing ground crowd did not actually meet at collision contact.'}
    if($spec.case -eq 'layers' -and ($collision.airMembers -lt 2 -or $collision.groundMembers -lt 1 -or $collision.buildingMembers -lt 1 -or $collision.airPairs -le 0 -or $collision.crossLayerOverlapObservations -le 0)){throw 'The layered crowd lacks actual flying collision and ground/air pass-over coverage.'}
    if($spec.case -eq 'crowd' -and ($collision.actualUnitCount -ne 31 -or $collision.deploymentCount -ne 16 -or $collision.groundMembers -ne 11 -or $collision.airMembers -ne 17 -or $collision.buildingMembers -ne 3 -or ($collision.paidPlays.handIndex -join ',') -ne ($portable.buildingPocketPaidSlots -join ','))){throw 'The paid three-building crowd lacks its exact initial physical member inventory and real hand sequence.'}
    if($spec.case -eq 'crowd' -and $spec.age -ge 12 -and ($collision.bridgeCrossingRequired -ne $true -or $collision.groundMembersFarBankCrossed -ne 11 -or $collision.allGroundMembersFarBankCrossed -ne $true -or @($collision.units|Where-Object {$_.kind -eq 0 -and $_.flying -ne $true -and ($_.crossedFarBank -ne $true -or @($_.farBankCrossingHistory).Count -ne 1)}).Count)){throw 'Every ground member of the paid three-building crowd must actually reach the far bank through its selected bridge.'}
    $independent=Assert-CollisionSamples $collision
    $markers=@($diagnostics.records|Where-Object {$_.message -like ('Actual unit collision capture: case='+$spec.case+' paid=* passed=1')})
    if($markers.Count -ne 1){throw 'Actual structured native diagnostics lack this exact completed collision fixture.'}
    $compactCollision=[ordered]@{};foreach($property in $collision.PSObject.Properties){if($property.Name -notin @('samples','pairs','units')){$compactCollision[$property.Name]=$property.Value}}
    $captures.Add([pscustomobject][ordered]@{name=$name;passed=$true;png=$png;state=$root+'.state.json';report=$root+'.json';processId=$run.processId;unitCollision=$compactCollision;independentPairAudit=$independent;arenaGeometry=$geometry;framing=$framing})
}


$metaSourcePins=[Collections.Generic.List[object]]::new()
if($MetaRoot -ne 'Artifacts/QA/collision135-meta100-final'){throw 'Only the explicit fresh one-hundred-match arena/routing cohort can be inherited.'}
$metaContext=Read-ModelInput ($MetaRoot+'/context.json');$metaRun=Read-ModelInput ($MetaRoot+'/run.json')
$meta=Read-ModelInput $MetaAudit;$metaLog=Pin-ModelInput ($MetaRoot+'/engine.log')
foreach($path in @($metaRun.dataset,$metaRun.export)){
    $resolved=Resolve-ModelInput $path;$relative=[IO.Path]::GetRelativePath($modelRepo,$resolved).Replace('\','/')
    if($relative -notmatch '^Artifacts/QA/collision135-meta100-final/UserData/(Meta/[A-F0-9]{32}\.json|meta-validation\.json)$'){throw 'Only the two actual synthetic cohort data/export files may enter the evidence archive.'}
    $null=$modelSyntheticInputs.Add($relative)
}
$data=Read-ModelInput $metaRun.dataset;$export=Read-ModelInput $metaRun.export
$datasetPin=Pin-ModelInput $metaRun.dataset
$metaStarted=Get-ModelUtcTimestamp $metaContext.startedUtc;$metaFinished=Get-ModelUtcTimestamp $metaRun.completedUtc
$runtime=@($context.runtimeModules|Where-Object path -eq 'Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll')
if($metaContext.schema -ne 1 -or $metaContext.requestedGames -ne 100 -or $metaRun.passed -ne $true -or $metaRun.games -ne 100 -or
   $runtime.Count -ne 1 -or $metaContext.runtimeSha256 -ne $runtime[0].sha256 -or $metaRun.runtimeSha256 -ne $runtime[0].sha256 -or
   $metaRun.datasetSha256 -ne $datasetPin.sha256 -or $metaStarted -ge $metaFinished -or $data.games -ne 100 -or $data.economyChecks -ne 100 -or
   $data.invalid -ne 0 -or $data.economyInvalid -ne 0 -or $data.rulesSnapshot.navigationRevision -ne 5 -or $data.telemetryRevision -ne 3 -or
   $data.rulesSnapshot.arenaWidth -ne 30 -or $data.rulesSnapshot.arenaHeight -ne 44 -or $data.rulesSnapshot.coreDepth -ne 17.3 -or
   $data.rulesSnapshot.guardDepth -ne 13.4 -or $data.rulesSnapshot.guardX -ne 8.2 -or $data.rulesSnapshot.pocketOuterX -ne 14.2 -or
   $data.rulesSnapshot.pocketMaxDepth -ne 10.25 -or $data.rulesSnapshot.navigation -notmatch 'swept disc clearance' -or
   $data.rulesSnapshot.collisionSkin -ne .02 -or $data.rulesSnapshot.structureClearance -ne .22 -or $data.rulesSnapshot.collision -notmatch 'both teams collide; spells have no body$' -or
   $meta.completeObserved -ne $true -or $meta.observedGames -ne 100 -or $meta.target -ne 100 -or $meta.attempts -ne 100 -or
   $meta.failedChecks -ne 0 -or @($meta.errors).Count -ne 0 -or $meta.finalExportValidated -ne $true -or $meta.fingerprint -ne $data.fingerprint){throw 'The actual fresh navigation-revision-five cohort and independent complete audit must agree.'}
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
if($auditEvidence.schema -ne 2 -or $auditEvidence.version -ne $Version -or $auditEvidence.navigationRevision -ne 5 -or
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

$modelPerfRoot='Artifacts/QA/Performance/'+$PerformanceName+'/'
$performanceRun=Read-ModelInput ($modelPerfRoot+'run.json');$performance=Read-ModelInput ($modelPerfRoot+'performance.json')
if($performanceRun.passed -ne $true -or $performanceRun.version -ne $Version -or $performanceRun.exitCode -ne 0 -or
   $performanceRun.executableSha256 -ne $native.sha256 -or $performanceRun.editor -ne $false -or $performance.frame.samples -lt 100 -or
   $performanceRun.reportSha256 -ne $modelPinIndex[$modelPerfRoot+'performance.json'].sha256 -or @($performanceRun.errors).Count){throw 'The actual current Shipping performance run must pass and match this native executable.'}
$null=Assert-ModelShippingDiagnostics $performanceRun 'Native collision stress performance'
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


foreach($path in $ReleaseDocuments+@('Build/Capture-Unreal.ps1','Build/Validate-NativeMeta.ps1','Build/Tests/Audit-NativeMeta.py','Build/Tests/RiftSimulationTests.cpp','Build/Tests/Verify-UnitCollision.ps1',
    'Build/Test-Unreal.ps1','Build/Curate-UnrealQA.ps1','Build/ShippingDiagnostics.ps1','Build/Measure-Unreal.ps1','Build/Test-LauncherPlay.ps1','Installer/Test-Installer.ps1')){$null=Pin-ModelInput $path}
foreach($failedHistory in @('Docs/QA/unit-collision-first-failed-native-1.3.5.json','Docs/QA/unit-collision-second-failed-native-1.3.5.json','Docs/QA/unit-collision-third-failed-native-1.3.5.json','Docs/QA/unit-collision-fourth-failed-native-1.3.5.json','Docs/QA/unit-collision-fifth-failed-native-1.3.5.json','Docs/QA/unit-collision-replay-seed-probe-1.3.5.json','Docs/QA/unit-collision-final-replay-seed-probe-1.3.5.json')){
    if(Test-Path -LiteralPath (Join-Path $modelRepo $failedHistory)){$null=Pin-ModelInput $failedHistory}
}
$pocketHistory=$null
$pocketHistoryPath='Docs/QA/unit-collision-building-pocket-first-failed-1.3.5.json'
if(Test-Path -LiteralPath (Join-Path $modelRepo $pocketHistoryPath)){
    $pocketHistory=Read-ModelInput $pocketHistoryPath
    if($pocketHistory.accepted -ne $false -or $pocketHistory.historicalEvidence -ne $true -or $pocketHistory.version -ne $Version -or @($pocketHistory.stalledReachableMembers).Count -ne 5){throw 'The preserved rejected building-pocket witness must remain explicitly historical and unaccepted.'}
    foreach($pin in $pocketHistory.inputs){Assert-HoverIdentity $pin;$null=Pin-ModelInput $pin.path}
    $null=Pin-ModelInput $pocketHistory.preservedCandidate
}
$inputs=@($modelInputs|Sort-Object path -Unique)
if(($inputs|Measure-Object bytes -Sum).Sum -gt 90MB){throw 'The compact unit collision evidence exceeds its90MiB budget.'}
$zipName=[IO.Path]::GetFileName($zipPath)
if(@(Get-Content -LiteralPath $sumPath|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'A frozen collision evidence checksum row already exists.'}
$summary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Every physical card member collides in its ground or air layer; structures remain stationary; continuous relative sweeps, crowd steering and source-side bridges; actual paid deployments, saved replay, complete native and portable regression, fresh100-match Meta cohort, Shipping stress and Windows delivery';
    nativeExecutable=$native;integrationTests=$integration.tests.Count;portableScenarios=$portable.scenarioCount;portableExecutable=$portableExecutable;portableProvenance=$portable.provenance;
    portableCollisionSummary=$portable.collisionSummary;newPortableScenarios=$requiredCollisionScenarios;denseBridgeQueues=$portable.denseBridgeQueues;fullCapacityFixtures=$portable.fullCapacityFixtures;paidBuildingPockets=$portable.paidBuildingPockets;buildingRequestedTileAdjustments=$portable.buildingRequestedTileAdjustments;buildingPocketPaidSlots=$portable.buildingPocketPaidSlots;buildingPocketDeck=$portable.buildingPocketDeck;buildingPocketSpeedToleranceTiles=$portable.buildingPocketSpeedToleranceTiles;
    captures=$captures.ToArray();physicalVisualReview=$VisualReview;performance=$performance;
    meta=[ordered]@{navigationRevision=5;games=100;fingerprint=$meta.fingerprint;audit=$MetaAudit;runtime=$metaSnapshot;allSixAuthoritativeSourcesMatch=$true;newCohortObserved=$true};
    preservedHistory=[ordered]@{scope='Immutable prior arena/routing release evidence; historical acceptance only';version='1.3.4';packet=$historicalPacket;checksumFile=$historicalSums;newHistoricalRun=$false};
    rejectedPocketCandidate=$(if($pocketHistory){[ordered]@{accepted=$false;historicalEvidence=$true;witness=$pocketHistoryPath;scope='Historical valid separation but incomplete reachable movement. Final acceptance uses only fresh navigation-revision-five source/runtime/captures.'}}else{$null});
    windowsChecks=$installer.checkCount;windowsDelivery=[ordered]@{manifest='Artifacts/Release/update-manifest.json';gameArchive=$gameArchive;installer=$msi;launcher=$launcher};inputs=$inputs;
    presentationScope='Existing enlarged models, fonts, card art and centered hand retained. Physical crowd readability inspected in current Shipping frames; no fresh model-art or audio acceptance claimed.';
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
    if($archive.Entries.Count -ne $inventory.Count -or @($archive.Entries.FullName|Sort-Object -Unique).Count -ne $inventory.Count){throw 'Unexpected unit collision evidence inventory after reopening.'}
    foreach($pin in $inventory){
        $entry=$archive.GetEntry($pin.entry);if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Arena/routing evidence entry size mismatch after reopening.'}
        $stream=$entry.Open();try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($hash -ne $pin.sha256){throw 'Arena/routing evidence entry hash mismatch after reopening.'}
    }
}finally{$archive.Dispose()}
foreach($pin in @($context.sourceHashes)+@($context.runtimeModules)+@($captureSourcePins.ToArray())+@($metaSourcePins.ToArray())+@($portable.sourcePins)+
    @($payload.ToArray())+@($native,$msi,$gameArchive,$launcher,$portableExecutable,$metaSnapshot,$historicalPacket)){Assert-HoverIdentity $pin}
if((Test-Path -LiteralPath $summaryPath) -or (Test-Path -LiteralPath $zipPath)){throw 'Frozen unit collision outputs appeared during acceptance; the verified temporary ZIP is retained.'}
Move-Item -LiteralPath $temporary -Destination $zipPath;[IO.File]::WriteAllBytes($summaryPath,$summaryBytes)
$zipHash=(Get-FileHash -LiteralPath $zipPath).Hash.ToLowerInvariant();$sumLines=@(Get-Content -LiteralPath $sumPath)
if(@($sumLines|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'The unit collision checksum row appeared during packaging.'}
$sumLines+=($zipHash+'  '+$zipName);[IO.File]::WriteAllLines($sumPath,$sumLines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($inventory.Count) verified unit collision evidence entries: $zipHash"


