param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Executable = '',
    [string]$Page = 'Battle',
    [ValidateSet('','roster','congestion')][string]$Scenario = '',
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
if (!$Executable) { $Executable = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Capture executable not found: $Executable" }
if (!$Name) { $Name = ($Page.ToLowerInvariant() -replace '[^a-z0-9]+','-') + $(if ($Scenario) { '-' + $Scenario } else { '' }) + "-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Capture name must contain lowercase letters, digits, underscores or hyphens.' }
New-Item -ItemType Directory -Path $captureRoot,$saveRoot -Force | Out-Null
$capturePath = Join-Path $captureRoot ($Name + '.png')
$logPath = Join-Path $captureRoot ($Name + '.log')
$stdoutPath = Join-Path $captureRoot ($Name + '.stdout.log')
$stderrPath = Join-Path $captureRoot ($Name + '.stderr.log')
$reportPath = Join-Path $captureRoot ($Name + '.json')
$started = Get-Date
$arguments = @(
    ('"' + $projectPath + '"'),
    '/Game/Rift/Maps/Arena',
    '-game','-RenderOffscreen','-windowed',"-ResX=$Width","-ResY=$Height",
    ('-RiftSaveRoot="' + $saveRoot + '"'),
    ('-RiftCapture="' + $capturePath + '"'),
    ('-RiftCapturePage="' + $Page + '"'),
    ('-RiftCaptureDelay=' + $Delay.ToString([System.Globalization.CultureInfo]::InvariantCulture)),
    '-RiftQuitAfterCapture','-unattended','-nosplash','-NoSound','-NoVSync',
    ('-abslog="' + $logPath + '"')
)
if ($Scenario) { $arguments += "-RiftVisualScenario=$Scenario" }
$process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
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
$errors = @()
if (Test-Path -LiteralPath $logPath) {
    $errors = @(Select-String -LiteralPath $logPath -Pattern 'LogRift: Error:|Fatal error:|Assertion failed:|Failed to load.*(/Game/Rift|Rift/)|Authored .* missing' | ForEach-Object { $_.Line })
}
$report = [ordered]@{
    schema = 1; name = $Name; page = $Page; scenario = $Scenario;
    width = $Width; height = $Height; actualWidth = $actualWidth; actualHeight = $actualHeight;
    resolutionMatches = $resolutionMatches; delay = $Delay;
    screenshot = $capturePath; engineLog = $logPath;
    captured = $freshCapture; timedOut = $timedOut; exitCode = $process.ExitCode;
    seconds = [math]::Round(((Get-Date)-$started).TotalSeconds,2);
    errors = $errors; visualInspection = 'pending'
}
[System.IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 20),[System.Text.UTF8Encoding]::new($false))
Write-Output ($report | ConvertTo-Json -Depth 20)
if ($timedOut -or !$freshCapture -or !$resolutionMatches -or $process.ExitCode -ne 0 -or $errors.Count -gt 0) { throw "Unreal capture failed; inspect $reportPath" }
