param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Executable = '',
    [int]$TimeoutSeconds = 180,
    [string]$ExpectedVersion = '',
    [string]$Name = 'native-audio-smoke'
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ShippingDiagnostics.ps1')
$ExpectedVersion = Get-RiftExpectedGameVersion $repoRoot $ExpectedVersion
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$qaRoot = Join-Path $repoRoot ('Artifacts\QA\Audio\' + $Name)
$isolatedRunRoot = Join-Path $qaRoot ('Runs/' + [Guid]::NewGuid().ToString('N'))
$saveRoot = Join-Path $isolatedRunRoot 'Save'
$engineUserRoot = Join-Path $isolatedRunRoot 'EngineUser'
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'QA name must contain lowercase letters, digits, underscores or hyphens.' }
if (!$Executable) { $Executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Audio QA executable not found: $Executable" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftAudioExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$isEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -in @('UnrealEditor','UnrealEditor-Cmd')
$riftAudioSourcePaths = @(
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Config') -File -Recurse |
        Where-Object Extension -in @('.cpp','.h','.cs','.ini') | ForEach-Object FullName
) + @($projectPath,(Join-Path $repoRoot 'Build/Test-UnrealAudio.ps1'),(Join-Path $repoRoot 'Assets/Source/Audio/audio_manifest.json'))
$riftAudioSourcePaths += @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Assets/Source/Audio') -File | Where-Object Extension -eq '.wav' | ForEach-Object FullName)
if ($isEditor) { $riftAudioSourcePaths += @('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll') | ForEach-Object { Join-Path $repoRoot $_ } }
$riftAudioSourcePins = @($riftAudioSourcePaths | Sort-Object -Unique | ForEach-Object {
    [ordered]@{path=[IO.Path]::GetRelativePath($repoRoot,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
})
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
$finished = [DateTime]::UtcNow
$freshReport = (Test-Path -LiteralPath $reportPath) -and ((Get-Item -LiteralPath $reportPath).LastWriteTime -ge $started.AddSeconds(-1))
$errors = @()
$errorPattern='LogRift: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Authored audio asset missing'
$diagnosticSource='editor-engine-log'; $nativeDiagnosticLogs=@(); $diagnosticVerification=$null
if ($isEditor) {
    if ((Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).LastWriteTimeUtc -ge $started.ToUniversalTime()) { $errors = @(Select-String -LiteralPath $logPath -Pattern $errorPattern | ForEach-Object { $_.Line }) }
    else { $errors=@('Fresh Editor engine log missing.') }
} else {
    $diagnostics=Get-RiftShippingDiagnostics -RepoRoot $repoRoot -DestinationRoot $qaRoot -SaveRoot $saveRoot -EngineUserRoot $engineUserRoot -ProcessId $process.Id -ExpectedVersion $ExpectedVersion -StartedUTC $started -FinishedUTC $finished -ErrorPattern $errorPattern
    $errors=@($diagnostics.errors); $diagnosticSource=$diagnostics.diagnosticSource; $nativeDiagnosticLogs=@($diagnostics.nativeDiagnosticLogs); $diagnosticVerification=$diagnostics.diagnosticVerification; $logPath=$diagnostics.engineLog
}
$passed = $false
if ($freshReport) {
    $riftAudioReport=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $riftAudioCheckNames=@($riftAudioReport.checks | ForEach-Object name)
    $passed=$riftAudioReport.passed -eq $true -and @($riftAudioReport.errors).Count -eq 0 -and @($riftAudioReport.checks).Count -gt 0 -and @($riftAudioReport.checks | Where-Object { !$_.passed }).Count -eq 0 -and @($riftAudioCheckNames | Sort-Object -Unique).Count -eq $riftAudioCheckNames.Count
    if($ExpectedVersion -eq '1.3.2') {
        $passed=$passed -and $riftAudioCheckNames.Count -eq 61
        foreach($riftRequired in @('riverVoiceInspectionDetectsSilentControl','noRiverVoiceInHome','actualBattleActiveForWaterCheck','noRiverVoiceInBattle','noRiverVoiceAfterRepeatedWorldBinding','noRiverVoiceAfterReturningHome','noRiverVoiceInSettings','noRiverVoiceAfterDeferredBurst','noRiverVoiceDuringLimiterOverload','noRiverVoiceInQuietScene','quietMusicSliderMuteApplied','quietLiveMixerHasNoConstantSound','postMixSettingsRestored')) {
            $passed=$passed -and $riftAudioCheckNames -contains $riftRequired
        }
    }
}
$riftAudioSourcesUnchanged=$true
foreach($riftPin in $riftAudioSourcePins){if((Get-FileHash -LiteralPath (Join-Path $repoRoot $riftPin.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $riftPin.sha256){$riftAudioSourcesUnchanged=$false}}
$riftAudioExecutableUnchanged=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant() -eq $riftAudioExecutableHash
if(!$riftAudioSourcesUnchanged){$errors+=@('Audio QA source/module bytes changed during execution.')}
if(!$riftAudioExecutableUnchanged){$errors+=@('Audio QA executable changed during execution.')}
$passed=$passed -and !$timedOut -and $freshReport -and $process.ExitCode -eq 0 -and $errors.Count -eq 0 -and $riftAudioSourcesUnchanged -and $riftAudioExecutableUnchanged
$run = [ordered]@{ version=$ExpectedVersion; executable=$Executable; executableSha256=$riftAudioExecutableHash; editor=$isEditor; workingDirectory=$workingDirectory; audioDisabled=$false;
    processId=$process.Id;startedUtc=$started.ToUniversalTime().ToString('o');finishedUtc=$finished.ToString('o');sourceHashes=$riftAudioSourcePins;sourcesUnchanged=$riftAudioSourcesUnchanged;executableUnchanged=$riftAudioExecutableUnchanged;
    engineUserRoot=$engineUserRoot; report=$reportPath; reportSha256=$(if($freshReport){(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToLowerInvariant()}else{''});checkCount=$(if($freshReport){@($riftAudioReport.checks).Count}else{0});engineLog=$logPath; freshReport=$freshReport;
    diagnosticSource=$diagnosticSource; nativeDiagnosticLogs=$nativeDiagnosticLogs; diagnosticVerification=$diagnosticVerification;
    timedOut=$timedOut; exitCode=$process.ExitCode; seconds=[math]::Round(((Get-Date)-$started).TotalSeconds,2); passed=$passed; errors=$errors }
[System.IO.File]::WriteAllText((Join-Path $qaRoot 'run.json'),($run | ConvertTo-Json -Depth 10),[System.Text.UTF8Encoding]::new($false))
Write-Output ($run | ConvertTo-Json -Depth 10)
if ($timedOut -or !$freshReport -or !$passed -or $process.ExitCode -ne 0 -or $errors.Count -gt 0) { throw "Native audio QA failed; inspect $qaRoot" }
