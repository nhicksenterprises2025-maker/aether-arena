param(
    [string]$Version='1.2.1',
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string[]]$CaptureNames,
    [Parameter(Mandatory)][string]$ReviewNotes,
    [Parameter(Mandatory)][string]$PerformanceName,
    [Parameter(Mandatory)][string]$AudioName,
    [Parameter(Mandatory)][string]$VFXName
)
# Focused acceptance for the two-spell timing patch. Earlier balance cohorts
# and presentation reviews retain their own version; they are not new results.
$ErrorActionPreference='Stop'
if($Version -ne '1.2.1'){throw 'This evidence gate describes the 1.2.1 spell timing patch.'}
$spellRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$spellInputs=[Collections.Generic.List[object]]::new()
function Resolve-SpellInput([string]$Path){
    $resolved=[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($Path)){$Path}else{Join-Path $spellRepo $Path}))
    if(!$resolved.StartsWith($spellRepo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or !(Test-Path -LiteralPath $resolved -PathType Leaf)){throw "Missing workspace evidence: $Path"}
    $ancestor=$resolved
    while($ancestor -and $ancestor.StartsWith($spellRepo,[StringComparison]::OrdinalIgnoreCase)){
        if(((Get-Item -LiteralPath $ancestor).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Evidence paths cannot contain links.'}
        $ancestor=[IO.Path]::GetDirectoryName($ancestor)
    }
    return $resolved
}
function Pin-SpellInput([string]$Path){
    $resolved=Resolve-SpellInput $Path
    $pin=[pscustomobject][ordered]@{path=[IO.Path]::GetRelativePath($spellRepo,$resolved).Replace('\','/');sha256=(Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $resolved).Length}
    if($pin.path -match '(^|/)(Replays|Saves|Backups|Saved|AutomationFixtures)(/|$)'){throw 'Private profiles and replay files cannot be packaged.'}
    $spellInputs.Add($pin)
    return $pin
}
function Read-SpellInput([string]$Path){
    $null=Pin-SpellInput $Path
    return Get-Content -LiteralPath (Resolve-SpellInput $Path) -Raw | ConvertFrom-Json -Depth 100
}
$spellNative=Resolve-SpellInput $Executable
$spellNativeHash=(Get-FileHash -LiteralPath $spellNative -Algorithm SHA256).Hash.ToLowerInvariant()
$spellManifest=Read-SpellInput 'Artifacts/Release/update-manifest.json'
$spellRelease=Read-SpellInput 'Artifacts/Release/windows-release-verification.json'
$spellNativeEntry=@($spellManifest.files | Where-Object path -match 'Binaries/Win64/RiftCrownArena-Win64-Shipping.exe$')
if(!$spellRelease.passed -or $spellRelease.version -ne $Version -or $spellManifest.version -ne $Version -or $spellNativeEntry.Count -ne 1 -or $spellNativeEntry[0].sha256 -ne $spellNativeHash){throw 'Accepted release and current native executable differ.'}
$spellIntegration=Read-SpellInput 'Docs/QA/native-integration.json'
if($spellIntegration.failed -ne 0 -or $spellIntegration.notRun -ne 0 -or @($spellIntegration.tests | Where-Object fullTestPath -eq 'Rift.Integration.SpellCastReplay').Count -ne 1){throw 'The complete integration suite must include spell replay acceptance.'}
$spellContext=$spellIntegration.curatedEvidence
if(!$spellContext -or $spellContext.version -ne $Version -or !$spellContext.allScenariosObserved -or $spellContext.commandletExitCode -ne 0 -or $spellContext.wrapperExitCode -ne 0){throw 'Current complete integration context is missing.'}
foreach($source in $spellContext.sourceHashes){
    $pin=Pin-SpellInput $source.path
    if($pin.sha256 -ne $source.sha256){throw "Source changed after integration acceptance: $($source.path)"}
}
foreach($module in $spellContext.runtimeModules){if((Get-FileHash -LiteralPath (Resolve-SpellInput $module.path)).Hash.ToLowerInvariant() -ne $module.sha256){throw 'Editor runtime changed after integration acceptance.'}}
$spellPortable=Pin-SpellInput 'Artifacts/QA/spell121-portable-tests.log'
if((Get-Content -LiteralPath (Resolve-SpellInput $spellPortable.path) -Raw) -notmatch 'Native authoritative simulation: 41 scenarios passed\.'){throw 'Complete portable regression success marker is missing.'}
$null=Pin-SpellInput 'Build/Tests/RiftSimulationTests.cpp'
$spellReview=Read-SpellInput $ReviewNotes
if($spellReview.version -ne $Version -or !$spellReview.passed){throw 'Explicit physical inspection notes are required for this version.'}
foreach($name in $CaptureNames){
    if($name -notmatch '^spell121-[a-z0-9-]+$'){throw 'Select explicit current patch captures.'}
    $run=Read-SpellInput ('Artifacts/QA/Visual/'+$name+'.json')
    $state=Read-SpellInput ('Artifacts/QA/Visual/'+$name+'.state.json')
    $pin=Pin-SpellInput ('Artifacts/QA/Visual/'+$name+'.png')
    if($run.executableSha256 -ne $spellNativeHash -or !$run.stateCaptured -or !$run.cameraFramingPassed -or $run.exitCode -ne 0){throw "Capture does not belong to the accepted native package: $name"}
    $note=@($spellReview.captures | Where-Object name -eq $name)
    if($note.Count -ne 1 -or !$note[0].reviewed -or [string]::IsNullOrWhiteSpace($note[0].observation) -or $note[0].pngSha256 -ne $pin.sha256){throw "Physical inspection does not pin this frame: $name"}
}
$spellPerfRoot='Artifacts/QA/Performance/'+$PerformanceName+'/'
$spellPerfRun=Read-SpellInput ($spellPerfRoot+'run.json')
$spellPerf=Read-SpellInput ($spellPerfRoot+'performance.json')
if(!$spellPerfRun.passed -or $spellPerfRun.exitCode -ne 0 -or $spellPerfRun.executableSha256 -ne $spellNativeHash -or $spellPerf.frame.samples -lt 100){throw 'Current native performance execution is missing.'}
$spellAudioRun=Read-SpellInput ('Artifacts/QA/Audio/'+$AudioName+'/run.json')
$spellAudio=Read-SpellInput ('Artifacts/QA/Audio/'+$AudioName+'/audio-smoke.json')
$spellVFX=Read-SpellInput ('Artifacts/QA/'+$VFXName+'-verification.json')
if(!$spellAudio.passed -or !$spellAudioRun.passed -or $spellAudioRun.executableSha256 -ne $spellNativeHash -or !$spellVFX.passed -or $spellVFX.executableSha256 -ne $spellNativeHash){throw 'Current native audio and VFX verification must pass.'}
foreach($path in @('Artifacts/QA/launcher-play-smoke.json','Artifacts/QA/installer-build.json','Artifacts/QA/installer-tests.json','Artifacts/QA/app-local-runtime.json')){
    $report=Read-SpellInput $path
    if(!$report.passed){throw "Windows delivery check failed: $path"}
}
$spellPlay=Get-Content -LiteralPath (Resolve-SpellInput 'Artifacts/QA/launcher-play-smoke.json') -Raw | ConvertFrom-Json
$spellMSIBuild=Get-Content -LiteralPath (Resolve-SpellInput 'Artifacts/QA/installer-build.json') -Raw | ConvertFrom-Json
$spellMSITest=Get-Content -LiteralPath (Resolve-SpellInput 'Artifacts/QA/installer-tests.json') -Raw | ConvertFrom-Json
$spellMSIHash=(Get-FileHash -LiteralPath (Resolve-SpellInput 'Artifacts/Release/RiftCrownArena-Setup.msi')).Hash.ToLowerInvariant()
if($spellPlay.installed -ne $Version -or $spellPlay.nativeSha256 -ne $spellNativeHash -or $spellPlay.verifiedGameFiles -ne $spellManifest.files.Count -or !$spellPlay.legacySavePreserved -or
   $spellMSIBuild.version -ne $Version -or $spellMSIBuild.sha256 -ne $spellMSIHash -or $spellMSIBuild.gameArchiveSha256 -ne $spellManifest.sha256 -or
   $spellMSITest.installedVersion -ne $Version -or $spellMSITest.upgradeMsiSha256 -ne $spellMSIHash -or $spellMSITest.installedArchiveSha256 -ne $spellManifest.sha256 -or $spellMSITest.checkCount -lt 51 -or
   $spellRelease.installerSha256 -ne $spellMSIHash -or $spellRelease.archiveSha256 -ne $spellManifest.sha256){throw 'Current-version Windows delivery identity/provenance mismatch.'}
foreach($path in @('Unreal/RiftCrownArena/Content/Rift/Cards/DA_meteor_shards.uasset','Unreal/RiftCrownArena/Content/Rift/Cards/DA_bullet_burst.uasset')){$null=Pin-SpellInput $path}
$spellMeta=Read-SpellInput 'Docs/QA/spell-timing-meta-100.json'
if(!$spellMeta.completeObserved -or $spellMeta.observedGames -ne 100 -or $spellMeta.failedChecks -ne 0 -or $spellMeta.errors.Count -ne 0 -or $spellMeta.fingerprint -ne 'b3968027993fd0ece72577b77c48d76e'){throw 'Updated-rule Meta sample must pass its complete independent audit.'}
# The Meta cohort precedes a presentation-only readability refinement. Its
# actual DLL is certified by the earlier complete integration run, and every
# authoritative simulation source must still match the final source tree.
$spellMetaIntegration=Read-SpellInput 'Artifacts/QA/spell121-first-native-integration.json'
if($spellMetaIntegration.failed -ne 0 -or $spellMetaIntegration.notRun -ne 0 -or $spellMetaIntegration.curatedEvidence.version -ne $Version -or $spellMeta.evidence.runtimeSha256 -ne $spellMetaIntegration.curatedEvidence.runtimeModuleSha256 -or $spellMeta.evidence.simulationSources.Count -ne 6){throw 'Meta runtime lacks its complete built-source provenance.'}
foreach($source in $spellMeta.evidence.simulationSources){
    $pin=Pin-SpellInput $source.path
    $built=@($spellMetaIntegration.curatedEvidence.sourceHashes | Where-Object path -eq $source.path)
    if($pin.sha256 -ne $source.sha256 -or $built.Count -ne 1 -or $built[0].sha256 -ne $source.sha256){throw 'Authoritative simulation changed after the updated-rule Meta cohort.'}
}
foreach($input in $spellMeta.evidence.inputs){
    $pin=Pin-SpellInput $input.path
    if($pin.sha256 -ne $input.sha256){throw 'Meta source evidence changed after its independent audit.'}
}
$spellVisual=Read-SpellInput 'Docs/QA/spell-timing-visual-1.2.1.json'
if(!$spellVisual.passed -or $spellVisual.nativeExecutableSha256 -ne $spellNativeHash){throw 'Updated-rule impact/deadline visual checks must pass on this native executable.'}
if($spellVisual.inputs.Count -ne 18){throw 'The six timing/framing capture triplets must retain their acceptance hashes.'}
foreach($input in $spellVisual.inputs){
    $pin=Pin-SpellInput $input.path
    if($pin.sha256 -ne $input.sha256){throw "Capture changed after timing acceptance: $($input.path)"}
}
foreach($path in @('Docs/PATCH_NOTES_1.2.1.md','Docs/SPELL_TIMING_1.2.1.md','Build/Package-SpellQAEvidence.ps1','Build/Test-SpellPresentation.ps1','Build/Tests/Audit-NativeMeta.py','Build/update_spell_timing_assets.py')){$null=Pin-SpellInput $path}
$spellUnique=@($spellInputs | Sort-Object path -Unique)
foreach($required in @('Docs/QA/native-integration.json','Docs/QA/spell-timing-meta-100.json','Docs/QA/spell-timing-visual-1.2.1.json','Artifacts/QA/installer-tests.json','Build/Package-SpellQAEvidence.ps1')){
    if(@($spellUnique | Where-Object path -eq $required).Count -ne 1){throw "Required evidence was lost while deduplicating inputs: $required"}
}
if($spellUnique.Count -lt $spellContext.sourceHashes.Count + 30){throw 'Evidence inventory is unexpectedly incomplete.'}
$spellSummary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Two-spell authoritative timing, replay, travel presentation, native regression and Windows delivery';nativeExecutableSha256=$spellNativeHash;delays=[ordered]@{meteor_shards=.75;bullet_burst=.30;nova_flask=0};historicalBalanceCohortIsCurrent=$false;captureNames=$CaptureNames;integrationTests=$spellIntegration.tests.Count;performance=$spellPerf;inputs=$spellUnique;publicDelivery='Pending publication; postpublication reports are separate evidence.'}
$spellSummaryPath=Join-Path $spellRepo 'Docs/QA/spell-timing-checks-1.2.1.json'
[IO.File]::WriteAllText($spellSummaryPath,($spellSummary|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
$spellZip=Join-Path $spellRepo ('Artifacts/Release/RiftCrownArena-QAEvidence-Windows-x64-'+$Version+'.zip')
if(Test-Path -LiteralPath $spellZip){throw 'The frozen evidence archive already exists; choose a fresh acceptance run before replacing it.'}
Add-Type -AssemblyName System.IO.Compression
$spellArchive=[IO.Compression.ZipFile]::Open($spellZip,[IO.Compression.ZipArchiveMode]::Create)
$spellEntryInventory=[Collections.Generic.List[object]]::new()
try{
    foreach($pin in $spellUnique){
        $source=Resolve-SpellInput $pin.path
        if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $pin.sha256){throw "Evidence changed while packaging: $($pin.path)"}
        if([IO.Path]::GetExtension($source) -in @('.json','.log')){
            $raw=Get-Content -LiteralPath $source -Raw
            $raw=$raw.Replace($spellRepo.Replace('\','\\'),'.').Replace($spellRepo.Replace('\','/'),'.').Replace($spellRepo,'.')
            if($env:USERPROFILE){$raw=$raw.Replace($env:USERPROFILE.Replace('\','\\'),'%USERPROFILE%').Replace($env:USERPROFILE.Replace('\','/'),'%USERPROFILE%')}
            if([IO.Path]::GetExtension($source) -eq '.json'){$null=$raw|ConvertFrom-Json -Depth 100}
            $entry=$spellArchive.CreateEntry($pin.path,[IO.Compression.CompressionLevel]::Optimal)
            $writer=[IO.StreamWriter]::new($entry.Open(),[Text.UTF8Encoding]::new($false))
            try{$writer.Write($raw)}finally{$writer.Dispose()}
            $entryBytes=[Text.UTF8Encoding]::new($false).GetBytes($raw)
            $entryHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($entryBytes)).ToLowerInvariant()
            $spellEntryInventory.Add([ordered]@{entry=$pin.path;sha256=$entryHash;bytes=$entryBytes.Length;sourceSha256=$pin.sha256;sourceBytes=$pin.bytes;sanitized=$true})
        }else{
            $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($spellArchive,$source,$pin.path,[IO.Compression.CompressionLevel]::Optimal)
            $spellEntryInventory.Add([ordered]@{entry=$pin.path;sha256=$pin.sha256;bytes=$pin.bytes;sourceSha256=$pin.sha256;sourceBytes=$pin.bytes;sanitized=$false})
        }
    }
    $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($spellArchive,$spellSummaryPath,'Docs/QA/spell-timing-checks-1.2.1.json',[IO.Compression.CompressionLevel]::Optimal)
    $spellEntryInventory.Add([ordered]@{entry='Docs/QA/spell-timing-checks-1.2.1.json';sha256=(Get-FileHash -LiteralPath $spellSummaryPath).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $spellSummaryPath).Length;sanitized=$false})
    $inventoryEntry=$spellArchive.CreateEntry('evidence-inventory.json',[IO.Compression.CompressionLevel]::Optimal)
    $inventoryWriter=[IO.StreamWriter]::new($inventoryEntry.Open(),[Text.UTF8Encoding]::new($false))
    try{$inventoryWriter.Write(([ordered]@{schema=1;version=$Version;entries=$spellEntryInventory}|ConvertTo-Json -Depth 20))}finally{$inventoryWriter.Dispose()}
}finally{$spellArchive.Dispose()}
$spellArchive=[IO.Compression.ZipFile]::OpenRead($spellZip)
try{
    if($spellArchive.Entries.Count -ne $spellEntryInventory.Count+1){throw 'Unexpected evidence archive entry count.'}
    foreach($pin in $spellEntryInventory){
        $entry=$spellArchive.GetEntry($pin.entry)
        if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Evidence archive entry size mismatch.'}
        $stream=$entry.Open()
        try{$entryHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($entryHash -ne $pin.sha256){throw 'Evidence archive entry hash mismatch.'}
    }
}finally{$spellArchive.Dispose()}
$spellZipHash=(Get-FileHash -LiteralPath $spellZip).Hash.ToLowerInvariant()
$spellSumPath=Join-Path $spellRepo 'Artifacts/Release/SHA256SUMS.txt'
$spellZipName=[IO.Path]::GetFileName($spellZip)
$spellSumLines=@(Get-Content -LiteralPath $spellSumPath | Where-Object {$_ -notmatch ('\s{2}'+[regex]::Escape($spellZipName)+'$')})
$spellSumLines+=($spellZipHash+'  '+$spellZipName)
[IO.File]::WriteAllLines($spellSumPath,$spellSumLines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($spellEntryInventory.Count+1) verified evidence entries: $spellZipHash"
