param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Executable = '',
    [int]$TimeoutSeconds = 180,
    [string]$Name = 'native-audio-smoke'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$qaRoot = Join-Path $repoRoot ('Artifacts\QA\Audio\' + $Name)
$saveRoot = Join-Path $qaRoot 'Save'
$engineUserRoot = Join-Path $qaRoot 'EngineUser'
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'QA name must contain lowercase letters, digits, underscores or hyphens.' }
if (!$Executable) { $Executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Audio QA executable not found: $Executable" }
$isEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -in @('UnrealEditor','UnrealEditor-Cmd')
$workingDirectory = if ($isEditor) { $repoRoot } else { Split-Path -Parent $Executable }
New-Item -ItemType Directory -Path $qaRoot,$saveRoot,$engineUserRoot -Force | Out-Null
$reportPath = Join-Path $qaRoot 'audio-smoke.json'
$logPath = Join-Path $qaRoot 'engine.log'
$arguments = @('/Game/Rift/Maps/Arena','-game','-RenderOffscreen','-windowed','-ResX=1280','-ResY=720',
    ('-RiftSaveRoot="' + $saveRoot + '"'),('-UserDir="' + $engineUserRoot + '"'),
    ('-RiftAudioSmoke="' + $reportPath + '"'),'-unattended','-nosplash','-NoVSync',('-abslog="' + $logPath + '"'))
if ($isEditor) { $arguments = @(('"' + $projectPath + '"')) + $arguments }
# Audio remains enabled: this validates actual AudioDevice components and the bound
# Settings slider callbacks. It is not an acoustic assessment of the loop seam.
$started = Get-Date
$process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $qaRoot 'stdout.log') -RedirectStandardError (Join-Path $qaRoot 'stderr.log')
$timedOut = $false
while (!$process.HasExited) {
    if (((Get-Date)-$started).TotalSeconds -gt $TimeoutSeconds) { $timedOut = $true; Stop-Process -Id $process.Id -Force; break }
    Start-Sleep -Milliseconds 250
    $process.Refresh()
}
$process.WaitForExit()
$freshReport = (Test-Path -LiteralPath $reportPath) -and ((Get-Item -LiteralPath $reportPath).LastWriteTime -ge $started.AddSeconds(-1))
$errors = @()
if (Test-Path -LiteralPath $logPath) { $errors = @(Select-String -LiteralPath $logPath -Pattern 'LogRift: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Authored audio asset missing' | ForEach-Object { $_.Line }) }
$passed = $false
if ($freshReport) { $passed = (Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json).passed -eq $true }
$run = [ordered]@{ executable=$Executable; editor=$isEditor; workingDirectory=$workingDirectory; audioDisabled=$false;
    engineUserRoot=$engineUserRoot; report=$reportPath; engineLog=$logPath; freshReport=$freshReport;
    timedOut=$timedOut; exitCode=$process.ExitCode; seconds=[math]::Round(((Get-Date)-$started).TotalSeconds,2); passed=$passed; errors=$errors }
[System.IO.File]::WriteAllText((Join-Path $qaRoot 'run.json'),($run | ConvertTo-Json -Depth 10),[System.Text.UTF8Encoding]::new($false))
Write-Output ($run | ConvertTo-Json -Depth 10)
if ($timedOut -or !$freshReport -or !$passed -or $process.ExitCode -ne 0 -or $errors.Count -gt 0) { throw "Native audio QA failed; inspect $qaRoot" }
