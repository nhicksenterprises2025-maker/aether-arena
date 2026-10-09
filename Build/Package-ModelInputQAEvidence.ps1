param(
    [string]$Version='1.3.0',
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string[]]$CaptureNames,
    [Parameter(Mandatory)][string]$ReviewNotes,
    [Parameter(Mandatory)][string]$PerformanceName,
    [Parameter(Mandatory)][string]$AudioName,
    [Parameter(Mandatory)][string]$VFXName,
    [Parameter(Mandatory)][string]$DragSmokeReport,
    [string]$ModelReviewNotes='Artifacts/QA/model130-production-model-review.json',
    [string]$PortableLog='Artifacts/QA/model130-portable-tests.log',
    [string[]]$ReleaseDocuments=@('Docs/PATCH_NOTES_1.3.0.md','Docs/MODEL_INPUT_1.3.0.md')
)
# Current model/art/font/input acceptance. The prior 100-match spell-rules
# cohort is retained with its actual runtime, not relabeled as a new cohort.
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this evidence packager with PowerShell 7.'}
if($Version -ne '1.3.0'){throw 'This evidence gate describes the 1.3.0 model, typography and card-input release.'}
$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$modelSummaryRelative='Docs/QA/model-input-checks-1.3.0.json'
$modelZip=Join-Path $modelRepo ('Artifacts/Release/RiftCrownArena-QAEvidence-Windows-x64-'+$Version+'.zip')
if(Test-Path -LiteralPath $modelZip){throw 'The frozen evidence archive already exists; a frozen archive cannot be replaced.'}
$modelInputs=[Collections.Generic.List[object]]::new()
$modelPinIndex=@{}
$modelSyntheticInputs=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
function Resolve-ModelInput([string]$Path){
    $resolved=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($Path)){$Path}else{Join-Path $modelRepo $Path}))
    if(!$resolved.StartsWith($modelRepo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $resolved -PathType Leaf)){throw "Missing workspace evidence: $Path"}
    $ancestor=$resolved
    while($ancestor -and $ancestor.StartsWith($modelRepo,[StringComparison]::OrdinalIgnoreCase)){
        if(((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Evidence paths cannot contain file or directory links.'}
        $ancestor=[IO.Path]::GetDirectoryName($ancestor)
    }
    return $resolved
}
function Pin-ModelInput([string]$Path){
    $resolved=Resolve-ModelInput $Path
    $pin=[pscustomobject][ordered]@{path=[IO.Path]::GetRelativePath($modelRepo,$resolved).Replace('\','/');sha256=(Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $resolved).Length}
    if($pin.path -match '(^|/)(Replays|Save|Saves|Backups|Saved|AutomationFixtures|EngineUserData|EngineUser)(/|$)' -or
       [IO.Path]::GetFileName($pin.path) -match '^(player_save|ue_save)\.json(\.|$)' -or
       ($pin.path -match '(^|/)UserData(/|$)' -and !$modelSyntheticInputs.Contains($pin.path))){throw 'Private profiles, saves, replay files and engine user folders cannot be packaged.'}
    if($modelPinIndex.ContainsKey($pin.path)){
        if($modelPinIndex[$pin.path].sha256 -ne $pin.sha256 -or $modelPinIndex[$pin.path].bytes -ne $pin.bytes){throw "Input changed during acceptance: $($pin.path)"}
    }else{$modelPinIndex[$pin.path]=$pin;$modelInputs.Add($pin)}
    return $pin
}
function Read-ModelInput([string]$Path){
    $null=Pin-ModelInput $Path
    return Get-Content -LiteralPath (Resolve-ModelInput $Path) -Raw | ConvertFrom-Json -Depth 100
}
function Get-ModelUtcTimestamp([object]$Value){
    # PowerShell 7.6 can deserialize ISO JSON timestamps as DateTime objects.
    # Preserve their kind instead of reparsing a culture string without an offset.
    if($Value -is [DateTime]){return $Value.ToUniversalTime()}
    if($Value -is [DateTimeOffset]){return $Value.UtcDateTime}
    return [DateTimeOffset]::Parse([string]$Value).UtcDateTime
}
function Assert-ModelShippingDiagnostics([object]$Run,[string]$Label){
    $kind=$(if($Run.diagnosticSource){$Run.diagnosticSource}else{$Run.logSource})
    $verification=$Run.diagnosticVerification
    if($kind -ne 'shipping-frift-diagnostics' -or $verification.passed -ne $true -or $verification.gameVersion -ne $Version -or $verification.build -ne 'Shipping' -or
       $verification.freshLog -ne $true -or $verification.contextMatches -ne $true -or $verification.errorCount -ne 0 -or $verification.processId -le 0 -or
       @($Run.nativeDiagnosticLogs).Count -lt 1 -or @($Run.nativeDiagnosticLogs.file | Sort-Object -Unique).Count -ne @($Run.nativeDiagnosticLogs).Count -or
       $Run.engineLog -notin $Run.nativeDiagnosticLogs.file){throw "Actual verified Shipping diagnostics are missing: $Label"}
    $started=Get-ModelUtcTimestamp $verification.startedUTC;$finished=Get-ModelUtcTimestamp $verification.finishedUTC
    $save=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($verification.saveRoot)){$verification.saveRoot}else{Join-Path $modelRepo $verification.saveRoot})).TrimEnd('\','/')
    $user=$(if($verification.engineUserRoot){$verification.engineUserRoot}else{Join-Path ([IO.Path]::GetDirectoryName($save)) 'EngineUser'})
    $user=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($user)){$user}else{Join-Path $modelRepo $user})).TrimEnd('\','/')
    if($started -ge $finished -or !$save.StartsWith($modelRepo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
       !$user.StartsWith($modelRepo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Shipping diagnostic execution/isolation context is invalid.'}
    $records=[Collections.Generic.List[object]]::new()
    foreach($record in $Run.nativeDiagnosticLogs){
        $pin=Assert-ModelRecord $record ($Label+' native structured log')
        $original=Resolve-ModelInput $record.sourceFile
        $expected=Join-Path (Join-Path $save 'Logs') ([IO.Path]::GetFileName($pin.path))
        if($original -ne $expected -or [IO.Path]::GetFileName($original) -notmatch ('^RiftGame-\d{4}-\d{2}-\d{2}-'+$verification.processId+'-\d{2}-\d{2}-\d{2}-\d+\.log$') -or
           (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256 -or (Get-Item -LiteralPath $original).Length -ne $pin.bytes){throw 'Archived diagnostic copy differs from the exact isolated native process log.'}
        $fileRecords=@(Get-Content -LiteralPath (Resolve-ModelInput $pin.path)|Where-Object {![string]::IsNullOrWhiteSpace($_)}|ForEach-Object {$_|ConvertFrom-Json -Depth 100})
        $contexts=@($fileRecords|Where-Object event -eq 'system_context')
        if($contexts.Count -ne 1){throw 'Each Shipping native diagnostic file must identify exactly one process context.'}
        foreach($context in $contexts){
            $config=[IO.Path]::GetFullPath($context.configRoot).TrimEnd('\','/')
            if($context.processId -ne $verification.processId -or $context.gameVersion -ne $Version -or $context.build -ne 'Shipping' -or $context.project -ne 'RiftCrownArena' -or
               [IO.Path]::GetFullPath($context.saveRoot).TrimEnd('\','/') -ne $save -or !$config.StartsWith($user+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Native diagnostic process/version/build/save/config context differs from its actual run.'}
        }
        foreach($entry in $fileRecords){
            $timestamp=Get-ModelUtcTimestamp $entry.timestamp
            if($entry.schemaVersion -ne 1 -or $timestamp -lt $started -or $timestamp -gt $finished -or $entry.level -in @('Error','Fatal')){throw 'Native diagnostic record is outside its run or contains an Error/Fatal.'}
            $records.Add($entry)
        }
    }
    if($records.Count -ne $verification.recordCount){throw 'Verified native diagnostic record inventory differs from the actual log bytes.'}
    return [pscustomobject]@{records=$records.ToArray()}
}
function Assert-ModelRecord([object]$Record,[string]$Label){
    if(!$Record -or [string]::IsNullOrWhiteSpace($Record.file) -or $Record.sha256 -notmatch '^[a-f0-9]{64}$' -or $Record.bytes -le 0){throw "Incomplete file provenance: $Label"}
    $pin=Pin-ModelInput $Record.file
    if($pin.sha256 -ne $Record.sha256 -or $pin.bytes -ne $Record.bytes){throw "File changed after independent acceptance: $Label"}
    return $pin
}
function Get-ModelFileRecords([object]$Node){
    if($null -eq $Node -or $Node -is [string] -or $Node -is [ValueType]){return}
    if($Node -is [System.Collections.IEnumerable] -and $Node -isnot [pscustomobject]){
        foreach($value in $Node){Get-ModelFileRecords $value}
        return
    }
    if($Node.PSObject.Properties['file'] -and $Node.PSObject.Properties['sha256'] -and $Node.PSObject.Properties['bytes']){$Node}
    foreach($property in $Node.PSObject.Properties){Get-ModelFileRecords $property.Value}
}
function Assert-ModelChecks([object]$Report,[int]$Minimum,[string]$Label){
    if($Report.passed -ne $true -or @($Report.checks).Count -lt $Minimum -or
       @($Report.checks | Where-Object {$_.passed -ne $true}).Count -ne 0 -or @($Report.errors | Where-Object {$_}).Count -gt 0){throw "Independent checks did not pass: $Label"}
}

$modelNative=Resolve-ModelInput $Executable
$modelNativeHash=(Get-FileHash -LiteralPath $modelNative -Algorithm SHA256).Hash.ToLowerInvariant()
$modelManifest=Read-ModelInput 'Artifacts/Release/update-manifest.json'
$modelRelease=Read-ModelInput 'Artifacts/Release/windows-release-verification.json'
$modelNativeEntry=@($modelManifest.files | Where-Object path -match 'Binaries/Win64/RiftCrownArena-Win64-Shipping.exe$')
if(!$modelRelease.passed -or $modelRelease.version -ne $Version -or $modelManifest.version -ne $Version -or $modelNativeEntry.Count -ne 1 -or $modelNativeEntry[0].sha256 -ne $modelNativeHash -or
   !$modelNative.Replace('\','/').EndsWith('/'+$modelNativeEntry[0].path,[StringComparison]::OrdinalIgnoreCase)){throw 'Accepted release, current version and requested native executable differ.'}
$modelPackageRoot=$modelNative.Substring(0,$modelNative.Length-$modelNativeEntry[0].path.Length)

$modelIntegration=Read-ModelInput 'Docs/QA/native-integration.json'
if($modelIntegration.failed -ne 0 -or $modelIntegration.notRun -ne 0 -or @($modelIntegration.tests).Count -lt 15 -or
   @($modelIntegration.tests | Where-Object {$_.state -notin @('Success','SuccessWithWarnings')}).Count -ne 0){throw 'The complete current native integration suite must pass at least fifteen tests.'}
foreach($required in @('Rift.Integration.Typography','Rift.Integration.BattleInputRouting','Rift.Integration.SpellCastReplay')){
    if(@($modelIntegration.tests | Where-Object fullTestPath -eq $required).Count -ne 1){throw "Required complete-suite scenario is missing: $required"}
}
$modelContext=$modelIntegration.curatedEvidence
if(!$modelContext -or $modelContext.version -ne $Version -or !$modelContext.allScenariosObserved -or $modelContext.commandletExitCode -ne 0 -or $modelContext.wrapperExitCode -ne 0){throw 'Current complete integration provenance is missing.'}
foreach($source in $modelContext.sourceHashes){
    $pin=Pin-ModelInput $source.path
    if($pin.sha256 -ne $source.sha256){throw "Source changed after complete-suite acceptance: $($source.path)"}
}
foreach($module in $modelContext.runtimeModules){
    if((Get-FileHash -LiteralPath (Resolve-ModelInput $module.path)).Hash.ToLowerInvariant() -ne $module.sha256){throw 'Editor runtime changed after complete-suite acceptance.'}
}
$modelPortable=Pin-ModelInput $PortableLog
$modelPortableText=Get-Content -LiteralPath (Resolve-ModelInput $modelPortable.path) -Raw
if($modelPortableText -notmatch 'Native authoritative simulation: 41 scenarios passed\.' -or $modelPortableText -notmatch 'SOAK 21 complete matches'){throw 'The forty-one-scenario portable regression and twenty-one-match soak must pass.'}
$null=Pin-ModelInput 'Build/Tests/RiftSimulationTests.cpp'

$modelMeta=Read-ModelInput 'Docs/QA/spell-timing-meta-100.json'
$modelHistory=Read-ModelInput 'Docs/QA/historical-native-integration-1.2.1.json'
$modelMetaBuild=Read-ModelInput 'Artifacts/QA/spell121-first-native-integration.json'
if(!$modelMeta.completeObserved -or $modelMeta.observedGames -ne 100 -or $modelMeta.failedChecks -ne 0 -or @($modelMeta.errors).Count -ne 0 -or $modelMeta.fingerprint -ne 'b3968027993fd0ece72577b77c48d76e' -or
   $modelHistory.failed -ne 0 -or $modelHistory.notRun -ne 0 -or $modelHistory.curatedEvidence.version -ne '1.2.1' -or !$modelHistory.curatedEvidence.allScenariosObserved -or
   $modelMetaBuild.failed -ne 0 -or $modelMetaBuild.notRun -ne 0 -or $modelMetaBuild.curatedEvidence.version -ne '1.2.1' -or !$modelMetaBuild.curatedEvidence.allScenariosObserved -or
   $modelMeta.evidence.runtimeSha256 -ne $modelMetaBuild.curatedEvidence.runtimeModuleSha256 -or @($modelMeta.evidence.simulationSources).Count -ne 6){throw 'The inherited spell-rules sample must retain its actual complete runtime provenance.'}
$modelCanonicalCore=@('Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftAI.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCombat.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftDeckAnalysis.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftPathfinding.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftSimulation.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftSimulation.h')
foreach($path in $modelCanonicalCore){
    $source=@($modelMeta.evidence.simulationSources | Where-Object path -eq $path)
    $built=@($modelMetaBuild.curatedEvidence.sourceHashes | Where-Object path -eq $path)
    $historical=@($modelHistory.curatedEvidence.sourceHashes | Where-Object path -eq $path)
    $pin=Pin-ModelInput $path
    if($source.Count -ne 1 -or $built.Count -ne 1 -or $historical.Count -ne 1 -or $pin.sha256 -ne $source[0].sha256 -or $pin.sha256 -ne $built[0].sha256 -or $pin.sha256 -ne $historical[0].sha256){throw 'Authoritative simulation changed after the accepted spell-rules cohort.'}
}
foreach($input in $modelMeta.evidence.inputs){
    if($input.path -notmatch '^Artifacts/QA/spell121-meta100/(context\.json|run\.json|engine\.log|UserData/meta-validation\.json|UserData/Meta/[A-F0-9]{32}\.json)$'){throw 'Only the recorded synthetic Meta cohort inputs can be inherited.'}
    $null=$modelSyntheticInputs.Add($input.path)
    $pin=Pin-ModelInput $input.path
    if($pin.sha256 -ne $input.sha256 -or $pin.bytes -ne $input.bytes){throw 'Meta source evidence changed after its independent audit.'}
}

$modelAssets=Read-ModelInput 'Assets/asset_manifest.json'
$modelAssetQA=Read-ModelInput 'Assets/asset_qa_report.json'
$modelPortraitQA=Read-ModelInput 'Artifacts/QA/model-card-portraits.json'
$modelPortraits=Read-ModelInput 'Assets/Source/CardArt/model_portraits.json'
$modelBefore=Read-ModelInput 'Artifacts/QA/model130-before/asset_manifest.json'
if($modelAssets.build -ne 'UE-1.3.0' -or $modelAssets.source.sha256 -eq $modelBefore.source.sha256){throw 'The current model manifest must identify the newly authored 1.3.0 Blender source.'}
Assert-ModelChecks $modelAssetQA 1000 'Blender source and raw FBX exports'
Assert-ModelChecks $modelPortraitQA 352 'Fourteen model-rendered card portraits'
foreach($name in @('assetManifest','sourceBlend','validatorScript')){
    $null=Assert-ModelRecord $modelAssetQA.provenance.$name ('Source model audit '+$name)
}
if($modelAssetQA.provenance.assetManifest.file -ne 'Assets/asset_manifest.json' -or $modelAssetQA.provenance.sourceBlend.file -ne $modelAssets.source.file -or
   $modelAssetQA.provenance.sourceBlend.sha256 -ne $modelAssets.source.sha256 -or $modelAssetQA.provenance.validatorScript.file -ne 'Build/validate_assets.py'){throw 'The independent source audit was not executed on this manifest/source/validator.'}
foreach($record in @(Get-ModelFileRecords $modelAssets)){$null=Assert-ModelRecord $record ('Current asset manifest '+$record.file)}
foreach($property in $modelPortraitQA.provenance.PSObject.Properties){$null=Assert-ModelRecord $property.Value ('Portrait audit '+$property.Name)}
if($modelPortraitQA.provenance.assetManifest.file -ne 'Assets/asset_manifest.json' -or $modelPortraitQA.provenance.portraitReport.file -ne 'Assets/Source/CardArt/model_portraits.json' -or
   $modelPortraitQA.provenance.sourceBlend.sha256 -ne $modelAssets.source.sha256 -or $modelPortraits.sourceBlend.sha256 -ne $modelAssets.source.sha256 -or
   $modelPortraits.aiGeneratedRasterArtwork -ne $false -or ($modelPortraits.resolution -join ',') -ne '768,960'){throw 'Portrait provenance is not linked to the final authored model source.'}
$modelCardIds=@('ironclad','ember_archer','twin_blades','boulderback','arc_mage','rambeast','sky_manta','vampire_bats','frost_fang','storm_raven','archer_tower','bullet_burst','nova_flask','meteor_shards')
foreach($dictionary in @($modelAssets.cards,$modelAssets.illustrations,$modelPortraits.cards,$modelPortraitQA.cards)){
    $ids=@($dictionary.PSObject.Properties.Name | Sort-Object)
    if(($ids -join ',') -ne (($modelCardIds | Sort-Object) -join ',')){throw 'All fourteen canonical cards must be present exactly once.'}
}
foreach($card in $modelCardIds){
    $art=$modelAssets.illustrations.$card
    $portrait=$modelPortraits.cards.$card
    $pin=Assert-ModelRecord $art ('Native illustration '+$card)
    if($art.file -ne 'Assets/Source/CardArt/'+$card+'.png' -or $art.sha256 -eq $modelBefore.illustrations.$card.sha256 -or $art.sha256 -ne $portrait.sha256 -or $art.bytes -ne $portrait.bytes -or
       @($portrait.sources).Count -lt 1){throw "The new card art must come from the current actual models: $card"}
    foreach($source in $portrait.sources){$null=Assert-ModelRecord $source.model ('Portrait model '+$card)}
    $png=[IO.File]::ReadAllBytes((Resolve-ModelInput $pin.path))
    if($png.Length -lt 24){throw "Portrait PNG is truncated: $card"}
    $pngWidth=([int]$png[16] -shl 24) -bor ([int]$png[17] -shl 16) -bor ([int]$png[18] -shl 8) -bor [int]$png[19]
    $pngHeight=([int]$png[20] -shl 24) -bor ([int]$png[21] -shl 16) -bor ([int]$png[22] -shl 8) -bor [int]$png[23]
    if([Convert]::ToHexString([byte[]]$png[0..7]) -ne '89504E470D0A1A0A' -or $pngWidth -ne 768 -or $pngHeight -ne 960){throw "Portrait PNG dimensions/signature differ from accepted art: $card"}
}

# Numerical skin/export checks do not certify equipment fit or art quality.
# Bind explicit human inspection of all portraits and all 54 source-action
# frames to the current authored source and exact inspected PNG bytes.
$modelArtReview=Read-ModelInput $ModelReviewNotes
if($modelArtReview.passed -ne $true -or $modelArtReview.version -ne $Version -or $modelArtReview.modelSourceSha256 -ne $modelAssets.source.sha256 -or
   @($modelArtReview.portraits).Count -ne 14 -or @($modelArtReview.poses).Count -ne 54){throw 'The current authored source requires physical review of all fourteen portraits and fifty-four action poses.'}
$modelArtSourcePaths=@('Assets/asset_manifest.json',$modelAssets.source.file,'Build/render_animation_review.py','Build/asset_contact_sheet.py','Build/render_card_portraits.py')
foreach($path in $modelArtSourcePaths){
    $source=@($modelArtReview.sourcePins | Where-Object path -eq $path)
    if($source.Count -ne 1){throw "Physical art review lacks its actual source pin: $path"}
}
foreach($source in $modelArtReview.sourcePins){
    $pin=Pin-ModelInput $source.path
    if($pin.sha256 -ne $source.sha256 -or $pin.bytes -ne $source.bytes){throw 'A source used for physical art review changed after inspection.'}
}
foreach($card in $modelCardIds){
    $note=@($modelArtReview.portraits | Where-Object cardId -eq $card)
    $art=$modelAssets.illustrations.$card
    if($note.Count -ne 1 -or $note[0].reviewed -ne $true -or [string]::IsNullOrWhiteSpace($note[0].observation) -or $note[0].path -ne $art.file -or
       $note[0].sha256 -ne $art.sha256 -or $note[0].bytes -ne $art.bytes){throw "Physical card-art inspection does not pin this source portrait: $card"}
}
$modelPoseKeys=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach($character in $modelAssets.characters.PSObject.Properties){
    $poses=@('Idle','Locomotion','Attack','Death')
    if($character.Value.rigFamily -eq 'humanoid'){$poses+=@('Attack_25','Attack_80')}
    foreach($pose in $poses){
        $null=$modelPoseKeys.Add($character.Name+'|'+$pose)
        $note=@($modelArtReview.poses | Where-Object {$_.modelId -eq $character.Name -and $_.pose -eq $pose})
        if($note.Count -ne 1 -or $note[0].reviewed -ne $true -or [string]::IsNullOrWhiteSpace($note[0].observation) -or
           $note[0].path -ne ('Assets/Renders/Poses/'+$character.Name+'_'+$pose+'.png')){throw "Missing physical action/equipment inspection: $($character.Name) $pose"}
        $pin=Pin-ModelInput $note[0].path
        if($pin.sha256 -ne $note[0].sha256 -or $pin.bytes -ne $note[0].bytes){throw 'An action-pose PNG changed after physical inspection.'}
        $png=[IO.File]::ReadAllBytes((Resolve-ModelInput $pin.path))
        if($png.Length -lt 24){throw 'An inspected action-pose PNG is truncated.'}
        $width=([int]$png[16] -shl 24) -bor ([int]$png[17] -shl 16) -bor ([int]$png[18] -shl 8) -bor [int]$png[19]
        $height=([int]$png[20] -shl 24) -bor ([int]$png[21] -shl 16) -bor ([int]$png[22] -shl 8) -bor [int]$png[23]
        if([Convert]::ToHexString([byte[]]$png[0..7]) -ne '89504E470D0A1A0A' -or $width -ne 640 -or $height -ne 640){throw 'An inspected action pose is not the actual 640-by-640 renderer output.'}
    }
}
if($modelPoseKeys.Count -ne 54 -or @($modelArtReview.poses | Where-Object {!$modelPoseKeys.Contains($_.modelId+'|'+$_.pose)}).Count -ne 0){throw 'Physical pose inventory does not exactly cover the production characters and required action frames.'}
foreach($path in @('Assets/Renders/characters_contact.png','Assets/Renders/environment_contact.png','Assets/Renders/model_cards_contact.png','Assets/Renders/equipment_contact.png')){
    $note=@($modelArtReview.contactSheets | Where-Object path -eq $path)
    if($note.Count -ne 1){throw "Physical art review lacks the final contact sheet: $path"}
}
if(@($modelArtReview.contactSheets.path | Sort-Object -Unique).Count -ne @($modelArtReview.contactSheets).Count){throw 'Physical art review contains duplicate contact-sheet paths.'}
foreach($note in $modelArtReview.contactSheets){
    if($note.reviewed -ne $true -or [string]::IsNullOrWhiteSpace($note.observation)){throw "Physical art review lacks inspection of its contact sheet: $($note.path)"}
    $pin=Pin-ModelInput $note.path
    if($pin.sha256 -ne $note.sha256 -or $pin.bytes -ne $note.bytes){throw 'A final art contact sheet changed after inspection.'}
}
foreach($input in $modelArtReview.inputs){
    $pin=Pin-ModelInput $input.path
    if($pin.sha256 -ne $input.sha256 -or $pin.bytes -ne $input.bytes){throw 'Physical model-review input changed after inspection.'}
}
$modelAssetImport=Read-ModelInput 'Artifacts/QA/unreal_asset_import.json'
$modelAssetAudit=Read-ModelInput 'Artifacts/QA/unreal_asset_audit.json'
if($modelAssetImport.schema -ne 2 -or $modelAssetAudit.schema -ne 2 -or $modelAssetImport.passed -ne $true -or $modelAssetAudit.passed -ne $true -or
   $modelAssetImport.mode -notin @('ModelsOnly','Full') -or !$modelAssetImport.validation -or !$modelAssetAudit.nativeValidation -or @($modelAssetImport.errors | Where-Object {$_}).Count -ne 0 -or @($modelAssetImport.validation.errors | Where-Object {$_}).Count -ne 0 -or @($modelAssetAudit.errors | Where-Object {$_}).Count -ne 0 -or
   @($modelAssetAudit.nativeValidation.errors | Where-Object {$_}).Count -ne 0 -or @($modelAssetAudit.nativeValidation.cards).Count -ne 14 -or @($modelAssetImport.validation.cards).Count -ne 14){throw 'Actual imported model/card bindings must pass the native asset audit.'}
foreach($operation in @('import','audit')){
    $run=Read-ModelInput ('Artifacts/QA/unreal_asset_'+$operation+'_run.json')
    $report=$(if($operation -eq 'import'){$modelAssetImport}else{$modelAssetAudit})
    $script=$(if($operation -eq 'import'){'Build/import_unreal_assets.py'}else{'Build/inspect_unreal_assets.py'})
    $started=Get-ModelUtcTimestamp $run.startedUTC
    $finished=Get-ModelUtcTimestamp $run.finishedUTC
    $generated=Get-ModelUtcTimestamp $report.generatedUTC
    if($run.passed -ne $true -or $run.version -ne $Version -or $run.operation -ne $operation -or $run.exitCode -ne 0 -or
       $run.timedOut -ne $false -or $run.engineErrorCount -ne 0 -or $started -ge $finished -or $generated -lt $started -or $generated -gt $finished -or
       $run.script.file -ne $script -or $run.report.file -ne ('Artifacts/QA/unreal_asset_'+$operation+'.json') -or
       $run.engineLog.file -ne ('Artifacts/QA/model130-'+$operation+'-final/engine.log') -or
       $run.module.file -ne 'Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll' -or
       $run.editor.sha256 -notmatch '^[a-f0-9]{64}$' -or $run.editor.bytes -le 0){throw 'Final import/audit must have a fresh successful actual commandlet run with its observed Editor identity.'}
    foreach($record in @($run.script,$run.report,$run.engineLog)){$null=Assert-ModelRecord $record ('Actual '+$operation+' commandlet input/output')}
    $modulePath=Resolve-ModelInput $run.module.file
    $moduleContext=@($modelContext.runtimeModules | Where-Object path -eq $run.module.file)
    if($moduleContext.Count -ne 1 -or $run.module.sha256 -ne $moduleContext[0].sha256 -or
       (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $run.module.sha256 -or
       (Get-Item -LiteralPath $modulePath).Length -ne $run.module.bytes){throw 'Observed asset commandlet runtime differs from the complete-suite context/current module.'}
    $written=(Get-Item -LiteralPath (Resolve-ModelInput $run.report.file)).LastWriteTimeUtc
    if($written -lt $started -or $written -gt $finished){throw 'Commandlet report was not freshly written during its observed execution.'}
    $log=Get-Content -LiteralPath (Resolve-ModelInput $run.engineLog.file) -Raw
    if(@([regex]::Matches($log,'(?im)(^|\])\s*\S+:\s*(Error|Fatal):')).Count -ne 0){throw 'An accepted asset commandlet log contains an engine error/fatal.'}
    # Editor/module identities are provenance. Workspace script/report/log
    # records become archive inputs without duplicating runtime DLL bytes.
}
$modelRawExpected=@((Get-ModelFileRecords $modelAssets | Where-Object {$_.file -match '\.fbx$'}).file) + @($modelAssets.illustrations.PSObject.Properties.Value.file) +
    @('BaseColor','Normal','ORM','TeamMask' | ForEach-Object {'Assets/Source/Textures/T_RiftAtlas_'+$_.ToString()+'.png'})
$modelRawExpected=@($modelRawExpected | Sort-Object -Unique)
foreach($record in @(@{report=$modelAssetImport;script='Build/import_unreal_assets.py'},@{report=$modelAssetAudit;script='Build/inspect_unreal_assets.py'})){
    $provenance=$record.report.provenance
    if($provenance.schema -ne 1 -or $provenance.build -ne $modelAssets.build -or $provenance.sourcesUnchanged -ne $true -or
       $provenance.assetManifest.file -ne 'Assets/asset_manifest.json' -or $provenance.sourceBlend.file -ne $modelAssets.source.file -or $provenance.sourceBlend.sha256 -ne $modelAssets.source.sha256 -or
       $provenance.script.file -ne $record.script -or $provenance.helperScript.file -ne 'Build/asset_import_provenance.py' -or
       ((@($provenance.rawSources.file | Sort-Object) -join '|') -ne ($modelRawExpected -join '|'))){throw 'Import/readback provenance does not identify the complete current production forge.'}
    foreach($source in @($provenance.assetManifest,$provenance.sourceBlend,$provenance.script,$provenance.helperScript) + @($provenance.rawSources)){
        $null=Assert-ModelRecord $source 'Final imported source provenance'
    }
}
foreach($path in $modelRawExpected){
    $sources=@($modelAssetImport.sourceImports | Where-Object {$_.source.file -eq $path -and $_.succeeded -eq $true -and @($_.assets).Count -gt 0})
    if($sources.Count -ne 1){throw "The successful import did not observe this production source exactly once: $path"}
}
foreach($source in $modelAssetImport.sourceImports){
    if($source.succeeded -ne $true -or @($source.assets).Count -lt 1){throw 'A raw-source import was incomplete.'}
    $null=Assert-ModelRecord $source.source 'Actual raw import task'
}
$modelExpectedAssets=@{}
foreach($character in $modelAssets.characters.PSObject.Properties){
    $name=$character.Name;$destination='/Game/Rift/Characters/'+$name
    $modelExpectedAssets[$destination+'/SK_'+$name+'.SK_'+$name]=$character.Value.mesh.file
    foreach($clip in $character.Value.animations){$modelExpectedAssets[$destination+'/Animations/'+$clip.action+'.'+$clip.action]=$clip.file}
}
foreach($prop in $modelAssets.statics.PSObject.Properties){$modelExpectedAssets['/Game/Rift/Environment/SM_'+$prop.Name+'.SM_'+$prop.Name]=$prop.Value.file}
foreach($art in $modelAssets.illustrations.PSObject.Properties){$modelExpectedAssets['/Game/Rift/CardArt/T_Card_'+$art.Name+'.T_Card_'+$art.Name]=$art.Value.file}
foreach($suffix in @('BaseColor','Normal','ORM','TeamMask')){$modelExpectedAssets['/Game/Rift/Textures/T_RiftAtlas_'+$suffix+'.T_RiftAtlas_'+$suffix]='Assets/Source/Textures/T_RiftAtlas_'+$suffix+'.png'}
$modelReadback=$modelAssetAudit.sourceReadback
if($modelReadback.schema -ne 1 -or $modelReadback.passed -ne $true -or @($modelReadback.errors | Where-Object {$_}).Count -ne 0 -or
   @($modelReadback.assets).Count -ne $modelExpectedAssets.Count -or @($modelReadback.assets.asset | Sort-Object -Unique).Count -ne $modelExpectedAssets.Count -or
   @($modelReadback.cards).Count -ne 14){throw 'Actual persisted import fingerprints/metadata were not completely inspected.'}
foreach($asset in $modelReadback.assets){
    if(!$modelExpectedAssets.ContainsKey($asset.asset) -or $asset.matches -ne $true -or $asset.assetImportDataAvailable -ne $true -or
       $asset.expectedSource.file -ne $modelExpectedAssets[$asset.asset] -or @($asset.importedSources).Count -lt 1 -or $asset.expectedSource.file -notin $asset.importedSources.file){throw 'Persisted AssetImportData does not identify the required production asset source.'}
    $null=Assert-ModelRecord $asset.expectedSource 'Persisted primary import source'
    foreach($source in $asset.importedSources){
        $pin=Assert-ModelRecord $source 'Persisted imported raw source'
        if($source.matches -ne $true -or $source.storedMD5 -notmatch '^[a-f0-9]{32}$' -or
           $source.storedMD5 -ne (Get-FileHash -LiteralPath (Resolve-ModelInput $pin.path) -Algorithm MD5).Hash.ToLowerInvariant()){throw 'Saved UE import fingerprint differs from the current raw bytes.'}
    }
}
foreach($card in $modelCardIds){
    $metadata=@($modelReadback.cards | Where-Object cardId -eq $card)
    if($metadata.Count -ne 1 -or $metadata[0].matches -ne $true -or $metadata[0].sourceManifest -ne $modelAssets.source.sha256 -or
       $metadata[0].portraitSha256 -ne $modelAssets.illustrations.$card.sha256){throw 'Saved card metadata identifies an older model source or portrait.'}
}
# Keep a complete hash inventory of the imported assets without duplicating
# more than 100 MiB of editor mesh data already present in the repository and
# frozen game payload. The evidence ZIP carries every original FBX export,
# authored source PNG, and the small UFont/card-binding assets themselves.
$modelImportedAssetPins=[Collections.Generic.List[object]]::new()
foreach($directory in @('Unreal/RiftCrownArena/Content/Rift/Characters','Unreal/RiftCrownArena/Content/Rift/CardArt','Unreal/RiftCrownArena/Content/Rift/Cards','Unreal/RiftCrownArena/Content/Rift/Environment','Unreal/RiftCrownArena/Content/Rift/Fonts')){
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $modelRepo $directory) -Filter '*.uasset' -File -Recurse){
        $resolved=Resolve-ModelInput $file.FullName
        $modelImportedAssetPins.Add([pscustomobject][ordered]@{path=[IO.Path]::GetRelativePath($modelRepo,$resolved).Replace('\','/');sha256=(Get-FileHash -LiteralPath $resolved).Hash.ToLowerInvariant();bytes=$file.Length})
        if($directory -match '/(Cards|Fonts)$'){$null=Pin-ModelInput $resolved}
    }
}
foreach($texture in @('BaseColor','Normal','ORM','TeamMask')){$null=Pin-ModelInput ('Assets/Source/Textures/T_RiftAtlas_'+$texture+'.png')}

$modelFontRoot='Assets/Fonts/BarlowSemiCondensed/'
$modelFontSources=Read-ModelInput ($modelFontRoot+'provenance.json')
$modelFontImport=Read-ModelInput 'Artifacts/QA/font-import.json'
if($modelFontSources.family -ne 'Barlow Semi Condensed' -or $modelFontSources.license -ne 'SIL Open Font License 1.1' -or $modelFontImport.passed -ne $true -or
   $modelFontImport.sourceCommit -ne $modelFontSources.commit -or $modelFontImport.font -ne '/Game/Rift/Fonts/F_RiftUI.F_RiftUI' -or @($modelFontSources.files).Count -ne 5 -or @($modelFontImport.faces).Count -ne 4){throw 'The professional font import must retain all four embedded faces and pinned licensing.'}
foreach($file in $modelFontSources.files){
    $pin=Pin-ModelInput ($modelFontRoot+$file.name)
    if($pin.sha256 -ne $file.sha256 -or $pin.bytes -ne $file.bytes){throw 'Bundled font source differs from its upstream provenance.'}
}
foreach($weight in @('Regular','Medium','SemiBold','Bold')){
    $face=@($modelFontImport.faces | Where-Object weight -eq $weight)
    $source=@($modelFontSources.files | Where-Object name -eq ('BarlowSemiCondensed-'+$weight+'.ttf'))
    if($face.Count -ne 1 -or $source.Count -ne 1 -or $face[0].sourceSha256 -ne $source[0].sha256 -or $face[0].loadingPolicy -ne 'Inline' -or
       $face[0].path -ne ('/Game/Rift/Fonts/FF_BarlowSemiCondensed_'+$weight+'.FF_BarlowSemiCondensed_'+$weight)){throw 'A named font face is missing, external, or incorrectly sourced.'}
}
$modelOFL=Pin-ModelInput ($modelFontRoot+'OFL.txt')
$modelStagedLicense=Pin-ModelInput 'Unreal/RiftCrownArena/Content/Rift/Fonts/Barlow-OFL.txt'
$modelLicenseRelative='RiftCrownArena/Content/Rift/Fonts/Barlow-OFL.txt'
$modelPackagedLicense=Pin-ModelInput (Join-Path $modelPackageRoot $modelLicenseRelative)
$modelLicenseEntry=@($modelManifest.files | Where-Object path -eq $modelLicenseRelative)
if($modelLicenseEntry.Count -ne 1 -or $modelLicenseEntry[0].sha256 -ne $modelOFL.sha256 -or $modelLicenseEntry[0].size -ne $modelOFL.bytes -or
   $modelPackagedLicense.sha256 -ne $modelOFL.sha256 -or $modelPackagedLicense.bytes -ne $modelOFL.bytes -or $modelStagedLicense.sha256 -ne $modelOFL.sha256){throw 'The actual Windows payload must contain the complete readable font license.'}

$modelReview=Read-ModelInput $ReviewNotes
if($modelReview.version -ne $Version -or !$modelReview.passed -or $modelReview.nativeExecutableSha256 -ne $modelNativeHash -or $CaptureNames.Count -lt 9 -or
   @($CaptureNames | Sort-Object -Unique).Count -ne $CaptureNames.Count){throw 'At least nine distinct current native captures need explicit physical review.'}
foreach($name in $CaptureNames){
    if($name -notmatch '^model130-[a-z0-9-]+$'){throw 'Select explicit current model/input release captures.'}
    $run=Read-ModelInput ('Artifacts/QA/Visual/'+$name+'.json')
    $state=Read-ModelInput ('Artifacts/QA/Visual/'+$name+'.state.json')
    $pin=Pin-ModelInput ('Artifacts/QA/Visual/'+$name+'.png')
    if($run.executableSha256 -ne $modelNativeHash -or $run.editor -ne $false -or !$run.stateCaptured -or !$run.captured -or $run.timedOut -or !$run.resolutionMatches -or
       !$run.cameraFramingPassed -or $run.exitCode -ne 0 -or @($run.errors).Count -ne 0 -or $run.allowExternalInput -ne $false -or !$state.captureInput.active -or !$state.captureInput.consuming){throw "Capture does not belong to the accepted native package: $name"}
    if($run.cameraFramingRequired -and (!$run.modelEnvelopeAvailable -or !$run.modelEnvelopePassed -or !$state.cameraFraming.passed -or !$state.cameraFraming.modelEnvelopePassed)){throw "Battle silhouettes/arena are not inside the UI-safe framing: $name"}
    $note=@($modelReview.captures | Where-Object name -eq $name)
    if($note.Count -ne 1 -or !$note[0].reviewed -or [string]::IsNullOrWhiteSpace($note[0].observation) -or $note[0].pngSha256 -ne $pin.sha256){throw "Physical inspection does not pin this actual frame: $name"}
    $null=Assert-ModelShippingDiagnostics $run ('Native capture '+$name)
}
$modelDrag=Read-ModelInput $DragSmokeReport
Assert-ModelChecks $modelDrag 29 'Actual native card drag routing'
$modelDragWords=($modelDrag.checks | ForEach-Object {$_.name+' '+$_.detail}) -join ' '
if($modelDrag.version -ne $Version -or $modelDrag.executableSha256 -ne $modelNativeHash -or $modelDrag.editor -ne $false -or !$modelDrag.freshReport -or $modelDrag.timedOut -or
   $modelDrag.exitCode -ne 0 -or $modelDrag.checkCount -ne 29 -or @($modelDrag.checks).Count -ne 29 -or $modelDragWords -notmatch '(?i)Slate' -or
   $modelDrag.scope -notmatch '(?i)Real Slate mouse routing through the production UMG hand' -or $modelDragWords -notmatch '(?i)(return|cancel)' -or
   $modelDragWords -notmatch '(?i)(deploy|play|commit)'){throw 'Drag smoke must prove all twenty-nine actual packaged Slate pointer-routing, deployment and return/cancel checks.'}
$modelDragRaw=Read-ModelInput $modelDrag.report
$modelDragRawPin=$modelPinIndex[$modelDrag.report]
if($modelDragRawPin.sha256 -ne $modelDrag.nativeReportSha256 -or $modelDragRaw.passed -ne $true -or $modelDragRaw.version -ne $Version -or $modelDragRaw.checkCount -ne 29 -or
   @($modelDragRaw.checks).Count -ne 29 -or @($modelDragRaw.checks | Where-Object {$_.passed -ne $true}).Count -ne 0 -or
   $modelDragRaw.route -notmatch 'FSlateApplication ProcessMouseButtonDownEvent / ProcessMouseMoveEvent / ProcessMouseButtonUpEvent'){throw 'Normalized drag checks lack their actual complete native pointer-routing report.'}
$modelDragRecords=(Assert-ModelShippingDiagnostics $modelDrag 'Native pointer routing').records
$modelDragPassRecords=@($modelDragRecords|Where-Object {$_.message -match '^Card drag route: PASS '})
if($modelDragPassRecords.Count -ne 29 -or @($modelDragRecords|Where-Object {$_.message -match '^Card drag route: FAIL '}).Count -ne 0 -or
   @($modelDragRecords|Where-Object {$_.message -match '^Native card drag smoke passed: '}).Count -ne 1){throw 'Structured native diagnostics do not contain all twenty-nine actual passing checks and final completion.'}
foreach($check in $modelDragRaw.checks){
    if(@($modelDragPassRecords|Where-Object message -eq ('Card drag route: PASS '+$check.name)).Count -ne 1){throw 'Raw pointer-routing report and native diagnostic check names differ.'}
}
foreach($input in $modelDrag.sourcePins){
    if($input.path -in @('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll')){
        $path=Resolve-ModelInput $input.path
        $runtime=@($modelContext.runtimeModules | Where-Object path -eq $input.path)
        if($runtime.Count -ne 1 -or $runtime[0].sha256 -ne $input.sha256 -or
           (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $input.sha256 -or
           (Get-Item -LiteralPath $path).Length -ne $input.bytes){throw 'Actual pointer-routing runtime differs from the complete-suite context/current module.'}
        continue
    }
    $pin=Pin-ModelInput $input.path
    if($pin.sha256 -ne $input.sha256 -or $pin.bytes -ne $input.bytes){throw 'Production card drag source changed after the native routing smoke.'}
}
foreach($input in $modelDrag.inputs){
    $pin=Pin-ModelInput $input.path
    if($pin.sha256 -ne $input.sha256 -or ($input.bytes -and $pin.bytes -ne $input.bytes)){throw 'Raw pointer-routing evidence changed after its native smoke run.'}
}

$modelPerfRoot='Artifacts/QA/Performance/'+$PerformanceName+'/'
$modelPerfRun=Read-ModelInput ($modelPerfRoot+'run.json')
$modelPerf=Read-ModelInput ($modelPerfRoot+'performance.json')
if(!$modelPerfRun.passed -or $modelPerfRun.exitCode -ne 0 -or $modelPerfRun.executableSha256 -ne $modelNativeHash -or $modelPerfRun.editor -ne $false -or $modelPerf.frame.samples -lt 100 -or
   $modelPerfRun.reportSha256 -ne $modelPinIndex[$modelPerfRoot+'performance.json'].sha256){throw 'Current native performance execution and its pinned metric report must pass.'}
$modelAudioRun=Read-ModelInput ('Artifacts/QA/Audio/'+$AudioName+'/run.json')
$modelAudio=Read-ModelInput ('Artifacts/QA/Audio/'+$AudioName+'/audio-smoke.json')
$modelVFX=Read-ModelInput ('Artifacts/QA/'+$VFXName+'-verification.json')
if(!$modelAudio.passed -or !$modelAudioRun.passed -or $modelAudioRun.exitCode -ne 0 -or $modelAudioRun.editor -ne $false -or $modelAudioRun.audioDisabled -ne $false -or $modelAudioRun.executableSha256 -ne $modelNativeHash -or
   !$modelVFX.passed -or $modelVFX.executableSha256 -ne $modelNativeHash){throw 'Current actual native audio and VFX verification must pass.'}
$null=Assert-ModelShippingDiagnostics $modelPerfRun 'Native performance'
$null=Assert-ModelShippingDiagnostics $modelAudioRun 'Native audio'
foreach($path in @('Artifacts/QA/launcher-play-smoke.json','Artifacts/QA/installer-build.json','Artifacts/QA/installer-tests.json','Artifacts/QA/app-local-runtime.json')){
    $report=Read-ModelInput $path
    if(!$report.passed){throw "Windows delivery check failed: $path"}
}
$modelPlay=Get-Content -LiteralPath (Resolve-ModelInput 'Artifacts/QA/launcher-play-smoke.json') -Raw | ConvertFrom-Json
$modelMSIBuild=Get-Content -LiteralPath (Resolve-ModelInput 'Artifacts/QA/installer-build.json') -Raw | ConvertFrom-Json
$modelMSITest=Get-Content -LiteralPath (Resolve-ModelInput 'Artifacts/QA/installer-tests.json') -Raw | ConvertFrom-Json
$modelMSIHash=(Get-FileHash -LiteralPath (Resolve-ModelInput 'Artifacts/Release/RiftCrownArena-Setup.msi')).Hash.ToLowerInvariant()
if($modelPlay.installed -ne $Version -or $modelPlay.nativeSha256 -ne $modelNativeHash -or $modelPlay.verifiedGameFiles -ne $modelManifest.files.Count -or !$modelPlay.legacySavePreserved -or
   $modelMSIBuild.version -ne $Version -or $modelMSIBuild.sha256 -ne $modelMSIHash -or $modelMSIBuild.gameArchiveSha256 -ne $modelManifest.sha256 -or
   $modelMSITest.installedVersion -ne $Version -or $modelMSITest.upgradeMsiSha256 -ne $modelMSIHash -or $modelMSITest.installedArchiveSha256 -ne $modelManifest.sha256 -or $modelMSITest.checkCount -lt 51 -or
   $modelRelease.installerSha256 -ne $modelMSIHash -or $modelRelease.archiveSha256 -ne $modelManifest.sha256){throw 'Current-version Windows delivery identity and provenance differ.'}

foreach($path in $ReleaseDocuments + @('Docs/UI_TYPOGRAPHY.md','Build/Package-ModelInputQAEvidence.ps1','Build/upgrade_card_models.py','Build/generate_assets.py',
    'Build/validate_assets.py','Build/render_card_portraits.py','Build/validate_card_portraits.py','Build/render_animation_review.py','Build/asset_contact_sheet.py','Build/import_unreal_assets.py','Build/inspect_unreal_assets.py','Build/asset_import_provenance.py',
    'Build/import_unreal_fonts.py','Build/Test-Unreal.ps1','Build/Curate-UnrealQA.ps1','Build/Capture-Unreal.ps1','Build/Measure-Unreal.ps1','Build/Test-UnrealAudio.ps1','Build/Test-UnrealVFX.ps1','Build/ShippingDiagnostics.ps1',
    'Build/Test-LauncherPlay.ps1','Build/Test-CardDrag.ps1','Installer/Test-Installer.ps1','Build/Tests/Audit-NativeMeta.py')){$null=Pin-ModelInput $path}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $modelRepo 'Build') -Filter '*Input*.ps1' -File){$null=Pin-ModelInput $file.FullName}
$modelUnique=@($modelInputs | Sort-Object path -Unique)
foreach($required in @('Docs/QA/native-integration.json','Docs/QA/historical-native-integration-1.2.1.json','Docs/QA/spell-timing-meta-100.json','Assets/asset_manifest.json',
    'Assets/asset_qa_report.json','Artifacts/QA/model-card-portraits.json','Artifacts/QA/font-import.json','Artifacts/QA/unreal_asset_import_run.json','Artifacts/QA/unreal_asset_audit_run.json','Artifacts/QA/installer-tests.json','Build/Package-ModelInputQAEvidence.ps1')){
    if(@($modelUnique | Where-Object path -eq $required).Count -ne 1){throw "Required evidence was lost while deduplicating inputs: $required"}
}
$modelTotalBytes=($modelUnique | Measure-Object bytes -Sum).Sum
if($modelTotalBytes -gt 200MB){throw 'Selected evidence exceeds the 200 MiB source/asset budget; select and disclose a smaller set before packaging.'}
$modelSummary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Authored model/equipment fit, fourteen model-rendered card portraits, embedded UI typography, card drag/return routing, native regression and Windows delivery';
    nativeExecutableSha256=$modelNativeHash;modelBuild=$modelAssets.build;modelSourceSha256=$modelAssets.source.sha256;portraitCount=14;fontFamily=$modelFontSources.family;fontWeights=@('Regular','Medium','SemiBold','Bold');
    dragChecks=$modelDrag.checks.Count;integrationTests=$modelIntegration.tests.Count;captureNames=$CaptureNames;physicalSourcePortraits=14;physicalSourceActionPoses=54;modelReview=(Pin-ModelInput $ModelReviewNotes).path;performance=$modelPerf;inputs=$modelUnique;
    importedAssetPins=$modelImportedAssetPins.ToArray();importedAssetInventoryScope='Original imported asset hashes. Editor mesh/animation/texture bytes remain in the maintained repository and frozen native package; this evidence archive includes all raw FBX exports, authored source PNGs, and UFont/card-binding assets.';
    inheritedRulesAudit=[ordered]@{version='1.2.1';games=100;fingerprint=$modelMeta.fingerprint;runtimeSha256=$modelMeta.evidence.runtimeSha256;allSixAuthoritativeSourcesUnchanged=$true;newBalanceCohortRun=$false;historical10000GamesAreCurrent=$false};
    publicDelivery='Pending publication; anonymous download and published-update reports are separate postpublication evidence.'}
$modelSummaryPath=Join-Path $modelRepo $modelSummaryRelative
foreach($pin in $modelImportedAssetPins){
    $source=Resolve-ModelInput $pin.path
    if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $pin.sha256 -or (Get-Item -LiteralPath $source).Length -ne $pin.bytes){throw 'Imported assets changed during acceptance; rebuild and rerun the native checks.'}
}
[IO.File]::WriteAllText($modelSummaryPath,($modelSummary|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
$modelTemporary=$modelZip+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
Add-Type -AssemblyName System.IO.Compression
$modelArchive=[IO.Compression.ZipFile]::Open($modelTemporary,[IO.Compression.ZipArchiveMode]::Create)
$modelEntryInventory=[Collections.Generic.List[object]]::new()
try{
    foreach($pin in $modelUnique){
        $source=Resolve-ModelInput $pin.path
        if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $pin.sha256 -or (Get-Item -LiteralPath $source).Length -ne $pin.bytes){throw "Evidence changed while packaging: $($pin.path)"}
        if([IO.Path]::GetExtension($source) -in @('.json','.log')){
            $raw=Get-Content -LiteralPath $source -Raw
            if($pin.path -in @('Artifacts/QA/unreal_asset_import_run.json','Artifacts/QA/unreal_asset_audit_run.json')){
                $run=$raw|ConvertFrom-Json -Depth 100
                $run.editor.PSObject.Properties.Remove('file');$run.editor.PSObject.Properties.Remove('path')
                if(!$run.editor.PSObject.Properties['name']){$run.editor|Add-Member -NotePropertyName name -NotePropertyValue 'UnrealEditor-Cmd.exe'}
                $raw=$run|ConvertTo-Json -Depth 100
            }
            $raw=$raw.Replace($modelRepo.Replace('\','\\'),'.').Replace($modelRepo.Replace('\','/'),'.').Replace($modelRepo,'.')
            if($env:USERPROFILE){$raw=$raw.Replace($env:USERPROFILE.Replace('\','\\'),'%USERPROFILE%').Replace($env:USERPROFILE.Replace('\','/'),'%USERPROFILE%').Replace($env:USERPROFILE,'%USERPROFILE%')}
            $raw=[regex]::Replace($raw,'("deviceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation"')
            $raw=[regex]::Replace($raw,'("instanceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation instance"')
            if([IO.Path]::GetExtension($source) -eq '.json'){$null=$raw|ConvertFrom-Json -Depth 100}
            $entry=$modelArchive.CreateEntry($pin.path,[IO.Compression.CompressionLevel]::Optimal)
            $writer=[IO.StreamWriter]::new($entry.Open(),[Text.UTF8Encoding]::new($false))
            try{$writer.Write($raw)}finally{$writer.Dispose()}
            $entryBytes=[Text.UTF8Encoding]::new($false).GetBytes($raw)
            $entryHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($entryBytes)).ToLowerInvariant()
            $modelEntryInventory.Add([pscustomobject][ordered]@{entry=$pin.path;sha256=$entryHash;bytes=$entryBytes.Length;sourceSha256=$pin.sha256;sourceBytes=$pin.bytes;sanitized=$true})
        }else{
            $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($modelArchive,$source,$pin.path,[IO.Compression.CompressionLevel]::Optimal)
            $modelEntryInventory.Add([pscustomobject][ordered]@{entry=$pin.path;sha256=$pin.sha256;bytes=$pin.bytes;sourceSha256=$pin.sha256;sourceBytes=$pin.bytes;sanitized=$false})
        }
    }
    $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($modelArchive,$modelSummaryPath,$modelSummaryRelative,[IO.Compression.CompressionLevel]::Optimal)
    $modelEntryInventory.Add([pscustomobject][ordered]@{entry=$modelSummaryRelative;sha256=(Get-FileHash -LiteralPath $modelSummaryPath).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $modelSummaryPath).Length;sanitized=$false})
    $inventoryText=[ordered]@{schema=1;version=$Version;entries=$modelEntryInventory.ToArray()}|ConvertTo-Json -Depth 20
    $inventoryEntry=$modelArchive.CreateEntry('evidence-inventory.json',[IO.Compression.CompressionLevel]::Optimal)
    $inventoryWriter=[IO.StreamWriter]::new($inventoryEntry.Open(),[Text.UTF8Encoding]::new($false))
    try{$inventoryWriter.Write($inventoryText)}finally{$inventoryWriter.Dispose()}
    $inventoryBytes=[Text.UTF8Encoding]::new($false).GetBytes($inventoryText)
    $modelEntryInventory.Add([pscustomobject][ordered]@{entry='evidence-inventory.json';sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($inventoryBytes)).ToLowerInvariant();bytes=$inventoryBytes.Length;sanitized=$false})
}finally{$modelArchive.Dispose()}
$modelArchive=[IO.Compression.ZipFile]::OpenRead($modelTemporary)
try{
    if($modelArchive.Entries.Count -ne $modelEntryInventory.Count){throw 'Unexpected evidence archive entry count.'}
    $modelSeen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $modelArchive.Entries){if(!$modelSeen.Add($entry.FullName)){throw 'Duplicate evidence archive entry.'}}
    foreach($pin in $modelEntryInventory){
        $entry=$modelArchive.GetEntry($pin.entry)
        if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Evidence archive entry size mismatch.'}
        $stream=$entry.Open()
        try{$entryHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($entryHash -ne $pin.sha256){throw 'Evidence archive entry hash mismatch.'}
    }
}finally{$modelArchive.Dispose()}
if(Test-Path -LiteralPath $modelZip){throw 'A frozen archive appeared while acceptance ran; the verified temporary archive is retained.'}
Move-Item -LiteralPath $modelTemporary -Destination $modelZip
$modelZipHash=(Get-FileHash -LiteralPath $modelZip).Hash.ToLowerInvariant()
$modelSumPath=Join-Path $modelRepo 'Artifacts/Release/SHA256SUMS.txt'
$modelZipName=[IO.Path]::GetFileName($modelZip)
$modelSumLines=@(Get-Content -LiteralPath $modelSumPath | Where-Object {$_ -notmatch ('\s{2}'+[regex]::Escape($modelZipName)+'$')})
$modelSumLines+=($modelZipHash+'  '+$modelZipName)
[IO.File]::WriteAllLines($modelSumPath,$modelSumLines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($modelEntryInventory.Count) verified evidence entries: $modelZipHash"
