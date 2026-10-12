param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Executable = '',
    [string]$Page = 'Battle',
    [ValidateSet('','roster','expansion_roster','expansion_new_roster','expansion_effects','expansion_warden_death','reaper_design','congestion','effects','effects17','placement','projectiles','spells','tower_pathing','unit_collision','swarm_split','pulverizer')][string]$Scenario = '',
    [ValidateRange(0,8)][float]$PulverizerAge = .35,
    [ValidateSet('crowd','contact','layers')][string]$CollisionCase = 'crowd',
    [ValidateRange(0,12)][float]$CollisionAge = 3,
    [ValidateSet('mini_stampede','stampede','twin_blades','vampire_bats')][string]$SwarmCard = 'stampede',
    [ValidateRange(0,12)][float]$SwarmAge = 3,
    [ValidateScript({ [math]::Abs($_) -eq .5 })][float]$SwarmX = .5,
    [ValidateRange(2.5,9.5)][float]$SwarmZ = 6.5,
    [switch]$SwarmLeftGuardDown,
    [ValidateRange(0,6)][float]$PathingAge = 0,
    [ValidateSet('clearance','left_pocket','right_pocket')][string]$RouteCase = 'clearance',
    [ValidateSet('player','enemy')][string]$RouteTeam = 'player',
    [ValidateRange(0.05,2)][float]$EffectAge = 0.12,
    [ValidateSet('cast','impact','shield')][string]$ExpansionEffectStage = 'shield',
    [ValidateSet('before','death','children','cleanup')][string]$WardenDeathStage='children',
    [ValidateSet('idle','walk','cleave','cloak','expired')][string]$ReaperStage='idle',
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
    [ValidateRange(-1,3)][int]$Quality=-1,
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
if($Scenario -eq 'expansion_warden_death' -and ($Page -ne 'Battle' -or $Phase -or $RecordedMatch -or $BreathSmoke -or $Developer)) { throw 'Warden death requires its isolated paid Battle fixture without another phase, replay, breath or developer fixture.' }
if($Scenario -eq 'reaper_design' -and ($Page -ne 'Battle' -or $Phase -or $RecordedMatch -or $BreathSmoke -or $Developer)) { throw 'Reaper design requires its isolated Battle fixture without another phase, replay, breath or developer fixture.' }
$riftTowerSources=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Config') -File -Recurse | Where-Object Extension -in @('.cpp','.h','.cs','.ini','.inc') | ForEach-Object FullName)+@($projectPath,$PSCommandPath,(Join-Path $repoRoot 'Assets/Data/card_manifest.json'),(Join-Path $repoRoot 'Build/generate_card_manifest.py'))
if($isEditor) { $riftTowerSources+=@('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll') | ForEach-Object { Join-Path $repoRoot $_ } }
if($isEditor) { $riftTowerSources+=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Content') -File -Recurse | Where-Object Extension -in @('.uasset','.umap') | ForEach-Object FullName) }
$riftTowerSourcePins=@($riftTowerSources | Sort-Object -Unique | ForEach-Object { [ordered]@{path=[IO.Path]::GetRelativePath($repoRoot,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length} })
$workingDirectory = if ($isEditor) { $repoRoot } else { Split-Path -Parent $Executable }
if (!$Name) { $Name = ($Page.ToLowerInvariant() -replace '[^a-z0-9]+','-') + $(if ($Scenario) { '-' + $Scenario } else { '' }) + "-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Capture name must contain lowercase letters, digits, underscores or hyphens.' }
$runRoot = Join-Path $repoRoot ('Artifacts/QA/VisualRuns/' + $Name + '-' + [Guid]::NewGuid().ToString('N'))
$saveRoot = Join-Path $runRoot 'Save'
$engineUserRoot = Join-Path $runRoot 'EngineUser'
New-Item -ItemType Directory -Path $captureRoot,$saveRoot,$engineUserRoot -Force | Out-Null
$capturePath = Join-Path $runRoot ($Name + '.png')
$logPath = Join-Path $runRoot ($Name + '.log')
$stdoutPath = Join-Path $runRoot ($Name + '.stdout.log')
$stderrPath = Join-Path $runRoot ($Name + '.stderr.log')
$reportPath = Join-Path $runRoot ($Name + '.json')
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
if ($Quality -ge 0) { $arguments += "-RiftQAQuality=$Quality" }
if ($Scenario) { $arguments += "-RiftVisualScenario=$Scenario" }
if ($Scenario -eq 'expansion_effects') { $arguments += "-RiftExpansionEffectStage=$ExpansionEffectStage" }
if ($Scenario -eq 'expansion_warden_death') { $arguments += "-RiftWardenDeathStage=$WardenDeathStage" }
if ($Scenario -eq 'reaper_design') { $arguments += "-RiftReaperStage=$ReaperStage" }
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
if ($Scenario -eq 'pulverizer') { $arguments += '-RiftPulverizerAge=' + $PulverizerAge.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
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
$riftCaptureQualityPassed = $Quality -lt 0 -or ($null -ne $riftCaptureState -and $riftCaptureState.observedQualityPreset -eq $Quality)
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
    # Retain the public pre-1.6 geometry contract. Current captures instead use
    # the approved manifest already pinned with the capture's producer inputs.
    $riftArenaWidth=30;$riftArenaHeight=44;$riftArenaFloor=1200;$riftArenaDecoration=640
    $riftArenaBridgeX=7.2;$riftArenaGuardDepth=13.4;$riftArenaCoreDepth=17.3;$riftArenaRiverDepth=3.3
    $riftArenaPlayerRows=5;$riftArenaEnemyRows=4;$riftArenaPavers=36;$riftArenaManifestPassed=$true
    $riftArenaCurrent=[version]$ExpectedVersion -ge [version]'1.6.0'
    if($riftArenaCurrent) {
        $riftArenaManifestPath=Join-Path $repoRoot 'Assets/Data/card_manifest.json'
        $riftArenaManifest=Get-Content -LiteralPath $riftArenaManifestPath -Raw | ConvertFrom-Json
        $riftArenaManifestPin=@($riftTowerSourcePins | Where-Object path -eq 'Assets/Data/card_manifest.json')
        $riftArenaRules=$riftArenaManifest.rules
        $riftArenaManifestPassed=$riftArenaManifest.schema -in @(2,3) -and $riftArenaManifest.status -eq 'approved' -and
            $riftArenaManifest.version -eq $ExpectedVersion -and $riftArenaManifestPin.Count -eq 1 -and
            $riftArenaManifestPin[0].sha256 -eq (Get-FileHash -LiteralPath $riftArenaManifestPath -Algorithm SHA256).Hash.ToLowerInvariant() -and
            @(@($riftArenaRules.arenaWidth,$riftArenaRules.arenaHeight,$riftArenaRules.riverHalfWidth,$riftArenaRules.bridgeCenterX,$riftArenaRules.guardDepth,$riftArenaRules.coreDepth) |
                Where-Object { $_ -isnot [ValueType] -or ![double]::IsFinite([double]$_) -or $_ -le 0 }).Count -eq 0
        if($riftArenaManifestPassed) {
            $riftArenaWidth=[int]$riftArenaRules.arenaWidth;$riftArenaHeight=[int]$riftArenaRules.arenaHeight
            $riftArenaBridgeX=[double]$riftArenaRules.bridgeCenterX;$riftArenaGuardDepth=[double]$riftArenaRules.guardDepth;$riftArenaCoreDepth=[double]$riftArenaRules.coreDepth
            $riftArenaRiverDepth=2*[double]$riftArenaRules.riverHalfWidth
            $riftArenaRiverRows=0;$riftArenaPlayerRows=0;$riftArenaEnemyRows=0
            for($riftArenaRow=0;$riftArenaRow -lt $riftArenaHeight;++$riftArenaRow) {
                $riftArenaRowZ=$riftArenaRow-($riftArenaHeight-1)/2.
                if([math]::Abs($riftArenaRowZ) -lt $riftArenaRules.riverHalfWidth) {++$riftArenaRiverRows;continue}
                # Native stone paths use two columns and every second board row
                # extending through the current Guard depth; row parity follows
                # the approved board height.
                if([math]::Abs($riftArenaRowZ) -lt ($riftArenaGuardDepth+.78) -and $riftArenaRow%2 -eq 0) {
                    if($riftArenaRowZ -gt 0){++$riftArenaPlayerRows}else{++$riftArenaEnemyRows}
                }
            }
            $riftArenaFloor=$riftArenaWidth*($riftArenaHeight-$riftArenaRiverRows)
            $riftArenaDecoration=($riftArenaWidth+10)*($riftArenaHeight+6-$riftArenaRiverRows)-$riftArenaFloor
            $riftArenaPavers=4*($riftArenaPlayerRows+$riftArenaEnemyRows)
        }
    }
    $riftArenaGeometryPassed=$riftArenaManifestPassed -and $null -ne $geometry -and $geometry.passed -eq $true -and $geometry.floorPassed -eq $true -and $geometry.riverPassed -eq $true -and
        $geometry.bridgesPassed -eq $true -and $geometry.borderPassed -eq $true -and $geometry.widthTiles -eq $riftArenaWidth -and $geometry.heightTiles -eq $riftArenaHeight -and
        $geometry.playableFloorTiles -eq $riftArenaFloor -and $geometry.decorativeFloorTiles -eq $riftArenaDecoration -and $geometry.riverWidthTiles -eq $riftArenaWidth -and [math]::Abs($geometry.riverDepthTiles-$riftArenaRiverDepth) -lt .00001 -and
        @($geometry.bridges).Count -eq 2
    if($riftArenaCurrent) {
        $riftArenaGeometryPassed=$riftArenaGeometryPassed -and $geometry.worldUnitsPerTile -eq 125 -and $geometry.authoredModelScale -eq 1.25 -and
            $geometry.gridShaderPassed -eq $true -and [math]::Abs($geometry.gridShaderTileSize-125) -lt .001 -and
            $geometry.expectedPlayableFloorTiles -eq $riftArenaFloor -and $geometry.expectedDecorativeFloorTiles -eq $riftArenaDecoration -and
            @($geometry.bridges | Where-Object { [math]::Abs([math]::Abs($_.x)-$riftArenaBridgeX) -gt .000001 -or [math]::Abs($_.z) -gt .000001 }).Count -eq 0 -and
            @($geometry.bridges.x | Sort-Object -Unique).Count -eq 2
    }
    if([version]$ExpectedVersion -ge [version]'1.4.0') {
        $riftArenaGeometryPassed=$riftArenaGeometryPassed -and $geometry.lanePathsPassed -eq $true -and
            $geometry.lanePaverCount -eq $riftArenaPavers -and $geometry.expectedLanePaverCount -eq $riftArenaPavers -and @($geometry.lanePavers).Count -eq $riftArenaPavers -and
            @($geometry.lanePaths).Count -eq 4 -and @($geometry.lanePaths|ForEach-Object{"$($_.team),$($_.lane)"}|Sort-Object -Unique).Count -eq 4 -and
            @($geometry.lanePaths|Where-Object{
                $_.alignmentPassed -ne $true -or $_.lane -notin @(-1,1) -or $_.team -notin @('player','enemy') -or
                [math]::Abs($_.centerX-$_.lane*$riftArenaBridgeX) -gt .000001 -or [math]::Abs(($_.minimumX+$_.maximumX)/2-$_.centerX) -gt .000001 -or
                [math]::Abs($_.maximumX-$_.minimumX-1) -gt .000001 -or $_.pavers -ne $_.rows*2 -or
                ($_.team -eq 'player' -and $_.rows -ne $riftArenaPlayerRows) -or ($_.team -eq 'enemy' -and $_.rows -ne $riftArenaEnemyRows)
            }).Count -eq 0 -and
            $geometry.towerSnapshotAvailable -eq $true -and $geometry.towerAlignmentPassed -eq $true -and
            $geometry.guardsAligned -eq 4 -and $geometry.coresCentered -eq 2 -and @($geometry.towers).Count -eq 6 -and
            @($geometry.towers.id|Sort-Object -Unique).Count -eq 6 -and
            @($geometry.towers|Where-Object{
                $_.alignmentPassed -ne $true -or $_.team -notin @('player','enemy') -or $_.kind -notin @('core','guard') -or
                [math]::Abs($_.x-$(if($_.kind -eq 'core'){0}else{$_.lane*$riftArenaBridgeX})) -gt .000001 -or
                [math]::Abs($_.z-$(if($_.team -eq 'player'){1}else{-1})*$(if($_.kind -eq 'core'){$riftArenaCoreDepth}else{$riftArenaGuardDepth})) -gt .000001 -or
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
function Test-RiftWardenDeathCapture($Capture,[string]$Stage) {
    function Test-RiftWardenNumber($Value) { return $null -ne $Value -and $Value -is [ValueType] -and $Value -isnot [bool] -and [double]::IsFinite([double]$Value) }
    if($Stage -notin @('before','death','children','cleanup')){return $false}
    if(!$Capture -or $Capture.schema -ne 1 -or $Capture.profile -cne 'expansion_warden_death' -or $Capture.stage -cne $Stage -or
        $Capture.passed -ne $true -or $Capture.setupPassed -ne $true -or $Capture.paused -ne $true -or $Capture.paidCostAndCyclePassed -ne $true -or
        $Capture.deterministicDeathTimingPassed -ne $true -or $Capture.parentPaidAttributionPassed -ne $true -or $Capture.actualLifecyclePassed -ne $true -or
        $Capture.childBirthModelsAndAnimationsPassed -ne $true -or $Capture.actualLivingSecondaryMotionPassed -ne $true -or
        $Capture.referenceLethalCardId -cne 'vampire_bats' -or @($Capture.plays).Count -ne 2 -or @($Capture.captiveMotion).Count -ne 8){return $false}
    foreach($field in @('wardenId','wardenPlayId','referenceDeathStep','referenceDeathSeconds','fixedSteps','cleanupSeconds','sampledAgeSeconds','actualWardenDeaths','actualChildSpawnEvents','soulThreadInstances','livingChildren','livingChildVisuals','wardenAnimationPosition')) {
        if(!(Test-RiftWardenNumber $Capture.$field)){return $false}
    }
    foreach($field in @('wardenId','wardenPlayId','referenceDeathStep','fixedSteps','actualWardenDeaths','actualChildSpawnEvents','soulThreadInstances','livingChildren','livingChildVisuals')){if([math]::Truncate($Capture.$field) -ne $Capture.$field){return $false}}
    if($Capture.wardenId -le 0 -or $Capture.wardenPlayId -le 0 -or $Capture.referenceDeathStep -lt 2 -or $Capture.referenceDeathStep -gt 1800 -or
        [math]::Abs($Capture.referenceDeathSeconds-$Capture.referenceDeathStep/60) -gt 1.e-8 -or [math]::Abs($Capture.sampledAgeSeconds-$Capture.fixedSteps/60) -gt 1.e-8 -or
        $Capture.cleanupSeconds -lt .1 -or $Capture.cleanupSeconds -gt 1.05){return $false}
    foreach($id in @('bone_warden','vampire_bats')) {
        $play=@($Capture.plays | Where-Object cardId -CEQ $id)
        if($play.Count -ne 1){return $false}
        foreach($field in @('team','playId','fixtureAetherGrant','cost','spentDelta','actualMembers','expectedMembers')){if(!(Test-RiftWardenNumber $play[0].$field)){return $false}}
        if($play.Count -ne 1 -or $play[0].accepted -ne $true -or $play[0].sandbox -ne $false -or $play[0].passed -ne $true -or
            $play[0].handCycledOnce -ne $true -or $play[0].playId -le 0 -or $play[0].spentDelta -ne $play[0].cost -or
            $play[0].cost -ne $(if($id -eq 'bone_warden'){6}else{5}) -or $play[0].team -ne $(if($id -eq 'bone_warden'){0}else{1}) -or $play[0].fixtureAetherGrant -lt 0 -or
            $play[0].actualMembers -ne $(if($id -eq 'bone_warden'){1}else{5}) -or $play[0].expectedMembers -ne $play[0].actualMembers -or
            @($play[0].memberIds).Count -ne $play[0].actualMembers -or @($play[0].memberIds|Sort-Object -Unique).Count -ne $play[0].actualMembers -or
            @($play[0].memberIds|Where-Object {!(Test-RiftWardenNumber $_) -or $_ -le 0 -or [math]::Truncate($_) -ne $_}).Count -gt 0){return $false}
        if($id -eq 'bone_warden' -and ($play[0].playId -ne $Capture.wardenPlayId -or $play[0].memberIds[0] -ne $Capture.wardenId)){return $false}
    }
    foreach($role in @('head','arm')) {foreach($index in 0..3) {
        $bone=@($Capture.captiveMotion | Where-Object bone -CEQ "captive_${role}_$index")
        if($bone.Count -ne 1){return $false}
        foreach($field in @('livingSamples','relativeTranslationChangeCm','relativeRotationChangeDegrees')){if(!(Test-RiftWardenNumber $bone[0].$field)){return $false}}
        if($bone.Count -ne 1 -or $bone[0].present -ne $true -or $bone[0].actualLivingSecondaryMotionObserved -ne $true -or $bone[0].livingSamples -lt 2 -or
            !$bone[0].parent -or ($bone[0].relativeTranslationChangeCm -le .001 -and $bone[0].relativeRotationChangeDegrees -le .01)){return $false}
    }}
    $before=$Stage -eq 'before';$expected=if($before){0}else{4}
    if($Capture.actualWardenDeaths -ne $(if($before){0}else{1}) -or $Capture.actualChildSpawnEvents -ne $expected -or @($Capture.childBirthVisuals).Count -ne $expected -or
        @($Capture.childBirthVisuals.id | Sort-Object -Unique).Count -ne $expected){return $false}
    foreach($child in $Capture.childBirthVisuals) {
        foreach($field in @('id','parentEntityId','playId','bornSeconds','animationPosition')){if(!(Test-RiftWardenNumber $child.$field)){return $false}}
        if($child.id -le 0 -or $child.id -eq $Capture.wardenId -or $child.parentEntityId -ne $Capture.wardenId -or $child.playId -ne $Capture.wardenPlayId -or
            $child.cardId -cne 'bone_warden_skeleton' -or $child.attributionCardId -cne 'bone_warden' -or $child.modelId -cne 'bone_warden_skeleton' -or
            $child.dead -ne $false -or $child.actualImportedModelBound -ne $true -or $child.noncollidingVisual -ne $true -or $child.actualDeployAnimationBound -ne $true -or
            $child.animation -cne 'Deploy' -or !$child.expectedDeployAnimationAsset -or $child.animationAsset -cne $child.expectedDeployAnimationAsset -or
            $child.animationPosition -lt 0 -or [math]::Abs($child.bornSeconds-$Capture.referenceDeathSeconds) -gt 1.e-8){return $false}
    }
    if(@($Capture.children).Count -gt $expected -or @($Capture.children.id|Sort-Object -Unique).Count -ne @($Capture.children).Count){return $false}
    foreach($child in $Capture.children) {
        foreach($field in @('id','parentEntityId','playId','animationPosition')){if(!(Test-RiftWardenNumber $child.$field)){return $false}}
        if($child.id -notin @($Capture.childBirthVisuals.id) -or $child.parentEntityId -ne $Capture.wardenId -or $child.playId -ne $Capture.wardenPlayId -or
            $child.cardId -cne 'bone_warden_skeleton' -or $child.attributionCardId -cne 'bone_warden' -or $child.dead -isnot [bool]){return $false}
        if(!$child.dead -and ($child.visualPresent -ne $true -or !$child.animationAsset -or !$child.animation -or $child.animationPosition -lt 0)){return $false}
    }
    if($Capture.livingChildren -ne @($Capture.children|Where-Object dead -EQ $false).Count -or
        $Capture.livingChildVisuals -ne @($Capture.children|Where-Object {$_.dead -eq $false -and $_.visualPresent -eq $true}).Count){return $false}
    if($before){return $Capture.fixedSteps -eq ($Capture.referenceDeathStep-1) -and $Capture.wardenVisualPresent -eq $true -and $Capture.wardenBodyPresent -eq $true -and $Capture.wardenDead -eq $false -and $Capture.soulThreadInstances -eq 0 -and @($Capture.children).Count -eq 0}
    if($Capture.actualDeathAnimationObservedAtLethalStep -ne $true){return $false}
    if($Stage -eq 'cleanup'){return $Capture.fixedSteps -eq ($Capture.referenceDeathStep+[math]::Ceiling($Capture.cleanupSeconds*60)+3) -and
        $Capture.wardenVisualPresent -eq $false -and $Capture.sampledAgeSeconds -gt ($Capture.referenceDeathSeconds+$Capture.cleanupSeconds) -and $Capture.soulThreadInstances -eq 0}
    return $Capture.fixedSteps -eq ($Capture.referenceDeathStep+$(if($Stage -eq 'death'){1}else{6})) -and $Capture.wardenVisualPresent -eq $true -and
        $Capture.wardenDead -eq $true -and $Capture.actualWardenDeathAnimationBound -eq $true -and !!$Capture.wardenAnimationAsset -and $Capture.soulThreadInstances -eq 20 -and
        $Capture.livingChildren -eq 4 -and $Capture.livingChildVisuals -eq 4 -and @($Capture.children).Count -eq 4 -and
        $Capture.sampledAgeSeconds -lt ($Capture.referenceDeathSeconds+.3) -and @($Capture.children|Where-Object {$_.parentEntityId -ne $Capture.wardenId -or $_.playId -ne $Capture.wardenPlayId -or $_.dead -ne $false -or !$_.animationAsset}).Count -eq 0
}
function Test-RiftReaperDesignCapture($Capture,[string]$Stage) {
    function Test-RiftReaperNumber($Value) { return $null -ne $Value -and $Value -is [ValueType] -and $Value -isnot [bool] -and [double]::IsFinite([double]$Value) }
    if($Stage -notin @('idle','walk','cleave','cloak','expired') -or !$Capture -or $Capture.schema -ne 1 -or $Capture.profile -cne 'reaper_design' -or $Capture.stage -cne $Stage){return $false}
    foreach($flag in @('passed','setupPassed','paused','paidCostAndCyclePassed','sandboxTargetAndPacketSetupPassed','allImportedModelsBound','allExpansionMaterialsBound','scalePassed','actualStagePassed','actualAuraMotionObserved','eventAttributionPassed','actualTelemetryPassed')) {
        if($Capture.$flag -isnot [bool] -or $Capture.$flag -ne $true){return $false}
    }
    foreach($field in @('reaperId','reaperPlayId','sampledAgeSeconds','fixedSteps','baseModelScale','actualModelScale','readabilityMultiplier','movementDistanceTiles','bodyHP','maxHP','firstPacketHP','invisibleUntil','cloakAppliedAt','actualCloakEvents','actualExpireEvents','actualDamagePackets','actualCleaveEvents','actualCleaveVictims','cleaveAt','actualCleaveMeshPieces','actualAuraInstances','actualPaidPlays','actualPaidSpend','actualDamageTaken','actualTroopDamageDealt')) {
        if(!(Test-RiftReaperNumber $Capture.$field)){return $false}
    }
    foreach($field in @('reaperId','reaperPlayId','fixedSteps','actualCloakEvents','actualExpireEvents','actualDamagePackets','actualCleaveEvents','actualCleaveVictims','actualCleaveMeshPieces','actualAuraInstances')) {if([math]::Truncate($Capture.$field) -ne $Capture.$field){return $false}}
    if($Capture.reaperId -le 0 -or $Capture.reaperPlayId -le 0 -or $Capture.fixedSteps -lt 1 -or $Capture.fixedSteps -gt 360 -or
       [math]::Abs($Capture.sampledAgeSeconds-$Capture.fixedSteps/60) -gt 1.e-8 -or [math]::Abs($Capture.baseModelScale-1.35) -gt 1.e-5 -or $Capture.readabilityMultiplier -le 0 -or
       [math]::Abs($Capture.actualModelScale-1.35*1.25*$Capture.readabilityMultiplier) -gt 1.e-5 -or $Capture.actualAuraInstances -ne 34 -or $Capture.maxHP -ne 445 -or
       !$Capture.model -or !$Capture.portrait -or !$Capture.animationAsset -or $Capture.materialBindings.expansionMaterialBindingsPassed -ne $true -or
       $Capture.hiddenFromViewer -isnot [bool] -or $Capture.hiddenFromViewer -ne $false -or $Capture.cloaked -isnot [bool] -or $Capture.healthCloakTriggered -isnot [bool]){return $false}
    $damaged=$Stage -in @('cloak','expired');$expired=$Stage -eq 'expired';$cleave=$Stage -eq 'cleave'
    if($Capture.actualPaidPlays -ne 1 -or $Capture.actualPaidSpend -ne 4 -or $Capture.actualDamageTaken -ne $(if($damaged){350}else{0}) -or $Capture.actualTroopDamageDealt -ne $(if($cleave){190}else{0})){return $false}
    if($Capture.bodyHP -ne $(if($damaged){95}else{445}) -or $Capture.firstPacketHP -ne $(if($damaged){270}else{-1}) -or
       $Capture.actualCloakEvents -ne [int]$damaged -or $Capture.actualExpireEvents -ne [int]$expired -or $Capture.healthCloakTriggered -ne $damaged -or $Capture.cloaked -ne ($Stage -eq 'cloak') -or
       $Capture.actualDamagePackets -ne $(if($damaged){2}else{0}) -or @($Capture.incomingPackets).Count -ne $(if($damaged){2}else{0}) -or
       $Capture.actualCleaveEvents -ne [int]$cleave -or $Capture.actualCleaveVictims -ne $(if($cleave){2}else{0}) -or $Capture.actualCleaveMeshPieces -ne $(if($cleave){20}else{0})){return $false}
    $spawns=@($Capture.sandboxSpawns);$expectedSpawns=if($damaged -or $cleave){2}else{1};$expectedCard=if($damaged){'bullet_burst'}else{'sky_manta'}
    if($spawns.Count -ne $expectedSpawns -or @($spawns|Where-Object {$_.cardId -cne $expectedCard -or $_.accepted -ne $true -or $_.sandbox -ne $true}).Count -gt 0){return $false}
    if($damaged) {
        if($Capture.cloakAppliedAt -le 0 -or @($Capture.incomingPackets.playId|Sort-Object -Unique).Count -ne 2){return $false}
        for($index=0;$index -lt 2;++$index) {
            $packet=$Capture.incomingPackets[$index]
            foreach($field in @('time','amount','requested','hpAfter','playId')){if(!(Test-RiftReaperNumber $packet.$field)){return $false}}
            if($packet.cardId -cne 'bullet_burst' -or $packet.amount -ne 175 -or $packet.requested -ne 175 -or $packet.hpAfter -ne $(if($index -eq 0){270}else{95}) -or $packet.playId -le 0 -or $packet.time -le 0){return $false}
        }
        if($Capture.incomingPackets[0].time -ge $Capture.incomingPackets[1].time -or [math]::Abs($Capture.cloakAppliedAt-$Capture.incomingPackets[1].time) -gt 1.e-8){return $false}
        if($Stage -eq 'cloak'){return $Capture.invisibleUntil -gt $Capture.sampledAgeSeconds -and [math]::Abs($Capture.invisibleUntil-$Capture.cloakAppliedAt-1) -lt 1.e-8}
        return $Capture.invisibleUntil -eq 0 -and $Capture.sampledAgeSeconds -ge ($Capture.cloakAppliedAt+1)
    }
    if($Capture.invisibleUntil -ne 0 -or $Capture.cloakAppliedAt -ne -1){return $false}
    if($Stage -eq 'idle'){return $Capture.animation -ceq 'Idle'}
    if($Stage -eq 'walk'){return $Capture.animation -ceq 'Locomotion' -and $Capture.movementDistanceTiles -gt .25}
    return $Capture.cleaveAt -ge 0 -and ($Capture.sampledAgeSeconds-$Capture.cleaveAt) -gt 0 -and ($Capture.sampledAgeSeconds-$Capture.cleaveAt) -lt .22
}
$riftExpansionRosterPassed=$true
if($Scenario -eq 'expansion_warden_death') {$riftExpansionRosterPassed=$stateCaptured -and (Test-RiftWardenDeathCapture $riftCaptureState.expansionRoster $WardenDeathStage)}
if($Scenario -eq 'reaper_design') {$riftExpansionRosterPassed=$stateCaptured -and (Test-RiftReaperDesignCapture $riftCaptureState.expansionRoster $ReaperStage)}
if($Scenario -in @('expansion_roster','expansion_new_roster','expansion_effects')) {
    $riftRoster=$riftCaptureState.expansionRoster
    $riftRosterNew=$Scenario -ne 'expansion_roster'
    $riftRosterEffects=$Scenario -eq 'expansion_effects'
    $riftExpansionEffectAdvance=switch($ExpansionEffectStage){'cast'{.12};'impact'{.37};default{.95}}
    $riftExpansionEffectAdvanceSteps=[math]::Round($riftExpansionEffectAdvance*60)
    $riftRosterSteps=if($riftRosterEffects){21+$riftExpansionEffectAdvanceSteps}else{21}
    $riftRosterAge=$riftRosterSteps/60
    $riftRosterTotal=if($riftRosterNew){6}else{42}
    $riftRosterTeamTotal=if($riftRosterNew){3}else{21}
    $riftRosterSpent=if($riftRosterNew){26}else{24}
    $riftRosterIds=if($riftRosterNew){@('bone_warden','aether_reaper','iron_giant')}else{@('mini_stampede','stampede','pulverizer')}
    $riftExpansionRosterPassed=$stateCaptured -and $null -ne $riftRoster -and $riftRoster.passed -eq $true -and
        $riftRoster.schema -eq 1 -and $riftRoster.setupPassed -eq $true -and $riftRoster.paidCostAndCyclePassed -eq $true -and
        $riftRoster.fixedSteps -eq $riftRosterSteps -and [math]::Abs($riftRoster.sampledAgeSeconds-$riftRosterAge) -lt 1.e-8 -and
        $riftRoster.expectedSelectedCount -eq $riftRosterTotal -and $riftRoster.actualSelectedCount -eq $riftRosterTotal -and $riftRoster.distinctMemberIds -eq $riftRosterTotal -and
        $riftRoster.playerMembers -eq $riftRosterTeamTotal -and $riftRoster.enemyMembers -eq $riftRosterTeamTotal -and $riftRoster.distinctPlayIds -eq 6 -and
        $riftRoster.paused -eq $true -and $riftRoster.allImportedModelsBound -eq $true -and $riftRoster.allTroopsWalking -eq $true -and $riftRoster.friendlyRearEnemyFrontPassed -eq $true -and
        @($riftRoster.members).Count -eq $riftRosterTotal -and @($riftRoster.members.id | Sort-Object -Unique).Count -eq $riftRosterTotal -and @($riftRoster.plays).Count -eq 6 -and
        @($riftRoster.plays | Where-Object { $_.accepted -ne $true -or $_.sandbox -ne $false -or $_.handCycledOnce -ne $true -or $_.spentDelta -ne $_.cost }).Count -eq 0 -and
        ($riftRoster.plays | Measure-Object -Property spentDelta -Sum).Sum -eq $riftRosterSpent -and @($riftRoster.cards).Count -eq 3 -and
        @($riftRoster.members | Where-Object { $_.dead -ne $false -or $_.actualImportedModelBound -ne $true -or $_.friendlyRearEnemyFront -ne $true -or $_.facingAlignmentDot -le .6 }).Count -eq 0
    foreach($riftRosterId in $riftRosterIds) {
        $riftRosterCount=switch($riftRosterId){'mini_stampede'{5};'stampede'{15};default{1}}
        foreach($riftRosterTeam in @(0,1)) {
            $riftRosterMembers=@($riftRoster.members | Where-Object { $_.cardId -eq $riftRosterId -and $_.team -eq $riftRosterTeam })
            $riftExpansionRosterPassed=$riftExpansionRosterPassed -and $riftRosterMembers.Count -eq $riftRosterCount -and
                @($riftRosterMembers | Where-Object { $_.modelId -ne $riftRosterId -or ($riftRosterId -ne 'pulverizer' -and $_.animation -ne 'Locomotion') }).Count -eq 0
        }
        $riftRosterCard=@($riftRoster.cards | Where-Object cardId -eq $riftRosterId)
        $riftRosterBones=switch($riftRosterId){'pulverizer'{16};'bone_warden'{40};'aether_reaper'{29};'iron_giant'{35};default{30}}
        $riftExpansionRosterPassed=$riftExpansionRosterPassed -and $riftRosterCard.Count -eq 1 -and $riftRosterCard[0].bones -eq $riftRosterBones -and $riftRosterCard[0].lods -eq 4 -and
            $riftRosterCard[0].forwardAxisIsX -eq $true -and @($riftRosterCard[0].clips).Count -eq 9 -and
            @($riftRosterCard[0].clips | Where-Object { [math]::Abs($_.targetSamplingFPS-100) -gt .001 -or ($isEditor -and [math]::Abs($_.sourceFPS-100) -gt .001) }).Count -eq 0
        if($riftRosterId -eq 'bone_warden') {
            $riftExpansionRosterPassed=$riftExpansionRosterPassed -and $riftRosterCard[0].allCaptiveBonesPresent -eq $true -and @($riftRosterCard[0].captiveBones).Count -eq 8
            foreach($role in @('head','arm')) {foreach($index in 0..3) {
                $riftExpansionRosterPassed=$riftExpansionRosterPassed -and @($riftRosterCard[0].captiveBones|Where-Object {$_.bone -ceq "captive_${role}_$index" -and $_.present -eq $true}).Count -eq 1
            }}
        }
    }
    if($riftRosterEffects) {
        $riftExpansionRosterPassed=$riftExpansionRosterPassed -and $riftRoster.actualMechanicVisualsPassed -eq $true -and
            $riftRoster.effectStage -eq $ExpansionEffectStage -and $riftRoster.effectFixedAdvanceSteps -eq $riftExpansionEffectAdvanceSteps -and
            [math]::Abs($riftRoster.effectCastAgeSeconds-.35) -lt 1.e-8 -and
            [math]::Abs($riftRoster.effectRequestedAdvanceSeconds-$riftExpansionEffectAdvance) -lt 1.e-8 -and
            [math]::Abs($riftRoster.effectFixedAdvanceSeconds-$riftExpansionEffectAdvanceSteps/60) -lt 1.e-8 -and
            [math]::Abs($riftRoster.requestedAgeSeconds-(.35+$riftExpansionEffectAdvance)) -lt 1.e-8 -and
            $riftRoster.actualNativeSourceEffectsPassed -eq $true -and
            @($riftRoster.effectPaidCasts).Count -eq 3 -and @($riftRoster.effectPaidCasts|Where-Object passed -ne $true).Count -eq 0
        if($ExpansionEffectStage -eq 'cast') {
            $riftExpansionRosterPassed=$riftExpansionRosterPassed -and $riftRoster.nativeSourceEffectsRequired -eq $true -and
                $riftRoster.actualNativeSourceEffect -eq 'ShadowWisps' -and $riftRoster.actualNativeSourceEffectCount -gt 0 -and
                @($riftRoster.members|Where-Object {$_.cardId -eq 'iron_giant' -and $_.bodyHP -eq 1000 -and $_.shieldHP -eq 310 -and $_.shieldFragments -eq 0 -and $_.cloaked -eq $false -and $_.hiddenFromViewer -eq $false}).Count -eq 2
        } elseif($ExpansionEffectStage -eq 'impact') {
            $riftExpansionRosterPassed=$riftExpansionRosterPassed -and $riftRoster.nativeSourceEffectsRequired -eq $true -and
                $riftRoster.actualNativeSourceEffect -eq 'ShadowGroundRibbon' -and $riftRoster.actualNativeSourceEffectCount -gt 0 -and
                @($riftRoster.members|Where-Object {$_.cardId -eq 'iron_giant' -and $_.bodyHP -eq 1000 -and $_.shieldHP -eq 310 -and $_.shieldFragments -eq 0 -and $_.cloaked -eq $true -and $_.hiddenFromViewer -eq ($_.team -eq 1)}).Count -eq 2
        } else {
            $riftExpansionRosterPassed=$riftExpansionRosterPassed -and $riftRoster.nativeSourceEffectsRequired -eq $false -and
                @($riftRoster.members|Where-Object {$_.cardId -eq 'iron_giant' -and $_.team -eq 0 -and $_.bodyHP -eq 1000 -and $_.shieldHP -eq 0 -and $_.shieldFragments -eq 6 -and $_.cloaked -eq $true -and $_.hiddenFromViewer -eq $false}).Count -eq 1 -and
                @($riftRoster.members|Where-Object {$_.cardId -eq 'iron_giant' -and $_.team -eq 1 -and $_.bodyHP -eq 1000 -and $_.shieldHP -eq 310 -and $_.shieldFragments -eq 0 -and $_.cloaked -eq $true -and $_.hiddenFromViewer -eq $true}).Count -eq 1
        }
    }
}
function Test-RiftPulverizerWitness($State,[double]$Age,$Manifest) {
    $w=$State.pulverizerWitness
    if($null -eq $w -or $null -eq $Manifest -or $w.schemaVersion -ne 1 -or $w.passed -ne $true -or $w.realPaidPlay -ne $true -or $w.sandbox -ne $false -or $State.speed -ne 0){return $false}
    foreach($field in @('sourceId','playId','paidPlaySequence','launchSequence','waveId','cost','spentDelta','aetherBefore','aetherAfterPaidPlay','handIndex','launchWaitFixedSteps','maximumLaunchWaitSeconds','fixtureStartedAt','launchBornSeconds','launchExpiresSeconds','launchDamage','waveSpeed','maximumRadius','requestedAgeSeconds','fixedAdvanceSteps','sampledAgeSeconds','sampleSimulationSeconds','firstWaveLiveRadius','groundHitCount','flyingHitCount','groundSlowCount','flyingSlowCount','sourceHPAtDeployment','sourceMaxHP','sourceHPAtSample')) {
        $v=$w.$field;if($v -isnot [ValueType] -or $v -is [bool] -or ![double]::IsFinite([double]$v)){return $false}
    }
    $card=@($Manifest.cards | Where-Object id -CEQ 'pulverizer');if($card.Count -ne 1){return $false}
    $steps=[math]::Round($Age*60);$sampleAge=$steps/60.;$tolerance=1.e-7
    if($w.manifestVersion -ne $Manifest.version -or $w.balanceHash -ne $Manifest.balanceHash -or $w.handCycledOnce -ne $true -or
        $w.sourceId -le 0 -or $w.playId -le 0 -or $w.paidPlaySequence -le 0 -or $w.launchSequence -le $w.paidPlaySequence -or $w.waveId -ne $w.launchSequence -or
        $w.cost -ne $card[0].cost -or $w.spentDelta -ne $w.cost -or $w.aetherBefore -ne 10 -or $w.aetherAfterPaidPlay -ne (10-$w.cost) -or
        $w.handIndex -notin @(0,1,2,3) -or $w.launchWaitFixedSteps -lt 1 -or $w.launchWaitFixedSteps -gt 120 -or $w.maximumLaunchWaitSeconds -ne 2 -or
        [math]::Abs($w.launchBornSeconds-$w.fixtureStartedAt-$w.launchWaitFixedSteps/60.) -gt $tolerance -or
        $w.waveSpeed -ne 8 -or $w.maximumRadius -ne 4 -or $w.launchDamage -ne 65 -or
        [math]::Abs($w.launchExpiresSeconds-$w.launchBornSeconds-.5) -gt $tolerance -or
        [math]::Abs($w.requestedAgeSeconds-$Age) -gt $tolerance -or $w.fixedAdvanceSteps -ne $steps -or
        [math]::Abs($w.sampledAgeSeconds-$sampleAge) -gt $tolerance -or [math]::Abs($w.sampleSimulationSeconds-$w.launchBornSeconds-$sampleAge) -gt $tolerance -or
        [math]::Abs($State.elapsed-$w.sampleSimulationSeconds) -gt $tolerance -or
        $w.sourceHPAtDeployment -ne $card[0].hp -or $w.sourceMaxHP -ne $card[0].hp -or $w.flyingHitCount -ne 0 -or $w.flyingSlowCount -ne 0){return $false}
    $enemy=@($w.enemyBodies);if($enemy.Count -ne 7 -or @($enemy.id | Sort-Object -Unique).Count -ne 7 -or @($enemy | Where-Object flying -eq $true).Count -ne 1){return $false}
    $hits=@($w.firstWaveHits);$slows=@($w.firstWaveSlows)
    if($hits.Count -ne $w.groundHitCount -or $slows.Count -ne $w.groundSlowCount -or @($hits.target | Sort-Object -Unique).Count -ne $hits.Count -or @($slows.target | Sort-Object -Unique).Count -ne $slows.Count){return $false}
    foreach($hit in $hits) {
        if($hit.knownEnemyTroop -ne $true -or $hit.flying -ne $false -or $hit.targetKind -ne 0 -or $hit.requestedDamage -ne 65 -or
            $hit.amount -le 0 -or $hit.amount -gt 65 -or $hit.time -lt $w.launchBornSeconds -or $hit.time -gt $w.launchExpiresSeconds+$tolerance -or
            @($enemy | Where-Object { $_.id -eq $hit.target -and $_.flying -eq $false }).Count -ne 1){return $false}
    }
    foreach($slow in $slows) {
        if($slow.knownEnemyTroop -ne $true -or $slow.flying -ne $false -or $slow.amount -ne .1 -or [math]::Abs($slow.until-$slow.time-1.5) -gt $tolerance -or
            @($hits | Where-Object { $_.target -eq $slow.target -and [math]::Abs($_.time-$slow.time) -le $tolerance }).Count -ne 1){return $false}
    }
    $source=@($State.niagara.unitAnimations | Where-Object { $_.entityId -eq $w.sourceId -and $_.assetId -eq 'pulverizer' -and $_.team -eq 'player' })
    if($sampleAge -le .8+$tolerance -and ($w.sourcePresent -ne $true -or $w.sourceLiving -ne $true -or $source.Count -ne 1 -or $w.sourceHPAtSample -le 0)){return $false}
    $waves=@($State.niagara.shockwaveVisuals | Where-Object id -EQ $w.waveId)
    $front=@($State.niagara.expansionSurfaceEffects | Where-Object name -CEQ 'PulverizerTravelingFront')
    $trail=@($State.niagara.expansionSurfaceEffects | Where-Object name -CEQ 'PulverizerTravelingTrail')
    if($sampleAge -lt .5-$tolerance) {
        if($w.firstWaveLive -ne $true -or $waves.Count -ne 1 -or $waves[0].source -ne $w.sourceId -or $waves[0].playId -ne $w.playId -or
            [math]::Abs($waves[0].born-$w.launchBornSeconds) -gt $tolerance -or [math]::Abs($waves[0].radius-$sampleAge*8) -gt $tolerance -or
            [math]::Abs($w.firstWaveLiveRadius-$sampleAge*8) -gt $tolerance -or $front.Count -ne 1 -or $front[0].instances -ne 48 -or $front[0].visible -ne $true -or
            $front[0].nonInteractive -ne $true -or $trail.Count -ne 1 -or $trail[0].instances -ne 48 -or $trail[0].visible -ne $true){return $false}
    } elseif($w.firstWaveLive -ne $false -or $waves.Count -ne 0){return $false}
    if([math]::Abs($sampleAge-.1) -lt $tolerance -and $source[0].pulverizerPulsePieces -ne 32){return $false}
    if([math]::Abs($sampleAge-.35) -lt $tolerance -and $source[0].pulverizerPulsePieces -ne 0){return $false}
    if([math]::Abs($sampleAge-.8) -lt $tolerance -and ($source[0].pulverizerPulsePieces -ne 0 -or @($front | Where-Object instances -gt 0).Count -gt 0 -or $w.groundHitCount -lt 1 -or $w.groundSlowCount -lt 1 -or $State.slowedEntities -lt 1 -or
        @($w.sampledEnemyBodies | Where-Object { $_.dead -eq $false -and $_.flying -eq $false -and $_.hp -gt 0 -and $_.slowPct -eq .1 -and $_.slowUntil -gt $w.sampleSimulationSeconds }).Count -lt 1)){return $false}
    return $true
}

$riftPulverizerWitnessPassed=$true
if($Scenario -eq 'pulverizer' -and [version]$ExpectedVersion -ge [version]'1.7.0') {
    $riftPulverizerManifest=Get-Content -LiteralPath (Join-Path $repoRoot 'Assets/Data/card_manifest.json') -Raw | ConvertFrom-Json
    $riftPulverizerWitnessPassed=$stateCaptured -and (Test-RiftPulverizerWitness $riftCaptureState ([double]$PulverizerAge) $riftPulverizerManifest)
}
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
    $riftSwarmFormationPassed = if ($SwarmCard -eq 'stampede') {
        $riftExpectedLeft = if ($SwarmX -lt 0) { 9 } else { 6 }
        $riftExpectedRight = 15 - $riftExpectedLeft
        $null -ne $swarm -and $swarm.formationGridPassed -eq $true -and
            $swarm.expectedLeftMembers -eq $riftExpectedLeft -and $swarm.expectedRightMembers -eq $riftExpectedRight -and
            $swarm.leftMembers -eq $riftExpectedLeft -and $swarm.rightMembers -eq $riftExpectedRight -and
            @($swarm.units.spawnX | Sort-Object -Unique).Count -eq 5 -and @($swarm.units.spawnZ | Sort-Object -Unique).Count -eq 3
    } else { $null -ne $swarm -and [math]::Abs($swarm.leftMembers-$swarm.rightMembers) -le 1 }
    $riftSwarmSplitPassed=$stateCaptured -and $null -ne $swarm -and $swarm.schemaVersion -eq 1 -and $swarm.passed -eq $true -and
        $swarm.cardId -eq $SwarmCard -and $swarm.team -eq 'player' -and $swarm.paidPlayOnly -eq $true -and $swarm.paidCostAndCyclePassed -eq $true -and
        $swarm.cost -eq $swarmCost -and $swarm.aetherBefore -eq 10 -and $swarm.aetherAfter -eq (10-$swarmCost) -and $swarm.spentDelta -eq $swarmCost -and
        $swarm.handIndex -eq $swarmSlot -and $swarm.handCycledOnce -eq $true -and ($swarm.handBefore -join '|') -eq 'mini_stampede|stampede|twin_blades|vampire_bats' -and
        $swarm.requestedX -eq $SwarmX -and $swarm.requestedZ -eq $SwarmZ -and $swarm.leftGuardDestroyed -eq [bool]$SwarmLeftGuardDown -and
        $swarm.expectedUnitCount -eq $swarmCount -and $swarm.actualUnitCount -eq $swarmCount -and @($swarm.units).Count -eq $swarmCount -and
        @($swarm.units.id | Sort-Object -Unique).Count -eq $swarmCount -and @($swarm.units.playId | Sort-Object -Unique).Count -eq 1 -and
        ($swarm.leftMembers+$swarm.rightMembers) -eq $swarmCount -and $swarm.leftMembers -gt 0 -and $swarm.rightMembers -gt 0 -and $riftSwarmFormationPassed -and
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
foreach($pin in $riftTowerSourcePins) { $file=Join-Path $repoRoot $pin.path;if(!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256 -or (Get-Item -LiteralPath $file).Length -ne $pin.bytes) { $riftTowerSourcesUnchanged=$false } }
$riftCaptureTreeRoots=@((Join-Path $repoRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Config'))
if($isEditor){$riftCaptureTreeRoots+=(Join-Path $repoRoot 'Unreal/RiftCrownArena/Content')}
$riftCaptureTreePaths=@(Get-ChildItem -LiteralPath $riftCaptureTreeRoots -File -Recurse|Where-Object Extension -in @('.cpp','.h','.cs','.ini','.inc','.uasset','.umap')|ForEach-Object {[IO.Path]::GetRelativePath($repoRoot,$_.FullName).Replace('\','/')})
$riftCapturePinnedTreePaths=@($riftTowerSourcePins.path|Where-Object {$_ -match '^Unreal/RiftCrownArena/(Source|Config|Content)/'})
if(Compare-Object ($riftCapturePinnedTreePaths|Sort-Object -Unique) ($riftCaptureTreePaths|Sort-Object -Unique)){$riftTowerSourcesUnchanged=$false}
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
$riftCapturePassed=!$timedOut -and $freshCapture -and $stateCaptured -and $resolutionMatches -and $riftCaptureQualityPassed -and $riftPhaseFixturePassed -and $riftCameraFramingPassed -and $riftModelEnvelopeCheckPassed -and $riftArenaGeometryPassed -and $riftTowerPathingPassed -and $riftUnitCollisionPassed -and $riftSwarmSplitPassed -and $riftExpansionRosterPassed -and $riftPulverizerWitnessPassed -and $riftTowerSourcesUnchanged -and $riftCaptureExecutableUnchanged -and $process.ExitCode -eq 0 -and $errors.Count -eq 0
$report = [ordered]@{
    schema = 1; version=$ExpectedVersion; passed=[bool]$riftCapturePassed; name = $Name; page = $Page; scenario = $Scenario; inspectCard = $InspectCard; speed = $Speed; breathSmoke = [bool]$BreathSmoke; uiScale=$UIScale; zoom=$Zoom;
    state = $statePath; stateCaptured = $stateCaptured; phaseFixture = $Phase; phaseFixturePassed = $riftPhaseFixturePassed; allowExternalInput = [bool]$AllowExternalInput;
    expansionRosterPassed = [bool]$riftExpansionRosterPassed;
    pulverizerWitnessPassed = [bool]$riftPulverizerWitnessPassed; pulverizerAge=$(if($Scenario -eq 'pulverizer'){$PulverizerAge}else{$null});
    expansionEffectStage = $(if($Scenario -eq 'expansion_effects'){$ExpansionEffectStage}else{$null});
    wardenDeathStage = $(if($Scenario -eq 'expansion_warden_death'){$WardenDeathStage}else{$null});
    reaperStage = $(if($Scenario -eq 'reaper_design'){$ReaperStage}else{$null});
    expansionEffectRequestedAdvanceSeconds = $(if($Scenario -eq 'expansion_effects'){$riftExpansionEffectAdvance}else{$null});
    expansionEffectFixedAdvanceSteps = $(if($Scenario -eq 'expansion_effects'){$riftExpansionEffectAdvanceSteps}else{$null});
    pathingAge = $(if($Scenario -eq 'tower_pathing'){$PathingAge}else{$null}); routeCase=$(if($Scenario -eq 'tower_pathing'){$RouteCase}else{$null}); routeTeam=$(if($Scenario -eq 'tower_pathing'){$RouteTeam}else{$null}); towerPathingPassed = $riftTowerPathingPassed;
    collisionAge=$(if($Scenario -eq 'unit_collision'){$CollisionAge}else{$null}); collisionCase=$(if($Scenario -eq 'unit_collision'){$CollisionCase}else{$null}); unitCollisionPassed=$riftUnitCollisionPassed;
    swarmAge=$(if($Scenario -eq 'swarm_split'){$SwarmAge}else{$null}); swarmCard=$(if($Scenario -eq 'swarm_split'){$SwarmCard}else{$null}); swarmX=$(if($Scenario -eq 'swarm_split'){$SwarmX}else{$null}); swarmZ=$(if($Scenario -eq 'swarm_split'){$SwarmZ}else{$null}); swarmLeftGuardDown=$(if($Scenario -eq 'swarm_split'){[bool]$SwarmLeftGuardDown}else{$null}); swarmSplitPassed=$riftSwarmSplitPassed;
    sourcePins=$riftTowerSourcePins; sourcePinsUnchanged=$riftTowerSourcesUnchanged; executableUnchanged=$riftCaptureExecutableUnchanged;
    cameraFramingRequired = $riftCameraFramingRequired; cameraFramingPassed = $riftCameraFramingPassed; cameraFraming = $riftCameraFraming;
    modelEnvelopeAvailable = $riftModelEnvelopeAvailable; modelEnvelopePassed = $riftModelEnvelopePassed;
    arenaGeometryRequired=$riftArenaGeometryRequired; arenaGeometryPassed=$riftArenaGeometryPassed;
    width = $Width; height = $Height; actualWidth = $actualWidth; actualHeight = $actualHeight;
    requestedQuality = $Quality; observedQualityPreset = $(if ($null -ne $riftCaptureState) { $riftCaptureState.observedQualityPreset } else { $null }); qualityPresetPassed = [bool]$riftCaptureQualityPassed;
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
# Existing callers retain latest aliases; accepted screenshots, state and raw
# logs always remain in the unique run directory referenced by the report.
foreach($riftCaptureOutput in @($capturePath,$statePath,$logPath,$stdoutPath,$stderrPath,$reportPath)) {
    if(Test-Path -LiteralPath $riftCaptureOutput -PathType Leaf) {
        Copy-Item -LiteralPath $riftCaptureOutput -Destination (Join-Path $captureRoot ([IO.Path]::GetFileName($riftCaptureOutput))) -Force
    }
}
Write-Output ($report | ConvertTo-Json -Depth 20)
if (!$riftCapturePassed) { throw "Unreal capture failed; inspect $reportPath" }
