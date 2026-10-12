param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Filter = 'Rift',
    [switch]$Rendered
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$runRoot = Join-Path $repoRoot ('Build\Automation\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N'))
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
        Where-Object Extension -in @('.cpp','.h','.cs','.ini','.inc') | ForEach-Object FullName
) + @($projectPath,(Join-Path $repoRoot 'Build/Test-Unreal.ps1'),(Join-Path $repoRoot 'Build/generate_audio.py'),(Join-Path $repoRoot 'Build/generate_expansion_audio.py'),
    (Join-Path $repoRoot 'Assets/Source/Audio/audio_manifest.json'),(Join-Path $repoRoot 'Assets/Source/Audio/Palette/sources.json'),
    (Join-Path $repoRoot 'Assets/Data/card_manifest.json'),(Join-Path $repoRoot 'Build/generate_card_manifest.py'))
$riftQaSourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Assets/Source/Audio') -File | Where-Object Extension -eq '.wav' | ForEach-Object FullName)
$riftQaSourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Content') -File -Recurse | Where-Object Extension -in @('.uasset','.umap') | ForEach-Object FullName)
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
    rendering=[ordered]@{requested=[bool]$Rendered;mode=$(if($Rendered){'offscreen-d3d12'}else{'nullrhi'});observedRhi=$null};
    report=[IO.Path]::GetRelativePath($repoRoot,(Join-Path $reportRoot 'index.json')).Replace('\','/');
    commandletLog=[IO.Path]::GetRelativePath($repoRoot,(Join-Path $runRoot 'UnrealIntegration.log')).Replace('\','/');
    sourceHashes=$riftQaSourceHashes;runtimeModules=$riftQaModules;
    isolation=[ordered]@{saveRoot=[IO.Path]::GetRelativePath($repoRoot,$saveRoot).Replace('\','/');engineUserRoot=[IO.Path]::GetRelativePath($repoRoot,$engineUserRoot).Replace('\','/');automationSandbox=$true};
    completed=$false;sourcesUnchanged=$false;runtimeModulesUnchanged=$false}
$riftQaContextPath = Join-Path $runRoot 'context.json'
[IO.File]::WriteAllText($riftQaContextPath,($riftQaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
[string[]]$riftQaRenderArguments = @(if($Rendered){'-d3d12';'-RenderOffscreen';'-windowed';'-ResX=1280';'-ResY=720';'-NoSplash'}else{'-NullRHI'})
& $editor $projectPath '-unattended' @riftQaRenderArguments '-nosound' '-nopause' '-stdout' '-FullStdOutLogOutput' '-RiftAutomationSandbox' "-RiftSaveRoot=$saveRoot" "-UserDir=$engineUserRoot" "-ReportExportPath=$reportRoot" "-ExecCmds=Automation RunTests $Filter" '-TestExit=Automation Test Queue Empty' "-abslog=$runRoot\UnrealIntegration.log"
$riftQaContext.commandletExitCode = $LASTEXITCODE
$riftQaContext.completedUtc = [DateTime]::UtcNow.ToString('o')
[IO.File]::WriteAllText($riftQaContextPath,($riftQaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
if ($riftQaContext.commandletExitCode -ne 0) { throw "Unreal integration automation failed with exit code $($riftQaContext.commandletExitCode)" }
$report = Join-Path $reportRoot 'index.json'
if (!(Test-Path -LiteralPath $report)) { throw "Automation did not export a report: $report" }
$result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
$riftQaLog = Get-Content -LiteralPath (Join-Path $runRoot 'UnrealIntegration.log') -Raw
if ($Rendered) {
    if ($riftQaLog -notmatch 'LogRHI:.*Using Default RHI: D3D12|LogD3D12RHI:.*Found D3D12 adapter|LogD3D12RHI:.*Chosen D3D12 Adapter') {
        throw 'Rendered automation did not confirm a D3D12 renderer in the engine log.'
    }
    $riftQaContext.rendering.observedRhi = 'D3D12'
} else {
    if ($riftQaLog -notmatch 'LogInit: Command Line:[^\r\n]*\s-NullRHI(?:\s|$)' -or
        $riftQaLog -match 'LogD3D12RHI:.*Chosen D3D12 Adapter|LogRHI:.*Using Default RHI: D3D12') {
        throw 'Headless automation did not confirm an intact NullRHI argument without hardware renderer initialization.'
    }
    $riftQaContext.rendering.observedRhi = 'NullRHI'
}
$passed = [int]$result.succeeded + [int]$result.succeededWithWarnings
if ($result.failed -gt 0 -or $result.notRun -gt 0 -or $passed -lt 1) { throw "Automation reported failures or unrun tests. Inspect $report" }
foreach ($riftQaHash in $riftQaSourceHashes + $riftQaModules) {
    if (!(Test-Path -LiteralPath (Join-Path $repoRoot $riftQaHash.path) -PathType Leaf)) {
        throw "Source or loaded module was deleted during automation: $($riftQaHash.path)."
    }
    if ((Get-FileHash -LiteralPath (Join-Path $repoRoot $riftQaHash.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $riftQaHash.sha256) {
        throw "Source or loaded module changed while automation ran: $($riftQaHash.path). Rebuild and rerun."
    }
}
$riftQaTrackedTrees=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Config'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Content') -File -Recurse |
    Where-Object Extension -in @('.cpp','.h','.cs','.ini','.inc','.uasset','.umap') |
    ForEach-Object {[IO.Path]::GetRelativePath($repoRoot,$_.FullName).Replace('\','/')})
$riftQaPinnedTreePaths=@($riftQaSourceHashes.path|Where-Object {$_ -match '^Unreal/RiftCrownArena/(Source|Config|Content)/'})
if (Compare-Object ($riftQaPinnedTreePaths|Sort-Object -Unique) ($riftQaTrackedTrees|Sort-Object -Unique)) {
    throw 'The source/config/Content inventory changed during automation. Rebuild and rerun.'
}
$riftQaContext.completed = $true
$riftQaContext.sourcesUnchanged = $true
$riftQaContext.runtimeModulesUnchanged = $true
$riftQaContext.wrapperExitCode = 0
$riftQaContext.passedTests = $passed
$riftQaContext.sourceReportSha256 = (Get-FileHash -LiteralPath $report -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($riftQaContextPath,($riftQaContext | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output "Unreal automation passed $passed tests. Report: $report"
