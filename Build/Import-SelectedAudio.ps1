param(
    [string[]]$AudioIds=@('shield_break','shadow_surge'),
    [string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8'
)
$ErrorActionPreference='Stop'
$riftRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).Replace('\','/')
$riftManifestPath="$riftRoot/Assets/Source/Audio/audio_manifest.json"
$riftManifest=Get-Content -LiteralPath $riftManifestPath -Raw|ConvertFrom-Json -Depth 100
$riftMasterIds=@($riftManifest.sounds.PSObject.Properties.Name)
if(!$AudioIds.Count -or @($AudioIds|Select-Object -Unique).Count -ne $AudioIds.Count -or @($AudioIds|Where-Object {$_ -notin $riftMasterIds}).Count){throw 'Audio selection must contain unique IDs from the current mastered manifest.'}
foreach($riftId in $riftMasterIds){
    $riftEntry=$riftManifest.sounds.PSObject.Properties[$riftId].Value
    if($riftEntry.file -notmatch '^Assets/Source/Audio/[a-z0-9_]+\.wav$' -or $riftEntry.sha256 -notmatch '^[a-f0-9]{64}$'){throw "Invalid audio source identity: $riftId"}
    if((Get-FileHash -LiteralPath "$riftRoot/$($riftEntry.file)" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $riftEntry.sha256){throw "Audio master differs from its manifest: $riftId"}
}
$riftEditor=[IO.Path]::GetFullPath("$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
if(!(Test-Path -LiteralPath $riftEditor -PathType Leaf)){throw "Editor commandlet not found: $riftEditor"}
$riftExecutableHash=(Get-FileHash -LiteralPath $riftEditor -Algorithm SHA256).Hash.ToLowerInvariant()
function Get-RiftSelectedAudioPins([string[]]$Paths){
    @($Paths|Sort-Object -Unique|ForEach-Object {
        if(Test-Path -LiteralPath $_ -PathType Leaf){[ordered]@{path=[IO.Path]::GetRelativePath($riftRoot,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}}
    })
}
function Get-RiftSelectedAudioSourceInventory {
    $riftPaths=@(Get-ChildItem -LiteralPath "$riftRoot/Unreal/RiftCrownArena/Source","$riftRoot/Unreal/RiftCrownArena/Config" -File -Recurse|ForEach-Object FullName)+@(
        "$riftRoot/Unreal/RiftCrownArena/RiftCrownArena.uproject","$riftRoot/Build/Import-SelectedAudio.ps1","$riftRoot/Build/import_unreal_audio.py",
        "$riftRoot/Build/generate_card_manifest.py","$riftRoot/Assets/Data/card_manifest.json","$riftRoot/Assets/asset_manifest.json",$riftManifestPath)
    $riftPaths+=@(Get-ChildItem -LiteralPath "$riftRoot/Assets/Source/Audio" -File|Where-Object Extension -eq '.wav'|ForEach-Object FullName)
    Get-RiftSelectedAudioPins $riftPaths
}
function Get-RiftSelectedAudioNativeInventory {
    Get-RiftSelectedAudioPins @(Get-ChildItem -LiteralPath "$riftRoot/Unreal/RiftCrownArena/Content" -File -Recurse|Where-Object {$_.Extension -in @('.uasset','.umap')}|ForEach-Object FullName)
}
function Compare-RiftSelectedAudioPins($Before,$After){
    $riftOld=@{};foreach($riftPin in $Before){$riftOld[$riftPin.path]=$riftPin.sha256}
    $riftNew=@{};foreach($riftPin in $After){$riftNew[$riftPin.path]=$riftPin.sha256}
    @(foreach($riftPath in @(@($riftOld.Keys)+@($riftNew.Keys)|Sort-Object -Unique)){
        if($riftOld[$riftPath] -ne $riftNew[$riftPath]){[ordered]@{path=$riftPath;beforeSha256=$riftOld[$riftPath];afterSha256=$riftNew[$riftPath];created=!$riftOld.ContainsKey($riftPath);deleted=!$riftNew.ContainsKey($riftPath)}}
    })
}
$riftRunRoot="$riftRoot/Artifacts/QA/ExpansionAudio/import-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $riftRunRoot|Out-Null
$riftReportPath="$riftRunRoot/import.json"
$riftBeforeNative=@(Get-RiftSelectedAudioNativeInventory)
$riftBeforeSource=@(Get-RiftSelectedAudioSourceInventory)
$riftModulePaths=@('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll')|ForEach-Object {"$riftRoot/$_"}
$riftBeforeModules=@(Get-RiftSelectedAudioPins $riftModulePaths)
if($riftBeforeModules.Count -ne 2){throw 'Both actual Editor modules must exist before audio import.'}
$riftPreviousReport=$env:RIFT_AUDIO_IMPORT_REPORT_PATH
$riftStarted=[DateTime]::UtcNow;$riftExit=-1;$riftLaunchError=''
try {
    $env:RIFT_AUDIO_IMPORT_REPORT_PATH=$riftReportPath
    & $riftEditor "$riftRoot/Unreal/RiftCrownArena/RiftCrownArena.uproject" '-run=pythonscript' "-script=$riftRoot/Build/import_unreal_audio.py" "-RiftSelectedAudio=$($AudioIds -join ',')" '-unattended' '-NullRHI' '-nosound' '-nopause' "-UserDir=$riftRunRoot/EngineUser" "-RiftSaveRoot=$riftRunRoot/Save" "-abslog=$riftRunRoot/commandlet.log" *> "$riftRunRoot/stdout.log"
    $riftExit=$LASTEXITCODE
} catch {$riftLaunchError=$_.Exception.Message} finally {$env:RIFT_AUDIO_IMPORT_REPORT_PATH=$riftPreviousReport}
$riftAfterNative=@(Get-RiftSelectedAudioNativeInventory);$riftAfterSource=@(Get-RiftSelectedAudioSourceInventory)
$riftAfterModules=@(Get-RiftSelectedAudioPins $riftModulePaths)
$riftNativeChanges=@(Compare-RiftSelectedAudioPins $riftBeforeNative $riftAfterNative)
$riftSourceChanges=@(Compare-RiftSelectedAudioPins $riftBeforeSource $riftAfterSource)
$riftModuleChanges=@(Compare-RiftSelectedAudioPins $riftBeforeModules $riftAfterModules)
$riftAllowedPaths=@($AudioIds|ForEach-Object {"Unreal/RiftCrownArena/Content/Rift/Audio/SFX_$_.uasset"})
$riftUnapproved=@($riftNativeChanges|Where-Object {$_.path -notin $riftAllowedPaths -or $_.deleted})
$riftReport=$null;$riftReportErrors=@()
if(Test-Path -LiteralPath $riftReportPath -PathType Leaf){
    try {$riftReport=Get-Content -LiteralPath $riftReportPath -Raw|ConvertFrom-Json -Depth 100}catch {$riftReportErrors+=@('Import report is invalid JSON.')}
}else {$riftReportErrors+=@('Fresh unique import report missing.')}
if($riftReport){
    if($riftReport.masterCount -ne $riftMasterIds.Count -or @($riftReport.sounds).Count -ne $AudioIds.Count -or @($riftReport.errors).Count -ne 0 -or (@($riftReport.selectedAudio|Sort-Object) -join ',') -ne (@($AudioIds|Sort-Object) -join ',')){$riftReportErrors+=@('Selected report roster or error contract differs.')}
    foreach($riftId in $AudioIds){
        $riftSounds=@($riftReport.sounds|Where-Object name -eq $riftId);$riftEntry=$riftManifest.sounds.PSObject.Properties[$riftId].Value
        if($riftSounds.Count -ne 1){$riftReportErrors+=@("Missing or duplicate imported cue: $riftId");continue}
        $riftSound=$riftSounds[0]
        if($riftSound.asset -ne "/Game/Rift/Audio/SFX_$riftId.SFX_$riftId" -or $riftSound.sourceSha256 -ne $riftEntry.sha256 -or $riftSound.channels -ne $riftEntry.channels -or $riftSound.loop -ne $false -or [math]::Abs($riftSound.duration-$riftEntry.duration) -gt .001 -or !(Test-Path -LiteralPath "$riftRoot/Unreal/RiftCrownArena/Content/Rift/Audio/SFX_$riftId.uasset" -PathType Leaf)){$riftReportErrors+=@("Imported cue identity/measurements differ: $riftId")}
    }
}
$riftExecutableUnchanged=(Get-FileHash -LiteralPath $riftEditor -Algorithm SHA256).Hash.ToLowerInvariant() -eq $riftExecutableHash
$riftPassed=$riftExit -eq 0 -and !$riftLaunchError -and $riftReportErrors.Count -eq 0 -and $riftUnapproved.Count -eq 0 -and $riftSourceChanges.Count -eq 0 -and $riftModuleChanges.Count -eq 0 -and $riftExecutableUnchanged
$riftProof=[ordered]@{schema=1;operation='selected-audio-import';passed=[bool]$riftPassed;startedUtc=$riftStarted.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o');selectedAudio=$AudioIds;exitCode=$riftExit;launchError=$riftLaunchError;executable=$riftEditor;executableSha256=$riftExecutableHash;executableUnchanged=$riftExecutableUnchanged;beforeRuntimeModules=$riftBeforeModules;afterRuntimeModules=$riftAfterModules;runtimeModulesUnchanged=$riftModuleChanges.Count -eq 0;beforeSourceHashes=$riftBeforeSource;afterSourceHashes=$riftAfterSource;sourcesUnchanged=$riftSourceChanges.Count -eq 0;sourceChanges=$riftSourceChanges;beforeNativeAssets=$riftBeforeNative;afterNativeAssets=$riftAfterNative;changedNativeAssets=$riftNativeChanges;unapprovedChanges=$riftUnapproved;unselectedMasters=$riftMasterIds.Count-$AudioIds.Count;unselectedMastersUnchanged=$riftSourceChanges.Count -eq 0;allowedNativeChanges=$riftAllowedPaths;importReport=$riftReportPath;reportErrors=$riftReportErrors;importReportSha256=$(if(Test-Path -LiteralPath $riftReportPath){(Get-FileHash -LiteralPath $riftReportPath -Algorithm SHA256).Hash.ToLowerInvariant()}else{''})}
[IO.File]::WriteAllText("$riftRunRoot/run.json",($riftProof|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
Write-Output $riftRunRoot
if(!$riftPassed){throw "Selected audio import failed; inspect $riftRunRoot/run.json and raw logs."}
Write-Output 'Selected audio import passed; all source masters and every unselected native asset stayed byte-identical.'
