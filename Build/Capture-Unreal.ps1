param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Executable = '',
    [string]$Page = 'Battle',
    [ValidateSet('','roster','congestion','effects','effects17','placement')][string]$Scenario = '',
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
    [ValidateRange(0.85,2)][float]$Zoom=1,
    [string]$PreviewCard = '',
    [float]$PreviewX = 0,
    [float]$PreviewY = 7,
    [int]$Width = 1920,
    [int]$Height = 1080,
    [float]$Delay = 6,
    [int]$TimeoutSeconds = 180,
    [string]$Name = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $repoRoot 'Unreal\RiftCrownArena\RiftCrownArena.uproject'
$captureRoot = Join-Path $repoRoot 'Artifacts\QA\Visual'
$saveRoot = Join-Path $repoRoot 'Artifacts\QA\VisualSave'
$engineUserRoot = Join-Path $saveRoot 'EngineUser'
if (!$Executable) { $Executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Capture executable not found: $Executable" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftCaptureExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$isEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -in @('UnrealEditor','UnrealEditor-Cmd')
$workingDirectory = if ($isEditor) { $repoRoot } else { Split-Path -Parent $Executable }
if (!$Name) { $Name = ($Page.ToLowerInvariant() -replace '[^a-z0-9]+','-') + $(if ($Scenario) { '-' + $Scenario } else { '' }) + "-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Capture name must contain lowercase letters, digits, underscores or hyphens.' }
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
if ($BreathSmoke) { $arguments += '-RiftBreathSmoke' }
if ($Hand -ge 0) { $arguments += "-RiftCaptureHand=$Hand" }
if ($Developer) { $arguments += '-RiftCaptureDeveloper' }
if ($BattleMenu) { $arguments += '-RiftCaptureBattleMenu' }
if ($RecordedMatch) { $arguments += '-RiftCaptureRecordedMatch' }
if ($AllowExternalInput) { $arguments += '-RiftCaptureAllowInput' }
if ($Phase) { $arguments += "-RiftCapturePhase=$Phase" }
if ($Overlay) { $arguments += "-RiftCaptureOverlay=$Overlay" }
$arguments += '-RiftCaptureUIScale=' + $UIScale.ToString([System.Globalization.CultureInfo]::InvariantCulture)
$arguments += '-RiftCaptureZoom=' + $Zoom.ToString([System.Globalization.CultureInfo]::InvariantCulture)
if ($Scenario -eq 'effects17') { $arguments += '-RiftEffectAge=' + $EffectAge.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($Scenario -in @('effects','congestion')) { $arguments += '-RiftCaptureSpeed=' + $Speed.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
if ($PreviewCard) {
    if ($PreviewCard -notmatch '^[a-z_]+$') { throw 'Preview card must be an original card ID.' }
    $arguments += "-RiftPreviewCard=$PreviewCard"
    $arguments += '-RiftPreviewX=' + $PreviewX.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    $arguments += '-RiftPreviewY=' + $PreviewY.ToString([System.Globalization.CultureInfo]::InvariantCulture)
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
$errors = @()
if (Test-Path -LiteralPath $logPath) {
    $errors = @(Select-String -LiteralPath $logPath -Pattern 'LogRift: Error:|LogUIActionRouter: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Failed to load.*(/Game/Rift|Rift/)|Authored .* missing|LogMaterial: (Error:|Warning:.*(Failed to compile|Default Material|missing usage flag))|LogShaderCompilers: Error:' | ForEach-Object { $_.Line })
}
$report = [ordered]@{
    schema = 1; name = $Name; page = $Page; scenario = $Scenario; speed = $Speed; breathSmoke = [bool]$BreathSmoke; uiScale=$UIScale; zoom=$Zoom;
    state = $statePath; stateCaptured = $stateCaptured; phaseFixture = $Phase; phaseFixturePassed = $riftPhaseFixturePassed; allowExternalInput = [bool]$AllowExternalInput;
    width = $Width; height = $Height; actualWidth = $actualWidth; actualHeight = $actualHeight;
    resolutionMatches = $resolutionMatches; delay = $Delay;
    screenshot = $capturePath; engineLog = $logPath; executable = $Executable; executableSha256 = $riftCaptureExecutableHash;
    editor = $isEditor; workingDirectory = $workingDirectory; engineUserRoot = $engineUserRoot;
    captured = $freshCapture; timedOut = $timedOut; exitCode = $process.ExitCode;
    seconds = [math]::Round(((Get-Date)-$started).TotalSeconds,2);
    errors = $errors; visualInspection = 'pending'
}
[System.IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 20),[System.Text.UTF8Encoding]::new($false))
Write-Output ($report | ConvertTo-Json -Depth 20)
if ($timedOut -or !$freshCapture -or !$stateCaptured -or !$resolutionMatches -or !$riftPhaseFixturePassed -or $process.ExitCode -ne 0 -or $errors.Count -gt 0) { throw "Unreal capture failed; inspect $reportPath" }
