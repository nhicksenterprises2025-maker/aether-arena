param(
    [string]$Executable = '',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$Seconds = 90,
    [switch]$Stress,
    [switch]$WithMeta,
    [string]$Name = ''
)
$ErrorActionPreference = 'Stop'
$riftMeasureRepo = Split-Path -Parent $PSScriptRoot
if (!$Executable) { $Executable = Join-Path $riftMeasureRepo 'Artifacts\Game\Windows\RiftCrownArena.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Real native game executable not found: $Executable" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftMeasureIsEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -match '^UnrealEditor(-Cmd)?$'
if (!$Name) { $Name = "$(if ($Stress) {'stress'} else {'ai-match'})-$(if ($WithMeta) {'meta-requested'} else {'meta-paused'})-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Use a lowercase alphanumeric QA report name.' }
$riftMeasureRoot = Join-Path $riftMeasureRepo ('Artifacts\QA\Performance\' + $Name)
$riftMeasureSaves = Join-Path $riftMeasureRoot 'UserData'
New-Item -ItemType Directory -Path $riftMeasureSaves -Force | Out-Null
$riftMeasureJson = Join-Path $riftMeasureRoot 'performance.json'
$riftMeasureLog = Join-Path $riftMeasureRoot 'engine.log'
$riftMeasureArgs = @('/Game/Rift/Maps/Arena','-windowed','-RenderOffscreen',"-ResX=$Width","-ResY=$Height",'-unattended','-nosplash','-NoVSync',
    ('-RiftSaveRoot="' + $riftMeasureSaves + '"'),('-UserDir="' + (Join-Path $riftMeasureSaves 'EngineUser') + '"'),
    ('-RiftPerfReport="' + $riftMeasureJson + '"'),"-RiftPerfSeconds=$Seconds",'-RiftPerfWarmup=8',('-abslog="' + $riftMeasureLog + '"'))
if ($riftMeasureIsEditor) { $riftMeasureArgs = @(('"' + (Join-Path $riftMeasureRepo 'Unreal\RiftCrownArena\RiftCrownArena.uproject') + '"'),'-game') + $riftMeasureArgs }
if ($Stress) { $riftMeasureArgs += '-RiftPerfStress' }
if ($WithMeta) { $riftMeasureArgs += '-RiftPerfWithMeta' }
$riftMeasureStarted = Get-Date
$riftMeasureProcess = Start-Process -FilePath $Executable -ArgumentList $riftMeasureArgs -WorkingDirectory (Split-Path -Parent $Executable) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $riftMeasureRoot 'stdout.log') -RedirectStandardError (Join-Path $riftMeasureRoot 'stderr.log')
while (!$riftMeasureProcess.HasExited) {
    if (((Get-Date) - $riftMeasureStarted).TotalSeconds -gt $Seconds + 180) { Stop-Process -Id $riftMeasureProcess.Id -Force; throw 'Native measurement timed out.' }
    Start-Sleep -Milliseconds 250
    $riftMeasureProcess.Refresh()
}
$riftMeasureProcess.WaitForExit()
if ($riftMeasureProcess.ExitCode -ne 0) { throw "Native measurement exited $($riftMeasureProcess.ExitCode). Inspect $riftMeasureRoot" }
if (!(Test-Path -LiteralPath $riftMeasureJson) -or (Get-Item -LiteralPath $riftMeasureJson).LastWriteTime -lt $riftMeasureStarted.AddSeconds(-1)) { throw 'Game did not produce a fresh performance report.' }
$riftMeasured = Get-Content -LiteralPath $riftMeasureJson -Raw | ConvertFrom-Json
if ($riftMeasured.frame.samples -lt 100 -or $riftMeasured.width -ne $Width -or $riftMeasured.height -ne $Height) { throw 'Measurement has insufficient frames or incorrect output dimensions.' }
if ($WithMeta -and $riftMeasured.metaGamesDuringBattle -ne 0) { throw 'Background Meta advanced while the battle was active.' }
Write-Output ($riftMeasured | ConvertTo-Json -Depth 20)
