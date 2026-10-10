param(
    [string]$Version='1.4.0',
    [string]$OutputDirectory='Artifacts/QA/BalancePatch'
)
$ErrorActionPreference='Stop'
$collisionRoot=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$collisionOutput=if([System.IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $collisionRoot $OutputDirectory}
$collisionRun=Join-Path $collisionOutput ([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $collisionRun -Force | Out-Null
$collisionLog=Join-Path $collisionRun 'portable.log'
$collisionPinFile=Join-Path $collisionRun 'before-source-pins.json'
$collisionExe=Join-Path $collisionRoot 'Build/NativeTests/RiftSimulationTests.exe'
$collisionSources=@(
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftSimulation.h',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftSimulation.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCombat.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftPathfinding.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftDeckAnalysis.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftAI.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Tests/RiftUnitCollisionTests.cpp',
    'Build/Tests/RiftSimulationTests.cpp',
    'Build/Tests/Run-NativeSimulationTests.ps1',
    'Build/Tests/Verify-BalancePatch.ps1'
)
function Get-CollisionPin([string]$Path){
    $collisionItem=Get-Item -LiteralPath $Path
    return [ordered]@{path=$collisionItem.FullName;sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant();bytes=$collisionItem.Length}
}
function Write-CollisionJson([string]$Path,$Value){
    [System.IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 30),[System.Text.UTF8Encoding]::new($false))
}
$collisionPins=@($collisionSources|ForEach-Object{Get-CollisionPin (Join-Path $collisionRoot $_)})
Write-CollisionJson $collisionPinFile $collisionPins
$collisionStart=[DateTime]::UtcNow
$collisionExit=1;$collisionFailure=$null
try{
    & (Join-Path $PSScriptRoot 'Run-NativeSimulationTests.ps1') -CompileOnly 2>&1 | Tee-Object -FilePath $collisionLog
    & $collisionExe 2>&1 | Tee-Object -FilePath $collisionLog -Append
    $collisionExit=$LASTEXITCODE
    if($collisionExit -ne 0){throw "Portable simulation returned exit code $collisionExit."}
}catch{$collisionFailure=$_.Exception.Message}
$collisionPinsUnchanged=$true
foreach($collisionPin in $collisionPins){
    $collisionAfter=Get-CollisionPin $collisionPin.path
    $collisionPinsUnchanged=$collisionPinsUnchanged -and $collisionAfter.sha256 -eq $collisionPin.sha256 -and $collisionAfter.bytes -eq $collisionPin.bytes
}
$collisionText=if(Test-Path -LiteralPath $collisionLog){Get-Content -LiteralPath $collisionLog -Raw -Encoding utf8}else{''}
$collisionPasses=@([regex]::Matches($collisionText,'(?m)^PASS (.+)\r?$')|ForEach-Object{$_.Groups[1].Value.Trim()})
$collisionSummary=[regex]::Match($collisionText,'COLLISION_SUMMARY paid_fixtures=(\d+) paid_plays=(\d+) members=(\d+) endpoint_pair_checks=(\d+) swept_pair_checks=(\d+) min_body_gap=([\d.eE+-]+) min_swept_gap=([\d.eE+-]+)')
$collisionFinal=[regex]::Match($collisionText,'Native authoritative simulation: (\d+) scenarios passed\.')
$collisionSoak=[regex]::Match($collisionText,'SOAK (\d+) complete matches wall seconds ([\d.eE+-]+)')
$collisionQueue=@([regex]::Matches($collisionText,'COLLISION_QUEUE team=(\d+) lane=(-?\d+) members=(\d+) crossed=(\d+)')|ForEach-Object{
    [ordered]@{team=[int]$_.Groups[1].Value;lane=[int]$_.Groups[2].Value;members=[int]$_.Groups[3].Value;crossed=[int]$_.Groups[4].Value}
})
$collisionCapacity=@([regex]::Matches($collisionText,'COLLISION_CAPACITY team=(\d+) building_spawned=(\d+) troop_spawned=(\d+) rejected_spawn_requests=(\d+) paid_rejected=(\d+) air_followup=(\d+)')|ForEach-Object{
    [ordered]@{team=[int]$_.Groups[1].Value;buildings=[int]$_.Groups[2].Value;troops=[int]$_.Groups[3].Value;rejectedSandboxRequests=[int]$_.Groups[4].Value;paidRejected=[int]$_.Groups[5].Value;airFollowup=[int]$_.Groups[6].Value}
})
$collisionPockets=@([regex]::Matches($collisionText,'COLLISION_BUILDING_POCKET team=(\d+) lane=(-?\d+) plays=(\d+) members=(\d+) buildings=(\d+) ground=(\d+) crossed=(\d+) crossed_at12=(\d+) living_buildings=(\d+) seconds=(\d+) adjusted_building_drops=(\d+)')|ForEach-Object{
    [ordered]@{team=[int]$_.Groups[1].Value;lane=[int]$_.Groups[2].Value;plays=[int]$_.Groups[3].Value;members=[int]$_.Groups[4].Value;buildings=[int]$_.Groups[5].Value;ground=[int]$_.Groups[6].Value;crossed=[int]$_.Groups[7].Value;crossedAt12=[int]$_.Groups[8].Value;livingBuildings=[int]$_.Groups[9].Value;seconds=[int]$_.Groups[10].Value;adjustedBuildingDrops=[int]$_.Groups[11].Value;buildingLifetimeSeconds=25}
})
$collisionDropAdjustments=@([regex]::Matches($collisionText,'COLLISION_BUILDING_DROP team=(\d+) lane=(-?\d+) play=(\d+) requested=([\d.eE+-]+),([\d.eE+-]+) legal=([\d.eE+-]+),([\d.eE+-]+)')|ForEach-Object{
    [ordered]@{team=[int]$_.Groups[1].Value;lane=[int]$_.Groups[2].Value;play=[int]$_.Groups[3].Value;requestedX=[double]::Parse($_.Groups[4].Value,[System.Globalization.CultureInfo]::InvariantCulture);requestedZ=[double]::Parse($_.Groups[5].Value,[System.Globalization.CultureInfo]::InvariantCulture);legalX=[double]::Parse($_.Groups[6].Value,[System.Globalization.CultureInfo]::InvariantCulture);legalZ=[double]::Parse($_.Groups[7].Value,[System.Globalization.CultureInfo]::InvariantCulture)}
})
$collisionSplit=[regex]::Match($collisionText,'SWARM_SPLIT fixtures=(\d+) center_fixtures=(\d+) paid_members=(\d+) all_crossed=(\d+)')
$towerAlignment=[regex]::Match($collisionText,'TOWER_ALIGNMENT width=(\d+) height=(\d+) guard_x=([\d.eE+-]+) bridge_x=([\d.eE+-]+) guard_depth=([\d.eE+-]+) core_depth=([\d.eE+-]+) guards=(\d+) cores=(\d+) aligned=(\d+)')
$collisionFloat=[System.Globalization.CultureInfo]::InvariantCulture
$towerAlignmentProof=if($towerAlignment.Success){[ordered]@{
    passed=[int]$towerAlignment.Groups[9].Value -eq 1;widthTiles=[int]$towerAlignment.Groups[1].Value;heightTiles=[int]$towerAlignment.Groups[2].Value
    guardX=[double]::Parse($towerAlignment.Groups[3].Value,$collisionFloat);bridgeCenterX=[double]::Parse($towerAlignment.Groups[4].Value,$collisionFloat)
    guardDepth=[double]::Parse($towerAlignment.Groups[5].Value,$collisionFloat);coreDepth=[double]::Parse($towerAlignment.Groups[6].Value,$collisionFloat)
    guardsAligned=[int]$towerAlignment.Groups[7].Value;coresCentered=[int]$towerAlignment.Groups[8].Value
}}else{$null}
$collisionReport=[ordered]@{
    schema='rift.portable.balance-patch.qa.v1';version=$Version;passed=$false;exitCode=$collisionExit
    recordedUtc=[DateTime]::UtcNow.ToString('o');wallSeconds=([DateTime]::UtcNow-$collisionStart).TotalSeconds
    scenarioCount=$collisionPasses.Count;expectedScenarioCount=63;scenarios=$collisionPasses
    retainedBaseScenarios=61;aiSoakMatches=if($collisionSoak.Success){[int]$collisionSoak.Groups[1].Value}else{0}
    soakSampleSeconds=.05;physicalRosterCards=13;groundCollisionLayer='All living ground troops and structures, either team'
    airCollisionLayer='All living flying troops, either team';spellBodies=0
    sourcePinsUnchanged=$collisionPinsUnchanged;sourcePins=$collisionPins;preRunSourcePinRecord=Get-CollisionPin $collisionPinFile
    log=if(Test-Path -LiteralPath $collisionLog){Get-CollisionPin $collisionLog}else{$null}
    executable=if(Test-Path -LiteralPath $collisionExe){Get-CollisionPin $collisionExe}else{$null}
    collisionSummary=if($collisionSummary.Success){[ordered]@{
        paidFixtures=[int]$collisionSummary.Groups[1].Value;paidPlays=[int]$collisionSummary.Groups[2].Value;members=[int]$collisionSummary.Groups[3].Value
        endpointPairChecks=[long]$collisionSummary.Groups[4].Value;sweptPairChecks=[long]$collisionSummary.Groups[5].Value
        minimumBodyGap=[double]::Parse($collisionSummary.Groups[6].Value,$collisionFloat)
        minimumSweptGap=[double]::Parse($collisionSummary.Groups[7].Value,$collisionFloat)
    }}else{$null}
    denseBridgeQueues=$collisionQueue;fullCapacityFixtures=$collisionCapacity;paidBuildingPockets=$collisionPockets;buildingRequestedTileAdjustments=$collisionDropAdjustments
    buildingPocketPaidSlots=@(1,2,2,3,0,1,0,1,0,1,2,3,2,3,2,3)
    buildingPocketDeck=@('ironclad','twin_blades','boulderback','archer_tower','sky_manta','vampire_bats','storm_raven','frost_fang')
    buildingPocketSpeedToleranceTiles=1.e-8
    splitSwarms=if($collisionSplit.Success){[ordered]@{fixtures=[int]$collisionSplit.Groups[1].Value;centerFixtures=[int]$collisionSplit.Groups[2].Value;paidMembers=[int]$collisionSplit.Groups[3].Value;allCrossed=[int]$collisionSplit.Groups[4].Value}}else{$null}
    splitCardIds=@('twin_blades','vampire_bats','mini_stampede','stampede');centerTilesX=@(-.5,.5);sideTilesX=@(-7.5,7.5);splitCrossingDeadlineSeconds=12
    towerAlignment=$towerAlignmentProof
    collisionSkinTiles=.02;structureSkinTiles=.22;endpointToleranceTiles=1.e-8;sweptToleranceTiles=1.e-8
    provenance='Fresh compilation and execution of the authoritative simulation; actual mirrored Guard/bridge axes and centered Core bodies, independent raw-radius pair checks, relative swept trajectories, real paid members, unchanged source pins, per-member split formations and same-lane Guard/Core targeting for every swarm on both teams, full fifteen-member economy and deterministic event positions, and twenty-one completed seeded AI matches. Failed attempts are retained in timestamped folders.'
    failure=$collisionFailure
}
$collisionReport.passed=$collisionExit -eq 0 -and $collisionFailure -eq $null -and $collisionPinsUnchanged -and
    $collisionPasses.Count -eq 63 -and $collisionFinal.Success -and [int]$collisionFinal.Groups[1].Value -eq 63 -and
    $collisionSoak.Success -and [int]$collisionSoak.Groups[1].Value -eq 21 -and $collisionSummary.Success -and
    $collisionSplit.Success -and [int]$collisionSplit.Groups[1].Value -eq 96 -and [int]$collisionSplit.Groups[2].Value -eq 48 -and [int]$collisionSplit.Groups[3].Value -eq 648 -and [int]$collisionSplit.Groups[4].Value -eq 1 -and
    $towerAlignment.Success -and $towerAlignmentProof.passed -eq $true -and $towerAlignmentProof.widthTiles -eq 30 -and $towerAlignmentProof.heightTiles -eq 44 -and
    $towerAlignmentProof.guardX -eq 7.2 -and $towerAlignmentProof.bridgeCenterX -eq 7.2 -and $towerAlignmentProof.guardDepth -eq 13.4 -and $towerAlignmentProof.coreDepth -eq 17.3 -and
    $towerAlignmentProof.guardsAligned -eq 4 -and $towerAlignmentProof.coresCentered -eq 2 -and
    $collisionQueue.Count -eq 4 -and @($collisionQueue|Where-Object{$_.members -ne 24 -or $_.crossed -lt 18}).Count -eq 0 -and
    $collisionCapacity.Count -eq 2 -and @($collisionCapacity|Where-Object{$_.buildings -lt 300 -or $_.troops -le 0 -or $_.paidRejected -ne 1 -or $_.airFollowup -ne 1}).Count -eq 0 -and
    $collisionPockets.Count -eq 4 -and @($collisionPockets|ForEach-Object{"$($_.team),$($_.lane)"}|Sort-Object -Unique).Count -eq 4 -and
    @($collisionPockets|Where-Object{$_.plays -ne 16 -or $_.members -ne 31 -or $_.buildings -ne 3 -or $_.ground -ne 11 -or $_.crossed -ne 11 -or $_.livingBuildings -ne 3 -or ($_.team -eq 0 -and ($_.seconds -ne 12 -or $_.crossedAt12 -ne 11)) -or ($_.team -eq 1 -and $_.seconds -ne 15) -or ($_.team -eq 0 -and $_.lane -eq 1 -and $_.adjustedBuildingDrops -ne 0)}).Count -eq 0 -and
    $collisionReport.collisionSummary.endpointPairChecks -gt 10000 -and $collisionReport.collisionSummary.sweptPairChecks -gt 10000 -and
    $collisionReport.collisionSummary.minimumBodyGap -ge .02-1.e-8 -and $collisionReport.collisionSummary.minimumSweptGap -ge .02-1.e-8
Write-CollisionJson (Join-Path $collisionRun 'portable-verification.json') $collisionReport
Write-CollisionJson (Join-Path $collisionOutput 'portable-verification.json') $collisionReport
if(-not $collisionReport.passed){throw "Balance patch verification failed. Preserved report: $(Join-Path $collisionRun 'portable-verification.json')"}
Write-Output ("Verified {0} scenarios, {1} AI matches, {2} endpoint pairs and {3} relative sweeps." -f $collisionReport.scenarioCount,$collisionReport.aiSoakMatches,$collisionReport.collisionSummary.endpointPairChecks,$collisionReport.collisionSummary.sweptPairChecks)
