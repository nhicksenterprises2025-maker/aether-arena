param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Filter = 'Rift'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$runRoot = Join-Path $repoRoot ('Build\Automation\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$saveRoot = Join-Path $runRoot 'UserData'
$engineUserRoot = Join-Path $runRoot 'EngineUserData'
$reportRoot = Join-Path $runRoot 'Report'
New-Item -ItemType Directory -Path $saveRoot, $engineUserRoot, $reportRoot -Force | Out-Null
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $editor)) { throw "Unreal commandlet not found: $editor" }
# Freeze what the commandlet actually loads before it runs. Source and runtime
# changes during automation invalidate the run instead of being relabeled later.
$riftQaSourcePaths = @(
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Config') -File -Recurse |
        Where-Object Extension -in @('.cpp','.h','.cs','.ini') | ForEach-Object FullName
) + @($projectPath,(Join-Path $repoRoot 'Build/Test-Unreal.ps1'),(Join-Path $repoRoot 'Build/generate_audio.py'),
    (Join-Path $repoRoot 'Assets/Source/Audio/audio_manifest.json'),(Join-Path $repoRoot 'Assets/Source/Audio/Palette/sources.json'))
$riftQaSourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Assets/Source/Audio') -File | Where-Object Extension -eq '.wav' | ForEach-Object FullName)
$riftQaSourceHashes = @($riftQaSourcePaths | Sort-Object -Unique | ForEach-Object {
    if (!(Test-Path -LiteralPath $_ -PathType Leaf)) { throw "Automation provenance source is missing: $_" }
    [ordered]@{path=[IO.Path]::GetRelativePath($repoRoot,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$riftQaModulePaths = @('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll',
    'Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll')
$riftQaModules = @($riftQaModulePaths | ForEach-Object {
    $riftQaModule = Join-Path $repoRoot $_
    if (!(Test-Path -LiteralPath $riftQaModule -PathType Leaf)) { throw "Build the Editor module before running automation: $_" }
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $riftQaModule -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$riftQaContext = [ordered]@{schema=2;startedUtc=[DateTime]::UtcNow.ToString('o');filter=$Filter;
    report=[IO.Path]::GetRelativePath($repoRoot,(Join-Path $reportRoot 'index.json')).Replace('\','/');
    commandletLog=[IO.Path]::GetRelativePath($repoRoot,(Join-Path $runRoot 'UnrealIntegration.log')).Replace('\','/');
    sourceHashes=$riftQaSourceHashes;runtimeModules=$riftQaModules;
    isolation=[ordered]@{saveRoot=[IO.Path]::GetRelativePath($repoRoot,$saveRoot).Replace('\','/');engineUserRoot=[IO.Path]::GetRelativePath($repoRoot,$engineUserRoot).Replace('\','/');automationSandbox=$true};
    completed=$false;sourcesUnchanged=$false;runtimeModulesUnchanged=$false}
$riftQaContextPath = Join-Path $runRoot 'context.json'
[IO.File]::WriteAllText($riftQaContextPath,($riftQaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
& $editor $projectPath '-unattended' '-NullRHI' '-nosound' '-nopause' '-stdout' '-FullStdOutLogOutput' '-RiftAutomationSandbox' "-RiftSaveRoot=$saveRoot" "-UserDir=$engineUserRoot" "-ReportExportPath=$reportRoot" "-ExecCmds=Automation RunTests $Filter" '-TestExit=Automation Test Queue Empty' "-abslog=$runRoot\UnrealIntegration.log"
$riftQaContext.commandletExitCode = $LASTEXITCODE
$riftQaContext.completedUtc = [DateTime]::UtcNow.ToString('o')
[IO.File]::WriteAllText($riftQaContextPath,($riftQaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
if ($riftQaContext.commandletExitCode -ne 0) { throw "Unreal integration automation failed with exit code $($riftQaContext.commandletExitCode)" }
$report = Join-Path $reportRoot 'index.json'
if (!(Test-Path -LiteralPath $report)) { throw "Automation did not export a report: $report" }
$result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
$passed = [int]$result.succeeded + [int]$result.succeededWithWarnings
if ($result.failed -gt 0 -or $result.notRun -gt 0 -or $passed -lt 1) { throw "Automation reported failures or unrun tests. Inspect $report" }
foreach ($riftQaHash in $riftQaSourceHashes + $riftQaModules) {
    if ((Get-FileHash -LiteralPath (Join-Path $repoRoot $riftQaHash.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $riftQaHash.sha256) {
        throw "Source or loaded module changed while automation ran: $($riftQaHash.path). Rebuild and rerun."
    }
}
$riftQaContext.completed = $true
$riftQaContext.sourcesUnchanged = $true
$riftQaContext.runtimeModulesUnchanged = $true
$riftQaContext.wrapperExitCode = 0
$riftQaContext.passedTests = $passed
$riftQaContext.sourceReportSha256 = (Get-FileHash -LiteralPath $report -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($riftQaContextPath,($riftQaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output "Unreal automation passed $passed tests. Report: $report"
