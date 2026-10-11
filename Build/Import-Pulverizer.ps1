param([string]$EngineRoot='C:/Program Files/Epic Games/UE_5.8',[switch]$Audit)
$ErrorActionPreference='Stop'
$riftRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$riftQa=Join-Path $riftRoot ('Artifacts/QA/pulverizer-import-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $riftQa -Force | Out-Null
$riftScript=Join-Path $PSScriptRoot $(if($Audit){'inspect_unreal_assets.py'}else{'import_unreal_assets.py'})
$riftProject=Join-Path $riftRoot 'Unreal/RiftCrownArena/RiftCrownArena.uproject'
$riftEditor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$riftBefore=@(Get-ChildItem -LiteralPath (Join-Path $riftRoot 'Unreal/RiftCrownArena/Content') -File -Recurse | Where-Object {$_.Extension -in @('.uasset','.umap') -and $_.FullName -notmatch '[\\/]pulverizer[\\/]|DA_pulverizer|T_Card_pulverizer|M_RiftShockwave'} | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($riftRoot,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$riftModules=@('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll') | ForEach-Object {
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $riftRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$riftStart=[DateTime]::UtcNow
& $riftEditor $riftProject '-run=pythonscript' "-script=$riftScript" '-RiftPulverizerOnly' '-unattended' '-NullRHI' '-nosound' '-nopause' "-UserDir=$riftQa/EngineUser" "-abslog=$riftQa/commandlet.log" *> (Join-Path $riftQa 'stdout.log')
$riftExit=$LASTEXITCODE
$riftReportName=if($Audit){'unreal_asset_audit.json'}else{'unreal_asset_import.json'}
$riftReportPath=Join-Path $riftRoot ('Artifacts/QA/'+$riftReportName)
Copy-Item -LiteralPath $riftReportPath -Destination (Join-Path $riftQa $riftReportName)
$riftReport=Get-Content -LiteralPath $riftReportPath -Raw | ConvertFrom-Json -Depth 100
$riftChanged=@($riftBefore | Where-Object {(Get-FileHash -LiteralPath (Join-Path $riftRoot $_.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $_.sha256})
$riftModulesUnchanged=@($riftModules | Where-Object {(Get-FileHash -LiteralPath (Join-Path $riftRoot $_.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $_.sha256}).Count -eq 0
$riftPassed=$riftExit -eq 0 -and $riftReport.passed -and $riftChanged.Count -eq 0 -and $riftModulesUnchanged
$riftProof=[ordered]@{schema=2;version='1.5.0';operation=$(if($Audit){'readonly-audit'}else{'pulverizer-only-import'});startedUtc=$riftStart.ToString('o');endedUtc=[DateTime]::UtcNow.ToString('o');exitCode=$riftExit;passed=$riftPassed;runtimeModules=$riftModules;runtimeModulesUnchanged=$riftModulesUnchanged;existingAssetCount=$riftBefore.Count;existingAssetsUnchanged=$riftChanged.Count -eq 0;changedExistingAssets=$riftChanged;existingAssetPins=$riftBefore;reportSha256=(Get-FileHash -LiteralPath $riftReportPath -Algorithm SHA256).Hash.ToLowerInvariant()}
[IO.File]::WriteAllText((Join-Path $riftQa 'run.json'),($riftProof|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
if(!$riftPassed){throw "Pulverizer asset operation failed: $riftQa"}
Write-Output "Pulverizer asset operation passed; $($riftBefore.Count) existing assets unchanged. $riftQa"
