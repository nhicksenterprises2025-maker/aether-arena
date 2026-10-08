param(
    [string]$Executable = '',
    [string]$Name = 'native-vfx',
    [switch]$InspectExisting
)
$ErrorActionPreference = 'Stop'
$riftVfxRepo = Split-Path -Parent $PSScriptRoot
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Use a lowercase QA report name.' }
$riftVfxExpected = [ordered]@{
    Deploy=12; Impact=7; ArrowFlight=1; ArcFlight=1; MantaFlight=1; StormFlight=1;
    TowerFlight=1; BulletBurst=7; Nova=24; Meteor=18; MeteorTick=5; Frost=8;
    Slow=1; Stun=1; Aura=20; TowerDestroy=32; CoreAwaken=16
}
$riftVfxPersistent = @('ArrowFlight','ArcFlight','MantaFlight','StormFlight','TowerFlight','Slow','Stun')
$riftVfxChecks = [Collections.Generic.List[object]]::new()
function Assert-RiftVfx([string]$Label,[bool]$Passed) {
    $riftVfxChecks.Add([ordered]@{name=$Label;passed=$Passed})
    if (!$Passed) { throw "Actual Niagara regression failed: $Label" }
}
function Read-RiftVfxCapture([string]$Suffix) {
    $riftVfxStem = Join-Path $riftVfxRepo ('Artifacts/QA/Visual/' + $Name + '-' + $Suffix)
    $riftVfxCapture = Get-Content -LiteralPath ($riftVfxStem + '.json') -Raw | ConvertFrom-Json
    Assert-RiftVfx ($Suffix + ' rendered cleanly at requested dimensions') ($riftVfxCapture.captured -and $riftVfxCapture.stateCaptured -and $riftVfxCapture.resolutionMatches -and $riftVfxCapture.exitCode -eq 0 -and !$riftVfxCapture.timedOut -and @($riftVfxCapture.errors).Count -eq 0)
    if ($Executable) {
        Assert-RiftVfx ($Suffix + ' used the requested executable') ([IO.Path]::GetFullPath($riftVfxCapture.executable) -eq [IO.Path]::GetFullPath($Executable))
    }
    return Get-Content -LiteralPath ($riftVfxStem + '.state.json') -Raw | ConvertFrom-Json
}
if (!$InspectExisting) {
    & (Join-Path $PSScriptRoot 'Capture-Unreal.ps1') -Executable $Executable -Page Battle -Scenario effects17 -EffectAge 0.12 -Delay 6 -Name ($Name + '-immediate') | Out-Null
    & (Join-Path $PSScriptRoot 'Capture-Unreal.ps1') -Executable $Executable -Page Battle -Scenario effects17 -EffectAge 1.2 -Delay 6 -Name ($Name + '-lifecycle') | Out-Null
}
$riftVfxImmediate = Read-RiftVfxCapture 'immediate'
$riftVfxLifecycle = Read-RiftVfxCapture 'lifecycle'
$riftVfxActual = @{}
foreach ($riftVfxComponent in $riftVfxImmediate.niagara.components) {
    $riftVfxId = ($riftVfxComponent.system -split '\.')[-1] -replace '^NS_Rift',''
    Assert-RiftVfx ($riftVfxId + ' appears once') (!$riftVfxActual.ContainsKey($riftVfxId))
    $riftVfxActual[$riftVfxId] = $riftVfxComponent
}
Assert-RiftVfx 'All 17 original effect systems were exercised' ($riftVfxActual.Count -eq $riftVfxExpected.Count)
foreach ($riftVfxId in $riftVfxExpected.Keys) {
    $riftVfxComponent = $riftVfxActual[$riftVfxId]
    Assert-RiftVfx ($riftVfxId + ' is a ready active visible system') ($null -ne $riftVfxComponent -and $riftVfxComponent.ready -and $riftVfxComponent.valid -and $riftVfxComponent.active -and $riftVfxComponent.visible -and !$riftVfxComponent.complete)
    $riftVfxEmitters = @($riftVfxComponent.emitters)
    Assert-RiftVfx ($riftVfxId + ' spawned its bounded recipe through the real system graph') (($riftVfxEmitters | Measure-Object totalSpawned -Sum).Sum -eq $riftVfxExpected[$riftVfxId] -and ($riftVfxEmitters | Measure-Object particles -Sum).Sum -gt 0)
    foreach ($riftVfxEmitter in $riftVfxEmitters) {
        if ($riftVfxEmitter.particles -le 0) { continue }
        Assert-RiftVfx ($riftVfxId + ' has finite nonzero bounded sprite dimensions') ($riftVfxEmitter.sizeDataAvailable -and [double]::IsFinite($riftVfxEmitter.spriteWidthMin) -and [double]::IsFinite($riftVfxEmitter.spriteHeightMin) -and $riftVfxEmitter.spriteWidthMin -gt 0 -and $riftVfxEmitter.spriteHeightMin -gt 0 -and $riftVfxEmitter.spriteWidthMax -le 512 -and $riftVfxEmitter.spriteHeightMax -le 512)
    }
}
foreach ($riftVfxId in $riftVfxPersistent) {
    $riftVfxComponent = @($riftVfxLifecycle.niagara.components | Where-Object { ($_.system -split '\.')[-1] -eq ('NS_Rift' + $riftVfxId) })
    Assert-RiftVfx ($riftVfxId + ' remains visible with exactly one live particle after one second') ($riftVfxComponent.Count -eq 1 -and $riftVfxComponent[0].age -ge 1 -and $riftVfxComponent[0].active -and !$riftVfxComponent[0].complete -and ($riftVfxComponent[0].emitters | Measure-Object particles -Sum).Sum -eq 1 -and ($riftVfxComponent[0].emitters | Measure-Object totalSpawned -Sum).Sum -eq 1)
}
$riftVfxReport = [ordered]@{
    passed=$true;utc=[DateTime]::UtcNow.ToString('o');name=$Name;executable=$Executable;
    effectSystems=$riftVfxExpected.Count;immediateParticles=$riftVfxImmediate.niagara.totalParticles;
    persistentSystems=$riftVfxPersistent.Count;checks=$riftVfxChecks.ToArray();
    scope='Actual rendered system graph execution, bounded spawn counts, sprite geometry and persistent particle lifecycle. Alpha initialization data and screenshots do not certify continuous animation or artistic quality.'
}
if ($Executable) { $riftVfxReport.executableSha256=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant() }
$riftVfxReportPath = Join-Path $riftVfxRepo ('Artifacts/QA/' + $Name + '-verification.json')
[IO.File]::WriteAllText($riftVfxReportPath,($riftVfxReport | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output "Actual Niagara regression passed $($riftVfxChecks.Count) checks for all 17 systems and seven persistent lifecycles. Report: $riftVfxReportPath"
