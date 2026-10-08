param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Filter = 'Rift'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$runRoot = Join-Path $repoRoot ('Build\Automation\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$saveRoot = Join-Path $runRoot 'UserData'
$reportRoot = Join-Path $runRoot 'Report'
New-Item -ItemType Directory -Path $saveRoot, $reportRoot -Force | Out-Null
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (!(Test-Path -LiteralPath $editor)) { throw "Unreal commandlet not found: $editor" }
& $editor $projectPath '-unattended' '-NullRHI' '-nosound' '-nopause' '-stdout' '-FullStdOutLogOutput' '-RiftAutomationSandbox' "-RiftSaveRoot=$saveRoot" "-ReportExportPath=$reportRoot" "-ExecCmds=Automation RunTests $Filter" '-TestExit=Automation Test Queue Empty' "-abslog=$runRoot\UnrealIntegration.log"
if ($LASTEXITCODE -ne 0) { throw "Unreal integration automation failed with exit code $LASTEXITCODE" }
$report = Join-Path $reportRoot 'index.json'
if (!(Test-Path -LiteralPath $report)) { throw "Automation did not export a report: $report" }
$result = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
$passed = [int]$result.succeeded + [int]$result.succeededWithWarnings
if ($result.failed -gt 0 -or $result.notRun -gt 0 -or $passed -lt 1) { throw "Automation reported failures or unrun tests. Inspect $report" }
Write-Output "Unreal automation passed $passed tests. Report: $report"
