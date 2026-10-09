param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Executable = '',
    [string]$Page = 'Battle',
    [ValidateSet('','roster','congestion','effects','effects17','placement','projectiles','spells','tower_pathing')][string]$Scenario = '',
    [ValidateRange(0,6)][float]$PathingAge = 0,
    [ValidateRange(0.05,2)][float]$EffectAge = 0.12,
    [ValidateRange(0.25,4)][float]$Speed = 1,
    [switch]$BreathSmoke,
    [ValidateRange(-1,3)][int]$Hand=-1,
    [switch]$Developer,
    [switch]$BattleMenu,
    [switch]$RecordedMatch,
    [switch]$AllowExternalInput,
    [ValidateSet('','double','triple','overtime','tiebreaker','victory')][string]$Phase='',
    [ValidateSet('','tiles','ranges','sight','paths','targets','locks','all')][string]$Overlay='',
    [ValidateRange(0.7,1.4)][float]$UIScale=1,
    [ValidateScript({ $_ -eq -1 -or ($_ -ge 0.1 -and $_ -le 2) })][float]$Zoom=-1,
    [string]$PreviewCard = '',
    [float]$PreviewX = 0,
    [float]$PreviewY = 7,
    [string]$InspectCard = '',
    [int]$Width = 1920,
    [int]$Height = 1080,
    [float]$Delay = 6,
    [int]$TimeoutSeconds = 180,
    [string]$ExpectedVersion = '',
    [string]$Name = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ShippingDiagnostics.ps1')
$ExpectedVersion = Get-RiftExpectedGameVersion $repoRoot $ExpectedVersion
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$captureRoot = Join-Path $repoRoot 'Artifacts\QA\Visual'
if (!$Executable) { $Executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Capture executable not found: $Executable" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftCaptureExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$isEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -in @('UnrealEditor','UnrealEditor-Cmd')
if($Scenario -eq 'tower_pathing' -and ($Page -ne 'Battle' -or $Phase -or $RecordedMatch -or $BreathSmoke)) { throw 'Tower pathing requires its live Battle fixture without another phase, recorded-match or breath fixture.' }
$riftTowerSourcePins=@()
if($Scenario -eq 'tower_pathing') {
    $riftTowerSources=@(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'Unreal/RiftCrownArena/Source'),(Join-Path $repoRoot 'Unreal/RiftCrownArena/Config') -File -Recurse | Where-Object Extension -in @('.cpp','.h','.cs','.ini') | ForEach-Object FullName)+@($projectPath,$PSCommandPath)
    if($isEditor) { $riftTowerSources+=@('Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArena.dll','Unreal/RiftCrownArena/Binaries/Win64/UnrealEditor-RiftCrownArenaEditor.dll') | ForEach-Object { Join-Path $repoRoot $_ } }
    $riftTowerSourcePins=@($riftTowerSources | Sort-Object -Unique | ForEach-Object { [ordered]@{path=[IO.Path]::GetRelativePath($repoRoot,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length} })
}
$workingDirectory = if ($isEditor) { $repoRoot } else { Split-Path -Parent $Executable }
if (!$Name) { $Name = ($Page.ToLowerInvariant() -replace '[^a-z0-9]+','-') + $(if ($Scenario) { '-' + $Scenario } else { '' }) + "-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Capture name must contain lowercase letters, digits, underscores or hyphens.' }
$runRoot = Join-Path $repoRoot ('Artifacts/QA/VisualRuns/' + $Name + '-' + [Guid]::NewGuid().ToString('N'))
$saveRoot = Join-Path $runRoot 'Save'
$engineUserRoot = Join-Path $runRoot 'EngineUser'
New-Item -ItemType Directory -Path $captureRoot,$saveRoot,$engineUserRoot -Force | Out-Null
$capturePath = Join-Path $captureRoot ($Name + '.png')
$logPath = Join-Path $captureRoot ($Name + '.log')
$stdoutPath = Join-Path $captureRoot ($Name + '.stdout.log')
$stderrPath = Join-Path $captureRoot ($Name + '.stderr.log')
$reportPath = Join-Path $captureRoot ($Name + '.json')
$started = Get-Date
$arguments = @(
    '/Game/Rift/Maps/Arena',
    '-game','-RenderOffscreen','-windowed',"-ResX=$Width","-ResY=$Height",
    ('-RiftSaveRoot="' + $saveRoot + '"'),
    ('-UserDir="' + $engineUserRoot + '"'),
    ('-RiftCapture="' + $capturePath + '"'),
    ('-RiftCapturePage="' + $Page + '"'),
    ('-RiftCaptureDelay=' + $Delay.ToString([System.Globalization.CultureInfo]::InvariantCulture)),
    '-RiftQuitAfterCapture','-unattended','-nosplash','-NoSound','-NoVSync',
    ('-abslog="' + $logPath + '"')
)
if ($isEditor) { $arguments = @(('"' + $projectPath + '"')) + $arguments }
if ($Scenario) { $arguments += "-RiftVisualScenario=$Scenario" }
if ($Scenario -eq 'tower_pathing') { $arguments += '-RiftPathingAge=' + $PathingAge.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($BreathSmoke) { $arguments += '-RiftBreathSmoke' }
if ($Hand -ge 0) { $arguments += "-RiftCaptureHand=$Hand" }
if ($Developer) { $arguments += '-RiftCaptureDeveloper' }
if ($BattleMenu) { $arguments += '-RiftCaptureBattleMenu' }
if ($RecordedMatch) { $arguments += '-RiftCaptureRecordedMatch' }
if ($AllowExternalInput) { $arguments += '-RiftCaptureAllowInput' }
if ($Phase) { $arguments += "-RiftCapturePhase=$Phase" }
if ($Overlay) { $arguments += "-RiftCaptureOverlay=$Overlay" }
$arguments += '-RiftCaptureUIScale=' + $UIScale.ToString([System.Globalization.CultureInfo]::InvariantCulture)
if ($Zoom -ne -1) { $arguments += '-RiftCaptureZoom=' + $Zoom.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($Scenario -in @('effects17','spells')) { $arguments += '-RiftEffectAge=' + $EffectAge.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($Scenario -in @('effects','congestion')) { $arguments += '-RiftCaptureSpeed=' + $Speed.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($PreviewCard) {
    if ($PreviewCard -notmatch '^[a-z_]+$') { throw 'Preview card must be an original card ID.' }
    $arguments += "-RiftPreviewCard=$PreviewCard"
    $arguments += '-RiftPreviewX=' + $PreviewX.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $arguments += '-RiftPreviewY=' + $PreviewY.ToString([System.Globalization.CultureInfo]::InvariantCulture)
}
if ($InspectCard) {
    if ($InspectCard -notmatch '^[a-z_]+$') { throw 'Inspected card must be an original card ID.' }
    $arguments += "-RiftInspectCard=$InspectCard"
}
$process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $workingDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$timedOut = $false
while (!$process.HasExited) {
    if (((Get-Date) - $started).TotalSeconds -gt $TimeoutSeconds) {
        $timedOut = $true
        Stop-Process -Id $process.Id -Force
        break
    }
    Start-Sleep -Milliseconds 250
    $process.Refresh()
}
$process.WaitForExit()
$finished = [DateTime]::UtcNow
$freshCapture = (Test-Path -LiteralPath $capturePath) -and ((Get-Item -LiteralPath $capturePath).LastWriteTime -ge $started.AddSeconds(-1))
$actualWidth = 0
$actualHeight = 0
if ($freshCapture) {
    $pngBytes = [System.IO.File]::ReadAllBytes($capturePath)
    if ($pngBytes.Length -ge 24 -and $pngBytes[0] -eq 137 -and $pngBytes[1] -eq 80 -and $pngBytes[2] -eq 78 -and $pngBytes[3] -eq 71) {
        $actualWidth = ([int]$pngBytes[16] -shl 24) -bor ([int]$pngBytes[17] -shl 16) -bor ([int]$pngBytes[18] -shl 8) -bor [int]$pngBytes[19]
        $actualHeight = ([int]$pngBytes[20] -shl 24) -bor ([int]$pngBytes[21] -shl 16) -bor ([int]$pngBytes[22] -shl 8) -bor [int]$pngBytes[23]
    }
}
$resolutionMatches = $actualWidth -eq $Width -and $actualHeight -eq $Height
$statePath = [System.IO.Path]::ChangeExtension($capturePath,'state.json')
$stateCaptured = (Test-Path -LiteralPath $statePath) -and ((Get-Item -LiteralPath $statePath).LastWriteTime -ge $started.AddSeconds(-1))
$riftCaptureState = if ($stateCaptured) { Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json } else { $null }
$riftCameraFraming = if ($stateCaptured) { $riftCaptureState.cameraFraming } else { $null }
$riftCameraFramingRequired = $Page -in @('Battle','ReplayView')
$riftCameraFramingPassed = !$riftCameraFramingRequired -or (
    $null -ne $riftCameraFraming -and $riftCameraFraming.activeBattleView -eq $true -and
    $riftCameraFraming.passed -eq $true -and @($riftCameraFraming.legalFieldCorners).Count -eq 4 -and
    @($riftCameraFraming.legalFieldCorners | Where-Object { $_.projected -ne $true -or $_.insideSafeArea -ne $true }).Count -eq 0
)
# Older public builds report legal ground corners only. When the native build
# also reports model-height safety, require every projected silhouette corner.
$riftModelEnvelopeAvailable = $null -ne $riftCameraFraming -and $null -ne $riftCameraFraming.PSObject.Properties['modelEnvelopePassed']
$riftModelEnvelopePassed = if ($riftModelEnvelopeAvailable) {
    $riftCameraFraming.modelEnvelopePassed -eq $true -and @($riftCameraFraming.modelEnvelope).Count -gt 0 -and
    @($riftCameraFraming.modelEnvelope | Where-Object { $_.projected -ne $true -or $_.insideSafeArea -ne $true }).Count -eq 0
} else { $null }
$riftModelEnvelopeCheckPassed = !$riftCameraFramingRequired -or !$riftModelEnvelopeAvailable -or $riftModelEnvelopePassed -eq $true
$riftPhaseFixturePassed = $true
if ($Phase) {
    $riftExpectedPhase = switch ($Phase) { 'double' { 'regulation' }; 'triple' { 'overtime' }; 'overtime' { 'overtime' }; 'tiebreaker' { 'tiebreaker' }; 'victory' { 'finished' } }
    $riftExpectedElapsed = switch ($Phase) { 'double' { 121 }; 'triple' { 241 }; 'overtime' { 181 }; 'tiebreaker' { 301 }; 'victory' { 0 } }
    $riftPhaseFixturePassed = $stateCaptured -and $riftCaptureState.phase -eq $riftExpectedPhase -and
        [math]::Abs($riftCaptureState.elapsed - $riftExpectedElapsed) -lt 0.01 -and
        $riftCaptureState.speed -eq 0 -and $riftCaptureState.events.match_start -eq 1
    if ($Phase -eq 'victory') {
        $riftPhaseFixturePassed = $riftPhaseFixturePassed -and $riftCaptureState.winner -eq 0 -and
            $riftCaptureState.playerCrowns -eq 3 -and $riftCaptureState.enemyCrowns -eq 0 -and
            $riftCaptureState.resultReason -eq 'core_destroyed' -and $riftCaptureState.events.match_end -eq 1
    }
}
$riftTowerPathingPassed=$true
$riftTowerSourcesUnchanged=$true
if($Scenario -eq 'tower_pathing') {
    $pathing=$riftCaptureState.towerPathing
    $riftTowerPathingPassed=$stateCaptured -and $null -ne $pathing -and $pathing.passed -eq $true -and $pathing.ordinarySpawnOnly -eq $true -and
        $pathing.deploymentCount -eq 6 -and $pathing.expectedUnitCount -eq 8 -and $pathing.actualUnitCount -eq 8 -and @($pathing.units).Count -eq 8 -and
        @($pathing.units.id | Sort-Object -Unique).Count -eq 8 -and @($pathing.towers).Count -eq 6 -and $pathing.spawnTowerClearancePassed -eq $true -and
        $pathing.allStepTowerClearancePassed -eq $true -and $pathing.allContinuousSegmentTowerClearancePassed -eq $true -and [math]::Abs($pathing.requestedAge-$PathingAge) -lt .00001 -and
        [math]::Abs($pathing.sampledAge-([math]::Round($PathingAge*60)/60)) -lt .00001 -and $riftCaptureState.speed -eq 0 -and
        @($pathing.units | Where-Object { $_.present -ne $true -or $_.spawnTowerClearance -lt -.000001 -or $_.minimumStepTowerClearance -lt -.000001 -or $_.minimumSegmentTowerClearance -lt -.000001 }).Count -eq 0
    if($PathingAge -ge 2) { $riftTowerPathingPassed=$riftTowerPathingPassed -and $pathing.progressRequired -eq $true -and $pathing.progressPassed -eq $true -and @($pathing.units | Where-Object progressPassed -ne $true).Count -eq 0 }
    foreach($pin in $riftTowerSourcePins) { $file=Join-Path $repoRoot $pin.path;if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.sha256 -or (Get-Item -LiteralPath $file).Length -ne $pin.bytes) { $riftTowerSourcesUnchanged=$false } }
}
$riftCaptureExecutableUnchanged=(Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant() -eq $riftCaptureExecutableHash
$errors = @()
$errorPattern='LogRift: Error:|LogUIActionRouter: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Failed to load.*(/Game/Rift|Rift/)|Authored .* missing|LogMaterial: (Error:|Warning:.*(Failed to compile|Default Material|missing usage flag))|LogShaderCompilers: Error:'
$diagnosticSource='editor-engine-log'; $nativeDiagnosticLogs=@(); $diagnosticVerification=$null
if ($isEditor) {
    if ((Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).LastWriteTimeUtc -ge $started.ToUniversalTime()) {
        $errors = @(Select-String -LiteralPath $logPath -Pattern $errorPattern | ForEach-Object { $_.Line })
    } else { $errors=@('Fresh Editor engine log missing.') }
} else {
    $diagnostics=Get-RiftShippingDiagnostics -RepoRoot $repoRoot -DestinationRoot $runRoot -SaveRoot $saveRoot -EngineUserRoot $engineUserRoot -ProcessId $process.Id -ExpectedVersion $ExpectedVersion -StartedUTC $started -FinishedUTC $finished -ErrorPattern $errorPattern
    $errors=@($diagnostics.errors); $diagnosticSource=$diagnostics.diagnosticSource; $nativeDiagnosticLogs=@($diagnostics.nativeDiagnosticLogs); $diagnosticVerification=$diagnostics.diagnosticVerification; $logPath=$diagnostics.engineLog
}
$riftCapturePassed=!$timedOut -and $freshCapture -and $stateCaptured -and $resolutionMatches -and $riftPhaseFixturePassed -and $riftCameraFramingPassed -and $riftModelEnvelopeCheckPassed -and $riftTowerPathingPassed -and $riftTowerSourcesUnchanged -and $riftCaptureExecutableUnchanged -and $process.ExitCode -eq 0 -and $errors.Count -eq 0
$report = [ordered]@{
    schema = 1; version=$ExpectedVersion; passed=[bool]$riftCapturePassed; name = $Name; page = $Page; scenario = $Scenario; inspectCard = $InspectCard; speed = $Speed; breathSmoke = [bool]$BreathSmoke; uiScale=$UIScale; zoom=$Zoom;
    state = $statePath; stateCaptured = $stateCaptured; phaseFixture = $Phase; phaseFixturePassed = $riftPhaseFixturePassed; allowExternalInput = [bool]$AllowExternalInput;
    pathingAge = $(if($Scenario -eq 'tower_pathing'){$PathingAge}else{$null}); towerPathingPassed = $riftTowerPathingPassed;
    sourcePins=$riftTowerSourcePins; sourcePinsUnchanged=$riftTowerSourcesUnchanged; executableUnchanged=$riftCaptureExecutableUnchanged;
    cameraFramingRequired = $riftCameraFramingRequired; cameraFramingPassed = $riftCameraFramingPassed; cameraFraming = $riftCameraFraming;
    modelEnvelopeAvailable = $riftModelEnvelopeAvailable; modelEnvelopePassed = $riftModelEnvelopePassed;
    width = $Width; height = $Height; actualWidth = $actualWidth; actualHeight = $actualHeight;
    resolutionMatches = $resolutionMatches; delay = $Delay;
    screenshot = $capturePath; engineLog = $logPath; executable = $Executable; executableSha256 = $riftCaptureExecutableHash;
    diagnosticSource=$diagnosticSource; nativeDiagnosticLogs=$nativeDiagnosticLogs; diagnosticVerification=$diagnosticVerification;
    editor = $isEditor; workingDirectory = $workingDirectory; engineUserRoot = $engineUserRoot;
    processId=$process.Id; startedUTC=$started.ToUniversalTime().ToString('o'); finishedUTC=$finished.ToString('o');
    captured = $freshCapture; timedOut = $timedOut; exitCode = $process.ExitCode;
    seconds = [math]::Round(((Get-Date)-$started).TotalSeconds,2);
    errors = $errors; visualInspection = 'pending'
}
[System.IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 20),[System.Text.UTF8Encoding]::new($false))
Write-Output ($report | ConvertTo-Json -Depth 20)
if (!$riftCapturePassed) { throw "Unreal capture failed; inspect $reportPath" }
