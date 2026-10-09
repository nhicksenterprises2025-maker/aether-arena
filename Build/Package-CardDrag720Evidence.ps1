param(
    [string]$Executable='Artifacts/Game-1.3.0/Windows/RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe',
    [string]$Report='Artifacts/QA/CardDrag/model130-drag-shipping720-verification.json'
)
# Supplemental evidence for the second actual packaged pointer-routing run.
# The previously frozen main acceptance archive remains immutable.
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this evidence packager with PowerShell 7.'}
$Version='1.3.0'
$modelRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$summaryRelative='Docs/QA/card-drag-720-1.3.0.json'
$zipRelative='Artifacts/Release/RiftCrownArena-CardDrag720-QAEvidence-1.3.0.zip'
$zip=Join-Path $modelRepo $zipRelative
$summaryPath=Join-Path $modelRepo $summaryRelative
if((Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath $summaryPath)){throw 'The frozen 720p evidence archive or curated summary already exists.'}
$modelInputs=[Collections.Generic.List[object]]::new()
$modelPinIndex=@{}
$modelSyntheticInputs=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

# Reuse the exact strict file/isolation/context validators in the main gate,
# without evaluating that script's top-level acceptance or packaging actions.
$validator=Join-Path $PSScriptRoot 'Package-ModelInputQAEvidence.ps1'
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($validator,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'The maintained main evidence validator does not parse.'}
$functions=@('Resolve-ModelInput','Pin-ModelInput','Read-ModelInput','Get-ModelUtcTimestamp','Assert-ModelShippingDiagnostics','Assert-ModelRecord','Assert-ModelChecks')
foreach($name in $functions){
    $matches=@($ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true))
    if($matches.Count -ne 1){throw "The exact maintained acceptance validator is missing: $name"}
    Invoke-Expression $matches[0].Extent.Text
}
$null=Pin-ModelInput $validator
$null=Pin-ModelInput $PSCommandPath
$native=Resolve-ModelInput $Executable
$nativeIdentity=[ordered]@{path=[IO.Path]::GetRelativePath($modelRepo,$native).Replace('\','/');sha256=(Get-FileHash -LiteralPath $native).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $native).Length}
$manifest=Read-ModelInput 'Artifacts/Release/update-manifest.json'
$release=Read-ModelInput 'Artifacts/Release/windows-release-verification.json'
$entry=@($manifest.files|Where-Object path -match 'Binaries/Win64/RiftCrownArena-Win64-Shipping.exe$')
if($release.passed -ne $true -or $release.version -ne $Version -or $manifest.version -ne $Version -or $entry.Count -ne 1 -or
   $entry[0].sha256 -ne $nativeIdentity.sha256 -or $entry[0].size -ne $nativeIdentity.bytes -or
   !$native.Replace('\','/').EndsWith('/'+$entry[0].path,[StringComparison]::OrdinalIgnoreCase)){throw 'The requested native executable differs from the final accepted Windows payload.'}
$integration=Read-ModelInput 'Docs/QA/native-integration.json'
$context=$integration.curatedEvidence
if($integration.failed -ne 0 -or $integration.notRun -ne 0 -or @($integration.tests).Count -lt 15 -or
   @($integration.tests|Where-Object {$_.state -notin @('Success','SuccessWithWarnings')}).Count -ne 0 -or
   $context.version -ne $Version -or $context.allScenariosObserved -ne $true -or $context.commandletExitCode -ne 0 -or $context.wrapperExitCode -ne 0){throw 'The current complete native integration context is not accepted.'}
foreach($required in @('Rift.Integration.Typography','Rift.Integration.BattleInputRouting','Rift.Integration.SpellCastReplay')){
    if(@($integration.tests|Where-Object fullTestPath -eq $required).Count -ne 1){throw "Required current native integration test is missing: $required"}
}
foreach($source in $context.sourceHashes){
    if((Get-FileHash -LiteralPath (Resolve-ModelInput $source.path)).Hash.ToLowerInvariant() -ne $source.sha256){throw 'A current complete-suite source changed after its accepted execution.'}
}
foreach($module in $context.runtimeModules){
    if((Get-FileHash -LiteralPath (Resolve-ModelInput $module.path)).Hash.ToLowerInvariant() -ne $module.sha256){throw 'A current complete-suite runtime module changed after acceptance.'}
}
$main=Read-ModelInput 'Docs/QA/model-input-checks-1.3.0.json'
$mainZip=Resolve-ModelInput 'Artifacts/Release/RiftCrownArena-QAEvidence-Windows-x64-1.3.0.zip'
if($main.passed -ne $true -or $main.version -ne $Version -or $main.nativeExecutableSha256 -ne $nativeIdentity.sha256){throw 'The immutable primary acceptance packet identifies a different runtime.'}
$primaryIdentity=[ordered]@{path=[IO.Path]::GetRelativePath($modelRepo,$mainZip).Replace('\','/');sha256=(Get-FileHash -LiteralPath $mainZip).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $mainZip).Length}

$run=Read-ModelInput $Report
Assert-ModelChecks $run 29 'Actual 720p native card drag routing'
$words=($run.checks|ForEach-Object {$_.name+' '+$_.detail}) -join ' '
if($run.schemaVersion -ne 1 -or $run.version -ne $Version -or $run.name -ne 'model130-drag-shipping720' -or
   $run.executableSha256 -ne $nativeIdentity.sha256 -or $run.editor -ne $false -or $run.freshReport -ne $true -or $run.timedOut -ne $false -or
   $run.exitCode -ne 0 -or $run.sourcePinsUnchanged -ne $true -or $run.executableUnchanged -ne $true -or $run.width -ne 1280 -or $run.height -ne 720 -or
   $run.checkCount -ne 29 -or @($run.checks).Count -ne 29 -or @($run.checks.name|Sort-Object -Unique).Count -ne 29 -or
   $run.scope -notmatch '(?i)Real Slate mouse routing through the production UMG hand' -or $words -notmatch '(?i)Slate' -or
   $words -notmatch '(?i)(return|cancel)' -or $words -notmatch '(?i)(deploy|play|commit)'){throw 'The actual second Shipping run must prove all twenty-nine production Slate checks at 1280 by 720.'}
$raw=Read-ModelInput $run.report
$rawPin=$modelPinIndex[$run.report]
if($rawPin.sha256 -ne $run.nativeReportSha256 -or $raw.schemaVersion -ne 1 -or $raw.passed -ne $true -or $raw.version -ne $Version -or
   $raw.width -ne 1280 -or $raw.height -ne 720 -or $raw.checkCount -ne 29 -or @($raw.checks).Count -ne 29 -or
   @($raw.checks|Where-Object {$_.passed -ne $true}).Count -ne 0 -or @($raw.checks.name|Sort-Object -Unique).Count -ne 29 -or
   $raw.route -notmatch 'FSlateApplication ProcessMouseButtonDownEvent / ProcessMouseMoveEvent / ProcessMouseButtonUpEvent' -or
   (($raw.checks.name|Sort-Object)-join '|') -ne (($run.checks.name|Sort-Object)-join '|')){throw 'Normalized 720p proof differs from its actual complete native pointer-routing report.'}
$started=Get-ModelUtcTimestamp $run.diagnosticVerification.startedUTC
$finished=Get-ModelUtcTimestamp $run.diagnosticVerification.finishedUTC
$written=(Get-Item -LiteralPath (Resolve-ModelInput $run.report)).LastWriteTimeUtc
if($written -lt $started -or $written -gt $finished){throw 'The raw 720p native report was not freshly written during its isolated execution.'}
$records=(Assert-ModelShippingDiagnostics $run '720p native pointer routing').records
$pass=@($records|Where-Object {$_.message -match '^Card drag route: PASS '})
if($pass.Count -ne 29 -or @($records|Where-Object {$_.message -match '^Card drag route: FAIL '}).Count -ne 0 -or
   @($records|Where-Object {$_.message -match '^Native card drag smoke passed: '}).Count -ne 1 -or
   $run.diagnosticVerification.passCount -ne 29 -or $run.diagnosticVerification.routeRecordsMatch -ne $true -or $run.diagnosticVerification.finalPass -ne $true){throw '720p native diagnostics do not prove all checks and final completion.'}
foreach($check in $raw.checks){
    if(@($pass|Where-Object message -eq ('Card drag route: PASS '+$check.name)).Count -ne 1){throw 'The raw 720p check names differ from their actual diagnostic PASS records.'}
}
foreach($required in @('Build/Test-CardDrag.ps1','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Tests/RiftCardDragSmoke.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftUIWidget.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftHandButton.cpp','Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftGameMode.cpp')){
    if(@($run.sourcePins|Where-Object path -eq $required).Count -ne 1){throw "Actual 720p test source provenance is missing: $required"}
}
foreach($source in $run.sourcePins){
    if($source.path -in @('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll')){
        $module=@($context.runtimeModules|Where-Object path -eq $source.path)
        $file=Resolve-ModelInput $source.path
        if($module.Count -ne 1 -or $module[0].sha256 -ne $source.sha256 -or (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -ne $source.sha256 -or
           (Get-Item -LiteralPath $file).Length -ne $source.bytes){throw 'Observed 720p runtime identity differs from the complete-suite context.'}
        continue
    }
    $pin=Pin-ModelInput $source.path
    if($pin.sha256 -ne $source.sha256 -or $pin.bytes -ne $source.bytes){throw 'Actual 720p drag source bytes changed after execution.'}
}
foreach($input in $run.inputs){
    $pin=Pin-ModelInput $input.path
    if($pin.sha256 -ne $input.sha256 -or ($input.bytes -and $pin.bytes -ne $input.bytes)){throw 'A native 720p execution input changed after verification.'}
}
$inputs=@($modelInputs|Sort-Object path -Unique)
if(($inputs|Measure-Object bytes -Sum).Sum -gt 20MB){throw 'The supplemental 720p evidence exceeds its 20 MiB budget.'}
$summary=[ordered]@{schema=1;version=$Version;passed=$true;utc=[DateTime]::UtcNow.ToString('o');scope='Independent second actual Shipping card drag run at 1280 by 720 through the production Slate hand';
    nativeExecutable=$nativeIdentity;primaryEvidence=$primaryIdentity;resolution=[ordered]@{width=1280;height=720};checkCount=29;checks=$raw.checks;
    route=$raw.route;processId=$run.diagnosticVerification.processId;startedUTC=$run.diagnosticVerification.startedUTC;finishedUTC=$run.diagnosticVerification.finishedUTC;
    diagnosticRecords=$records.Count;diagnosticsPassed=$true;sourcePinsUnchanged=$true;executableUnchanged=$true;report=$Report;nativeReport=$run.report;inputs=$inputs;
    publicDelivery='Supplemental local acceptance evidence. The separately frozen main evidence packet remains unchanged.'}
$summaryBytes=[Text.UTF8Encoding]::new($false).GetBytes(($summary|ConvertTo-Json -Depth 100))
$temporary=$zip+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
$archive=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create)
$inventory=[Collections.Generic.List[object]]::new()
function Write-CardDragEntry([string]$Name,[byte[]]$Bytes){
    $entry=$archive.CreateEntry($Name,[IO.Compression.CompressionLevel]::Optimal)
    $stream=$entry.Open();try{$stream.Write($Bytes,0,$Bytes.Length)}finally{$stream.Dispose()}
    return [pscustomobject][ordered]@{entry=$Name;sha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant();bytes=$Bytes.Length}
}
try{
    foreach($pin in $inputs){
        $source=Resolve-ModelInput $pin.path
        if((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $pin.sha256 -or (Get-Item -LiteralPath $source).Length -ne $pin.bytes){throw 'Evidence changed during supplemental packaging.'}
        $sanitized=[IO.Path]::GetExtension($source) -in @('.json','.log')
        if($sanitized){
            $text=Get-Content -LiteralPath $source -Raw
            $text=$text.Replace($modelRepo.Replace('\','\\'),'.').Replace($modelRepo.Replace('\','/'),'.').Replace($modelRepo,'.')
            if($env:USERPROFILE){$text=$text.Replace($env:USERPROFILE.Replace('\','\\'),'%USERPROFILE%').Replace($env:USERPROFILE.Replace('\','/'),'%USERPROFILE%').Replace($env:USERPROFILE,'%USERPROFILE%')}
            $text=[regex]::Replace($text,'("deviceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation"')
            $text=[regex]::Replace($text,'("instanceName"\s*:\s*)"(?:\\.|[^"\\])*"','$1"QA workstation instance"')
            if([IO.Path]::GetExtension($source) -eq '.json'){$null=$text|ConvertFrom-Json -Depth 100}
            if($text.Contains($modelRepo) -or ($env:USERPROFILE -and $text.Contains($env:USERPROFILE))){throw 'A sanitized supplemental entry still exposes its local workspace or profile path.'}
            $bytes=[Text.UTF8Encoding]::new($false).GetBytes($text)
        }else{$bytes=[IO.File]::ReadAllBytes($source)}
        $entry=Write-CardDragEntry $pin.path $bytes
        $entry|Add-Member -NotePropertyName sourceSha256 -NotePropertyValue $pin.sha256
        $entry|Add-Member -NotePropertyName sourceBytes -NotePropertyValue $pin.bytes
        $entry|Add-Member -NotePropertyName sanitized -NotePropertyValue $sanitized
        $inventory.Add($entry)
    }
    $inventory.Add((Write-CardDragEntry $summaryRelative $summaryBytes))
    $inventoryBytes=[Text.UTF8Encoding]::new($false).GetBytes(([ordered]@{schema=1;version=$Version;entries=$inventory.ToArray()}|ConvertTo-Json -Depth 20))
    $inventory.Add((Write-CardDragEntry 'evidence-inventory.json' $inventoryBytes))
}finally{$archive.Dispose()}
$archive=[IO.Compression.ZipFile]::OpenRead($temporary)
try{
    if($archive.Entries.Count -ne $inventory.Count -or @($archive.Entries.FullName|Sort-Object -Unique).Count -ne $inventory.Count){throw 'Unexpected supplemental evidence entry inventory.'}
    foreach($pin in $inventory){
        $entry=$archive.GetEntry($pin.entry)
        if(!$entry -or $entry.Length -ne $pin.bytes){throw 'Supplemental evidence entry size differs after reopening.'}
        $stream=$entry.Open();try{$hash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()}finally{$stream.Dispose()}
        if($hash -ne $pin.sha256){throw 'Supplemental evidence entry hash differs after reopening.'}
    }
}finally{$archive.Dispose()}
foreach($source in $context.sourceHashes){
    if((Get-FileHash -LiteralPath (Resolve-ModelInput $source.path)).Hash.ToLowerInvariant() -ne $source.sha256){throw 'A complete-suite source changed during supplemental acceptance.'}
}
if((Get-FileHash -LiteralPath $native).Hash.ToLowerInvariant() -ne $nativeIdentity.sha256 -or (Get-Item -LiteralPath $native).Length -ne $nativeIdentity.bytes -or
   (Get-FileHash -LiteralPath $mainZip).Hash.ToLowerInvariant() -ne $primaryIdentity.sha256){throw 'The frozen executable or primary evidence changed during supplemental acceptance.'}
if((Test-Path -LiteralPath $zip) -or (Test-Path -LiteralPath $summaryPath)){throw 'Frozen supplemental outputs appeared during acceptance; the verified temporary archive is retained.'}
Move-Item -LiteralPath $temporary -Destination $zip
[IO.File]::WriteAllBytes($summaryPath,$summaryBytes)
$zipHash=(Get-FileHash -LiteralPath $zip).Hash.ToLowerInvariant()
$sums=Join-Path $modelRepo 'Artifacts/Release/SHA256SUMS.txt'
$zipName=[IO.Path]::GetFileName($zip)
$lines=@(Get-Content -LiteralPath $sums)
if(@($lines|Where-Object {$_ -match ('\s{2}'+[regex]::Escape($zipName)+'$')}).Count){throw 'The supplemental checksum row already exists.'}
$lines+=($zipHash+'  '+$zipName)
[IO.File]::WriteAllLines($sums,$lines,[Text.UTF8Encoding]::new($false))
"Packaged and reopened $($inventory.Count) verified 720p evidence entries: $zipHash"
