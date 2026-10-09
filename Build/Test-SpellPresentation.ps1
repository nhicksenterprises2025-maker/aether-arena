param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference='Stop'
$spellVisualRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$spellVisualHash=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$spellVisualChecks=[Collections.Generic.List[object]]::new()
$spellVisualPins=[Collections.Generic.List[object]]::new()
function Assert-SpellVisual([string]$Label,[bool]$Passed){
    $spellVisualChecks.Add([ordered]@{name=$Label;passed=$Passed})
    if(!$Passed){throw "Native spell presentation check failed: $Label"}
}
function Read-SpellFrame([string]$Name){
    $base='Artifacts/QA/Visual/'+$Name
    foreach($ext in @('.json','.state.json','.png')){
        $path=Join-Path $spellVisualRepo ($base+$ext)
        $spellVisualPins.Add([ordered]@{path=$base+$ext;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
    $run=Get-Content -LiteralPath (Join-Path $spellVisualRepo ($base+'.json')) -Raw | ConvertFrom-Json -Depth 100
    Assert-SpellVisual ($Name+' exact Shipping binary/clean exit/framing') (!$run.editor -and $run.exitCode -eq 0 -and $run.executableSha256 -eq $spellVisualHash -and $run.cameraFramingPassed -and $run.stateCaptured -and !$run.allowExternalInput)
    return Get-Content -LiteralPath (Join-Path $spellVisualRepo ($base+'.state.json')) -Raw | ConvertFrom-Json -Depth 100
}
$before=Read-SpellFrame 'spell121-flight015'
$bullet=Read-SpellFrame 'spell121-impact030'
$fall=Read-SpellFrame 'spell121-flight055'
$meteor=Read-SpellFrame 'spell121-impact075'
$after=Read-SpellFrame 'spell121-after090'
Assert-SpellVisual 'Both casts have their fixed target circles before damage' ($before.spellCasts -eq 2 -and $before.hazards -eq 0 -and $before.niagara.liveSpellCastRings -eq 2 -and $before.niagara.spellImpactEvents -eq 0)
Assert-SpellVisual 'All seven bullets and five shards are visibly airborne' ($before.niagara.liveSpellCastBodies -eq 12 -and @($before.niagara.spellCastVisuals.bodies | Where-Object visible).Count -eq 12)
Assert-SpellVisual 'Flight glows stay attached and frozen with the paused cast' ($before.niagara.liveSpellCastGlows -eq 12 -and @($before.niagara.spellCastVisuals.bodies | Where-Object {$_.glowPresent -and $_.glowPaused}).Count -eq 12)
foreach($cast in $before.niagara.spellCastVisuals){
    $delay=if($cast.cardId -eq 'meteor_shards'){.75}elseif($cast.cardId -eq 'bullet_burst'){.30}else{throw 'Unexpected animated spell.'}
    Assert-SpellVisual ($cast.cardId+' authoritative clock/deadline/visible ring') ([math]::Abs($cast.impactAt-$cast.born-$delay) -lt 1e-6 -and [math]::Abs($cast.progress-.15/$delay) -lt 1e-6 -and $cast.decalVisible)
    foreach($body in $cast.bodies){Assert-SpellVisual ($cast.cardId+' mesh '+$body.index+' is authored/noncolliding') ($body.collisionDisabled -and $body.mesh -match '/Game/Rift/Environment/SM_(bullet_round|meteor_shard)')}
}
Assert-SpellVisual 'Bullet deadline clears its travel and creates exactly one impact' ($bullet.spellCasts -eq 1 -and $bullet.niagara.liveSpellCastBodies -eq 5 -and $bullet.niagara.liveSpellCastRings -eq 1 -and $bullet.niagara.spellImpactEvents -eq 1 -and $bullet.niagara.recentSpellImpacts[0].cardId -eq 'bullet_burst' -and $bullet.hazards -eq 0)
Assert-SpellVisual 'Meteor remains airborne with no premature zone' ($fall.spellCasts -eq 1 -and $fall.niagara.liveSpellCastBodies -eq 5 -and $fall.hazards -eq 0 -and $fall.niagara.spellImpactEvents -eq 1)
Assert-SpellVisual 'Meteor deadline clears all travel and creates its zone/impact once' ($meteor.spellCasts -eq 0 -and $meteor.niagara.liveSpellCastBodies -eq 0 -and $meteor.niagara.liveSpellCastRings -eq 0 -and $meteor.hazards -eq 1 -and $meteor.niagara.spellImpactEvents -eq 2)
Assert-SpellVisual 'No repeated impact or lingering airborne cast after landing' ($after.spellCasts -eq 0 -and $after.niagara.liveSpellCastBodies -eq 0 -and $after.hazards -eq 1 -and $after.niagara.spellImpactEvents -eq 2)
Assert-SpellVisual 'All incoming flight glows are released at impact' ($meteor.niagara.liveSpellCastGlows -eq 0 -and $after.niagara.liveSpellCastGlows -eq 0)
function Boulder([object]$State,[string]$Team){
    $units=@($State.niagara.unitAnimations | Where-Object {$_.assetId -eq 'boulderback' -and $_.team -eq $Team})
    if($units.Count -ne 1){throw "Expected one actual $Team Boulderback in the fixture."}
    return $units[0]
}
$blueBefore=Boulder $before 'Player';$redBefore=Boulder $before 'Enemy'
$blueHit=Boulder $bullet 'Player';$redFlying=Boulder $fall 'Enemy';$redHit=Boulder $meteor 'Enemy'
Assert-SpellVisual 'Both targets healthy and their HP bars hidden during initial travel' ($blueBefore.hp -eq $blueBefore.maxHp -and $redBefore.hp -eq $redBefore.maxHp -and !$blueBefore.healthBarVisible -and !$redBefore.healthBarVisible)
Assert-SpellVisual 'Bullet applies its existing175 troop damage at .30 seconds and reveals HP' ([math]::Abs($blueBefore.hp-$blueHit.hp-175) -lt 1e-6 -and $blueHit.hasTakenDamage -and $blueHit.healthBarVisible)
Assert-SpellVisual 'Meteor target still healthy at .55 seconds' ($redFlying.hp -eq $redBefore.hp -and !$redFlying.healthBarVisible)
Assert-SpellVisual 'Meteor applies its existing262 troop damage at .75 seconds and reveals HP' ([math]::Abs($redBefore.hp-$redHit.hp-262) -lt 1e-6 -and $redHit.hasTakenDamage -and $redHit.healthBarVisible)
Assert-SpellVisual 'First Meteor DOT is later than .90-second sample' ([math]::Abs((Boulder $after 'Enemy').hp-$redHit.hp) -lt 1e-6)
$roster=Read-SpellFrame 'spell121-roster720-largeui'
Assert-SpellVisual '720p large UI retains enlarged arena with no model overlap' ($roster.cameraFraming.arenaGroundWidthPixels -ge 500 -and $roster.cameraFraming.projectedModelBounds.Count -eq 38 -and $roster.cameraFraming.projectedModelBoundsPassed -and $roster.cameraFraming.modelEnvelopePassed)
Assert-SpellVisual 'Healthy roster HP bars remain hidden' (@($roster.niagara.unitAnimations | Where-Object healthBarVisible).Count -eq 0)
$report=[ordered]@{schema=1;version='1.2.1';passed=$true;utc=[DateTime]::UtcNow.ToString('o');nativeExecutableSha256=$spellVisualHash;checks=$spellVisualChecks;inputs=$spellVisualPins;scope='Actual paused Shipping casts sampled from ordinary simulation steps; image inspection is separate.'}
[IO.File]::WriteAllText((Join-Path $spellVisualRepo 'Docs/QA/spell-timing-visual-1.2.1.json'),($report | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
"Native spell presentation passed $($spellVisualChecks.Count) checks."
