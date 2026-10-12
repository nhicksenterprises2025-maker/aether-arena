param(
    [string[]]$CardIds=@(),
    [switch]$ArenaOnly,
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$riftRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).Replace('\','/')
$riftManifest=Get-Content -LiteralPath "$riftRoot/Assets/asset_manifest.json" -Raw|ConvertFrom-Json -Depth 100
if($ArenaOnly -and $CardIds.Count){throw 'ArenaOnly cannot be combined with model imports.'}
if(!$ArenaOnly -and (!$CardIds.Count -or @($CardIds|Select-Object -Unique).Count -ne $CardIds.Count)){throw 'Select one or more unique model or spell portrait IDs.'}
foreach($riftId in $CardIds){if($riftId -notmatch '^[a-z0-9_]+$' -or (!$riftManifest.characters.PSObject.Properties[$riftId] -and !($riftId -eq 'shadow_surge' -and $riftManifest.illustrations.PSObject.Properties[$riftId]))){throw "Unknown selected asset ID: $riftId"}}
$riftOutput="$riftRoot/Artifacts/QA/ExpansionImport/import-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $riftOutput -Force|Out-Null
$riftReportPath="$riftOutput/import.json"
$riftBefore=@(Get-ChildItem -LiteralPath "$riftRoot/Unreal/RiftCrownArena/Content" -File -Recurse|
    Where-Object {$_.Extension -in @('.uasset','.umap')}|ForEach-Object {
        [ordered]@{path=[IO.Path]::GetRelativePath($riftRoot,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
$riftModules=@('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll')|ForEach-Object {
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath "$riftRoot/$_" -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$riftSourcePaths=@(Get-ChildItem -LiteralPath "$riftRoot/Unreal/RiftCrownArena/Source","$riftRoot/Unreal/RiftCrownArena/Config" -File -Recurse | ForEach-Object FullName)+@(
    "$riftRoot/Build/Import-SelectedAssets.ps1","$riftRoot/Build/import_unreal_assets.py","$riftRoot/Build/asset_import_provenance.py",
    "$riftRoot/Build/generate_card_manifest.py","$riftRoot/Assets/Data/card_manifest.json","$riftRoot/Assets/asset_manifest.json")
$riftSourcePins=@($riftSourcePaths|Sort-Object -Unique|ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($riftRoot,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$riftPreviousReport=$env:RIFT_IMPORT_REPORT_PATH
$env:RIFT_IMPORT_REPORT_PATH=$riftReportPath
$riftStarted=[DateTime]::UtcNow
try {
    $riftImportMode = if($ArenaOnly){'-RiftArenaOnly'}else{"-RiftSelectedModels=$($CardIds -join ',')"}
    & "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$riftRoot/Unreal/RiftCrownArena/RiftCrownArena.uproject" '-run=pythonscript' "-script=$riftRoot/Build/import_unreal_assets.py" $riftImportMode '-unattended' '-NullRHI' '-nosound' '-nopause' "-UserDir=$riftOutput/EngineUser" "-RiftSaveRoot=$riftOutput/Save" "-abslog=$riftOutput/commandlet.log" *> "$riftOutput/stdout.log"
    $riftExit=$LASTEXITCODE
} finally {$env:RIFT_IMPORT_REPORT_PATH=$riftPreviousReport}
$riftReport=if(Test-Path -LiteralPath $riftReportPath){Get-Content -LiteralPath $riftReportPath -Raw|ConvertFrom-Json -Depth 100}else{$null}
$riftBeforeByPath=@{};foreach($riftPin in $riftBefore){$riftBeforeByPath[$riftPin.path]=$riftPin.sha256}
$riftAfter=@(Get-ChildItem -LiteralPath "$riftRoot/Unreal/RiftCrownArena/Content" -File -Recurse|
    Where-Object {$_.Extension -in @('.uasset','.umap')}|ForEach-Object {
        [ordered]@{path=[IO.Path]::GetRelativePath($riftRoot,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
$riftAfterByPath=@{};foreach($riftPin in $riftAfter){$riftAfterByPath[$riftPin.path]=$riftPin.sha256}
$riftChanges=@(foreach($riftPath in @(@($riftBeforeByPath.Keys)+@($riftAfterByPath.Keys)|Sort-Object -Unique)) {
    if($riftBeforeByPath[$riftPath] -ne $riftAfterByPath[$riftPath]) {
        [ordered]@{path=$riftPath;beforeSha256=$riftBeforeByPath[$riftPath];afterSha256=$riftAfterByPath[$riftPath];created=!$riftBeforeByPath.ContainsKey($riftPath);deleted=!$riftAfterByPath.ContainsKey($riftPath)}
    }
})
$riftUnapprovedChanges=@($riftChanges|Where-Object {
    $riftPath=$_.path
    if($ArenaOnly){return $riftPath -ne 'Unreal/RiftCrownArena/Content/Rift/Materials/M_RiftArena.uasset' -or $_.deleted}
    # New schema fields require canonical numerical rebinding of CardData;
    # art, models, audio and effects of all other IDs remain pinned.
    $riftAllowed=$riftPath -match '^Unreal/RiftCrownArena/Content/Rift/Cards/DA_[a-z0-9_]+\.uasset$'
    $riftAllowed=$riftAllowed -or $riftPath -match '^Unreal/RiftCrownArena/Content/Rift/Entities/DA_bone_warden_skeleton\.uasset$'
    foreach($riftId in $CardIds){$riftAllowed=$riftAllowed -or $riftPath.StartsWith("Unreal/RiftCrownArena/Content/Rift/Characters/$riftId/") -or $riftPath -eq "Unreal/RiftCrownArena/Content/Rift/CardArt/T_Card_$riftId.uasset"}
    if($CardIds -contains 'pulverizer'){$riftAllowed=$riftAllowed -or $riftPath -eq 'Unreal/RiftCrownArena/Content/Rift/Materials/M_RiftShockwave.uasset'}
    if($CardIds -contains 'iron_giant'){$riftAllowed=$riftAllowed -or $riftPath -eq 'Unreal/RiftCrownArena/Content/Rift/Environment/SM_iron_shield_fragment.uasset'}
    if($CardIds -contains 'shadow_surge'){$riftAllowed=$riftAllowed -or $riftPath -in @('Unreal/RiftCrownArena/Content/Rift/Materials/M_RiftCloak.uasset','Unreal/RiftCrownArena/Content/Rift/Environment/SM_shadow_surge_wisp.uasset','Unreal/RiftCrownArena/Content/Rift/Environment/SM_shadow_surge_ribbon.uasset')}
    $riftAllowed=$riftAllowed -or $riftPath -match '^Unreal/RiftCrownArena/Content/Rift/(Materials/M_RiftExpansion|Textures/T_RiftExpansionAtlas_)'
    if(@($CardIds|Where-Object {$_ -in @('bone_warden','bone_warden_skeleton','aether_reaper','iron_giant','shadow_surge')}).Count){$riftAllowed=$riftAllowed -or $riftPath -match '^Unreal/RiftCrownArena/Content/Rift/Textures/T_RiftAscensionAtlas_'}
    !$riftAllowed -or $_.deleted
})
$riftModulesUnchanged=@($riftModules|Where-Object {(Get-FileHash -LiteralPath "$riftRoot/$($_.path)" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $_.sha256}).Count -eq 0
$riftSourcesUnchanged=@($riftSourcePins|Where-Object {(Get-FileHash -LiteralPath "$riftRoot/$($_.path)" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $_.sha256}).Count -eq 0
$riftPassed=$riftExit -eq 0 -and $riftReport -and $riftReport.passed -and $riftUnapprovedChanges.Count -eq 0 -and $riftModulesUnchanged -and $riftSourcesUnchanged
$riftProof=[ordered]@{schema=1;operation='selected-model-import-and-canonical-data-rebind';startedUtc=$riftStarted.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o');selectedModels=$CardIds;exitCode=$riftExit;passed=[bool]$riftPassed;runtimeModules=$riftModules;runtimeModulesUnchanged=$riftModulesUnchanged;sourceHashes=$riftSourcePins;sourcesUnchanged=$riftSourcesUnchanged;beforeNativeAssets=$riftBefore;afterNativeAssets=$riftAfter;changedNativeAssets=$riftChanges;unapprovedChanges=$riftUnapprovedChanges;importReport=$riftReportPath;importReportSha256=$(if($riftReport){(Get-FileHash -LiteralPath $riftReportPath -Algorithm SHA256).Hash.ToLowerInvariant()}else{''})}
[IO.File]::WriteAllText("$riftOutput/run.json",($riftProof|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
Write-Output $riftOutput
if(!$riftPassed){throw "Selected model import failed; inspect $riftOutput/run.json and raw logs."}
Write-Output "Selected import passed; $($riftBefore.Count-@($riftChanges|Where-Object {!$_.created}).Count) previously existing native assets stayed byte-identical."
