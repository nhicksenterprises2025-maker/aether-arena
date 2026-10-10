param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Executable = '',
    [string]$Page = 'Battle',
    [ValidateSet('','roster','congestion','effects','effects17','placement','projectiles','spells','tower_pathing','unit_collision','swarm_split')][string]$Scenario = '',
    [ValidateSet('crowd','contact','layers')][string]$CollisionCase = 'crowd',
    [ValidateRange(0,12)][float]$CollisionAge = 3,
    [ValidateSet('mini_stampede','stampede','twin_blades','vampire_bats')][string]$SwarmCard = 'stampede',
    [ValidateRange(0,12)][float]$SwarmAge = 3,
    [ValidateScript({ [math]::Abs($_) -eq .5 })][float]$SwarmX = .5,
    [ValidateRange(8.5,11.5)][float]$SwarmZ = 8.5,
    [switch]$SwarmLeftGuardDown,
    [ValidateRange(0,6)][float]$PathingAge = 0,
    [ValidateSet('clearance','left_pocket','right_pocket')][string]$RouteCase = 'clearance',
    [ValidateSet('player','enemy')][string]$RouteTeam = 'player',
    [ValidateRange(0.05,2)][float]$EffectAge = 0.12,
    [ValidateRange(0.25,4)][float]$Speed = 1,
    [switch]$BreathSmoke,
    [ValidateRange(-1,3)][int]$Hand=-1,
    [switch]$Developer,
    [switch]$BattleMenu,
    [switch]$RecordedMatch,
    [switch]$AllowExternalInput,
    [ValidateSet('','double','triple','overtime','tiebreaker','victory')][string]$Phase='',
    [ValidateSet('','tiles','ranges','sight','paths','targets','locks','all')][string]$Overlay='',
    [ValidateRange(0.7,1.4)][float]$UIScale=1,
    [ValidateScript({ $_ -eq -1 -or ($_ -ge 0.1 -and $_ -le 2) })][float]$Zoom=-1,
    [string]$PreviewCard = '',
    [float]$PreviewX = 0,
    [float]$PreviewY = 7,
    [string]$InspectCard = '',
    [int]$Width = 1920,
    [int]$Height = 1080,
    [float]$Delay = 6,
    [int]$TimeoutSeconds = 180,
    [string]$ExpectedVersion = '',
    [string]$Name = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ShippingDiagnostics.ps1')
$ExpectedVersion = Get-RiftExpectedGameVersion $repoRoot $ExpectedVersion
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$captureRoot = Join-Path $repoRoot 'Artifacts\QA\Visual'
if (!$Executable) { $Executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Capture executable not found: $Executable" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftCaptureExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$isEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -in @('UnrealEditor','UnrealEditor-Cmd')
if($Scenario -eq 'tower_pathing' -and ($Page -ne 'Battle' -or $Phase -or $RecordedMatch -or $BreathSmoke)) { throw 'Tower pathing requires its live Battle fixture without another phase, recorded-match or breath fixture.' }
if($Scenario -eq 'unit_collision' -and ($Page -ne 'Battle' -or $Phase -or $RecordedMatch -or $BreathSmoke)) { throw 'Unit collision requires its live paid Battle fixture without another phase, recorded-match or breath fixture.' }
if($Scenario -eq 'swarm_split' -and ($Page -ne 'Battle' -or $Phase -or $RecordedMatch -or $BreathSmoke)) { throw 'Swarm splitting requires its live paid Battle fixture without another phase, recorded-match or breath fixture.' }
$riftTowerSources=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Config') -File -Recurse | Where-Object Extension -in @('.cpp','.h','.cs','.ini') | ForEach-Object FullName)+@($projectPath,$PSCommandPath)
if($isEditor) { $riftTowerSources+=@('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll') | ForEach-Object { Join-Path $repoRoot $_ } }
$riftTowerSourcePins=@($riftTowerSources | Sort-Object -Unique | ForEach-Object { [ordered]@{path=[IO.Path]::GetRelativePath($repoRoot,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length} })
$workingDirectory = if ($isEditor) { $repoRoot } else { Split-Path -Parent $Executable }
if (!$Name) { $Name = ($Page.ToLowerInvariant() -replace '[^a-z0-9]+','-') + $(if ($Scenario) { '-' + $Scenario } else { '' }) + "-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Capture name must contain lowercase letters, digits, underscores or hyphens.' }
$runRoot = Join-Path $repoRoot ('Artifacts/QA/VisualRuns/' + $Name + '-' + [Guid]::NewGuid().ToString('N'))
$saveRoot = Join-Path $runRoot 'Save'
$engineUserRoot = Join-Path $runRoot 'EngineUser'
New-Item -ItemType Directory -Path $captureRoot,$saveRoot,$engineUserRoot -Force | Out-Null
$capturePath = Join-Path $captureRoot ($Name + '.png')
$logPath = Join-Path $captureRoot ($Name + '.log')
$stdoutPath = Join-Path $captureRoot ($Name + '.stdout.log')
$stderrPath = Join-Path $captureRoot ($Name + '.stderr.log')
$reportPath = Join-Path $captureRoot ($Name + '.json')
$started = Get-Date
$arguments = @(
    '/Game/Rift/Maps/Arena',
    '-game','-RenderOffscreen','-windowed',"-ResX=$Width","-ResY=$Height",
    ('-RiftSaveRoot="' + $saveRoot + '"'),
    ('-UserDir="' + $engineUserRoot + '"'),
    ('-RiftCapture="' + $capturePath + '"'),
    ('-RiftCapturePage="' + $Page + '"'),
    ('-RiftCaptureDelay=' + $Delay.ToString([System.Globalization.CultureInfo]::InvariantCulture)),
    '-RiftQuitAfterCapture','-unattended','-nosplash','-NoSound','-NoVSync',
    ('-abslog="' + $logPath + '"')
)
if ($isEditor) { $arguments = @(('"' + $projectPath + '"')) + $arguments }
if ($Scenario) { $arguments += "-RiftVisualScenario=$Scenario" }
if ($Scenario -eq 'tower_pathing') { $arguments += '-RiftPathingAge=' + $PathingAge.ToString([System.Globalization.CultureInfo]::InvariantCulture);$arguments += "-RiftRouteCase=$RouteCase";$arguments += "-RiftRouteTeam=$RouteTeam" }
if ($Scenario -eq 'unit_collision') { $arguments += '-RiftCollisionAge=' + $CollisionAge.ToString([System.Globalization.CultureInfo]::InvariantCulture);$arguments += "-RiftCollisionCase=$CollisionCase" }
if ($Scenario -eq 'swarm_split') {
    $arguments += "-RiftSwarmCard=$SwarmCard"
    $arguments += '-RiftSwarmAge=' + $SwarmAge.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $arguments += '-RiftSwarmX=' + $SwarmX.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $arguments += '-RiftSwarmZ=' + $SwarmZ.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    if ($SwarmLeftGuardDown) { $arguments += '-RiftSwarmLeftGuardDown' }
}
if ($BreathSmoke) { $arguments += '-RiftBreathSmoke' }
if ($Hand -ge 0) { $arguments += "-RiftCaptureHand=$Hand" }
if ($Developer) { $arguments += '-RiftCaptureDeveloper' }
if ($BattleMenu) { $arguments += '-RiftCaptureBattleMenu' }
if ($RecordedMatch) { $arguments += '-RiftCaptureRecordedMatch' }
if ($AllowExternalInput) { $arguments += '-RiftCaptureAllowInput' }
if ($Phase) { $arguments += "-RiftCapturePhase=$Phase" }
if ($Overlay) { $arguments += "-RiftCaptureOverlay=$Overlay" }
$arguments += '-RiftCaptureUIScale=' + $UIScale.ToString([System.Globalization.CultureInfo]::InvariantCulture)
if ($Zoom -ne -1) { $arguments += '-RiftCaptureZoom=' + $Zoom.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($Scenario -in @('effects17','spells')) { $arguments += '-RiftEffectAge=' + $EffectAge.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($Scenario -in @('effects','congestion')) { $arguments += '-RiftCaptureSpeed=' + $Speed.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($PreviewCard) {
    if ($PreviewCard -notmatch '^[a-z_]+$') { throw 'Preview card must be an original card ID.' }
    $arguments += "-RiftPreviewCard=$PreviewCard"
    $arguments += '-RiftPreviewX=' + $PreviewX.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $arguments += '-RiftPreviewY=' + $PreviewY.ToString([System.Globalization.CultureInfo]::InvariantCulture)
}
if ($InspectCard) {
    if ($InspectCard -notmatch '^[a-z_]+$') { throw 'Inspected card must be an original card ID.' }
    $arguments += "-RiftInspectCard=$InspectCard"
}
$process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$timedOut = $false
while (!$process.HasExited) {
    if (((Get-Date) - $started).TotalSeconds -gt $TimeoutSeconds) {
        $timedOut = $true
        Stop-Process -Id $process.Id -Force
        break
    }
    Start-Sleep -Milliseconds 250
    $process.Refresh()
}
$process.WaitForExit()
$finished = [DateTime]::UtcNow
$freshCapture = (Test-Path -LiteralPath $capturePath) -and ((Get-Item -LiteralPath $capturePath).LastWriteTime -ge $started.AddSeconds(-1))
$actualWidth = 0
$actualHeight = 0
if ($freshCapture) {
    $pngBytes = [System.IO.File]::ReadAllBytes($capturePath)
    if ($pngBytes.Length -ge 24 -and $pngBytes[0] -eq 137 -and $pngBytes[1] -eq 80 -and $pngBytes[2] -eq 78 -and $pngBytes[3] -eq 71) {
        $actualWidth = ([int]$pngBytes[16] -shl 24) -bor ([int]$pngBytes[17] -shl 16) -bor ([int]$pngBytes[18] -shl 8) -bor [int]$pngBytes[19]
        $actualHeight = ([int]$pngBytes[20] -shl 24) -bor ([int]$pngBytes[21] -shl 16) -bor ([int]$pngBytes[22] -shl 8) -bor [int]$pngBytes[23]
    }
}
$resolutionMatches = $actualWidth -eq $Width -and $actualHeight -eq $Height
$statePath = [System.IO.Path]::ChangeExtension($capturePath,'state.json')
$stateCaptured = (Test-Path -LiteralPath $statePath) -and ((Get-Item -LiteralPath $statePath).LastWriteTime -ge $started.AddSeconds(-1))
$riftCaptureState = if ($stateCaptured) { Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json } else { $null }
$riftCameraFraming = if ($stateCaptured) { $riftCaptureState.cameraFraming } else { $null }
$riftCameraFramingRequired = $Page -in @('Battle','ReplayView')
$riftCameraFramingPassed = !$riftCameraFramingRequired -or (
    $null -ne $riftCameraFraming -and $riftCameraFraming.activeBattleView -eq $true -and
    $riftCameraFraming.passed -eq $true -and @($riftCameraFraming.legalFieldCorners).Count -eq 4 -and
    @($riftCameraFraming.legalFieldCorners | Where-Object { $_.projected -ne $true -or $_.insideSafeArea -ne $true }).Count -eq 0
)
# Older public builds report legal ground corners only. When the native build
# also reports model-height safety, require every projected silhouette corner.
$riftModelEnvelopeAvailable = $null -ne $riftCameraFraming -and $null -ne $riftCameraFraming.PSObject.Properties['modelEnvelopePassed']
$riftModelEnvelopePassed = if ($riftModelEnvelopeAvailable) {
    $riftCameraFraming.modelEnvelopePassed -eq $true -and @($riftCameraFraming.modelEnvelope).Count -gt 0 -and
    @($riftCameraFraming.modelEnvelope | Where-Object { $_.projected -ne $true -or $_.insideSafeArea -ne $true }).Count -eq 0
} else { $null }
$riftModelEnvelopeCheckPassed = !$riftCameraFramingRequired -or !$riftModelEnvelopeAvailable -or $riftModelEnvelopePassed -eq $true
$riftArenaGeometryRequired=$riftCameraFramingRequired -and [version]$ExpectedVersion -ge [version]'1.3.4'
$riftArenaGeometryPassed=$true
if($riftArenaGeometryRequired) {
    $geometry=$riftCaptureState.arenaGeometry
    $riftArenaGeometryPassed=$null -ne $geometry -and $geometry.passed -eq $true -and $geometry.floorPassed -eq $true -and $geometry.riverPassed -eq $true -and
        $geometry.bridgesPassed -eq $true -and $geometry.borderPassed -eq $true -and $geometry.widthTiles -eq 30 -and $geometry.heightTiles -eq 44 -and
        $geometry.playableFloorTiles -eq 1200 -and $geometry.decorativeFloorTiles -eq 640 -and $geometry.riverWidthTiles -eq 30 -and [math]::Abs($geometry.riverDepthTiles-3.3) -lt .00001 -and
        @($geometry.bridges).Count -eq 2
    if([version]$ExpectedVersion -ge [version]'1.4.0') {
        $riftArenaGeometryPassed=$riftArenaGeometryPassed -and $geometry.lanePathsPassed -eq $true -and
            $geometry.lanePaverCount -eq 36 -and $geometry.expectedLanePaverCount -eq 36 -and @($geometry.lanePavers).Count -eq 36 -and
            @($geometry.lanePaths).Count -eq 4 -and @($geometry.lanePaths|ForEach-Object{"$($_.team),$($_.lane)"}|Sort-Object -Unique).Count -eq 4 -and
            @($geometry.lanePaths|Where-Object{
                $_.alignmentPassed -ne $true -or $_.lane -notin @(-1,1) -or $_.team -notin @('player','enemy') -or
                [math]::Abs($_.centerX-$_.lane*7.2) -gt .000001 -or [math]::Abs(($_.minimumX+$_.maximumX)/2-$_.centerX) -gt .000001 -or
                [math]::Abs($_.maximumX-$_.minimumX-1) -gt .000001 -or $_.pavers -ne $_.rows*2 -or
                ($_.team -eq 'player' -and $_.rows -ne 5) -or ($_.team -eq 'enemy' -and $_.rows -ne 4)
            }).Count -eq 0 -and
            $geometry.towerSnapshotAvailable -eq $true -and $geometry.towerAlignmentPassed -eq $true -and
            $geometry.guardsAligned -eq 4 -and $geometry.coresCentered -eq 2 -and @($geometry.towers).Count -eq 6 -and
            @($geometry.towers.id|Sort-Object -Unique).Count -eq 6 -and
            @($geometry.towers|Where-Object{
                $_.alignmentPassed -ne $true -or $_.team -notin @('player','enemy') -or $_.kind -notin @('core','guard') -or
                [math]::Abs($_.x-$(if($_.kind -eq 'core'){0}else{$_.lane*7.2})) -gt .000001 -or
                [math]::Abs($_.z-$(if($_.team -eq 'player'){1}else{-1})*$(if($_.kind -eq 'core'){17.3}else{13.4})) -gt .000001 -or
                ($_.kind -eq 'core' -and $_.lane -ne 0) -or ($_.kind -eq 'guard' -and $_.lane -notin @(-1,1)) -or
                ($_.dead -ne $true -and $_.visualAvailable -ne $true) -or
                ($_.visualAvailable -eq $true -and ([math]::Abs($_.visualX-$_.x) -gt .000001 -or [math]::Abs($_.visualZ-$_.z) -gt .000001))
            }).Count -eq 0
    }
}
$riftPhaseFixturePassed = $true
if ($Phase) {
    $riftExpectedPhase = switch ($Phase) { 'double' { 'regulation' }; 'triple' { 'overtime' }; 'overtime' { 'overtime' }; 'tiebreaker' { 'tiebreaker' }; 'victory' { 'finished' } }
    $riftExpectedElapsed = switch ($Phase) { 'double' { 121 }; 'triple' { 241 }; 'overtime' { 181 }; 'tiebreaker' { 301 }; 'victory' { 0 } }
    $riftPhaseFixturePassed = $stateCaptured -and $riftCaptureState.phase -eq $riftExpectedPhase -and
        [math]::Abs($riftCaptureState.elapsed - $riftExpectedElapsed) -lt 0.01 -and
        $riftCaptureState.speed -eq 0 -and $riftCaptureState.events.match_start -eq 1
    if ($Phase -eq 'victory') {
        $riftPhaseFixturePassed = $riftPhaseFixturePassed -and $riftCaptureState.winner -eq 0 -and
            $riftCaptureState.playerCrowns -eq 3 -and $riftCaptureState.enemyCrowns -eq 0 -and
            $riftCaptureState.resultReason -eq 'core_destroyed' -and $riftCaptureState.events.match_end -eq 1
    }
}
$riftTowerPathingPassed=$true
$riftUnitCollisionPassed=$true
$riftSwarmSplitPassed=$true
$riftTowerSourcesUnchanged=$true
if($Scenario -eq 'tower_pathing') {
    $pathing=$riftCaptureState.towerPathing
    $riftTowerPathingPassed=$stateCaptured -and $null -ne $pathing -and $pathing.passed -eq $true -and $pathing.routeCase -eq $RouteCase -and
        $pathing.expectedUnitCount -ge 2 -and $pathing.actualUnitCount -eq $pathing.expectedUnitCount -and @($pathing.units).Count -eq $pathing.expectedUnitCount -and
        @($pathing.units.id | Sort-Object -Unique).Count -eq $pathing.expectedUnitCount -and @($pathing.towers).Count -eq 6 -and $pathing.spawnTowerClearancePassed -eq $true -and
        $pathing.allStepTowerClearancePassed -eq $true -and $pathing.allContinuousSegmentTowerClearancePassed -eq $true -and [math]::Abs($pathing.requestedAge-$PathingAge) -lt .00001 -and
        [math]::Abs($pathing.sampledAge-([math]::Round($PathingAge*60)/60)) -lt .00001 -and $riftCaptureState.speed -eq 0 -and
        @($pathing.units | Where-Object { $_.present -ne $true -or $_.spawnTowerClearance -lt -.000001 -or $_.minimumStepTowerClearance -lt -.000001 -or $_.minimumSegmentTowerClearance -lt -.000001 }).Count -eq 0
    if($PathingAge -ge 2) { $riftTowerPathingPassed=$riftTowerPathingPassed -and $pathing.progressRequired -eq $true -and $pathing.progressPassed -eq $true -and @($pathing.units | Where-Object progressPassed -ne $true).Count -eq 0 }
    if($RouteCase -eq 'clearance') {
        $riftTowerPathingPassed=$riftTowerPathingPassed -and $pathing.ordinarySpawnOnly -eq $true -and $pathing.deploymentCount -eq 6 -and $pathing.expectedUnitCount -eq 8
    } else {
        $lane=if($RouteCase -eq 'left_pocket'){-1}else{1}
        $riftTowerPathingPassed=$riftTowerPathingPassed -and $pathing.routeTeam -eq $RouteTeam -and $pathing.paidPlayOnly -eq $true -and $pathing.deploymentCount -eq 2 -and
            $pathing.sameSideGuardDestroyed -eq $true -and $pathing.paidCostAndCyclePassed -eq $true -and $pathing.allBridgeHistoriesPassed -eq $true -and
            $pathing.intendedBridge -eq $lane -and @($pathing.paidPlays).Count -eq 2 -and
            @($pathing.paidPlays | Where-Object { $_.accepted -ne $true -or $_.handCycledOnce -ne $true -or $_.spentDelta -ne $_.cost -or $_.aetherAfter -ne (10-$_.cost) }).Count -eq 0 -and
            @($pathing.units | Where-Object { $_.team -ne $RouteTeam -or $_.intendedBridge -ne $lane -or $_.bridgeHistoryPassed -ne $true }).Count -eq 0 -and
            @($pathing.units | Where-Object role -eq 'own_half').Count -gt 0 -and @($pathing.units | Where-Object role -eq 'pocket').Count -gt 0 -and
            @($pathing.towers | Where-Object { $_.id -eq $pathing.destroyedGuardId -and $_.kind -eq 'guard' -and $_.lane -eq $lane -and $_.dead -eq $true -and $_.team -ne $RouteTeam }).Count -eq 1 -and
            @($pathing.towers | Where-Object { $_.id -eq $pathing.oppositeGuardId -and $_.kind -eq 'guard' -and $_.lane -eq -$lane -and $_.dead -eq $false -and $_.team -ne $RouteTeam }).Count -eq 1
        if($PathingAge -gt 0) { $riftTowerPathingPassed=$riftTowerPathingPassed -and $pathing.allCoreTargetsObserved -eq $true -and @($pathing.units | Where-Object { $_.sawCoreTarget -ne $true -or ($_.alive -eq $true -and ($_.targetKind -ne 'core' -or $_.targetId -ne $pathing.expectedCoreId)) }).Count -eq 0 }
        if($PathingAge -ge 5) { $riftTowerPathingPassed=$riftTowerPathingPassed -and $pathing.crossingRequired -eq $true -and @($pathing.units | Where-Object { $_.role -eq 'own_half' -and ($_.crossedRiver -ne $true -or @($_.crossingHistory | Where-Object { $_.event -eq 'river_center' -and $_.bridge -eq $lane -and $_.x*$lane -gt 0 }).Count -ne 1) }).Count -eq 0 }
    }
}
if($Scenario -eq 'unit_collision') {
    $collision=$riftCaptureState.unitCollision
    $riftUnitCollisionPassed=$stateCaptured -and $null -ne $collision -and $collision.schemaVersion -eq 1 -and $collision.passed -eq $true -and
        $collision.case -eq $CollisionCase -and $collision.paidPlayOnly -eq $true -and $collision.paidCostAndCyclePassed -eq $true -and
        $collision.expectedUnitCount -gt 0 -and $collision.actualUnitCount -eq $collision.expectedUnitCount -and @($collision.units).Count -eq $collision.actualUnitCount -and
        @($collision.units.id | Sort-Object -Unique).Count -eq $collision.actualUnitCount -and $collision.allMembersAccounted -eq $true -and
        $collision.initialMemberCount -eq $collision.actualUnitCount -and $collision.initialGroundMemberCount -eq $collision.groundMembers -and
        $collision.allMembersSeen -eq $true -and $collision.captureSeenMemberCount -eq $collision.actualUnitCount -and
        (($collision.captureSeenIds | Sort-Object) -join '|') -eq (($collision.units.id | Sort-Object) -join '|') -and
        $collision.spawnClearancePassed -eq $true -and $collision.allFixedStepClearancePassed -eq $true -and
        $collision.allRelativeMovementSegmentClearancePassed -eq $true -and $collision.allBridgeHistoriesPassed -eq $true -and $collision.stationaryBuildingsPassed -eq $true -and
        $collision.layerCoveragePassed -eq $true -and $collision.minimumGap -ge -.000001 -and $collision.minimumSegmentGap -ge -.000001 -and
        $collision.collisionSkin -eq .02 -and $collision.structurePadding -eq .22 -and @($collision.paidPlays).Count -eq $collision.deploymentCount -and
        @($collision.paidPlays | Where-Object { $_.accepted -ne $true -or $_.handCycledOnce -ne $true -or $_.spentDelta -ne $_.cost -or $_.aetherAfter -ne (10-$_.cost) }).Count -eq 0 -and
        @($collision.pairs).Count -eq ($collision.groundPairs+$collision.airPairs) -and @($collision.pairs | Where-Object { $_.minimumGap -lt -.000001 -or $_.minimumSegmentGap -lt -.000001 }).Count -eq 0 -and
        [math]::Abs($collision.requestedAge-$CollisionAge) -lt .00001 -and [math]::Abs($collision.sampledAge-([math]::Round($CollisionAge*60)/60)) -lt .00001 -and
        $collision.fixedSteps -eq [math]::Round($CollisionAge*60) -and @($collision.samples).Count -eq ($collision.fixedSteps+1) -and $riftCaptureState.speed -eq 0
    if($CollisionAge -ge 2) { $riftUnitCollisionPassed=$riftUnitCollisionPassed -and $collision.progressRequired -eq $true -and $collision.progressPassed -eq $true -and $collision.membersProgressed -gt 0 }
    if($CollisionCase -eq 'contact' -and $CollisionAge -ge 3) { $riftUnitCollisionPassed=$riftUnitCollisionPassed -and $collision.enemyContactRequired -eq $true -and $collision.enemyContactObserved -eq $true }
    if($CollisionCase -eq 'layers') { $riftUnitCollisionPassed=$riftUnitCollisionPassed -and $collision.airMembers -ge 2 -and $collision.groundMembers -ge 1 -and $collision.buildingMembers -ge 1 -and $collision.airPairs -gt 0 -and $collision.crossLayerOverlapObservations -gt 0 }
    if($CollisionCase -eq 'crowd') { $riftUnitCollisionPassed=$riftUnitCollisionPassed -and $collision.actualUnitCount -eq 31 -and $collision.deploymentCount -eq 16 -and $collision.groundMembers -eq 11 -and $collision.airMembers -eq 17 -and $collision.buildingMembers -eq 3 }
    if($CollisionCase -eq 'crowd' -and $CollisionAge -ge 12) { $riftUnitCollisionPassed=$riftUnitCollisionPassed -and $collision.bridgeCrossingRequired -eq $true -and $collision.farBankDepth -eq 1.93 -and $collision.groundMembersFarBankCrossed -eq 11 -and $collision.allGroundMembersFarBankCrossed -eq $true -and @($collision.units | Where-Object { $_.kind -eq 0 -and $_.flying -ne $true -and ($_.crossedFarBank -ne $true -or @($_.farBankCrossingHistory).Count -ne 1) }).Count -eq 0 }
}
if($Scenario -eq 'swarm_split') {
    $swarm=$riftCaptureState.swarmSplit
    $swarmCount=switch($SwarmCard){'stampede'{15};'twin_blades'{2};default{5}}
    $swarmCost=switch($SwarmCard){'stampede'{7};'vampire_bats'{5};default{2}}
    $swarmSlot=switch($SwarmCard){'mini_stampede'{0};'stampede'{1};'twin_blades'{2};default{3}}
    $riftSwarmSplitPassed=$stateCaptured -and $null -ne $swarm -and $swarm.schemaVersion -eq 1 -and $swarm.passed -eq $true -and
        $swarm.cardId -eq $SwarmCard -and $swarm.team -eq 'player' -and $swarm.paidPlayOnly -eq $true -and $swarm.paidCostAndCyclePassed -eq $true -and
        $swarm.cost -eq $swarmCost -and $swarm.aetherBefore -eq 10 -and $swarm.aetherAfter -eq (10-$swarmCost) -and $swarm.spentDelta -eq $swarmCost -and
        $swarm.handIndex -eq $swarmSlot -and $swarm.handCycledOnce -eq $true -and ($swarm.handBefore -join '|') -eq 'mini_stampede|stampede|twin_blades|vampire_bats' -and
        $swarm.requestedX -eq $SwarmX -and $swarm.requestedZ -eq $SwarmZ -and $swarm.leftGuardDestroyed -eq [bool]$SwarmLeftGuardDown -and
        $swarm.expectedUnitCount -eq $swarmCount -and $swarm.actualUnitCount -eq $swarmCount -and @($swarm.units).Count -eq $swarmCount -and
        @($swarm.units.id | Sort-Object -Unique).Count -eq $swarmCount -and @($swarm.units.playId | Sort-Object -Unique).Count -eq 1 -and
        ($swarm.leftMembers+$swarm.rightMembers) -eq $swarmCount -and $swarm.leftMembers -gt 0 -and $swarm.rightMembers -gt 0 -and [math]::Abs($swarm.leftMembers-$swarm.rightMembers) -le 1 -and
        $swarm.spawnLegalPassed -eq $true -and $swarm.allMembersAccounted -eq $true -and $swarm.allClearancePassed -eq $true -and $swarm.allSweptClearancePassed -eq $true -and
        $swarm.allSpeedLimitsPassed -eq $true -and $swarm.allLaneTargetsPassed -eq $true -and $swarm.allBridgeHistoriesPassed -eq $true -and
        $swarm.minimumGap -ge -1.e-8 -and $swarm.minimumSweptGap -ge -1.e-8 -and $swarm.tolerance -eq 1.e-8 -and $swarm.collisionSkin -eq .02 -and $swarm.structurePadding -eq .22 -and
        $swarm.fixedSteps -eq [math]::Round($SwarmAge*60) -and @($swarm.samples).Count -eq ($swarm.fixedSteps+1) -and
        [math]::Abs($swarm.requestedAge-$SwarmAge) -lt .00001 -and [math]::Abs($swarm.sampledAge-([math]::Round($SwarmAge*60)/60)) -lt .00001 -and $riftCaptureState.speed -eq 0 -and
        @($swarm.units | Where-Object { $_.landingLane -ne $(if($_.spawnX -lt 0){-1}else{1}) -or $_.bridgeHistoryPassed -ne $true -or ($_.present -ne $true -and $_.deathObserved -ne $true) }).Count -eq 0 -and
        @($swarm.visuals | Where-Object { $_.animationAsset -notlike "*/Characters/$SwarmCard/Animations/AN_${SwarmCard}_*" }).Count -eq 0 -and
        @($swarm.units | Where-Object alive -eq $true).Count -eq @($swarm.visuals | Where-Object dead -eq $false).Count
    if($SwarmAge -gt 0) { $riftSwarmSplitPassed=$riftSwarmSplitPassed -and $swarm.allExpectedCrownTargetsObserved -eq $true -and @($swarm.units | Where-Object expectedCrownObserved -ne $true).Count -eq 0 }
    if($SwarmAge -ge 2) { $riftSwarmSplitPassed=$riftSwarmSplitPassed -and $swarm.progressRequired -eq $true -and $swarm.membersProgressed -eq $swarmCount -and @($swarm.visuals | Where-Object { $_.dead -ne $true -and $_.locomotionPhase -le 0 }).Count -eq 0 }
    if($SwarmAge -ge 12) { $riftSwarmSplitPassed=$riftSwarmSplitPassed -and $swarm.crossingRequired -eq $true -and $swarm.farBankMembers -eq $swarmCount -and @($swarm.units | Where-Object crossedFarBank -ne $true).Count -eq 0 }
}
foreach($pin in $riftTowerSourcePins) { $file=Join-Path $repoRoot $pin.path;if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256 -or (Get-Item -LiteralPath $file).Length -ne $pin.bytes) { $riftTowerSourcesUnchanged=$false } }
$riftCaptureExecutableUnchanged=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant() -eq $riftCaptureExecutableHash
$errors = @()
$errorPattern='LogRift: Error:|LogUIActionRouter: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Failed to load.*(/Game/Rift|Rift/)|Authored .* missing|LogMaterial: (Error:|Warning:.*(Failed to compile|Default Material|missing usage flag))|LogShaderCompilers: Error:'
$diagnosticSource='editor-engine-log'; $nativeDiagnosticLogs=@(); $diagnosticVerification=$null
if ($isEditor) {
    if ((Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).LastWriteTimeUtc -ge $started.ToUniversalTime()) {
        $errors = @(Select-String -LiteralPath $logPath -Pattern $errorPattern | ForEach-Object { $_.Line })
    } else { $errors=@('Fresh Editor engine log missing.') }
} else {
    $diagnostics=Get-RiftShippingDiagnostics -RepoRoot $repoRoot -DestinationRoot $runRoot -SaveRoot $saveRoot -EngineUserRoot $engineUserRoot -ProcessId $process.Id -ExpectedVersion $ExpectedVersion -StartedUTC $started -FinishedUTC $finished -ErrorPattern $errorPattern
    $errors=@($diagnostics.errors); $diagnosticSource=$diagnostics.diagnosticSource; $nativeDiagnosticLogs=@($diagnostics.nativeDiagnosticLogs); $diagnosticVerification=$diagnostics.diagnosticVerification; $logPath=$diagnostics.engineLog
}
$riftCapturePassed=!$timedOut -and $freshCapture -and $stateCaptured -and $resolutionMatches -and $riftPhaseFixturePassed -and $riftCameraFramingPassed -and $riftModelEnvelopeCheckPassed -and $riftArenaGeometryPassed -and $riftTowerPathingPassed -and $riftUnitCollisionPassed -and $riftSwarmSplitPassed -and $riftTowerSourcesUnchanged -and $riftCaptureExecutableUnchanged -and $process.ExitCode -eq 0 -and $errors.Count -eq 0
$report = [ordered]@{
    schema = 1; version=$ExpectedVersion; passed=[bool]$riftCapturePassed; name = $Name; page = $Page; scenario = $Scenario; inspectCard = $InspectCard; speed = $Speed; breathSmoke = [bool]$BreathSmoke; uiScale=$UIScale; zoom=$Zoom;
    state = $statePath; stateCaptured = $stateCaptured; phaseFixture = $Phase; phaseFixturePassed = $riftPhaseFixturePassed; allowExternalInput = [bool]$AllowExternalInput;
    pathingAge = $(if($Scenario -eq 'tower_pathing'){$PathingAge}else{$null}); routeCase=$(if($Scenario -eq 'tower_pathing'){$RouteCase}else{$null}); routeTeam=$(if($Scenario -eq 'tower_pathing'){$RouteTeam}else{$null}); towerPathingPassed = $riftTowerPathingPassed;
    collisionAge=$(if($Scenario -eq 'unit_collision'){$CollisionAge}else{$null}); collisionCase=$(if($Scenario -eq 'unit_collision'){$CollisionCase}else{$null}); unitCollisionPassed=$riftUnitCollisionPassed;
    swarmAge=$(if($Scenario -eq 'swarm_split'){$SwarmAge}else{$null}); swarmCard=$(if($Scenario -eq 'swarm_split'){$SwarmCard}else{$null}); swarmX=$(if($Scenario -eq 'swarm_split'){$SwarmX}else{$null}); swarmZ=$(if($Scenario -eq 'swarm_split'){$SwarmZ}else{$null}); swarmLeftGuardDown=$(if($Scenario -eq 'swarm_split'){[bool]$SwarmLeftGuardDown}else{$null}); swarmSplitPassed=$riftSwarmSplitPassed;
    sourcePins=$riftTowerSourcePins; sourcePinsUnchanged=$riftTowerSourcesUnchanged; executableUnchanged=$riftCaptureExecutableUnchanged;
    cameraFramingRequired = $riftCameraFramingRequired; cameraFramingPassed = $riftCameraFramingPassed; cameraFraming = $riftCameraFraming;
    modelEnvelopeAvailable = $riftModelEnvelopeAvailable; modelEnvelopePassed = $riftModelEnvelopePassed;
    arenaGeometryRequired=$riftArenaGeometryRequired; arenaGeometryPassed=$riftArenaGeometryPassed;
    width = $Width; height = $Height; actualWidth = $actualWidth; actualHeight = $actualHeight;
    resolutionMatches = $resolutionMatches; delay = $Delay;
    screenshot = $capturePath; engineLog = $logPath; executable = $Executable; executableSha256 = $riftCaptureExecutableHash;
    diagnosticSource=$diagnosticSource; nativeDiagnosticLogs=$nativeDiagnosticLogs; diagnosticVerification=$diagnosticVerification;
    editor = $isEditor; workingDirectory = $workingDirectory; engineUserRoot = $engineUserRoot;
    processId=$process.Id; startedUTC=$started.ToUniversalTime().ToString('o'); finishedUTC=$finished.ToString('o');
    captured = $freshCapture; timedOut = $timedOut; exitCode = $process.ExitCode;
    seconds = [math]::Round(((Get-Date)-$started).TotalSeconds,2);
    errors = $errors; visualInspection = 'pending'
}
[System.IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 20),[System.Text.UTF8Encoding]::new($false))
Write-Output ($report | ConvertTo-Json -Depth 20)
if (!$riftCapturePassed) { throw "Unreal capture failed; inspect $reportPath" }
