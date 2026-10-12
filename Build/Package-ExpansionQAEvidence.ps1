<#
Aggregate explicit completed expansion runs. No engine, importer or publisher is run.
ReadbackReview and VisualReview are authored after inspection, with version,
balanceHash, passed, errors, and inputs [{path,sha256,bytes}] binding actual evidence.
ReadbackReview additionally binds producerPins and reflectedCardDataUnchanged=true.
ShippingReport is optional: its absence produces a pre-Shipping report, never a
release-ready claim. A supplied ShippingReport must bind its measured inputs too.
#>
param(
    [Parameter(Mandatory)][string]$PortableReport,
    [Parameter(Mandatory)][string]$NativeAutomationContext,
    [Parameter(Mandatory)][string]$RenderedAutomationContext,
    [Parameter(Mandatory)][string]$MetaRoot,
    [Parameter(Mandatory)][string]$MetaAudit,
    [Parameter(Mandatory)][string]$ImportRun,
    [Parameter(Mandatory)][string]$AudioImportRun,
    [Parameter(Mandatory)][string]$AudioSmokeRun,
    [Parameter(Mandatory)][string]$DragRun,
    [Parameter(Mandatory)][string[]]$CaptureReports,
    [Parameter(Mandatory)][string[]]$PerformanceRuns,
    [Parameter(Mandatory)][string]$WorkbookProof,
    [Parameter(Mandatory)][string]$ReadbackReview,
    [Parameter(Mandatory)][string]$VisualReview,
    [string]$ShippingReport='',
    [string]$OutputDirectory='',
    [string]$PythonExecutable='',
    [switch]$Zip
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Use PowerShell 7.'}
$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# Reuse the maintained path/link rejection, identity, sanitization and ZIP writer.
foreach($library in @(
    @{file='Build/Package-ModelInputQAEvidence.ps1';names=@('Resolve-ModelInput')},
    @{file='Build/Package-HoverQAEvidence.ps1';names=@('Get-HoverIdentity','Convert-HoverSanitized','Write-HoverEntry')}
)){
    $tokens=$null;$parseErrors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $modelRepo $library.file),[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count){throw 'Maintained evidence helpers do not parse.'}
    foreach($name in $library.names){
        $found=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
        if($found.Count -ne 1){throw "Missing maintained helper: $name"}
        Invoke-Expression $found[0].Extent.Text
    }
}
$riftEvidence=[ordered]@{};$riftBound=[ordered]@{}
function Add-RiftEvidence([string]$Path){
    $identity=Get-HoverIdentity $Path
    if($riftBound.Contains($identity.path)){Assert-RiftCondition ($riftBound[$identity.path].sha256 -eq $identity.sha256) 'input changed during aggregation'}
    $riftBound[$identity.path]=$identity
    $riftEvidence[$identity.path]=$identity
    return $identity
}
function Read-RiftEvidence([string]$Path){$null=Add-RiftEvidence $Path;Get-Content -LiteralPath (Resolve-ModelInput $Path) -Raw|ConvertFrom-Json -Depth 100}
function Read-RiftEvidenceText([string]$Path){$null=Add-RiftEvidence $Path;Get-Content -LiteralPath (Resolve-ModelInput $Path) -Raw}
function Assert-RiftCondition([bool]$Condition,[string]$Label){if(!$Condition){throw "Expansion evidence rejected: $Label"}}
function Assert-RiftPassed($Report,[string]$Label){
    Assert-RiftCondition ($Report -and $Report.passed -eq $true -and !$Report.synthetic -and !$Report.fixture -and @($Report.errors|Where-Object {$_}).Count -eq 0 -and !$Report.failure) $Label
}
function Assert-RiftPin($Pin,[string]$Base=$modelRepo){
    $path=if($Pin.path){$Pin.path}else{$Pin.file}
    Assert-RiftCondition (![string]::IsNullOrWhiteSpace($path) -and $Pin.sha256 -match '^[a-fA-F0-9]{64}$') 'missing input identity'
    if(![IO.Path]::IsPathRooted($path)){$path=Join-Path $Base $path}
    $actual=Get-HoverIdentity $path
    $same=$actual.sha256 -eq $Pin.sha256 -and (!$Pin.bytes -or $actual.bytes -eq $Pin.bytes)
    Assert-RiftCondition $same "stale or changed input: $($actual.path)"
    if($riftBound.Contains($actual.path)){Assert-RiftCondition ($riftBound[$actual.path].sha256 -eq $actual.sha256) 'input changed during aggregation'}
    $riftBound[$actual.path]=$actual
    return $actual
}
function Assert-RiftPins($Pins,[string]$Base=$modelRepo){
    Assert-RiftCondition (@($Pins).Count -gt 0) 'missing recorded pins'
    foreach($pin in $Pins){$null=Assert-RiftPin $pin $Base}
}
function Assert-RiftAutomationContext($Context,[string]$Label){
    Assert-RiftCondition ($Context.schema -eq 2 -and $Context.completed -eq $true -and $Context.sourcesUnchanged -eq $true -and $Context.runtimeModulesUnchanged -eq $true -and $Context.commandletExitCode -eq 0 -and $Context.wrapperExitCode -eq 0 -and !$Context.synthetic -and !$Context.fixture) "$Label completed native process"
}
function Assert-RiftAutomationResults($Result,[string[]]$RequiredTests,[string]$Label){
    $tests=@($Result.tests)
    Assert-RiftCondition ($Result.failed -eq 0 -and $Result.notRun -eq 0 -and ($Result.succeeded+$Result.succeededWithWarnings) -gt 0 -and $tests.Count -eq ($Result.succeeded+$Result.succeededWithWarnings) -and @($tests.fullTestPath|Sort-Object -Unique).Count -eq $tests.Count) "$Label complete unique successful results"
    Assert-RiftCondition (@($tests|Where-Object {$_.state -notin @('Success','SuccessWithWarnings') -or $_.errors -gt 0}).Count -eq 0) "$Label no failed or skipped test rows"
    foreach($test in $RequiredTests){Assert-RiftCondition (@($tests|Where-Object {$_.fullTestPath -eq $test -and $_.state -in @('Success','SuccessWithWarnings')}).Count -eq 1) "$Label required actual test: $test"}
}
function Assert-RiftRenderedContext($Context,[string]$EngineLog){
    Assert-RiftAutomationContext $Context 'rendered automation'
    Assert-RiftCondition ($Context.rendering.requested -eq $true -and $Context.rendering.mode -eq 'offscreen-d3d12' -and $Context.rendering.observedRhi -eq 'D3D12') 'requested and observed D3D12 automation'
    Assert-RiftCondition ($EngineLog -match 'LogRHI:.*Using Default RHI: D3D12|LogD3D12RHI:.*Chosen D3D12 Adapter' -and $EngineLog -notmatch 'NiagaraComponent is not allowed to spawn when rendering is disabled') 'actual rendering-enabled D3D12 log'
}
function Assert-RiftImportedAssets($Imported){
    Assert-RiftPassed $Imported 'actual import readback'
    Assert-RiftCondition ($Imported.provenance.sourcesUnchanged -eq $true -and @($Imported.validation.errors|Where-Object {$_}).Count -eq 0) 'authored and imported asset errors'
    $playables=@($riftManifest.cards.id);$children=@($riftManifest.entities.id);$expected=@($playables)+@($children);$entries=@($Imported.validation.cards)
    Assert-RiftCondition ($playables.Count -eq 21 -and $children.Count -eq 1 -and $entries.Count -eq 22 -and @($entries.id|Sort-Object -Unique).Count -eq 22 -and @((Compare-Object ($expected|Sort-Object) ($entries.id|Sort-Object))).Count -eq 0) 'exact21 playable and1 child native DataAsset readbacks'
    $selected=@('mini_stampede','stampede','pulverizer','bone_warden','bone_warden_skeleton','aether_reaper','iron_giant','shadow_surge')
    Assert-RiftCondition (@($Imported.selectedModels).Count -eq 8 -and @(Compare-Object ($selected|Sort-Object) ($Imported.selectedModels|Sort-Object)).Count -eq 0 -and @($Imported.cardDefinitions).Count -eq 21 -and @($Imported.entityDefinitions).Count -eq 1 -and @(Compare-Object ($playables|Sort-Object) ($Imported.cardDefinitions.id|Sort-Object)).Count -eq 0 -and @(Compare-Object ($children|Sort-Object) ($Imported.entityDefinitions.id|Sort-Object)).Count -eq 0) 'exact selected import and compiled definition identities'
    foreach($entry in $entries){
        Assert-RiftCondition ($entry.subordinate -eq ($children -contains $entry.id) -and $entry.sounds -ge 3 -and $entry.effects -ge 17 -and (($children -contains $entry.id) -or $entry.illustration -eq $true)) "loaded card bindings: $($entry.id)"
    }
    foreach($rig in @(@{id='mini_stampede';bones=30},@{id='stampede';bones=30},@{id='pulverizer';bones=16},@{id='bone_warden';bones=40},@{id='bone_warden_skeleton';bones=28},@{id='aether_reaper';bones=29},@{id='iron_giant';bones=35})){
        $entry=@($entries|Where-Object id -eq $rig.id)[0]
        Assert-RiftCondition ($entry.animations -eq 9 -and $entry.bones -eq $rig.bones -and $entry.lods -eq 4 -and $entry.physics -eq $true -and $entry.physicsBodies -gt 0 -and $entry.physicsIsAsset -eq $true -and $entry.physicsRegisteredOnDisk -eq $true) "actual selected rig/LOD/physics: $($rig.id)"
    }
    $shadow=@($entries|Where-Object id -eq 'shadow_surge')[0]
    Assert-RiftCondition ($shadow.animations -eq 0 -and !$shadow.bones -and !$Imported.skeletal.shadow_surge -and $shadow.illustration -eq $true) 'Shadow spell has illustration and no creature rig'
    $staticExpected=@{iron_shield_fragment='M_RiftExpansionAscensionSurface';shadow_surge_ribbon='M_RiftExpansionAscensionGlow';shadow_surge_wisp='M_RiftExpansionAscensionGlow'}
    foreach($id in $staticExpected.Keys){
        $path=$Imported.statics.PSObject.Properties[$id].Value;$bindings=$Imported.expansionMaterialBindings.PSObject.Properties[$path].Value
        $material="/Game/Rift/Materials/$($staticExpected[$id]).$($staticExpected[$id])"
        Assert-RiftCondition ($path -eq "/Game/Rift/Environment/SM_$id.SM_$id" -and $bindings.passed -eq $true -and @($bindings.slots).Count -eq 1 -and $bindings.slots[0].materialInterface -eq $material -and $bindings.slots[0].expectedInterface -eq $material -and $bindings.slots[0].exactInterface -eq $true -and $bindings.slots[0].manifestOwned -eq $true) "actual static material readback: $id"
    }
    Assert-RiftCondition (@($Imported.expansionMaterialBindings.PSObject.Properties).Count -eq 10) 'all7 actual rig and3 static material readbacks'
    foreach($id in $selected|Where-Object {$_ -ne 'shadow_surge'}){
        $path="/Game/Rift/Characters/$id/SK_$id.SK_$id";$binding=$Imported.expansionMaterialBindings.PSObject.Properties[$path].Value
        Assert-RiftCondition ($Imported.skeletal.PSObject.Properties[$id].Value -eq $path -and $binding.passed -eq $true -and @($binding.slots).Count -gt 0) "actual selected skeletal material readback: $id"
    }
    foreach($bindings in $Imported.expansionMaterialBindings.PSObject.Properties){Assert-RiftCondition ($bindings.Value.passed -eq $true -and @($bindings.Value.slots|Where-Object {$_.exactInterface -ne $true -or $_.manifestOwned -ne $true}).Count -eq 0) "exact actual selected material: $($bindings.Name)"}
    Assert-RiftCondition ($Imported.expansionFXMaterial.material -eq 'M_RiftExpansionFX' -and $Imported.expansionFXMaterial.uniformColorParameter -eq 'EffectColor' -and @($Imported.expansionFXMaterial.connections).Count -eq 11 -and @($Imported.expansionFXMaterial.connections|Where-Object connected -ne $true).Count -eq 0 -and @($Imported.cloakMaterial.connections).Count -eq 8 -and @($Imported.cloakMaterial.connections|Where-Object succeeded -ne $true).Count -eq 0) 'actual dedicated FX and cloak shader connection returns'
    Assert-RiftCondition (@($Imported.shaderConnections.M_RiftShockwave).Count -eq 18 -and @($Imported.shaderConnections.M_RiftShockwave|Where-Object connected -ne $true).Count -eq 0 -and $Imported.expansionFXMaterial.defaultOpacity -eq .55 -and $Imported.expansionFXMaterial.maximumOpacity -eq .65 -and $Imported.expansionFXMaterial.defaultEmission -eq .25 -and $Imported.expansionFXMaterial.maximumEmissionPerChannel -eq .45 -and $Imported.cloakMaterial.sharedMaterialsChanged -eq $false) 'actual shockwave returns and bounded dedicated material contract'
}
function Assert-RiftPerformanceQualities($Qualities){
    Assert-RiftCondition ($Qualities.Contains(0) -and $Qualities.Contains(2)) 'separate screenshot-disabled LOW0 and HIGH2 measurements'
}
function Assert-RiftImportAssetChain($Model,$Audio){
    $allowed=@('Unreal/RiftCrownArena/Content/Rift/Audio/SFX_shield_break.uasset','Unreal/RiftCrownArena/Content/Rift/Audio/SFX_shadow_surge.uasset')
    $modelPins=@($Model.afterNativeAssets);$before=@($Audio.beforeNativeAssets);$after=@($Audio.afterNativeAssets)
    Assert-RiftCondition ($modelPins.Count -gt 0 -and $modelPins.Count -eq $before.Count -and $before.Count -eq $after.Count -and @($modelPins.path|Sort-Object -Unique).Count -eq $modelPins.Count -and @($before.path|Sort-Object -Unique).Count -eq $before.Count -and @($after.path|Sort-Object -Unique).Count -eq $after.Count) 'complete unique model-to-audio Content inventories'
    $previous=@{};$current=@{};foreach($pin in $before){$previous[$pin.path]=$pin.sha256};foreach($pin in $after){$current[$pin.path]=$pin.sha256}
    foreach($pin in $modelPins){Assert-RiftCondition ($previous.ContainsKey($pin.path) -and $previous[$pin.path] -eq $pin.sha256 -and $current.ContainsKey($pin.path)) "exact model-after/audio-before native pin: $($pin.path)"}
    $changed=@($before|Where-Object {$_.sha256 -ne $current[$_.path]})
    Assert-RiftCondition ($changed.Count -eq 2 -and @(Compare-Object ($allowed|Sort-Object) ($changed.path|Sort-Object)).Count -eq 0 -and @($Audio.changedNativeAssets).Count -eq 2 -and @($Audio.unapprovedChanges).Count -eq 0) 'exact two selected audio changes after model import'
    foreach($pin in $changed){
        $record=@($Audio.changedNativeAssets|Where-Object path -eq $pin.path)
        Assert-RiftCondition ($record.Count -eq 1 -and $record[0].beforeSha256 -eq $pin.sha256 -and $record[0].afterSha256 -eq $current[$pin.path] -and $record[0].created -eq $false -and $record[0].deleted -eq $false) "actual audio transition record: $($pin.path)"
    }
    Assert-RiftCondition ([DateTimeOffset]$Audio.startedUtc -ge [DateTimeOffset]$Model.endedUtc -and $Audio.releasedMastersUnchanged -eq $true) 'audio follows model import and preserves released masters'
}
function Assert-RiftContentCoverage($Pins){
    $expected=@(Get-ChildItem -LiteralPath (Join-Path $modelRepo 'Unreal/RiftCrownArena/Content') -File -Recurse|Where-Object Extension -in @('.uasset','.umap')|ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,$_.FullName).Replace('\','/')})
    Assert-RiftCondition (@($Pins).Count -eq $expected.Count -and @($Pins.path|Sort-Object -Unique).Count -eq $expected.Count -and @(Compare-Object ($expected|Sort-Object) ($Pins.path|Sort-Object)).Count -eq 0) 'complete current post-audio Content inventory'
}
function Assert-RiftSourceCoverage($Pins,[switch]$Content){
    $paths=@($Pins|ForEach-Object { $p=if($_.path){$_.path}else{$_.file};if([IO.Path]::IsPathRooted($p)){$p=[IO.Path]::GetRelativePath($modelRepo,$p)};$p.Replace('\','/')})
    foreach($required in @('Assets/Data/card_manifest.json','Build/generate_card_manifest.py')){Assert-RiftCondition ($paths -contains $required) "missing canonical/generator pin: $required"}
    $trees=@('Source','Config');if($Content){$trees+='Content'}
    foreach($tree in $trees){
        $expected=@(Get-ChildItem -LiteralPath (Join-Path $modelRepo "Unreal/RiftCrownArena/$tree") -File -Recurse|Where-Object Extension -in @('.cpp','.h','.cs','.ini','.inc','.uasset','.umap')|ForEach-Object {[IO.Path]::GetRelativePath($modelRepo,$_.FullName).Replace('\','/')})
        Assert-RiftCondition (@(Compare-Object ($expected|Sort-Object -Unique) (@($paths|Where-Object {$_ -like "Unreal/RiftCrownArena/$tree/*"})|Sort-Object -Unique)).Count -eq 0) "incomplete current $tree inventory"
    }
}
function Assert-RiftIdentity($Report,[string]$Label){Assert-RiftCondition ($Report.version -eq $riftManifest.version -and $Report.balanceHash -eq $riftManifest.balanceHash) "$Label version/mechanical hash"}
function Assert-RiftRun($Run,[string]$Label){
    Assert-RiftPassed $Run $Label
    Assert-RiftCondition ($Run.version -eq $riftManifest.version -and $Run.exitCode -eq 0 -and $Run.executableUnchanged -eq $true -and !$Run.timedOut) "$Label process/version"
    $pins=if($Run.sourcePins){$Run.sourcePins}else{$Run.sourceHashes}
    Assert-RiftCondition (($Run.sourcePinsUnchanged -eq $true -or $Run.sourcesUnchanged -eq $true) -and $Run.runtimePinsUnchanged -eq $true) "$Label unchanged inputs"
    Assert-RiftPins $pins;Assert-RiftSourceCoverage $pins
    $runtime=Resolve-RiftDirectory $Run.runtimeRoot
    Assert-RiftPins $Run.runtimePins $runtime
    $expected=@(Get-ChildItem -LiteralPath $runtime -File -Recurse|ForEach-Object {
        [IO.Path]::GetRelativePath($runtime,$_.FullName).Replace('\','/')
    }|Where-Object {
        !$Run.editor -or $_ -match '^(Binaries|Config|Content)/' -or $_ -ceq 'RiftCrownArena.uproject'
    })
    Assert-RiftCondition (@(Compare-Object ($expected|Sort-Object -Unique) (@($Run.runtimePins.path)|Sort-Object -Unique)).Count -eq 0) "$Label complete runtime inventory"
}
function Resolve-RiftDirectory([string]$Path){
    if(![IO.Path]::IsPathRooted($Path)){$Path=Join-Path $modelRepo $Path}
    [IO.Path]::GetFullPath($Path)
}
function Read-RiftBoundReport([string]$Path,[string]$Digest){
    $identity=Add-RiftEvidence $Path
    Assert-RiftCondition ($identity.sha256 -eq $Digest) "report bytes: $($identity.path)"
    Read-RiftEvidence $Path
}
function Assert-RiftReview($Review,[string]$Label){
    Assert-RiftPassed $Review $Label;Assert-RiftIdentity $Review $Label
    Assert-RiftCondition (@($Review.inputs).Count -gt 0) "$Label has no actual reviewed inputs"
    foreach($pin in $Review.inputs){$null=Assert-RiftPin $pin;$null=Add-RiftEvidence $(if($pin.path){$pin.path}else{$pin.file})}
}
function Assert-RiftReviewedInput($Review,[string]$Path){
    $actual=Get-HoverIdentity $Path
    Assert-RiftCondition (@($Review.inputs|Where-Object {($_.sha256 -eq $actual.sha256) -and ((Get-HoverIdentity $(if($_.path){$_.path}else{$_.file})).path -eq $actual.path)}).Count -eq 1) "review does not identify actual input: $($actual.path)"
}
function Assert-RiftAudioChecks($Raw){
    Assert-RiftPassed $Raw 'loaded native audio'
    Assert-RiftCondition ($Raw.loadedSounds -eq 41 -and $Raw.loadedCombatVariations -eq 20 -and $Raw.loadedExpansionSounds -eq 2) '63 actual native master sounds'
    foreach($name in @('all41ImportedSoundsLoad','all20CombatVariationsLoad','all2ExpansionSoundsLoadWithoutLoops','shieldBreakSoundLoadsWithoutLoop','shadowSurgeSoundLoadsWithoutLoop',
        'liveAudioDevice','profileAvailable','settingsInterfaceAvailable','allVoicesHaveLimiterRoute','masterMixRegisteredWithLiveDevice','stereoPeakLimiterBelowFullScale','limiterDetectsIsolatedPeaksWithoutAttackSmoothing',
        'physicalBudgetIncludes32OneShotsAndMusic','riverVoiceInspectionDetectsSilentControl','noRiverVoiceInHome','battleLifecycleSubsystemAvailable','actualBattleActiveForWaterCheck','noRiverVoiceInBattle',
        'noRiverVoiceAfterRepeatedWorldBinding','noRiverVoiceAfterReturningHome','musicPreservedAcrossPagesAndWorldBinding','noRiverVoiceInSettings','noRiverVoiceAfterDeferredBurst','noRiverVoiceDuringLimiterOverload',
        'quietMusicMuteUsesActualSettingsSlider','quietMusicSliderMuteApplied','quietSceneSettledAcrossGameTicks','noRiverVoiceInQuietScene','quietLiveMixerHasNoConstantSound','postMixSettingsRestored')){
        Assert-RiftCondition (@($Raw.checks|Where-Object {$_.name -eq $name -and $_.passed -eq $true}).Count -eq 1) "audio check: $name"
    }
    Assert-RiftCondition (@($Raw.checks|Where-Object passed -ne $true).Count -eq 0) 'all native audio checks'
    $lifecycle=$Raw.waterAmbienceLifecycle
    Assert-RiftCondition ($Raw.deviceMaxChannels -ge 33 -and $lifecycle.controlPlayingVoices -eq 1 -and $lifecycle.homePlayingVoices -eq 0 -and $lifecycle.battlePlayingVoices -eq 0 -and $lifecycle.reboundPlayingVoices -eq 0 -and $lifecycle.returnedHomePlayingVoices -eq 0 -and $lifecycle.settingsPlayingVoices -eq 0 -and $lifecycle.burstPlayingVoices -eq 0 -and $lifecycle.overloadPlayingVoices -eq 0 -and $lifecycle.quietPlayingVoices -eq 0) 'actual silent river-control and page/burst/quiet lifecycle voice counts'
}
function Assert-RiftDragChecks($Raw){
    Assert-RiftPassed $Raw 'actual drag checks'
    Assert-RiftCondition ($Raw.version -eq $riftManifest.version -and $Raw.checkCount -eq 32 -and @($Raw.checks).Count -eq 32 -and @($Raw.checks.name|Sort-Object -Unique).Count -eq 32 -and @($Raw.checks|Where-Object passed -ne $true).Count -eq 0) '32 actual unique successful drag checks'
    foreach($name in @('live controller and Slate available','four production Slate hand buttons','legal arena target projects to its actual board tile','hand press selects and owns pointer capture','drag threshold and ghost survive an earlier queued viewport release',
        'return to hand cancels and preserves full match state','release over illegal enemy tile cancels without spending or cycling','release over HUD cancels without spending or cycling',
        'Escape cancels first without opening battle menu','unaffordable drag returns to hand without mutation','legal drop spends exactly one card cost','legal drop cycles the selected slot exactly once',
        'legal drop creates the selected troop or building','duplicate mouse release cannot spend or cycle again','finished phase cancels a pending drag without deployment')){
        Assert-RiftCondition (@($Raw.checks|Where-Object {$_.name -eq $name -and $_.passed -eq $true}).Count -eq 1) "drag positive control: $name"
    }
}
function Assert-RiftPerformanceWorkload($Measured){
    $fixture=$Measured.expansionFixture
    Assert-RiftCondition ($fixture.passed -eq $true -and @($fixture.errors|Where-Object {$_}).Count -eq 0 -and $fixture.balanceHash -eq $riftManifest.balanceHash -and $fixture.manifestVersion -eq $riftManifest.version -and $fixture.paidDeploymentProof -eq $true -and $fixture.firstWaveComplete -eq $true -and $fixture.observedCardTeamLaneCombinations -eq 32 -and $Measured.frame.samples -ge 100 -and $Measured.metaGamesDuringBattle -eq 0) 'actual complete paid performance workload'
    Assert-RiftCondition ($fixture.shockwaveLaunchEvents -gt 0 -and $fixture.shockwaveDamageEvents -gt 0 -and $fixture.maximumDeathsAtOneSimulationTimestamp -ge 2 -and $fixture.deathVisualsObservedAtEvent -gt 0 -and $fixture.deathVisualsObservedRetired -gt 0) 'observed real waves damage simultaneous deaths and retirement'
    Assert-RiftCondition ($fixture.allDueObservedDeathVisualsRetiredOnTime -eq $true -and $fixture.lateObservedDeathVisuals -eq 0 -and $fixture.pendingDeathVisualsPastDeadlineTolerance -eq 0 -and $fixture.expiredBodyDeathFleckObservations -eq 0 -and $fixture.noDeathFlecksOnExpiredBodies -eq $true) 'actual on-time death cleanup without expired-body native flecks'
    foreach($id in @('mini_stampede','stampede','pulverizer','bone_warden','aether_reaper','iron_giant')){Assert-RiftCondition ($fixture.observedCardModelIds -contains $id) "actual stress model observed: $id"}
    $visuals=$fixture.maximumObservedUnitVisuals
    foreach($field in @('maximumObservedHoofDustInstances','maximumObservedFormationDustInstances','maximumObservedDeathFleckInstances','maximumObservedSlowRippleInstances','maximumObservedSelectedSurfaceInstances')){Assert-RiftCondition ($null -ne $fixture.PSObject.Properties[$field] -and $fixture.PSObject.Properties[$field].Value -ge 0) "recorded native surface count: $field"}
    Assert-RiftCondition ($visuals -gt 0 -and $fixture.maximumObservedHoofDustInstances -le 4*$visuals -and $fixture.maximumObservedFormationDustInstances -le 4*$visuals -and $fixture.maximumObservedDeathFleckInstances -le 3*$visuals -and $fixture.maximumObservedSlowRippleInstances -le 24*$visuals -and $fixture.maximumObservedSelectedSurfaceInstances -le 35*$visuals) 'native selected surface instance bounds per observed actor'
}
function Assert-RiftMetaCohort($Dataset,$Audit){
    Assert-RiftCondition ($Dataset.version -eq $riftManifest.version -and $Dataset.balanceHash -eq $riftManifest.balanceHash -and $Dataset.telemetryRevision -eq 4 -and $Audit.balanceHash -eq $riftManifest.balanceHash -and $Audit.telemetryRevision -eq 4 -and $Audit.completeObserved -eq $true -and $Audit.failedChecks -eq 0 -and @($Audit.errors).Count -eq 0 -and $Audit.observedGames -ge 100 -and $Dataset.games -eq $Audit.observedGames -and $Audit.capturedCardCount -eq $riftManifest.cards.Count -and $Audit.capturedSubordinateCount -eq $riftManifest.entities.Count -and $Audit.capturedArena.arenaWidth -eq $riftManifest.rules.arenaWidth -and $Audit.capturedArena.arenaHeight -eq $riftManifest.rules.arenaHeight) 'current complete Meta cohort; historical data cannot substitute'
}
$null=Add-RiftEvidence $PSCommandPath
foreach($helper in @('Build/Package-ModelInputQAEvidence.ps1','Build/Package-HoverQAEvidence.ps1')){$null=Add-RiftEvidence $helper}
$riftManifest=Read-RiftEvidence 'Assets/Data/card_manifest.json'
$riftGuard=& (Join-Path $PSScriptRoot 'Validate-BalanceRelease.ps1') -Version $riftManifest.version -PythonExecutable $PythonExecutable -PassThru
Assert-RiftCondition ($riftGuard.passed -eq $true -and $riftGuard.inputsUnchanged -eq $true) 'approved canonical release-input guard'
foreach($pin in $riftGuard.inputPins){if($pin.path.StartsWith($modelRepo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){$null=Assert-RiftPin $pin}}
Assert-RiftCondition ($riftManifest.schema -eq 2 -and $riftManifest.rules.arenaWidth -eq 24 -and $riftManifest.rules.arenaHeight -eq 34) 'approved expansion schema and arena'
$riftPortable=Read-RiftEvidence $PortableReport
Assert-RiftPassed $riftPortable 'portable scenarios'
Assert-RiftCondition ($riftPortable.mechanicalHash -eq $riftManifest.balanceHash -and $riftPortable.approvedRoster -eq $riftManifest.cards.Count -and $riftPortable.subordinateDefinitions -eq $riftManifest.entities.Count -and $riftPortable.arenaWidth -eq $riftManifest.rules.arenaWidth -and $riftPortable.arenaHeight -eq $riftManifest.rules.arenaHeight -and $riftPortable.sourcePinsUnchanged -eq $true -and $riftPortable.scenariosFailed -eq 0 -and $riftPortable.scenariosPassed -gt 0 -and $riftPortable.completeAIMatches -ge 21 -and $riftPortable.compileExitCode -eq 0 -and $riftPortable.testExitCode -eq 0) 'actual approved portable coverage'
Assert-RiftPins $riftPortable.sourcePins
$riftNative=Read-RiftEvidence $NativeAutomationContext
Assert-RiftAutomationContext $riftNative 'main automation'
Assert-RiftPins $riftNative.sourceHashes;Assert-RiftSourceCoverage $riftNative.sourceHashes -Content;Assert-RiftPins $riftNative.runtimeModules
$riftAutomation=Read-RiftBoundReport $riftNative.report $riftNative.sourceReportSha256
Assert-RiftAutomationResults $riftAutomation @('Rift.Integration.CardData','Rift.Integration.ExpansionMechanics','Rift.Integration.SnapshotRoundtrip','Rift.Integration.HistoricalReplayBalance','Rift.Integration.ReplayTimeline','Rift.Integration.FullLengthReplay','Rift.Integration.SpellCastReplay','Rift.Integration.PausedResultAndReplayEvents','Rift.Integration.CorpseVisualCleanup','Rift.Integration.UnitCollision','Rift.Integration.UnitMotion','Rift.Integration.TowerPathing','Rift.Integration.Pulverizer','Rift.Presentation.SelectedSurfaceEffects','Rift.Presentation.ExpansionShieldCloak','Rift.Performance.ExpansionPaidFixture','Rift.Meta.ExpansionAttribution','Rift.Meta.AggregationEconomy','Rift.Meta.ArchiveIdentityAndFilters','Rift.Meta.WorkerPauseAndRecovery') 'main native automation'
$riftRendered=Read-RiftEvidence $RenderedAutomationContext
Assert-RiftPins $riftRendered.sourceHashes;Assert-RiftSourceCoverage $riftRendered.sourceHashes -Content;Assert-RiftPins $riftRendered.runtimeModules
$riftRenderedLog=Read-RiftEvidenceText $riftRendered.commandletLog
Assert-RiftRenderedContext $riftRendered $riftRenderedLog
$riftRenderedAutomation=Read-RiftBoundReport $riftRendered.report $riftRendered.sourceReportSha256
Assert-RiftAutomationResults $riftRenderedAutomation @('Rift.Integration.CloakSourceEffects') 'rendered native automation'
$riftMetaDirectory=Resolve-RiftDirectory $MetaRoot
$riftMetaRun=Read-RiftEvidence (Join-Path $riftMetaDirectory 'run.json');Assert-RiftPassed $riftMetaRun 'native Meta run'
$riftMetaContext=Read-RiftBoundReport $riftMetaRun.provenanceContext $riftMetaRun.provenanceContextSha256
Assert-RiftCondition ($riftMetaContext.completed -eq $true -and $riftMetaContext.sourcePinsUnchanged -eq $true -and $riftMetaContext.runtimeSnapshotPinsUnchanged -eq $true -and $riftMetaContext.editorExecutableUnchanged -eq $true -and $riftMetaContext.commandletExitCode -eq 0) 'native Meta identity'
Assert-RiftPins $riftMetaContext.sourcePins;Assert-RiftSourceCoverage $riftMetaContext.sourcePins
Assert-RiftPins $riftMetaContext.originalRuntimePins (Join-Path $modelRepo 'Unreal/RiftCrownArena')
Assert-RiftPins $riftMetaContext.runtimeSnapshotPins (Join-Path $riftMetaDirectory 'RuntimeProject')
$riftDataset=Read-RiftBoundReport $riftMetaRun.dataset $riftMetaRun.datasetSha256
$riftAudit=Read-RiftEvidence $MetaAudit
Assert-RiftCondition ((Resolve-ModelInput $riftAudit.dataset) -eq (Resolve-ModelInput $riftMetaRun.dataset) -and (Resolve-ModelInput $riftAudit.matchingManifest) -eq (Resolve-ModelInput 'Assets/Data/card_manifest.json')) 'audit identifies the actual completed dataset and canonical manifest'
Assert-RiftMetaCohort $riftDataset $riftAudit
$null=Add-RiftEvidence $riftMetaRun.export;$null=Add-RiftEvidence (Join-Path $riftMetaDirectory 'engine.log')
$riftReadback=Read-RiftEvidence $ReadbackReview;Assert-RiftReview $riftReadback 'readback review'
Assert-RiftReviewedInput $riftReadback $NativeAutomationContext;Assert-RiftReviewedInput $riftReadback $ImportRun
Assert-RiftReviewedInput $riftReadback $RenderedAutomationContext
Assert-RiftCondition ($riftReadback.reflectedCardDataUnchanged -eq $true) 'current reflected CardData confirmation'
Assert-RiftPins $riftReadback.producerPins
foreach($producer in @('Build/expansion_asset_models.py','Build/expansion_new_models.py','Build/render_approved_expansion.py','Build/render_expansion_portraits.py','Build/generate_expansion_fx_contract.py')){
    Assert-RiftCondition (@($riftReadback.producerPins|Where-Object {(Get-HoverIdentity $(if($_.path){$_.path}else{$_.file})).path -eq $producer}).Count -eq 1) "missing authored producer pin: $producer"
}
$riftImport=Read-RiftEvidence $ImportRun;Assert-RiftPassed $riftImport 'selected asset import'
Assert-RiftCondition ($riftImport.sourcesUnchanged -eq $true -and $riftImport.runtimeModulesUnchanged -eq $true -and $riftImport.exitCode -eq 0 -and @($riftImport.unapprovedChanges).Count -eq 0) 'asset import execution'
$riftAudioImport=Read-RiftEvidence $AudioImportRun;Assert-RiftPassed $riftAudioImport 'two sound import'
Assert-RiftImportAssetChain $riftImport $riftAudioImport
$riftAudioTransitionPaths=@($riftAudioImport.changedNativeAssets.path)
Assert-RiftPins $riftImport.sourceHashes;Assert-RiftPins @($riftImport.afterNativeAssets|Where-Object {$_.path -notin $riftAudioTransitionPaths});Assert-RiftPins $riftImport.runtimeModules
$riftImported=Read-RiftBoundReport $riftImport.importReport $riftImport.importReportSha256;Assert-RiftPassed $riftImported 'actual import readback'
Assert-RiftPins (@($riftImported.provenance.assetManifest,$riftImported.provenance.sourceBlend,$riftImported.provenance.script,$riftImported.provenance.helperScript)+@($riftImported.provenance.rawSources))
Assert-RiftImportedAssets $riftImported
Assert-RiftCondition ($riftAudioImport.sourcesUnchanged -eq $true -and $riftAudioImport.runtimeModulesUnchanged -eq $true -and $riftAudioImport.executableUnchanged -eq $true -and $riftAudioImport.exitCode -eq 0 -and @($riftAudioImport.unapprovedChanges).Count -eq 0 -and @($riftAudioImport.reportErrors).Count -eq 0 -and (@($riftAudioImport.selectedAudio|Sort-Object) -join ',') -eq 'shadow_surge,shield_break') 'audio import contract'
Assert-RiftPins $riftAudioImport.afterSourceHashes;Assert-RiftPins $riftAudioImport.afterNativeAssets;Assert-RiftContentCoverage $riftAudioImport.afterNativeAssets
Assert-RiftPins $riftAudioImport.afterRuntimeModules
$riftImportedAudio=Read-RiftBoundReport $riftAudioImport.importReport $riftAudioImport.importReportSha256
Assert-RiftCondition ($riftImportedAudio.masterCount -eq 63 -and @($riftImportedAudio.errors).Count -eq 0 -and $riftImportedAudio.sounds.Count -eq 2 -and (@($riftImportedAudio.sounds.name|Sort-Object) -join ',') -eq 'shadow_surge,shield_break' -and @($riftImportedAudio.sounds|Where-Object {$_.loop -ne $false}).Count -eq 0) 'actual imported cues do not loop'
$riftMasters=Read-RiftEvidence 'Assets/Source/Audio/audio_manifest.json'
foreach($sound in $riftImportedAudio.sounds){$master=$riftMasters.sounds.PSObject.Properties[$sound.name].Value;Assert-RiftCondition ($sound.sourceSha256 -eq $master.sha256 -and $sound.channels -eq $master.channels -and [math]::Abs($sound.duration-$master.duration) -le .001) "audio master readback: $($sound.name)"}
$riftAudio=Read-RiftEvidence $AudioSmokeRun;Assert-RiftRun $riftAudio 'audio smoke'
$riftAudioRaw=Read-RiftBoundReport $riftAudio.report $riftAudio.reportSha256;Assert-RiftAudioChecks $riftAudioRaw
$riftDrag=Read-RiftEvidence $DragRun;Assert-RiftRun $riftDrag 'drag smoke'
$riftDragRaw=Read-RiftBoundReport $riftDrag.report $riftDrag.nativeReportSha256
Assert-RiftDragChecks $riftDragRaw
$riftCaptures=[Collections.Generic.List[object]]::new();$riftArenaObserved=$false
foreach($path in $CaptureReports){
    $capture=Read-RiftEvidence $path;Assert-RiftPassed $capture 'native capture'
    Assert-RiftCondition ($capture.version -eq $riftManifest.version -and $capture.sourcePinsUnchanged -eq $true -and $capture.executableUnchanged -eq $true -and $capture.captured -eq $true -and $capture.stateCaptured -eq $true -and $capture.resolutionMatches -eq $true -and $capture.exitCode -eq 0 -and !$capture.timedOut) 'native capture execution'
    Assert-RiftPins $capture.sourcePins;Assert-RiftSourceCoverage $capture.sourcePins -Content
    $state=Read-RiftEvidence $capture.state;$null=Add-RiftEvidence $capture.screenshot
    $geometry=$state.arenaGeometry
    if($geometry -and $geometry.widthTiles -eq $riftManifest.rules.arenaWidth -and $geometry.heightTiles -eq $riftManifest.rules.arenaHeight -and $geometry.worldUnitsPerTile -eq 125 -and $geometry.passed -eq $true -and $geometry.gridShaderPassed -eq $true){$riftArenaObserved=$true}
    $riftCaptures.Add([ordered]@{report=$path;quality=$capture.observedQualityPreset;width=$capture.width;height=$capture.height})
}
Assert-RiftCondition $riftArenaObserved 'actual rendered24Ã—34 arena and125cm shader grid'
$riftVisual=Read-RiftEvidence $VisualReview;Assert-RiftReview $riftVisual 'visual review'
foreach($path in $CaptureReports){Assert-RiftReviewedInput $riftVisual $path;Assert-RiftReviewedInput $riftVisual (Read-RiftEvidence $path).screenshot}
$riftMeasurements=[Collections.Generic.List[object]]::new();$riftQualities=[Collections.Generic.HashSet[int]]::new()
foreach($path in $PerformanceRuns){
    $run=Read-RiftEvidence $path;Assert-RiftRun $run 'performance capture'
    $measured=Read-RiftBoundReport $run.report $run.reportSha256
    Assert-RiftPerformanceWorkload $measured
    if(!$run.screenshotCaptureEnabled){$null=$riftQualities.Add([int]$measured.observedQualityPreset)}
    $riftMeasurements.Add([ordered]@{run=$path;quality=$measured.observedQualityPreset;screenshotCapture=[bool]$run.screenshotCaptureEnabled;frame=$measured.frame;gameThread=$measured.gameThread;renderThread=$measured.renderThread;gpuFrame=$measured.gpuFrame;processPeakPhysicalBytes=$measured.processPeakPhysicalBytes;paidCombinations=$measured.expansionFixture.observedCardTeamLaneCombinations;fixturePassed=$measured.expansionFixture.passed})
}
Assert-RiftPerformanceQualities $riftQualities
$riftWorkbook=Read-RiftEvidence $WorkbookProof
Assert-RiftCondition ($riftWorkbook.passed -eq $true -and !$riftWorkbook.fixture -and $riftWorkbook.version -eq $riftManifest.version -and $riftWorkbook.balanceHash -eq $riftManifest.balanceHash -and $riftWorkbook.cards -eq $riftManifest.cards.Count -and $riftWorkbook.subordinates -eq $riftManifest.entities.Count -and $riftWorkbook.formulaCells -gt 0 -and @($riftWorkbook.failures).Count -eq 0) 'independently verified final workbook'
$riftWorkbookFile="outputs/balance-expansion/Rift_Crown_Arena_Balance_Reference_$($riftManifest.version).xlsx"
$riftWorkbookIdentity=Add-RiftEvidence $riftWorkbookFile
Assert-RiftCondition ($riftWorkbookIdentity.sha256 -eq $riftWorkbook.workbookSHA256 -and (Get-HoverIdentity 'Assets/Data/card_manifest.json').sha256 -eq $riftWorkbook.manifestFileSHA256) 'exact workbook bytes and canonical source'
Assert-RiftCondition ($riftWorkbook.visualReviewPassed -eq $true -and $riftWorkbook.nativeAutomationContext) 'workbook visual/native gating proof'
Assert-RiftCondition ((Resolve-ModelInput $riftWorkbook.nativeAutomationContext) -eq (Resolve-ModelInput $NativeAutomationContext)) 'workbook belongs to current accepted native context'
$riftShipping=[ordered]@{status='pending';passed=$false;scope='Shipping, launcher, ZIP, MSI lifecycle and publication require later actual evidence.'}
if($ShippingReport){$shipping=Read-RiftEvidence $ShippingReport;Assert-RiftReview $shipping 'Shipping acceptance';$riftShipping=[ordered]@{status='measured';passed=$true;report=$ShippingReport;scope=$shipping.scope}}
foreach($pin in @($riftBound.Values)){$null=Assert-RiftPin $pin}
if(!$OutputDirectory){$OutputDirectory=Join-Path $modelRepo ('Artifacts/QA/ExpansionEvidence/'+[Guid]::NewGuid().ToString('N'))}
$OutputDirectory=Resolve-RiftDirectory $OutputDirectory
Assert-RiftCondition ($OutputDirectory.StartsWith((Join-Path $modelRepo 'Artifacts')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and !(Test-Path -LiteralPath $OutputDirectory)) 'fresh artifact output directory'
for($ancestor=[IO.Path]::GetDirectoryName($OutputDirectory);$ancestor -and $ancestor.StartsWith($modelRepo,[StringComparison]::OrdinalIgnoreCase);$ancestor=[IO.Path]::GetDirectoryName($ancestor)){if(Test-Path -LiteralPath $ancestor){Assert-RiftCondition (((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'linked output directory'}}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
# Re-execute the maintained read-only auditor, binding supplied aggregate analysis
# to this actual dataset/log/export rather than trusting its broad success fields.
if(!$PythonExecutable){$PythonExecutable=Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'}
$riftFreshAuditPath=Join-Path $OutputDirectory 'independent-meta-recheck.json'
& $PythonExecutable -B (Join-Path $PSScriptRoot 'Tests/Audit-NativeMeta.py') $riftMetaRun.dataset --output $riftFreshAuditPath --log (Join-Path $riftMetaDirectory 'engine.log') --export $riftMetaRun.export --manifest (Join-Path $modelRepo 'Assets/Data/card_manifest.json') --target $riftAudit.observedGames --require-complete | Out-Null
Assert-RiftCondition ($LASTEXITCODE -eq 0) 'maintained independent Meta recheck'
$riftFreshAudit=Read-RiftEvidence $riftFreshAuditPath
$riftSuppliedParity=[ordered]@{};$riftFreshParity=[ordered]@{}
foreach($property in $riftAudit.PSObject.Properties){if($property.Name -ne 'auditedAtUtc'){$riftSuppliedParity[$property.Name]=$property.Value}}
foreach($property in $riftFreshAudit.PSObject.Properties){if($property.Name -ne 'auditedAtUtc'){$riftFreshParity[$property.Name]=$property.Value}}
Assert-RiftCondition (($riftSuppliedParity|ConvertTo-Json -Depth 100 -Compress) -ceq ($riftFreshParity|ConvertTo-Json -Depth 100 -Compress)) 'complete supplied/current audit parity'
$summary=[ordered]@{schema=1;version=$riftManifest.version;balanceHash=$riftManifest.balanceHash;passed=$true;scope='Approved expansion portable/native/asset/audio/drag/render/workbook evidence; Shipping is separately stated.';shipping=$riftShipping;releaseReady=$false;utc=[DateTime]::UtcNow.ToString('o');arena=[ordered]@{widthTiles=$riftManifest.rules.arenaWidth;heightTiles=$riftManifest.rules.arenaHeight;worldUnitsPerTile=125};portable=[ordered]@{scenarios=$riftPortable.scenariosPassed;matches=$riftPortable.completeAIMatches};nativeTests=$riftAutomation.tests.Count;meta=[ordered]@{games=$riftAudit.observedGames;independentChecks=$riftAudit.checks;limits=$riftAudit.limits};captures=$riftCaptures;measurements=$riftMeasurements;performanceInterpretation='Observed frame percentiles and memory only. No fabricated budget or FPS pass. Screenshot captures include readback overhead; headless Meta timing is not rendered frame time.';renderedAutomation=[ordered]@{context=$RenderedAutomationContext;observedRhi=$riftRendered.rendering.observedRhi;tests=$riftRenderedAutomation.tests.Count;cloakSourceEffects='measured'};inputs=@($riftBound.Values);payload=@($riftEvidence.Values)}
$summaryText=Convert-HoverSanitized ($summary|ConvertTo-Json -Depth 100)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'expansion-evidence.json'),$summaryText,[Text.UTF8Encoding]::new($false))
if($Zip){
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zipPath=Join-Path $OutputDirectory "RiftCrownArena-Expansion-QAEvidence-$($riftManifest.version).zip"
    $archive=[IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Create)
    $entries=[Collections.Generic.List[object]]::new()
    try{
        $entries.Add((Write-HoverEntry 'expansion-evidence.json' ([Text.UTF8Encoding]::new($false).GetBytes($summaryText))))
        foreach($pin in $riftEvidence.Values){
            $file=Resolve-ModelInput $pin.path;$bytes=[IO.File]::ReadAllBytes($file)
            if([IO.Path]::GetExtension($file) -in @('.json','.log','.txt','.md','.tsv')){$bytes=[Text.UTF8Encoding]::new($false).GetBytes((Convert-HoverSanitized ([Text.Encoding]::UTF8.GetString($bytes))))}
            $entries.Add((Write-HoverEntry ('evidence/'+$pin.path) $bytes))
        }
        $null=Write-HoverEntry 'entries.json' ([Text.UTF8Encoding]::new($false).GetBytes((@($entries)|ConvertTo-Json -Depth 5)))
    }finally{$archive.Dispose()}
    $verify=[IO.Compression.ZipFile]::OpenRead($zipPath)
    try{foreach($expected in $entries){$entry=$verify.GetEntry($expected.entry);Assert-RiftCondition ($null -ne $entry -and $entry.Length -eq $expected.bytes) 'archived entry size';$stream=$entry.Open();try{$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()};Assert-RiftCondition ($digest -eq $expected.sha256) 'archived entry hash'}}finally{$verify.Dispose()}
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'archive-verification.json'),([ordered]@{passed=$true;entries=$entries.Count;archive=[IO.Path]::GetFileName($zipPath);sha256=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $zipPath).Length}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
}
foreach($pin in @($riftBound.Values)){$null=Assert-RiftPin $pin}
Write-Output "Scoped expansion evidence: $OutputDirectory (Shipping $($riftShipping.status); releaseReady=false)"
