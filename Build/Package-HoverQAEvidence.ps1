param(
    [string]$Executable='Artifacts/Game-1.3.1/Windows/RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe',
    [string[]]$DragReports=@('Artifacts/QA/CardDrag/hover131-drag-verification.json','Artifacts/QA/CardDrag/hover131-drag720-verification.json'),
    [string[]]$ReleaseDocuments=@('Docs/PATCH_NOTES_1.3.1.md')
)
# A compact hotfix packet. Model/art/font review remains historical 1.3.0
# evidence, with unchanged current source and imported-asset fingerprints.
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this evidence packager with PowerShell 7.'}
$Version='1.3.1'
$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$summaryRelative='Docs/QA/hover-stats-1.3.1.json'
$zipRelative='Artifacts/Release/RiftCrownArena-Hover-QAEvidence-1.3.1.zip'
$summaryPath=Join-Path $modelRepo $summaryRelative
$zipPath=Join-Path $modelRepo $zipRelative
$sumPath=Join-Path $modelRepo 'Artifacts/Release/SHA256SUMS.txt'
if((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath $summaryPath)){throw 'The frozen hotfix evidence archive or curated summary already exists.'}
$modelInputs=[Collections.Generic.List[object]]::new()
$modelPinIndex=@{}
$modelSyntheticInputs=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$validator=Join-Path $PSScriptRoot 'Package-ModelInputQAEvidence.ps1'
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($validator,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'The maintained strict evidence validator does not parse.'}
foreach($name in @('Resolve-ModelInput','Pin-ModelInput','Read-ModelInput','Get-ModelUtcTimestamp','Assert-ModelShippingDiagnostics','Assert-ModelRecord','Assert-ModelChecks')){
    $definitions=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
    if($definitions.Count -ne 1){throw "The maintained strict acceptance function is missing: $name"}
    Invoke-Expression $definitions[0].Extent.Text
}
$null=Pin-ModelInput $validator
$null=Pin-ModelInput $PSCommandPath
function Get-HoverIdentity([string]$Path){
    $file=Resolve-ModelInput $Path
    return [pscustomobject][ordered]@{path=[IO.Path]::GetRelativePath($modelRepo,$file).Replace('\','/');sha256=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $file).Length}
}
function Assert-HoverIdentity([object]$Pin){
    $actual=Get-HoverIdentity $Pin.path
    if($actual.sha256 -ne $Pin.sha256 -or ($Pin.bytes -and $actual.bytes -ne $Pin.bytes)){throw "Acceptance identity changed: $($Pin.path)"}
}
$native=Get-HoverIdentity $Executable
$manifest=Read-ModelInput 'Artifacts/Release/update-manifest.json'
$release=Read-ModelInput 'Artifacts/Release/windows-release-verification.json'
$entry=@($manifest.files|Where-Object path -match 'Binaries/Win64/RiftCrownArena-Win64-Shipping.exe$')
if($release.passed -ne $true -or $release.version -ne $Version -or $release.tag -ne ('v'+$Version) -or $manifest.version -ne $Version -or
   $entry.Count -ne 1 -or $entry[0].sha256 -ne $native.sha256 -or $entry[0].size -ne $native.bytes -or
   !$native.path.EndsWith('/'+$entry[0].path,[StringComparison]::OrdinalIgnoreCase) -or
   $release.manifestSha256 -ne $modelPinIndex['Artifacts/Release/update-manifest.json'].sha256){throw 'Current final Windows release, manifest and actual Shipping executable differ.'}
$packageRoot=$native.path.Substring(0,$native.path.Length-$entry[0].path.Length)
$payload=[Collections.Generic.List[object]]::new()
foreach($file in $manifest.files){
    if($file.path -match '(^|/)\.\.(/|$)|^[A-Za-z]:|^[/\\]' -or $file.sha256 -notmatch '^[a-f0-9]{64}$' -or $file.size -le 0){throw 'Invalid final payload record.'}
    $pin=Get-HoverIdentity ($packageRoot+$file.path)
    if($pin.sha256 -ne $file.sha256 -or $pin.bytes -ne $file.size){throw 'Actual frozen Windows payload differs from its final manifest.'}
    $payload.Add($pin)
}
if(@($manifest.files.path|Sort-Object -Unique).Count -ne $manifest.files.Count -or $release.gameFiles -ne $manifest.files.Count){throw 'Final Windows payload inventory differs.'}

$integration=Read-ModelInput 'Docs/QA/native-integration.json'
$context=$integration.curatedEvidence
if($integration.failed -ne 0 -or $integration.notRun -ne 0 -or @($integration.tests).Count -lt 15 -or
   @($integration.tests|Where-Object {$_.state -ne 'Success' -or $_.errors -ne 0}).Count -ne 0 -or
   @($integration.tests.fullTestPath|Sort-Object -Unique).Count -ne $integration.tests.Count -or
   $context.version -ne $Version -or $context.allScenariosObserved -ne $true -or $context.commandletExitCode -ne 0 -or $context.wrapperExitCode -ne 0){throw 'The complete current fifteen-test native suite must pass.'}
foreach($required in @('Rift.Integration.Typography','Rift.Integration.BattleInputRouting','Rift.Integration.SpellCastReplay')){
    if(@($integration.tests|Where-Object fullTestPath -eq $required).Count -ne 1){throw "A required current complete-suite scenario is missing: $required"}
}
foreach($pin in @($context.sourceHashes)+@($context.runtimeModules)){Assert-HoverIdentity $pin}
$rawIntegration=Read-ModelInput $context.sourceReport
$rawContext=Read-ModelInput $context.context
$log=Pin-ModelInput $context.commandletLog
if($modelPinIndex[$context.sourceReport].sha256 -ne $context.sourceReportSha256 -or $modelPinIndex[$context.context].sha256 -ne $context.contextSha256 -or
   $rawIntegration.failed -ne 0 -or $rawIntegration.notRun -ne 0 -or $rawIntegration.inProcess -ne 0 -or @($rawIntegration.tests).Count -ne $integration.tests.Count -or
   $rawContext.completed -ne $true -or $rawContext.sourcesUnchanged -ne $true -or $rawContext.runtimeModulesUnchanged -ne $true -or
   $rawContext.commandletExitCode -ne 0 -or $rawContext.wrapperExitCode -ne 0){throw 'Curated native integration lacks its exact completed raw run/context.'}
$logText=Get-Content -LiteralPath (Resolve-ModelInput $log.path) -Raw
if(!$logText.Contains("Automation Test Queue Empty $($integration.tests.Count) tests performed.") -or
   $logText -notmatch 'FPlatformMisc::RequestExitWithStatus\(1, 0' -or $logText -match '(?im)(^|\])\s*\S+:\s*(Error|Fatal):'){throw 'Current native suite log lacks exact completion or contains an engine error.'}

# Anchor the inherited source/art acceptance to its immutable original ZIP.
$baseline=Read-ModelInput 'Docs/QA/model-input-checks-1.3.0.json'
$baselineZip=Get-HoverIdentity 'Artifacts/Release/RiftCrownArena-QAEvidence-Windows-x64-1.3.0.zip'
if($baseline.passed -ne $true -or $baseline.version -ne '1.3.0' -or $baseline.physicalSourcePortraits -ne 14 -or $baseline.physicalSourceActionPoses -ne 54 -or
   $baselineZip.sha256 -ne '12c65630445adc8bf064a15d611a6fe238301e3f6f6ac47372ab643b4791dd4e'){throw 'The historical accepted 1.3.0 source/art evidence identity changed.'}
$archive=[IO.Compression.ZipFile]::OpenRead((Resolve-ModelInput $baselineZip.path))
try{
    $baselineEntry=$archive.GetEntry('Docs/QA/model-input-checks-1.3.0.json')
    if(!$baselineEntry){throw 'The immutable source/art evidence lacks its curated baseline.'}
    $stream=$baselineEntry.Open();try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
    if($hash -ne $modelPinIndex['Docs/QA/model-input-checks-1.3.0.json'].sha256){throw 'The inherited model/font source snapshot differs from its immutable archive.'}
}finally{$archive.Dispose()}
$assets=Read-ModelInput 'Assets/asset_manifest.json'
$core=@('Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftAI.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCombat.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftDeckAnalysis.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftPathfinding.cpp',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftSimulation.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftSimulation.h')
$unchangedPaths=$core+@('Assets/asset_manifest.json',$assets.source.file,'Assets/Source/CardArt/model_portraits.json',
    'Assets/Fonts/BarlowSemiCondensed/provenance.json','Assets/Fonts/BarlowSemiCondensed/OFL.txt','Unreal/RiftCrownArena/Content/Rift/Fonts/Barlow-OFL.txt',
    'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftTypography.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Public/RiftTypography.h')
$unchangedPaths+=@($baseline.inputs.path|Where-Object {$_ -match '^Assets/.*\.fbx$|^Assets/Source/CardArt/[a-z_]+\.png$|^Assets/Fonts/BarlowSemiCondensed/.*\.ttf$'})
$unchanged=[Collections.Generic.List[object]]::new()
foreach($path in $unchangedPaths|Sort-Object -Unique){
    $old=@($baseline.inputs|Where-Object path -eq $path)
    if($old.Count -ne 1){throw "The historical snapshot does not pin the required unchanged source: $path"}
    Assert-HoverIdentity $old[0]
    $unchanged.Add($old[0])
}
if($assets.source.sha256 -ne $baseline.modelSourceSha256 -or @($unchanged|Where-Object path -match '^Assets/Source/CardArt/[a-z_]+\.png$').Count -ne 14 -or
   @($unchanged|Where-Object path -match '^Assets/Fonts/BarlowSemiCondensed/.*\.ttf$').Count -ne 4){throw 'The inherited authored model/card-art/font inventory is incomplete.'}
foreach($pin in $baseline.importedAssetPins){Assert-HoverIdentity $pin}
$license='RiftCrownArena/Content/Rift/Fonts/Barlow-OFL.txt'
$packagedLicense=Pin-ModelInput ($packageRoot+$license)
$licenseBaseline=@($baseline.inputs|Where-Object path -eq 'Assets/Fonts/BarlowSemiCondensed/OFL.txt')
if($licenseBaseline.Count -ne 1 -or $packagedLicense.sha256 -ne $licenseBaseline[0].sha256 -or $packagedLicense.bytes -ne $licenseBaseline[0].bytes){throw 'The current hotfix payload must retain the complete readable original font license.'}
foreach($path in $core){$null=Pin-ModelInput $path}
$oldDragPath='Artifacts/QA/CardDrag/model130-drag-shipping-verification.json'
$oldDrag=Read-ModelInput $oldDragPath
$oldDragPin=@($baseline.inputs|Where-Object path -eq $oldDragPath)
if($oldDragPin.Count -ne 1 -or $oldDragPin[0].sha256 -ne $modelPinIndex[$oldDragPath].sha256 -or $oldDrag.passed -ne $true -or
   $oldDrag.version -ne '1.3.0' -or $oldDrag.checkCount -ne 29){throw 'The inherited twenty-nine production routing check names lack their original observed baseline.'}

if($DragReports.Count -ne 2 -or @($DragReports|Sort-Object -Unique).Count -ne 2){throw 'Select exactly two current actual Shipping hover/routing reports.'}
$runs=[Collections.Generic.List[object]]::new()
$hoverNames=@('hovering hand card opens its production Slate stats tooltip',
    'unchanged hand hover keeps the same tooltip open through twelve HUD refreshes',
    'cycling a hand card refreshes its stats tooltip once and keeps the new hover open')
foreach($reportPath in $DragReports){
    $run=Read-ModelInput $reportPath
    Assert-ModelChecks $run 32 'Actual Shipping hover and pointer routing'
    $expectedWidth=$(if($run.name -eq 'hover131-drag'){1920}elseif($run.name -eq 'hover131-drag720'){1280}else{throw 'Choose the two explicit current hover hotfix runs.'})
    $expectedHeight=$(if($expectedWidth -eq 1920){1080}else{720})
    if($run.schemaVersion -ne 1 -or $run.version -ne $Version -or $run.executableSha256 -ne $native.sha256 -or $run.editor -ne $false -or
       $run.freshReport -ne $true -or $run.timedOut -ne $false -or $run.exitCode -ne 0 -or $run.sourcePinsUnchanged -ne $true -or $run.executableUnchanged -ne $true -or
       $run.width -ne $expectedWidth -or $run.height -ne $expectedHeight -or $run.checkCount -ne 32 -or @($run.checks).Count -ne 32 -or
       @($run.checks.name|Sort-Object -Unique).Count -ne 32 -or $run.scope -notmatch '(?i)Real Slate mouse routing through the production UMG hand'){throw 'The hotfix requires both complete thirty-two-check packaged production Slate runs.'}
    $raw=Read-ModelInput $run.report
    if($modelPinIndex[$run.report].sha256 -ne $run.nativeReportSha256 -or $raw.schemaVersion -ne 1 -or $raw.version -ne $Version -or $raw.passed -ne $true -or
       $raw.width -ne $expectedWidth -or $raw.height -ne $expectedHeight -or $raw.checkCount -ne 32 -or @($raw.checks).Count -ne 32 -or
       @($raw.checks|Where-Object {$_.passed -ne $true}).Count -ne 0 -or @($raw.checks.name|Sort-Object -Unique).Count -ne 32 -or
       $raw.route -notmatch 'FSlateApplication ProcessMouseButtonDownEvent / ProcessMouseMoveEvent / ProcessMouseButtonUpEvent' -or
       (($raw.checks.name|Sort-Object)-join '|') -ne (($run.checks.name|Sort-Object)-join '|')){throw 'Normalized hover proof differs from its actual complete native report.'}
    foreach($check in $oldDrag.checks){if(@($raw.checks|Where-Object name -eq $check.name).Count -ne 1){throw 'The hover regression dropped an existing production pointer-routing check.'}}
    $hover=@($raw.checks|Where-Object {$_.name -notin $oldDrag.checks.name})
    if($hover.Count -ne 3 -or (($hover.name|Sort-Object)-join '|') -ne (($hoverNames|Sort-Object)-join '|')){throw 'All three exact additional actual hover/tooltip assertions must be present.'}
    $tooltip=$raw.tooltipLifecycle
    if($tooltip.route -ne 'application-local FFauxSlateCursor / ProcessMouseMoveEvent / FSlateApplication UpdateToolTip / production SObjectWidget Tick' -or
       $tooltip.usesHardwareCursor -ne $false -or $tooltip.hudSlateWidgetType -ne 'SObjectWidget' -or $tooltip.initialOpened -ne $true -or
       $tooltip.unchangedRefreshCount -ne 12 -or $tooltip.unchangedIdentityStable -ne $true -or $tooltip.unchangedWindowStable -ne $true -or
       $tooltip.minimumUnchangedOpacity -le .99 -or $tooltip.cycledRefreshCount -ne 6 -or $tooltip.cycleTextAndIdentityCorrect -ne $true -or $tooltip.cycledOpenedAndStable -ne $true -or
       [string]::IsNullOrWhiteSpace($tooltip.initialCardId) -or [string]::IsNullOrWhiteSpace($tooltip.cycledCardId) -or $tooltip.initialCardId -eq $tooltip.cycledCardId -or
       [string]::IsNullOrWhiteSpace($tooltip.initialText) -or [string]::IsNullOrWhiteSpace($tooltip.cycledText) -or $tooltip.initialText -eq $tooltip.cycledText){throw 'Actual production tooltip lifecycle metadata does not prove stable initial and cycled stats windows.'}
    $started=Get-ModelUtcTimestamp $run.diagnosticVerification.startedUTC;$finished=Get-ModelUtcTimestamp $run.diagnosticVerification.finishedUTC
    $written=(Get-Item -LiteralPath (Resolve-ModelInput $run.report)).LastWriteTimeUtc
    if($written -lt $started -or $written -gt $finished){throw 'The native hover report was not freshly written during its observed run.'}
    $records=(Assert-ModelShippingDiagnostics $run 'Current hover hotfix').records
    $passes=@($records|Where-Object {$_.message -match '^Card drag route: PASS '})
    if($passes.Count -ne 32 -or @($records|Where-Object {$_.message -match '^Card drag route: FAIL '}).Count -ne 0 -or
       @($records|Where-Object {$_.message -match '^Native card drag smoke passed: '}).Count -ne 1 -or $run.diagnosticVerification.passCount -ne 32 -or
       $run.diagnosticVerification.routeRecordsMatch -ne $true -or $run.diagnosticVerification.finalPass -ne $true){throw 'Actual native diagnostics lack all passing hover/routing assertions and final completion.'}
    foreach($check in $raw.checks){if(@($passes|Where-Object message -eq ('Card drag route: PASS '+$check.name)).Count -ne 1){throw 'Native diagnostic PASS names differ from the raw hover report.'}}
    foreach($required in @('Build/Test-CardDrag.ps1','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Tests/RiftCardDragSmoke.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftBattleHUD.cpp')){
        if(@($run.sourcePins|Where-Object path -eq $required).Count -ne 1){throw "Observed hover source pin is missing: $required"}
    }
    foreach($pin in $run.sourcePins){
        Assert-HoverIdentity $pin
        if($pin.path -match '\.dll$'){
            if(@($context.runtimeModules|Where-Object {$_.path -eq $pin.path -and $_.sha256 -eq $pin.sha256}).Count -ne 1){throw 'Observed native runtime differs from the current complete-suite context.'}
        }else{$null=Pin-ModelInput $pin.path}
    }
    foreach($pin in $run.inputs){Assert-HoverIdentity $pin;$null=Pin-ModelInput $pin.path}
    $runs.Add([pscustomobject][ordered]@{name=$run.name;report=$reportPath;nativeReport=$run.report;width=$expectedWidth;height=$expectedHeight;checks=$raw.checks;hoverChecks=$hover;tooltipLifecycle=$tooltip;
        processId=$run.diagnosticVerification.processId;startedUTC=$run.diagnosticVerification.startedUTC;finishedUTC=$run.diagnosticVerification.finishedUTC;diagnosticRecords=$records.Count;passed=$true})
}
if(@($runs.name|Sort-Object -Unique).Count -ne 2){throw 'Both required screen sizes must be observed independently.'}

$play=Read-ModelInput 'Artifacts/QA/launcher-play-smoke.json'
$msiBuild=Read-ModelInput 'Artifacts/QA/installer-build.json'
$msiTest=Read-ModelInput 'Artifacts/QA/installer-tests.json'
$runtime=Read-ModelInput 'Artifacts/QA/app-local-runtime.json'
$msi=Get-HoverIdentity ('Artifacts/Release/'+$release.installer)
$gameArchive=Get-HoverIdentity ('Artifacts/Release/'+$release.archive)
$launcher=Get-HoverIdentity ('Artifacts/Release/'+$release.launcher)
if($play.passed -ne $true -or $play.installed -ne $Version -or $play.nativeSha256 -ne $native.sha256 -or $play.verifiedGameFiles -ne $manifest.files.Count -or
   $play.legacySavePreserved -ne $true -or $play.launcherSha256 -ne $launcher.sha256 -or $play.nativeExitCode -ne 0 -or $play.bootstrapExitCode -ne 0 -or
   $msiBuild.passed -ne $true -or $msiBuild.version -ne $Version -or $msiBuild.sha256 -ne $msi.sha256 -or $msiBuild.size -ne $msi.bytes -or $msiBuild.gameArchiveSha256 -ne $gameArchive.sha256 -or
   $msiTest.passed -ne $true -or $msiTest.installedVersion -ne $Version -or $msiTest.upgradeMsiSha256 -ne $msi.sha256 -or $msiTest.installedArchiveSha256 -ne $gameArchive.sha256 -or
   $msiTest.installedGameFiles -ne $manifest.files.Count -or $msiTest.checkCount -lt 51 -or @($msiTest.checks).Count -ne $msiTest.checkCount -or $msiTest.failure -or
   $runtime.passed -ne $true -or $release.installerChecks -lt 51 -or $release.installerSha256 -ne $msi.sha256 -or $release.archiveSha256 -ne $gameArchive.sha256 -or
   $manifest.sha256 -ne $gameArchive.sha256 -or $manifest.size -ne $gameArchive.bytes -or $release.launcherSha256 -ne $launcher.sha256){throw 'Current Windows finalization, real WPF Play and fifty-one-check installer delivery identities must agree.'}
if(@($play.contexts|Where-Object {$_.event -eq 'system_context' -and $_.gameVersion -eq $Version -and $_.build -eq 'Shipping' -and $_.processId -eq $play.nativeProcessId}).Count -lt 1){throw 'Actual WPF Play lacks its current observed Shipping process context.'}
foreach($path in $ReleaseDocuments+@('Build/Test-CardDrag.ps1','Build/Test-Unreal.ps1','Build/Curate-UnrealQA.ps1','Build/ShippingDiagnostics.ps1','Build/Test-LauncherPlay.ps1','Installer/Test-Installer.ps1')){$null=Pin-ModelInput $path}
$inputs=@($modelInputs|Sort-Object path -Unique)
if(($inputs|Measure-Object bytes -Sum).Sum -gt 25MB){throw 'The compact hover evidence exceeds its 25 MiB budget.'}
$zipName=[IO.Path]::GetFileName($zipPath)
$sumLines=@(Get-Content -LiteralPath $sumPath)
if(@($sumLines|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'A frozen hotfix checksum row already exists.'}
$summary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Stable native card-stat tooltips plus retained production pointer routing at 1920 by 1080 and 1280 by 720';
    nativeExecutable=$native;integrationTests=$integration.tests.Count;runs=$runs.ToArray();windowsChecks=$msiTest.checkCount;windowsDelivery=[ordered]@{manifest='Artifacts/Release/update-manifest.json';gameArchive=$gameArchive;installer=$msi;launcher=$launcher};
    inheritedSourceAcceptance=[ordered]@{version='1.3.0';evidence=$baselineZip;modelSourceSha256=$baseline.modelSourceSha256;unchangedSixAuthoritativeSources=$true;unchangedModelSourceAndCardArt=$true;unchangedFontSourcesAndImportedAssets=$true;
        sourcePins=$unchanged.ToArray();importedAssetPins=$baseline.importedAssetPins;physicalPortraitsPreviouslyReviewed=14;physicalActionPosesPreviouslyReviewed=54;newPhysicalModelAuditRun=$false;newBalanceCohortRun=$false};
    inputs=$inputs;publicDelivery='Pending publication. Anonymous download and published update checks are separate postpublication evidence.'}
function Convert-HoverSanitized([string]$Text){
    $Text=$Text.Replace($modelRepo.Replace('\','\\'),'.').Replace($modelRepo.Replace('\','/'),'.').Replace($modelRepo,'.')
    if($env:USERPROFILE){$Text=$Text.Replace($env:USERPROFILE.Replace('\','\\'),'%USERPROFILE%').Replace($env:USERPROFILE.Replace('\','/'),'%USERPROFILE%').Replace($env:USERPROFILE,'%USERPROFILE%')}
    $Text=[regex]::Replace($Text,'("deviceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation"')
    $Text=[regex]::Replace($Text,'("instanceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation instance"')
    if($Text.Contains($modelRepo) -or ($env:USERPROFILE -and $Text.Contains($env:USERPROFILE))){throw 'A sanitized hotfix entry exposes a workspace or profile path.'}
    return $Text
}
$summaryText=Convert-HoverSanitized ($summary|ConvertTo-Json -Depth 100)
$null=$summaryText|ConvertFrom-Json -Depth 100
$summaryBytes=[Text.UTF8Encoding]::new($false).GetBytes($summaryText)
$temporary=$zipPath+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
$archive=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create)
$inventory=[Collections.Generic.List[object]]::new()
function Write-HoverEntry([string]$Name,[byte[]]$Bytes){
    $entry=$archive.CreateEntry($Name,[IO.Compression.CompressionLevel]::Optimal)
    $stream=$entry.Open();try{$stream.Write($Bytes,0,$Bytes.Length)}finally{$stream.Dispose()}
    return [pscustomobject][ordered]@{entry=$Name;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant();bytes=$Bytes.Length}
}
try{
    foreach($pin in $inputs){
        Assert-HoverIdentity $pin
        $source=Resolve-ModelInput $pin.path
        $sanitized=[IO.Path]::GetExtension($source) -in @('.json','.log')
        if($sanitized){
            $text=Convert-HoverSanitized (Get-Content -LiteralPath $source -Raw)
            if([IO.Path]::GetExtension($source) -eq '.json'){$null=$text|ConvertFrom-Json -Depth 100}
            $bytes=[Text.UTF8Encoding]::new($false).GetBytes($text)
        }else{$bytes=[IO.File]::ReadAllBytes($source)}
        $entry=Write-HoverEntry $pin.path $bytes
        $entry|Add-Member sourceSha256 $pin.sha256;$entry|Add-Member sourceBytes $pin.bytes;$entry|Add-Member sanitized $sanitized
        $inventory.Add($entry)
    }
    $inventory.Add((Write-HoverEntry $summaryRelative $summaryBytes))
    $inventoryBytes=[Text.UTF8Encoding]::new($false).GetBytes(([ordered]@{schema=1;version=$Version;entries=$inventory.ToArray()}|ConvertTo-Json -Depth 20))
    $inventory.Add((Write-HoverEntry 'evidence-inventory.json' $inventoryBytes))
}finally{$archive.Dispose()}
$archive=[IO.Compression.ZipFile]::OpenRead($temporary)
try{
    if($archive.Entries.Count -ne $inventory.Count -or @($archive.Entries.FullName|Sort-Object -Unique).Count -ne $inventory.Count){throw 'Unexpected hotfix evidence inventory after reopening.'}
    foreach($pin in $inventory){
        $entry=$archive.GetEntry($pin.entry)
        if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Hotfix evidence entry size mismatch after reopening.'}
        $stream=$entry.Open();try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($hash -ne $pin.sha256){throw 'Hotfix evidence entry hash mismatch after reopening.'}
    }
}finally{$archive.Dispose()}
foreach($pin in @($context.sourceHashes)+@($context.runtimeModules)+@($unchanged.ToArray())+@($baseline.importedAssetPins)+@($payload.ToArray())+@($native,$baselineZip,$msi,$gameArchive,$launcher)){Assert-HoverIdentity $pin}
if((Test-Path -LiteralPath $zipPath) -or (Test-Path -LiteralPath $summaryPath)){throw 'Frozen hotfix outputs appeared during acceptance; the verified temporary ZIP remains available.'}
Move-Item -LiteralPath $temporary -Destination $zipPath
[IO.File]::WriteAllBytes($summaryPath,$summaryBytes)
$zipHash=(Get-FileHash -LiteralPath $zipPath).Hash.ToLowerInvariant()
# Re-read just before writing so independently created release rows survive.
$sumLines=@(Get-Content -LiteralPath $sumPath)
if(@($sumLines|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'The hotfix checksum row appeared during packaging.'}
$sumLines+=($zipHash+'  '+$zipName)
[IO.File]::WriteAllLines($sumPath,$sumLines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($inventory.Count) verified hover hotfix evidence entries: $zipHash"
