param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateSet('Development','Shipping')][string]$Configuration = 'Development',
    [switch]$Package,
    [string]$ArchiveDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$outputRoot = if ($ArchiveDirectory) { [IO.Path]::GetFullPath($ArchiveDirectory) } else { Join-Path $repoRoot 'Artifacts\Game' }
if (!(Test-Path -LiteralPath $projectPath)) { throw "Project not found: $projectPath" }
if ($Package) {
    $riftDebugStageArgs = @()
    if ($Configuration -eq 'Shipping') { $riftDebugStageArgs += '-nodebuginfo' }
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$projectPath" -noP4 -platform=Win64 "-clientconfig=$Configuration" -build -cook -stage -pak -iostore -archive "-archivedirectory=$outputRoot" -prereqs -unattended -utf8output @riftDebugStageArgs
} else {
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') RiftCrownArenaEditor Win64 Development "-Project=$projectPath" -WaitMutex -NoHotReloadFromIDE
}
if ($LASTEXITCODE -ne 0) { throw "Unreal build failed with exit code $LASTEXITCODE" }
