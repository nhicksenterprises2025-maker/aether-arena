param(
    [string]$Executable = '',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$Seconds = 90,
    [switch]$Stress,
    [switch]$WithMeta,
    [string]$ExpectedVersion = '',
    [string]$Name = ''
)
$ErrorActionPreference = 'Stop'
$riftMeasureRepo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ShippingDiagnostics.ps1')
$ExpectedVersion = Get-RiftExpectedGameVersion $riftMeasureRepo $ExpectedVersion
if (!$Executable) { $Executable = Join-Path $riftMeasureRepo 'Artifacts\Game\Windows\RiftCrownArena.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Real native game executable not found: $Executable" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftMeasureExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$riftMeasureIsEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -match '^UnrealEditor(-Cmd)?$'
if (!$Name) { $Name = "$(if ($Stress) {'stress'} else {'ai-match'})-$(if ($WithMeta) {'meta-requested'} else {'meta-paused'})-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Use a lowercase alphanumeric QA report name.' }
$riftMeasureRoot = Join-Path $riftMeasureRepo ('Artifacts\QA\Performance\' + $Name)
$riftMeasureIsolatedRoot = Join-Path $riftMeasureRoot ('Runs/' + [Guid]::NewGuid().ToString('N'))
$riftMeasureSaves = Join-Path $riftMeasureIsolatedRoot 'UserData'
$riftMeasureUser = Join-Path $riftMeasureIsolatedRoot 'EngineUser'
New-Item -ItemType Directory -Path $riftMeasureSaves -Force | Out-Null
$riftMeasureJson = Join-Path $riftMeasureRoot 'performance.json'
$riftMeasureLog = Join-Path $riftMeasureRoot 'engine.log'
$riftMeasureArgs = @('/Game/Rift/Maps/Arena','-windowed','-RenderOffscreen',"-ResX=$Width","-ResY=$Height",'-unattended','-nosplash','-NoVSync',
    ('-RiftSaveRoot="' + $riftMeasureSaves + '"'),('-UserDir="' + $riftMeasureUser + '"'),
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
$riftMeasureFinished = [DateTime]::UtcNow
if ($riftMeasureProcess.ExitCode -ne 0) { throw "Native measurement exited $($riftMeasureProcess.ExitCode). Inspect $riftMeasureRoot" }
if (!(Test-Path -LiteralPath $riftMeasureJson) -or (Get-Item -LiteralPath $riftMeasureJson).LastWriteTime -lt $riftMeasureStarted.AddSeconds(-1)) { throw 'Game did not produce a fresh performance report.' }
$riftMeasured = Get-Content -LiteralPath $riftMeasureJson -Raw | ConvertFrom-Json
if ($riftMeasured.frame.samples -lt 100 -or $riftMeasured.width -ne $Width -or $riftMeasured.height -ne $Height) { throw 'Measurement has insufficient frames or incorrect output dimensions.' }
if ($WithMeta -and $riftMeasured.metaGamesDuringBattle -ne 0) { throw 'Background Meta advanced while the battle was active.' }
$riftMeasureErrors=@(); $riftMeasureDiagnosticSource='editor-engine-log'; $riftMeasureDiagnosticLogs=@(); $riftMeasureDiagnosticVerification=$null
$riftMeasureErrorPattern='LogRift: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Authored .* missing'
if ($riftMeasureIsEditor) {
    if ((Test-Path -LiteralPath $riftMeasureLog) -and (Get-Item -LiteralPath $riftMeasureLog).LastWriteTimeUtc -ge $riftMeasureStarted.ToUniversalTime()) { $riftMeasureErrors=@(Select-String -LiteralPath $riftMeasureLog -Pattern $riftMeasureErrorPattern|ForEach-Object {$_.Line}) }
    else { $riftMeasureErrors=@('Fresh Editor engine log missing.') }
} else {
    $riftMeasureDiagnostics=Get-RiftShippingDiagnostics -RepoRoot $riftMeasureRepo -DestinationRoot $riftMeasureRoot -SaveRoot $riftMeasureSaves -EngineUserRoot $riftMeasureUser -ProcessId $riftMeasureProcess.Id -ExpectedVersion $ExpectedVersion -StartedUTC $riftMeasureStarted -FinishedUTC $riftMeasureFinished -ErrorPattern $riftMeasureErrorPattern
    $riftMeasureErrors=@($riftMeasureDiagnostics.errors); $riftMeasureDiagnosticSource=$riftMeasureDiagnostics.diagnosticSource; $riftMeasureDiagnosticLogs=@($riftMeasureDiagnostics.nativeDiagnosticLogs); $riftMeasureDiagnosticVerification=$riftMeasureDiagnostics.diagnosticVerification; $riftMeasureLog=$riftMeasureDiagnostics.engineLog
}
$riftMeasureRun = [ordered]@{ passed=$riftMeasureErrors.Count -eq 0; version=$ExpectedVersion; utc=[DateTime]::UtcNow.ToString('o'); executable=$Executable; executableSha256=$riftMeasureExecutableHash;
    editor=$riftMeasureIsEditor; exitCode=$riftMeasureProcess.ExitCode; report=$riftMeasureJson;
    reportSha256=(Get-FileHash -LiteralPath $riftMeasureJson -Algorithm SHA256).Hash.ToLowerInvariant();
    engineLog=$riftMeasureLog; diagnosticSource=$riftMeasureDiagnosticSource; nativeDiagnosticLogs=$riftMeasureDiagnosticLogs; diagnosticVerification=$riftMeasureDiagnosticVerification; errors=$riftMeasureErrors;
    saveRoot=$riftMeasureSaves; seconds=[math]::Round(((Get-Date)-$riftMeasureStarted).TotalSeconds,2) }
[IO.File]::WriteAllText((Join-Path $riftMeasureRoot 'run.json'),($riftMeasureRun | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output ($riftMeasured | ConvertTo-Json -Depth 20)
if ($riftMeasureErrors.Count -gt 0) { throw "Native measurement diagnostics failed; inspect $riftMeasureRoot" }
