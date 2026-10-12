<# Isolated schema rejection tests of the actual package functions. No engine runs.
The output is explicitly a fixture, never actual native/release acceptance.
#>
param(
    [string]$ImportReport='Artifacts/QA/ExpansionImport/import-fbbc3832a8594630803d8acdee7681bd/import.json',
    [string]$ModelRun='Artifacts/QA/ExpansionImport/import-fbbc3832a8594630803d8acdee7681bd/run.json',
    [string]$AudioRun='Artifacts/QA/ExpansionAudio/import-b3104ae9801d4b57a23bcd00f0b3dcb6/run.json',
    [string]$AudioReport='Artifacts/QA/Audio/expansion160-native-audio/audio-smoke.json',
    [string]$DragReport='Artifacts/QA/CardDrag/expansion-material-bound-drag-6656c766026f43d29080ab2011460100/card-drag-smoke.json',
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Use PowerShell 7.'}
$fixtureRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$packagePath=Join-Path $fixtureRepo 'Build/Package-ExpansionQAEvidence.ps1'
$inputPaths=@($packagePath,$PSCommandPath,(Join-Path $fixtureRepo 'Assets/Data/card_manifest.json'),(Join-Path $fixtureRepo $ImportReport),
    (Join-Path $fixtureRepo $ModelRun),(Join-Path $fixtureRepo $AudioRun),(Join-Path $fixtureRepo $AudioReport),(Join-Path $fixtureRepo $DragReport),
    (Join-Path $fixtureRepo 'Build/Package-ModelInputQAEvidence.ps1'),(Join-Path $fixtureRepo 'Build/Package-HoverQAEvidence.ps1'))
$inputPins=@($inputPaths|ForEach-Object {[ordered]@{path=[IO.Path]::GetRelativePath($fixtureRepo,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length}})
if(!$OutputDirectory){$OutputDirectory=Join-Path $fixtureRepo ('Artifacts/QA/ExpansionEvidenceFixtures/'+[guid]::NewGuid().ToString('N'))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(!( $OutputDirectory.StartsWith((Join-Path $fixtureRepo 'Artifacts')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) -or (Test-Path -LiteralPath $OutputDirectory)){throw 'Use a fresh Artifacts output directory.'}
New-Item -ItemType Directory -Path $OutputDirectory|Out-Null
$modelRepo=Join-Path $OutputDirectory 'IsolatedInputs'
New-Item -ItemType Directory -Path $modelRepo|Out-Null
$riftManifest=Get-Content -LiteralPath (Join-Path $fixtureRepo 'Assets/Data/card_manifest.json') -Raw|ConvertFrom-Json -Depth 100
$actualImport=Get-Content -LiteralPath (Join-Path $fixtureRepo $ImportReport) -Raw|ConvertFrom-Json -Depth 100
$actualModelRun=Get-Content -LiteralPath (Join-Path $fixtureRepo $ModelRun) -Raw|ConvertFrom-Json -Depth 100
$actualAudioRun=Get-Content -LiteralPath (Join-Path $fixtureRepo $AudioRun) -Raw|ConvertFrom-Json -Depth 100
$actualAudio=Get-Content -LiteralPath (Join-Path $fixtureRepo $AudioReport) -Raw|ConvertFrom-Json -Depth 100
$actualDrag=Get-Content -LiteralPath (Join-Path $fixtureRepo $DragReport) -Raw|ConvertFrom-Json -Depth 100
function Load-FixtureFunctions([string]$Path,[string[]]$Names){
    $tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Path,[ref]$tokens,[ref]$parseErrors)
    if($parseErrors.Count){throw "Source parse failed: $($parseErrors -join '; ')"}
    foreach($name in $Names){
        $node=@($ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true))
        if($node.Count -ne 1){throw "Missing exact production function: $name"}
        . ([scriptblock]::Create($node[0].Extent.Text))
        Set-Item -LiteralPath "Function:script:$name" -Value (Get-Item "Function:$name").ScriptBlock
    }
    return $ast
}
$packageAst=Load-FixtureFunctions $packagePath @('Assert-RiftCondition','Assert-RiftPassed','Assert-RiftPin','Assert-RiftPins','Assert-RiftSourceCoverage','Assert-RiftAutomationContext','Assert-RiftAutomationResults','Assert-RiftRenderedContext','Assert-RiftImportedAssets','Assert-RiftPerformanceQualities','Assert-RiftImportAssetChain','Assert-RiftContentCoverage','Assert-RiftAudioChecks','Assert-RiftDragChecks','Assert-RiftPerformanceWorkload')
$null=Load-FixtureFunctions (Join-Path $fixtureRepo 'Build/Package-ModelInputQAEvidence.ps1') @('Resolve-ModelInput')
$null=Load-FixtureFunctions (Join-Path $fixtureRepo 'Build/Package-HoverQAEvidence.ps1') @('Get-HoverIdentity')
$cases=[Collections.Generic.List[object]]::new()
function Check-Fixture([string]$Name,[scriptblock]$Action,[bool]$Reject=$false){
    $caught=$null;$riftBound=[ordered]@{}
    try{& $Action}catch{$caught=$_.Exception.Message}
    $passed=$(if($Reject){$null -ne $caught}else{$null -eq $caught})
    $cases.Add([ordered]@{name=$Name;passed=$passed;expectedRejection=$Reject;observedError=$caught})
    if(!$passed){throw "Fixture failed: $Name / $caught"}
}
function Copy-Fixture($Object){$Object|ConvertTo-Json -Depth 100|ConvertFrom-Json -Depth 100}
$riftBound=[ordered]@{}
Check-Fixture 'Actual successful native import schema without validation.passed' {Assert-RiftImportedAssets $actualImport}
Check-Fixture 'Actual final model-to-audio import transition' {Assert-RiftImportAssetChain $actualModelRun $actualAudioRun}
foreach($mutation in @('wrongBeforeHash','extraAssetChange','wrongTransitionRecord','deletedSound','missingInventory','audioBeforeModel','oldMasterChanged')){
    $candidate=Copy-Fixture $actualAudioRun
    switch($mutation){
        'wrongBeforeHash' {$candidate.beforeNativeAssets[0].sha256='0'*64}
        'extraAssetChange' {$candidate.afterNativeAssets[0].sha256='0'*64}
        'wrongTransitionRecord' {$candidate.changedNativeAssets[0].afterSha256='0'*64}
        'deletedSound' {$candidate.changedNativeAssets[0].deleted=$true}
        'missingInventory' {$candidate.afterNativeAssets=@($candidate.afterNativeAssets|Select-Object -Skip 1)}
        'audioBeforeModel' {$candidate.startedUtc='2000-01-01T00:00:00Z'}
        'oldMasterChanged' {$candidate.releasedMastersUnchanged=$false}
    }
    Check-Fixture "Reject model/audio chain: $mutation" {Assert-RiftImportAssetChain $actualModelRun $candidate} $true
}
Check-Fixture 'Actual63-sound audio smoke schema with silent river control' {Assert-RiftAudioChecks $actualAudio}
foreach($mutation in @('emptyChecks','missingControl','falseDevice','badControlCount','riverInHome','riverInQuiet','expansionLoopFailure')){
    $candidate=Copy-Fixture $actualAudio
    switch($mutation){
        'emptyChecks' {$candidate.checks=@()}
        'missingControl' {$candidate.checks=@($candidate.checks|Where-Object name -ne 'riverVoiceInspectionDetectsSilentControl')}
        'falseDevice' {($candidate.checks|Where-Object name -eq 'liveAudioDevice').passed=$false}
        'badControlCount' {$candidate.waterAmbienceLifecycle.controlPlayingVoices=0}
        'riverInHome' {$candidate.waterAmbienceLifecycle.homePlayingVoices=1}
        'riverInQuiet' {$candidate.waterAmbienceLifecycle.quietPlayingVoices=1}
        'expansionLoopFailure' {($candidate.checks|Where-Object name -eq 'shieldBreakSoundLoadsWithoutLoop').passed=$false}
    }
    Check-Fixture "Reject audio positive controls: $mutation" {Assert-RiftAudioChecks $candidate} $true
}
$dragFixture=Copy-Fixture $actualDrag;$dragFixture.version=$riftManifest.version
Check-Fixture 'Isolated copy of actual32 drag rows with current version field' {Assert-RiftDragChecks $dragFixture}
foreach($mutation in @('emptyChecks','duplicateCheck','missingLegalControl','oldVersion','falseCancel')){
    $candidate=Copy-Fixture $dragFixture
    switch($mutation){
        'emptyChecks' {$candidate.checks=@()};'duplicateCheck' {$candidate.checks[0].name=$candidate.checks[1].name}
        'missingLegalControl' {($candidate.checks|Where-Object name -eq 'legal drop spends exactly one card cost').name='Fake positive control'}
        'oldVersion' {$candidate.version='0.0.0'}
        'falseCancel' {($candidate.checks|Where-Object name -eq 'return to hand cancels and preserves full match state').passed=$false}
    }
    Check-Fixture "Reject drag false positive: $mutation" {Assert-RiftDragChecks $candidate} $true
}
$perf=[pscustomobject]@{frame=[pscustomobject]@{samples=100};metaGamesDuringBattle=0;expansionFixture=[pscustomobject]@{
    passed=$true;errors=@();balanceHash=$riftManifest.balanceHash;manifestVersion=$riftManifest.version;paidDeploymentProof=$true;firstWaveComplete=$true;observedCardTeamLaneCombinations=32;
    shockwaveLaunchEvents=1;shockwaveDamageEvents=1;maximumDeathsAtOneSimulationTimestamp=2;deathVisualsObservedAtEvent=2;deathVisualsObservedRetired=2;
    allDueObservedDeathVisualsRetiredOnTime=$true;lateObservedDeathVisuals=0;pendingDeathVisualsPastDeadlineTolerance=0;expiredBodyDeathFleckObservations=0;noDeathFlecksOnExpiredBodies=$true;
    observedCardModelIds=@('mini_stampede','stampede','pulverizer','bone_warden','aether_reaper','iron_giant');maximumObservedUnitVisuals=2;maximumObservedHoofDustInstances=8;
    maximumObservedFormationDustInstances=8;maximumObservedDeathFleckInstances=6;maximumObservedSlowRippleInstances=48;maximumObservedSelectedSurfaceInstances=70}}
Check-Fixture 'Synthetic paid workload schema with real positive controls and exact instance bounds' {Assert-RiftPerformanceWorkload $perf}
foreach($mutation in @('unpaid','incompleteOpening','noLaunch','noHit','noMassDeaths','noRetirement','lateDeath','overdueDeath','expiredFlecks','missingModel','missingCount','overBound','wrongHash','tooFewSamples','backgroundMeta')){
    $candidate=Copy-Fixture $perf;$f=$candidate.expansionFixture
    switch($mutation){
        'unpaid' {$f.paidDeploymentProof=$false};'incompleteOpening' {$f.firstWaveComplete=$false};'noLaunch' {$f.shockwaveLaunchEvents=0};'noHit' {$f.shockwaveDamageEvents=0}
        'noMassDeaths' {$f.maximumDeathsAtOneSimulationTimestamp=1};'noRetirement' {$f.deathVisualsObservedRetired=0};'lateDeath' {$f.lateObservedDeathVisuals=1}
        'overdueDeath' {$f.pendingDeathVisualsPastDeadlineTolerance=1};'expiredFlecks' {$f.expiredBodyDeathFleckObservations=1};'missingModel' {$f.observedCardModelIds=@('mini_stampede')}
        'missingCount' {$f.PSObject.Properties.Remove('maximumObservedHoofDustInstances')};'overBound' {$f.maximumObservedHoofDustInstances=9};'wrongHash' {$f.balanceHash='0'*64}
        'tooFewSamples' {$candidate.frame.samples=99};'backgroundMeta' {$candidate.metaGamesDuringBattle=1}
    }
    Check-Fixture "Reject workload false positive: $mutation" {Assert-RiftPerformanceWorkload $candidate} $true
}
foreach($mutation in @('missingChild','duplicateCard','badSubordinate','badBones','staleWardenSkeleton','missingPhysics','missingRigBinding','wrongStaticMaterial','missingStatic','falseFXPin','falseShockwavePin','ShadowCreature')){
    $candidate=Copy-Fixture $actualImport
    switch($mutation){
        'missingChild' {$candidate.validation.cards=@($candidate.validation.cards|Where-Object id -ne 'bone_warden_skeleton')}
        'duplicateCard' {$candidate.validation.cards[-1].id=$candidate.validation.cards[0].id}
        'badSubordinate' {$candidate.validation.cards[-1].subordinate=$false}
        'badBones' {($candidate.validation.cards|Where-Object id -eq 'iron_giant').bones=34}
        'staleWardenSkeleton' {($candidate.validation.cards|Where-Object id -eq 'bone_warden').bones=32}
        'missingPhysics' {($candidate.validation.cards|Where-Object id -eq 'bone_warden_skeleton').physicsRegisteredOnDisk=$false}
        'missingRigBinding' {$candidate.expansionMaterialBindings.PSObject.Properties.Remove('/Game/Rift/Characters/iron_giant/SK_iron_giant.SK_iron_giant')}
        'wrongStaticMaterial' {$candidate.expansionMaterialBindings.PSObject.Properties['/Game/Rift/Environment/SM_iron_shield_fragment.SM_iron_shield_fragment'].Value.slots[0].materialInterface='/Game/Rift/Materials/M_RiftSurface.M_RiftSurface'}
        'missingStatic' {$candidate.statics.PSObject.Properties.Remove('shadow_surge_wisp')}
        'falseFXPin' {$candidate.expansionFXMaterial.connections[0].connected=$false}
        'falseShockwavePin' {$candidate.shaderConnections.M_RiftShockwave[0].connected=$false}
        'ShadowCreature' {$candidate.skeletal|Add-Member shadow_surge '/Game/Rift/Characters/fake/fake'}
    }
    Check-Fixture "Reject actual-import copy: $mutation" {Assert-RiftImportedAssets $candidate} $true
}
$context=[pscustomobject]@{schema=2;completed=$true;sourcesUnchanged=$true;runtimeModulesUnchanged=$true;commandletExitCode=0;wrapperExitCode=0;rendering=[pscustomobject]@{requested=$true;mode='offscreen-d3d12';observedRhi='D3D12'}}
Check-Fixture 'Synthetic completed D3D12 context with actual-RHI log shape' {Assert-RiftRenderedContext $context 'LogRHI: Using Default RHI: D3D12'}
foreach($mutation in @('incomplete','sourceChanged','moduleChanged','exitFailed','fixture','nullrhi','adapterEnumerationOnly','disabledNiagara')){
    $candidate=Copy-Fixture $context;$log='LogRHI: Using Default RHI: D3D12'
    switch($mutation){
        'incomplete' {$candidate.completed=$false};'sourceChanged' {$candidate.sourcesUnchanged=$false};'moduleChanged' {$candidate.runtimeModulesUnchanged=$false};'exitFailed' {$candidate.commandletExitCode=1}
        'fixture' {$candidate|Add-Member fixture $true};'nullrhi' {$candidate.rendering.observedRhi='NullRHI'}
        'adapterEnumerationOnly' {$log='LogD3D12RHI: Found D3D12 adapter'}
        'disabledNiagara' {$log+=' NiagaraComponent is not allowed to spawn when rendering is disabled'}
    }
    Check-Fixture "Reject rendered context: $mutation" {Assert-RiftRenderedContext $candidate $log} $true
}
$results=[pscustomobject]@{failed=0;notRun=0;succeeded=2;succeededWithWarnings=0;tests=@([pscustomobject]@{fullTestPath='Rift.Integration.CloakSourceEffects';state='Success';errors=0},[pscustomobject]@{fullTestPath='Rift.Integration.FullLengthReplay';state='Success';errors=0})}
Check-Fixture 'Synthetic successful required native test rows' {Assert-RiftAutomationResults $results @('Rift.Integration.CloakSourceEffects','Rift.Integration.FullLengthReplay') 'fixture'}
foreach($mutation in @('missingRequired','duplicate','failed','notRun','rowSkipped','summaryMismatch')){
    $candidate=Copy-Fixture $results
    switch($mutation){'missingRequired' {$candidate.tests[0].fullTestPath='Other'};'duplicate' {$candidate.tests[0].fullTestPath=$candidate.tests[1].fullTestPath};'failed' {$candidate.failed=1};'notRun' {$candidate.notRun=1};'rowSkipped' {$candidate.tests[0].state='Skipped'};'summaryMismatch' {$candidate.succeeded=1}}
    Check-Fixture "Reject native results: $mutation" {Assert-RiftAutomationResults $candidate @('Rift.Integration.CloakSourceEffects','Rift.Integration.FullLengthReplay') 'fixture'} $true
}
$qualities=[Collections.Generic.HashSet[int]]::new();$null=$qualities.Add(0);$null=$qualities.Add(2)
Check-Fixture 'Actual LOW0/HIGH2 preset contract' {Assert-RiftPerformanceQualities $qualities}
$wrongQualities=[Collections.Generic.HashSet[int]]::new();$null=$wrongQualities.Add(0);$null=$wrongQualities.Add(3)
Check-Fixture 'Reject old HIGH3 substitution' {Assert-RiftPerformanceQualities $wrongQualities} $true
$pinFile=Join-Path $modelRepo 'input.txt';[IO.File]::WriteAllText($pinFile,'Synthetic fixture, never game data')
$pin=Get-HoverIdentity 'input.txt'
Check-Fixture 'Actual isolated byte/hash pin match' {$null=Assert-RiftPin $pin}
$stale=Copy-Fixture $pin;$stale.sha256='0'*64
Check-Fixture 'Reject actual stale input hash' {$null=Assert-RiftPin $stale} $true
$wrongBytes=Copy-Fixture $pin;$wrongBytes.bytes++
Check-Fixture 'Reject actual stale input byte count' {$null=Assert-RiftPin $wrongBytes} $true
Check-Fixture 'Reject missing producer input' {$null=Assert-RiftPin ([pscustomobject]@{path='missing.py';sha256='0'*64})} $true
$coveragePaths=@('Assets/Data/card_manifest.json','Build/generate_card_manifest.py','Unreal/RiftCrownArena/Source/Fixture.cpp','Unreal/RiftCrownArena/Config/Fixture.ini','Unreal/RiftCrownArena/Content/Fixture.uasset')
foreach($relative in $coveragePaths){$file=Join-Path $modelRepo $relative;New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($file)) -Force|Out-Null;[IO.File]::WriteAllText($file,'Isolated synthetic inventory fixture; never imported or approved')}
$coveragePins=@($coveragePaths|ForEach-Object {Get-HoverIdentity $_})
Check-Fixture 'Actual isolated complete Source/Config/Content inventory' {Assert-RiftSourceCoverage $coveragePins -Content}
Check-Fixture 'Reject absent canonical generator pin' {Assert-RiftSourceCoverage @($coveragePins|Where-Object path -ne 'Build/generate_card_manifest.py') -Content} $true
Check-Fixture 'Reject incomplete Source inventory' {Assert-RiftSourceCoverage @($coveragePins|Where-Object path -ne 'Unreal/RiftCrownArena/Source/Fixture.cpp') -Content} $true
$contentPins=@($coveragePins|Where-Object path -like 'Unreal/RiftCrownArena/Content/*')
Check-Fixture 'Actual isolated complete current post-audio Content coverage' {Assert-RiftContentCoverage $contentPins}
Check-Fixture 'Reject incomplete current Content inventory' {Assert-RiftContentCoverage @()} $true
Check-Fixture 'Reject duplicated current Content pin' {Assert-RiftContentCoverage @($contentPins[0],$contentPins[0])} $true
$parameters=@($packageAst.ParamBlock.Parameters.Name.VariablePath.UserPath)
Check-Fixture 'Rendered automation is required as package parameter' {
    if($parameters -notcontains 'RenderedAutomationContext'){throw 'Missing rendered input'}
    $param=$packageAst.ParamBlock.Parameters|Where-Object {$_.Name.VariablePath.UserPath -eq 'RenderedAutomationContext'}
    if($param.Extent.Text -notmatch 'Mandatory'){throw 'Rendered input is optional'}
}
Check-Fixture 'Historical source bypass parameter removed' {if($parameters -contains 'ImportHistoricalSourcePaths'){throw 'Historical bypass present'}}
foreach($pin in $inputPins){if((Get-FileHash -LiteralPath (Join-Path $fixtureRepo $pin.path)).Hash.ToLowerInvariant() -ne $pin.sha256){throw 'Real source/evidence changed during fixture run'}}
$report=[ordered]@{schema=1;fixture=$true;synthetic=$true;passed=$true;scope='Extracted actual aggregator helpers tested with one genuine import schema and isolated synthetic negative controls; no engine or packaging acceptance';cases=$cases;checkCount=$cases.Count;inputs=$inputPins;errors=@()}
$reportPath=Join-Path $OutputDirectory 'fixture-report.json';[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
Write-Output "Expansion evidence fixtures PASS $($cases.Count): $reportPath"
