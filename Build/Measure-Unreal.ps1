param(
    [string]$Executable = '',
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$Width = 1920,
    [int]$Height = 1080,
    [ValidateRange(10,600)][int]$Seconds = 90,
    [switch]$Stress,
    [switch]$ExpansionStress,
    [switch]$CaptureScreenshots,
    [ValidateRange(0,299)][double]$StartSeconds = 0,
    [ValidateSet('0.25','0.5','1','2','4')][string]$SimulationSpeed = '1',
    [uint32]$Seed = 74021,
    [ValidateRange(-1,3)][int]$Quality=-1,
    [switch]$WithMeta,
    [string]$ExpectedVersion = '',
    [string]$Name = ''
)
$ErrorActionPreference = 'Stop'
if ($CaptureScreenshots -and !$ExpansionStress) { throw 'Paid combat screenshots require the isolated ExpansionStress fixture.' }
if ($CaptureScreenshots -and (($Seconds + 8) * [double]::Parse($SimulationSpeed,[Globalization.CultureInfo]::InvariantCulture)) -lt 40) {
    throw 'This duration and simulation speed cannot reach all eight paid combat screenshot ages; increase Seconds.'
}
$riftMeasureRepo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'ShippingDiagnostics.ps1')
$ExpectedVersion = Get-RiftExpectedGameVersion $riftMeasureRepo $ExpectedVersion
if (!$Executable) { $Executable = Join-Path $riftMeasureRepo 'Artifacts\Game\Windows\RiftCrownArena.exe' }
if (!(Test-Path -LiteralPath $Executable)) { throw "Real native game executable not found: $Executable" }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftMeasureExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$riftMeasureIsEditor = [System.IO.Path]::GetFileNameWithoutExtension($Executable) -match '^UnrealEditor(-Cmd)?$'
if (!$Name) { $Name = "$(if ($ExpansionStress) {'expansion-stress'} elseif ($Stress) {'stress'} else {'ai-match'})-$(if ($WithMeta) {'meta-requested'} else {'meta-paused'})-${Width}x${Height}" }
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Use a lowercase alphanumeric QA report name.' }
$riftMeasureRoot = Join-Path $riftMeasureRepo ('Artifacts\QA\Performance\' + $Name)
$riftMeasureIsolatedRoot = Join-Path $riftMeasureRoot ('Runs/' + [Guid]::NewGuid().ToString('N'))
$riftMeasureSaves = Join-Path $riftMeasureIsolatedRoot 'UserData'
$riftMeasureUser = Join-Path $riftMeasureIsolatedRoot 'EngineUser'
New-Item -ItemType Directory -Path $riftMeasureSaves -Force | Out-Null
$riftMeasureJson = Join-Path $riftMeasureIsolatedRoot 'performance.json'
$riftMeasureLog = Join-Path $riftMeasureIsolatedRoot 'engine.log'
$riftMeasureArgs = @('/Game/Rift/Maps/Arena','-windowed','-RenderOffscreen',"-ResX=$Width","-ResY=$Height",'-unattended','-nosplash','-NoVSync',
    ('-RiftSaveRoot="' + $riftMeasureSaves + '"'),('-UserDir="' + $riftMeasureUser + '"'),
    ('-RiftPerfReport="' + $riftMeasureJson + '"'),"-RiftPerfSeconds=$Seconds",'-RiftPerfWarmup=8',('-abslog="' + $riftMeasureLog + '"'))
if ($riftMeasureIsEditor) { $riftMeasureArgs = @(('"' + (Join-Path $riftMeasureRepo 'Unreal\RiftCrownArena\RiftCrownArena.uproject') + '"'),'-game') + $riftMeasureArgs }
if ($Stress) { $riftMeasureArgs += '-RiftPerfStress' }
if ($Quality -ge 0) { $riftMeasureArgs += "-RiftQAQuality=$Quality" }
if ($ExpansionStress) {
    $riftMeasureArgs += @('-RiftPerfExpansionStress',('-RiftPerfStartSeconds=' + $StartSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)),"-RiftPerfSpeed=$SimulationSpeed","-RiftPerfSeed=$Seed")
    if ($CaptureScreenshots) { $riftMeasureArgs += ('-RiftPerfScreenshotRoot="' + (Join-Path $riftMeasureIsolatedRoot 'Screenshots') + '"') }
}
if ($WithMeta) { $riftMeasureArgs += '-RiftPerfWithMeta' }
function Get-RiftMeasurePins([string]$Base,[string[]]$Paths) {
    @($Paths | Sort-Object -Unique | ForEach-Object {
        if (!(Test-Path -LiteralPath $_ -PathType Leaf)) { throw "Performance provenance input is missing: $_" }
        [ordered]@{path=[IO.Path]::GetRelativePath($Base,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length}
    })
}
function Get-RiftMeasureSourcePaths {
    @(Get-ChildItem -LiteralPath (Join-Path $riftMeasureRepo 'Unreal/RiftCrownArena/Source'),(Join-Path $riftMeasureRepo 'Unreal/RiftCrownArena/Config') -File -Recurse | ForEach-Object FullName) +
        @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Assets/Data/card_manifest.json','Build/generate_card_manifest.py','Build/Measure-Unreal.ps1','Build/ShippingDiagnostics.ps1' | ForEach-Object { Join-Path $riftMeasureRepo $_ })
}
function Get-RiftMeasureRuntimePaths {
    if ($riftMeasureIsEditor) {
        @(Get-ChildItem -LiteralPath (Join-Path $riftMeasureRuntimeRoot 'Binaries'),(Join-Path $riftMeasureRuntimeRoot 'Config'),(Join-Path $riftMeasureRuntimeRoot 'Content') -File -Recurse | ForEach-Object FullName) +
            @((Join-Path $riftMeasureRuntimeRoot 'RiftCrownArena.uproject'))
    } else {
        @(Get-ChildItem -LiteralPath $riftMeasureRuntimeRoot -File -Recurse | ForEach-Object FullName)
    }
}
$riftMeasureRuntimeRoot = if ($riftMeasureIsEditor) { Join-Path $riftMeasureRepo 'Unreal/RiftCrownArena' } else { $null }
if (!$riftMeasureIsEditor) {
    for ($riftMeasureAncestor = [IO.DirectoryInfo]::new((Split-Path -Parent $Executable)); $riftMeasureAncestor; $riftMeasureAncestor = $riftMeasureAncestor.Parent) {
        if ((Test-Path -LiteralPath (Join-Path $riftMeasureAncestor.FullName 'RiftCrownArena.exe') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $riftMeasureAncestor.FullName 'RiftCrownArena/Content') -PathType Container)) {
            $riftMeasureRuntimeRoot = $riftMeasureAncestor.FullName
            break
        }
    }
    if (!$riftMeasureRuntimeRoot) { throw 'Performance capture requires an attributable packaged game root containing RiftCrownArena.exe and RiftCrownArena/Content.' }
} else {
    foreach ($riftMeasureModule in @('UnrealEditor-RiftCrownArena.dll','UnrealEditor-RiftCrownArenaEditor.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $riftMeasureRuntimeRoot ('Binaries/Win64/' + $riftMeasureModule)) -PathType Leaf)) { throw "Build the Editor module before performance capture: $riftMeasureModule" }
    }
}
$riftMeasureSourcePins = @(Get-RiftMeasurePins $riftMeasureRepo (Get-RiftMeasureSourcePaths))
$riftMeasureRuntimePins = @(Get-RiftMeasurePins $riftMeasureRuntimeRoot (Get-RiftMeasureRuntimePaths))
$riftMeasureStarted = Get-Date
$riftMeasureProvenancePath = Join-Path $riftMeasureIsolatedRoot 'provenance.json'
$riftMeasureProvenance = [ordered]@{schema=1;startedUtc=$riftMeasureStarted.ToUniversalTime().ToString('o');
    executable=$Executable;executableSha256=$riftMeasureExecutableHash;sourcePins=$riftMeasureSourcePins;
    runtimeRoot=$riftMeasureRuntimeRoot;runtimePins=$riftMeasureRuntimePins;completed=$false;
    sourcePinsUnchanged=$false;runtimePinsUnchanged=$false;executableUnchanged=$false}
[IO.File]::WriteAllText($riftMeasureProvenancePath,($riftMeasureProvenance | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
$riftMeasureProcess = Start-Process -FilePath $Executable -ArgumentList $riftMeasureArgs -WorkingDirectory (Split-Path -Parent $Executable) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $riftMeasureIsolatedRoot 'stdout.log') -RedirectStandardError (Join-Path $riftMeasureIsolatedRoot 'stderr.log')
while (!$riftMeasureProcess.HasExited) {
    if (((Get-Date) - $riftMeasureStarted).TotalSeconds -gt $Seconds + 180) { Stop-Process -Id $riftMeasureProcess.Id -Force; throw 'Native measurement timed out.' }
    Start-Sleep -Milliseconds 250
    $riftMeasureProcess.Refresh()
}
$riftMeasureProcess.WaitForExit()
$riftMeasureFinished = [DateTime]::UtcNow
$riftMeasureProvenanceErrors = @()
try {
    $riftMeasureCurrentSourcePins = @(Get-RiftMeasurePins $riftMeasureRepo (Get-RiftMeasureSourcePaths))
    $riftMeasureCurrentRuntimePins = @(Get-RiftMeasurePins $riftMeasureRuntimeRoot (Get-RiftMeasureRuntimePaths))
    $riftMeasureProvenance.sourcePinsUnchanged = ($riftMeasureSourcePins | ConvertTo-Json -Depth 5 -Compress) -eq ($riftMeasureCurrentSourcePins | ConvertTo-Json -Depth 5 -Compress)
    $riftMeasureProvenance.runtimePinsUnchanged = ($riftMeasureRuntimePins | ConvertTo-Json -Depth 5 -Compress) -eq ($riftMeasureCurrentRuntimePins | ConvertTo-Json -Depth 5 -Compress)
    $riftMeasureProvenance.executableUnchanged = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant() -eq $riftMeasureExecutableHash
} catch { $riftMeasureProvenanceErrors += 'Performance provenance verification failed: ' + $_.Exception.Message }
if (!$riftMeasureProvenance.sourcePinsUnchanged) { $riftMeasureProvenanceErrors += 'Source/configuration/canonical input inventory changed during performance capture.' }
if (!$riftMeasureProvenance.runtimePinsUnchanged) { $riftMeasureProvenanceErrors += 'Loaded native modules or Content inventory changed during performance capture.' }
if (!$riftMeasureProvenance.executableUnchanged) { $riftMeasureProvenanceErrors += 'Performance executable changed during capture.' }
$riftMeasureProvenance.completed = $true
$riftMeasureProvenance.completedUtc = $riftMeasureFinished.ToString('o')
$riftMeasureProvenance.exitCode = $riftMeasureProcess.ExitCode
$riftMeasureProvenance.errors = $riftMeasureProvenanceErrors
[IO.File]::WriteAllText($riftMeasureProvenancePath,($riftMeasureProvenance | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
if ($riftMeasureProcess.ExitCode -ne 0) { throw "Native measurement exited $($riftMeasureProcess.ExitCode). Inspect $riftMeasureRoot" }
if (!(Test-Path -LiteralPath $riftMeasureJson) -or (Get-Item -LiteralPath $riftMeasureJson).LastWriteTime -lt $riftMeasureStarted.AddSeconds(-1)) { throw 'Game did not produce a fresh performance report.' }
$riftMeasured = Get-Content -LiteralPath $riftMeasureJson -Raw | ConvertFrom-Json
if ($Quality -ge 0 -and $riftMeasured.observedQualityPreset -ne $Quality) { throw 'Measurement did not apply the requested native quality preset.' }
$riftMeasureScreenshots = @()
if ($CaptureScreenshots) {
    $riftRequests = @($riftMeasured.screenshotRequests)
    $riftScheduledAges = @(.12,.37,.6,3.,6.,8.,20.,40.)
    if (!$riftMeasured.screenshotCaptureEnabled -or $riftRequests.Count -ne $riftScheduledAges.Count -or @($riftRequests.path | Sort-Object -Unique).Count -ne $riftScheduledAges.Count) { throw 'Paid combat visual coverage is incomplete: every distinct scheduled screenshot is required.' }
    for ($riftRequestIndex = 0; $riftRequestIndex -lt $riftScheduledAges.Count; ++$riftRequestIndex) {
        $riftRequest = $riftRequests[$riftRequestIndex]
        if ($null -eq $riftRequest.scheduledAgeSeconds -or [math]::Abs($riftRequest.scheduledAgeSeconds-$riftScheduledAges[$riftRequestIndex]) -gt 1.e-8 -or
            $null -eq $riftRequest.simulationSeconds -or $riftRequest.simulationSeconds -lt ($StartSeconds+$riftScheduledAges[$riftRequestIndex]-1.e-8)) { throw 'Native screenshot schedule or observed request time is incomplete.' }
    }
    $riftFirstRequest = $riftRequests[0]
    if (@($riftFirstRequest.hands).Count -ne 2 -or @($riftFirstRequest.hands[0]).Count -ne 4 -or @($riftFirstRequest.hands[1]).Count -ne 4) { throw 'Paid combat screenshot lacks two real four-card hands.' }
    foreach ($riftSelectedModel in @('mini_stampede','stampede','pulverizer')) {
        if ($riftFirstRequest.hands[0] -notcontains $riftSelectedModel) { throw "Opening hand screenshot does not cover $riftSelectedModel art." }
        foreach ($riftSelectedTeam in @(0,1)) {
            if (@($riftFirstRequest.selectedVisuals | Where-Object { $_.modelId -eq $riftSelectedModel -and $_.team -eq $riftSelectedTeam -and $_.dead -eq $false }).Count -lt 1) { throw "Opening screenshot has no observed living $riftSelectedModel model for team $riftSelectedTeam." }
        }
    }
    foreach ($riftRequest in $riftMeasured.screenshotRequests) {
        $riftPngPath = [IO.Path]::GetFullPath([string]$riftRequest.path)
        $riftPngRoot = [IO.Path]::GetFullPath((Join-Path $riftMeasureIsolatedRoot 'Screenshots')) + [IO.Path]::DirectorySeparatorChar
        if (!$riftPngPath.StartsWith($riftPngRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Native screenshot escaped its isolated fixture directory.' }
        if (!(Test-Path -LiteralPath $riftPngPath -PathType Leaf) -or (Get-Item -LiteralPath $riftPngPath).LastWriteTimeUtc -lt $riftMeasureStarted.ToUniversalTime()) { throw "Fresh native screenshot is missing: $riftPngPath" }
        $riftPngBytes = [IO.File]::ReadAllBytes($riftPngPath)
        if ($riftPngBytes.Length -lt 24 -or [BitConverter]::ToString($riftPngBytes[0..7]) -ne '89-50-4E-47-0D-0A-1A-0A') { throw 'Native combat capture is not a PNG.' }
        $riftPngWidth = [int64]$riftPngBytes[16]*16777216 + [int64]$riftPngBytes[17]*65536 + [int64]$riftPngBytes[18]*256 + $riftPngBytes[19]
        $riftPngHeight = [int64]$riftPngBytes[20]*16777216 + [int64]$riftPngBytes[21]*65536 + [int64]$riftPngBytes[22]*256 + $riftPngBytes[23]
        if ($riftPngWidth -ne $Width -or $riftPngHeight -ne $Height) { throw 'Native combat screenshot has unexpected dimensions.' }
        $riftMeasureScreenshots += [ordered]@{path=$riftPngPath;sha256=(Get-FileHash -LiteralPath $riftPngPath -Algorithm SHA256).Hash.ToLowerInvariant();width=$riftPngWidth;height=$riftPngHeight;scheduledAgeSeconds=$riftRequest.scheduledAgeSeconds;requestedSimulationSeconds=$riftRequest.simulationSeconds}
    }
}
if ($riftMeasured.frame.samples -lt 100 -or $riftMeasured.width -ne $Width -or $riftMeasured.height -ne $Height) { throw 'Measurement has insufficient frames or incorrect output dimensions.' }
if ($WithMeta -and $riftMeasured.metaGamesDuringBattle -ne 0) { throw 'Background Meta advanced while the battle was active.' }
if ($ExpansionStress) {
    $riftMeasureFixture = $riftMeasured.expansionFixture
    $riftMeasureManifest = Get-Content -LiteralPath (Join-Path $riftMeasureRepo 'Assets/Data/card_manifest.json') -Raw | ConvertFrom-Json
    if (!$riftMeasureFixture -or !$riftMeasureFixture.passed -or !$riftMeasureFixture.paidDeploymentProof -or !$riftMeasureFixture.firstWaveComplete -or $riftMeasureFixture.observedCardTeamLaneCombinations -ne 32) { throw "Expansion fixture did not prove all paid card/team/lane deployments; inspect $riftMeasureJson" }
    if ($riftMeasureFixture.balanceHash -ne $riftMeasureManifest.balanceHash -or $riftMeasureFixture.manifestVersion -ne $riftMeasureManifest.version) { throw 'Expansion fixture executable balance hash/version differs from the canonical manifest.' }
    if ($riftMeasureFixture.startSimulationSeconds -ne $StartSeconds -or $riftMeasureFixture.simulationSpeed -ne [double]::Parse($SimulationSpeed,[Globalization.CultureInfo]::InvariantCulture) -or $riftMeasureFixture.seed -ne $Seed) { throw 'Expansion fixture did not honor the requested simulation phase, speed or seed.' }
    if ($riftMeasureFixture.shockwaveLaunchEvents -lt 1 -or $riftMeasureFixture.shockwaveDamageEvents -lt 1 -or $riftMeasureFixture.maximumDeathsAtOneSimulationTimestamp -lt 2 -or $riftMeasureFixture.deathVisualsObservedRetired -lt 1) { throw 'Expansion fixture is missing observed wave hits, simultaneous defeats or visual cleanup.' }
    foreach ($riftMeasureModel in @('mini_stampede','stampede','pulverizer','bone_warden','aether_reaper','iron_giant')) { if ($riftMeasureFixture.observedCardModelIds -notcontains $riftMeasureModel) { throw "Expansion fixture never observed the real $riftMeasureModel model." } }
}
$riftMeasureErrors=@(); $riftMeasureDiagnosticSource='editor-engine-log'; $riftMeasureDiagnosticLogs=@(); $riftMeasureDiagnosticVerification=$null
$riftMeasureErrorPattern='LogRift: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Authored .* missing|LogMaterial: (Error:|Warning:.*(Failed to compile|Default Material|missing usage flag))|LogShaderCompilers: Error:'
if ($riftMeasureIsEditor) {
    if ((Test-Path -LiteralPath $riftMeasureLog) -and (Get-Item -LiteralPath $riftMeasureLog).LastWriteTimeUtc -ge $riftMeasureStarted.ToUniversalTime()) { $riftMeasureErrors=@(Select-String -LiteralPath $riftMeasureLog -Pattern $riftMeasureErrorPattern|ForEach-Object {$_.Line}) }
    else { $riftMeasureErrors=@('Fresh Editor engine log missing.') }
} else {
    $riftMeasureDiagnostics=Get-RiftShippingDiagnostics -RepoRoot $riftMeasureRepo -DestinationRoot $riftMeasureIsolatedRoot -SaveRoot $riftMeasureSaves -EngineUserRoot $riftMeasureUser -ProcessId $riftMeasureProcess.Id -ExpectedVersion $ExpectedVersion -StartedUTC $riftMeasureStarted -FinishedUTC $riftMeasureFinished -ErrorPattern $riftMeasureErrorPattern
    $riftMeasureErrors=@($riftMeasureDiagnostics.errors); $riftMeasureDiagnosticSource=$riftMeasureDiagnostics.diagnosticSource; $riftMeasureDiagnosticLogs=@($riftMeasureDiagnostics.nativeDiagnosticLogs); $riftMeasureDiagnosticVerification=$riftMeasureDiagnostics.diagnosticVerification; $riftMeasureLog=$riftMeasureDiagnostics.engineLog
}
$riftMeasureErrors += $riftMeasureProvenanceErrors
$riftMeasureRun = [ordered]@{ passed=$riftMeasureErrors.Count -eq 0; version=$ExpectedVersion; utc=[DateTime]::UtcNow.ToString('o'); executable=$Executable; executableSha256=$riftMeasureExecutableHash;
    editor=$riftMeasureIsEditor; exitCode=$riftMeasureProcess.ExitCode; report=$riftMeasureJson;
    reportSha256=(Get-FileHash -LiteralPath $riftMeasureJson -Algorithm SHA256).Hash.ToLowerInvariant();
    engineLog=$riftMeasureLog; diagnosticSource=$riftMeasureDiagnosticSource; nativeDiagnosticLogs=$riftMeasureDiagnosticLogs; diagnosticVerification=$riftMeasureDiagnosticVerification; errors=$riftMeasureErrors;
    saveRoot=$riftMeasureSaves; seconds=[math]::Round(((Get-Date)-$riftMeasureStarted).TotalSeconds,2);
    sourcePins=$riftMeasureSourcePins;sourcePinsUnchanged=$riftMeasureProvenance.sourcePinsUnchanged;
    runtimeRoot=$riftMeasureRuntimeRoot;runtimePins=$riftMeasureRuntimePins;runtimePinsUnchanged=$riftMeasureProvenance.runtimePinsUnchanged;executableUnchanged=$riftMeasureProvenance.executableUnchanged;
    runRoot=$riftMeasureIsolatedRoot;provenance=$riftMeasureProvenancePath;provenanceSha256=(Get-FileHash -LiteralPath $riftMeasureProvenancePath -Algorithm SHA256).Hash.ToLowerInvariant() }
$riftMeasureRun.screenshotCaptureEnabled = [bool]$CaptureScreenshots
$riftMeasureRun.screenshots = $riftMeasureScreenshots
if ($CaptureScreenshots) { $riftMeasureRun.measurementLimitation = 'Screenshot rendering/readback occurs during this visual-review fixture. Use separate screenshot-disabled runs for performance comparisons.' }
if ($ExpansionStress) { $riftMeasureRun.expansionFixture=[ordered]@{ name=$riftMeasureFixture.name; seed=$Seed; startSimulationSeconds=$StartSeconds; simulationSpeed=$SimulationSpeed; balanceHash=$riftMeasureFixture.balanceHash; manifestVersion=$riftMeasureFixture.manifestVersion; passed=$riftMeasureFixture.passed } }
[IO.File]::WriteAllText((Join-Path $riftMeasureIsolatedRoot 'run.json'),($riftMeasureRun | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
# Preserve historical latest-report aliases; their records point to the immutable run.
if ($riftMeasureRun.passed) {
    Copy-Item -LiteralPath $riftMeasureJson -Destination (Join-Path $riftMeasureRoot 'performance.json') -Force
    Copy-Item -LiteralPath (Join-Path $riftMeasureIsolatedRoot 'run.json') -Destination (Join-Path $riftMeasureRoot 'run.json') -Force
}
Write-Output ($riftMeasured | ConvertTo-Json -Depth 20)
if ($riftMeasureErrors.Count -gt 0) { throw "Native measurement diagnostics failed; inspect $riftMeasureRoot" }
