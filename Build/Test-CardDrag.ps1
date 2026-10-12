param(
    [string]$Executable = '',
    [string]$Name = 'card-drag-native',
    [string]$ExpectedVersion = '',
    [ValidateRange(1280,7680)][int]$Width = 1920,
    [ValidateRange(720,4320)][int]$Height = 1080,
    [ValidateRange(30,600)][int]$TimeoutSeconds = 180
)
$ErrorActionPreference = 'Stop'
$riftDragRepo = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
. (Join-Path $PSScriptRoot 'ShippingDiagnostics.ps1')
$ExpectedVersion = Get-RiftExpectedGameVersion $riftDragRepo $ExpectedVersion
if ($Name -notmatch '^[a-z0-9][a-z0-9_-]*$') { throw 'Use a lowercase QA report name.' }
if ($ExpectedVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Use a three-part release version.' }
$riftDragExpectedChecks = if ($ExpectedVersion -eq '1.3.0') { 29 } elseif ([version]$ExpectedVersion -lt [version]'1.7.0') { 32 } else { 37 }
if (!$Executable) { $Executable = Join-Path $riftDragRepo ('Artifacts/Game-' + $ExpectedVersion + '/Windows/RiftCrownArena/Binaries/Win64/RiftCrownArena-Win64-Shipping.exe') }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$riftDragExecutableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
$riftDragIsEditor = [IO.Path]::GetFileNameWithoutExtension($Executable) -in @('UnrealEditor','UnrealEditor-Cmd')
$riftDragRoot = Join-Path $riftDragRepo ('Artifacts/QA/CardDrag/' + $Name + '-' + [Guid]::NewGuid().ToString('N'))
$riftDragSave = Join-Path $riftDragRoot 'Save'
$riftDragUser = Join-Path $riftDragRoot 'EngineUser'
New-Item -ItemType Directory -Path $riftDragRoot,$riftDragSave,$riftDragUser -Force | Out-Null
$riftDragNativeReport = Join-Path $riftDragRoot 'card-drag-smoke.json'
$riftDragLog = Join-Path $riftDragRoot 'engine.log'
$riftDragArguments = @(
    '/Game/Rift/Maps/Arena','-game','-RenderOffscreen','-windowed',('-ResX=' + $Width),('-ResY=' + $Height),
    ('-RiftSaveRoot="' + $riftDragSave + '"'),('-UserDir="' + $riftDragUser + '"'),
    ('-RiftDragSmoke="' + $riftDragNativeReport + '"'),'-RiftAutomationSandbox',
    '-unattended','-nosplash','-NoSound','-NoVSync',('-abslog="' + $riftDragLog + '"')
)
if ($riftDragIsEditor) { $riftDragArguments = @(('"' + (Join-Path $riftDragRepo 'Unreal/RiftCrownArena/RiftCrownArena.uproject') + '"')) + $riftDragArguments }
function Get-RiftDragPins([string]$Base, [string[]]$Paths) {
    @($Paths | Sort-Object -Unique | ForEach-Object {
        if (!(Test-Path -LiteralPath $_ -PathType Leaf)) { throw "Native smoke provenance input is missing: $_" }
        [ordered]@{path=[IO.Path]::GetRelativePath($Base,$_).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length}
    })
}
function Get-RiftDragSourcePaths {
    $riftDragPaths = @(Get-ChildItem -LiteralPath (Join-Path $riftDragRepo 'Unreal/RiftCrownArena/Source'),(Join-Path $riftDragRepo 'Unreal/RiftCrownArena/Config') -File -Recurse | ForEach-Object FullName) +
        @('Unreal/RiftCrownArena/RiftCrownArena.uproject','Assets/Data/card_manifest.json','Build/generate_card_manifest.py','Build/Test-CardDrag.ps1','Build/ShippingDiagnostics.ps1' | ForEach-Object { Join-Path $riftDragRepo $_ })
    # Historical evidence readers also find Editor module pins in sourcePins.
    if ($riftDragIsEditor) {
        $riftDragPaths += @('UnrealEditor-RiftCrownArena.dll','UnrealEditor-RiftCrownArenaEditor.dll' | ForEach-Object { Join-Path $riftDragRepo ('Unreal/RiftCrownArena/Binaries/Win64/' + $_) })
    }
    $riftDragPaths
}
function Get-RiftDragRuntimePaths {
    if ($riftDragIsEditor) {
        @(Get-ChildItem -LiteralPath (Join-Path $riftDragRuntimeRoot 'Binaries'),(Join-Path $riftDragRuntimeRoot 'Config'),(Join-Path $riftDragRuntimeRoot 'Content') -File -Recurse | ForEach-Object FullName) +
            @((Join-Path $riftDragRuntimeRoot 'RiftCrownArena.uproject'))
    } else {
        @(Get-ChildItem -LiteralPath $riftDragRuntimeRoot -File -Recurse | ForEach-Object FullName)
    }
}
$riftDragRuntimeRoot = if ($riftDragIsEditor) { Join-Path $riftDragRepo 'Unreal/RiftCrownArena' } else { $null }
if (!$riftDragIsEditor) {
    for ($riftDragAncestor = [IO.DirectoryInfo]::new((Split-Path -Parent $Executable)); $riftDragAncestor; $riftDragAncestor = $riftDragAncestor.Parent) {
        if ((Test-Path -LiteralPath (Join-Path $riftDragAncestor.FullName 'RiftCrownArena.exe') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $riftDragAncestor.FullName 'RiftCrownArena/Content') -PathType Container)) {
            $riftDragRuntimeRoot = $riftDragAncestor.FullName
            break
        }
    }
    if (!$riftDragRuntimeRoot) { throw 'Native smoke requires an attributable packaged game root containing RiftCrownArena.exe and RiftCrownArena/Content.' }
}
$riftDragPins = @(Get-RiftDragPins $riftDragRepo (Get-RiftDragSourcePaths))
$riftDragRuntimePins = @(Get-RiftDragPins $riftDragRuntimeRoot (Get-RiftDragRuntimePaths))
if ($riftDragIsEditor) {
    foreach ($riftDragModule in @('UnrealEditor-RiftCrownArena.dll','UnrealEditor-RiftCrownArenaEditor.dll')) {
        if (!(Test-Path -LiteralPath (Join-Path $riftDragRuntimeRoot ('Binaries/Win64/' + $riftDragModule)) -PathType Leaf)) { throw "Build the Editor module before native smoke: $riftDragModule" }
    }
}
$riftDragStarted = [DateTime]::UtcNow
[IO.File]::WriteAllText((Join-Path $riftDragRoot 'provenance.json'),([ordered]@{
    startedUtc=$riftDragStarted.ToString('o');executable=$Executable;executableSha256=$riftDragExecutableHash;
    sourcePins=$riftDragPins;runtimeRoot=$riftDragRuntimeRoot;runtimePins=$riftDragRuntimePins
} | ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($false))
$riftDragProcess = Start-Process -FilePath $Executable -ArgumentList $riftDragArguments -WorkingDirectory $(if ($riftDragIsEditor) { $riftDragRepo } else { Split-Path -Parent $Executable }) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $riftDragRoot 'stdout.log') -RedirectStandardError (Join-Path $riftDragRoot 'stderr.log')
$riftDragTimedOut = $false
while (!$riftDragProcess.HasExited) {
    if (([DateTime]::UtcNow - $riftDragStarted).TotalSeconds -gt $TimeoutSeconds) {
        $riftDragTimedOut = $true
        Stop-Process -Id $riftDragProcess.Id -Force
        break
    }
    Start-Sleep -Milliseconds 250
    $riftDragProcess.Refresh()
}
$riftDragProcess.WaitForExit()
$riftDragFinished = [DateTime]::UtcNow
$riftDragFresh = (Test-Path -LiteralPath $riftDragNativeReport) -and (Get-Item -LiteralPath $riftDragNativeReport).LastWriteTimeUtc -ge $riftDragStarted.AddSeconds(-1)
$riftDragResult = if ($riftDragFresh) { Get-Content -LiteralPath $riftDragNativeReport -Raw | ConvertFrom-Json } else { $null }
$riftDragErrors = @()
$riftDragErrorPattern = 'LogRift: Error:|LogUIActionRouter: Error:|Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Failed to load.*(/Game/Rift|Rift/)|Authored .* missing|LogMaterial: (Error:|Warning:.*(Failed to compile|Default Material|missing usage flag))|LogShaderCompilers: Error:'
$riftDragLogSource = 'editor-engine-log'
$riftDragEngineLogAvailable = Test-Path -LiteralPath $riftDragLog
$riftDragDiagnosticLogs = @()
$riftDragDiagnosticVerification = $null
if ($riftDragIsEditor) {
    if ($riftDragEngineLogAvailable) {
        $riftDragErrors = @(Select-String -LiteralPath $riftDragLog -Pattern $riftDragErrorPattern | ForEach-Object { $_.Line.Replace($riftDragRepo,'<repo>').Replace($env:USERPROFILE,'<user>') })
    } else { $riftDragErrors = @('Native Editor engine log missing.') }
} else {
    # Installed-engine Shipping builds write game diagnostics through FRiftDiagnostics.
    # Require their actual process/context/route records rather than treating an absent engine.log as a pass.
    $riftDragLogSource = 'shipping-frift-diagnostics'
    $riftDragDiagnosticDirectory = Join-Path $riftDragSave 'Logs'
    $riftDragLogFiles = @(if (Test-Path -LiteralPath $riftDragDiagnosticDirectory) { Get-ChildItem -LiteralPath $riftDragDiagnosticDirectory -Filter 'RiftGame-*.log' -File | Sort-Object Name })
    $riftDragDiagnosticRecords = @()
    $riftDragDiagnosticFresh = $riftDragLogFiles.Count -gt 0
    $riftDragDiagnosticCopyDirectory = Join-Path $riftDragRoot 'Diagnostics'
    New-Item -ItemType Directory -Path $riftDragDiagnosticCopyDirectory -Force | Out-Null
    if ($riftDragLogFiles.Count -eq 0) { $riftDragErrors += 'Fresh Shipping game diagnostics missing.' }
    foreach ($riftDragLogFile in $riftDragLogFiles) {
        if ($riftDragLogFile.Name -notmatch ('^RiftGame-\d{4}-\d{2}-\d{2}-' + $riftDragProcess.Id + '-\d{2}-\d{2}-\d{2}-\d+\.log$') -or $riftDragLogFile.LastWriteTimeUtc -lt $riftDragStarted.AddSeconds(-1)) {
            $riftDragErrors += 'Shipping game diagnostic filename or freshness does not match the launched process.'
            $riftDragDiagnosticFresh = $false
        }
        try {
            $riftDragDiagnosticRecords += @([IO.File]::ReadAllLines($riftDragLogFile.FullName) | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json -Depth 30 })
        } catch { $riftDragErrors += 'Shipping game diagnostics contain malformed JSON records.' }
        $riftDragDiagnosticHash = (Get-FileHash -LiteralPath $riftDragLogFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        # Archive only reviewed log bytes, outside private save/config directories.
        $riftDragDiagnosticCopy = Join-Path $riftDragDiagnosticCopyDirectory $riftDragLogFile.Name
        Copy-Item -LiteralPath $riftDragLogFile.FullName -Destination $riftDragDiagnosticCopy
        if ((Get-Item -LiteralPath $riftDragDiagnosticCopy).Length -ne $riftDragLogFile.Length -or (Get-FileHash -LiteralPath $riftDragDiagnosticCopy -Algorithm SHA256).Hash.ToLowerInvariant() -ne $riftDragDiagnosticHash) {
            $riftDragErrors += 'Shipping game diagnostic evidence copy differs from the original isolated log.'
        }
        $riftDragDiagnosticLogs += [ordered]@{file=$riftDragDiagnosticCopy;sourceFile=$riftDragLogFile.FullName;bytes=$riftDragLogFile.Length;sha256=$riftDragDiagnosticHash}
    }
    $riftDragContexts = @($riftDragDiagnosticRecords | Where-Object event -eq 'system_context')
    $riftDragContextMatches = $riftDragLogFiles.Count -gt 0 -and $riftDragContexts.Count -eq $riftDragLogFiles.Count
    foreach ($riftDragContext in $riftDragContexts) {
        if ($riftDragContext.schemaVersion -ne 1 -or $riftDragContext.project -ne 'RiftCrownArena' -or $riftDragContext.build -ne 'Shipping' -or
            $riftDragContext.gameVersion -ne $ExpectedVersion -or $riftDragContext.processId -ne $riftDragProcess.Id -or
            [IO.Path]::GetFullPath($riftDragContext.saveRoot) -ne [IO.Path]::GetFullPath($riftDragSave)) { $riftDragContextMatches = $false }
    }
    if (!$riftDragContextMatches) { $riftDragErrors += 'Shipping game diagnostic context does not match the exact launched version, process, build and isolated save root.' }
    foreach ($riftDragRecord in $riftDragDiagnosticRecords) {
        if ($riftDragRecord.schemaVersion -ne 1) { $riftDragErrors += 'Shipping game diagnostic record has an unsupported schema.' }
        try {
            # PowerShell 7.6 decodes JSON timestamps as DateTime; retain their UTC kind.
            $riftDragRecordTime = if ($riftDragRecord.timestamp -is [DateTime]) { $riftDragRecord.timestamp.ToUniversalTime() }
                elseif ($riftDragRecord.timestamp -is [DateTimeOffset]) { $riftDragRecord.timestamp.UtcDateTime }
                else { [DateTimeOffset]::Parse([string]$riftDragRecord.timestamp,[Globalization.CultureInfo]::InvariantCulture).UtcDateTime }
            if ($riftDragRecordTime -lt $riftDragStarted.AddSeconds(-1) -or $riftDragRecordTime -gt $riftDragFinished.AddSeconds(1)) { $riftDragDiagnosticFresh = $false }
        } catch { $riftDragDiagnosticFresh = $false }
        if ($riftDragRecord.level -in @('Error','Fatal') -or $riftDragRecord.message -match $riftDragErrorPattern) {
            $riftDragErrors += ([string]$riftDragRecord.message).Replace($riftDragRepo,'<repo>').Replace($env:USERPROFILE,'<user>')
        }
    }
    if (!$riftDragDiagnosticFresh) { $riftDragErrors += 'Shipping game diagnostic records are missing fresh timestamps from this run.' }
    $riftDragRoutePasses = @($riftDragDiagnosticRecords | Where-Object { $_.level -eq 'Log' -and $_.message -like 'Card drag route: PASS *' })
    $riftDragRouteRecordsMatch = $null -ne $riftDragResult -and $riftDragRoutePasses.Count -eq $riftDragExpectedChecks
    foreach ($riftDragCheck in @($riftDragResult.checks)) {
        if (@($riftDragRoutePasses | Where-Object message -eq ('Card drag route: PASS ' + $riftDragCheck.name)).Count -ne 1) { $riftDragRouteRecordsMatch = $false }
    }
    $riftDragCompletionMatches = @($riftDragDiagnosticRecords | Where-Object { $_.level -eq 'Log' -and $_.message -eq ('Native card drag smoke passed: ' + $riftDragNativeReport) }).Count -eq 1
    if (!$riftDragRouteRecordsMatch) { $riftDragErrors += ('Shipping game diagnostics do not contain each of the exact '+$riftDragExpectedChecks+' native PASS assertions once.') }
    if (!$riftDragCompletionMatches) { $riftDragErrors += 'Shipping game diagnostics do not contain the exact fresh smoke-passed completion record.' }
    $riftDragDiagnosticVerification = [ordered]@{passed=@($riftDragErrors).Count -eq 0;processId=$riftDragProcess.Id;startedUTC=$riftDragStarted.ToString('o');finishedUTC=$riftDragFinished.ToString('o');saveRoot=$riftDragSave;gameVersion=$(if ($riftDragContexts.Count -gt 0) { $riftDragContexts[0].gameVersion } else { $null });build=$(if ($riftDragContexts.Count -gt 0) { $riftDragContexts[0].build } else { $null });freshLog=[bool]$riftDragDiagnosticFresh;contextMatches=[bool]$riftDragContextMatches;passCount=$riftDragRoutePasses.Count;routeRecordsMatch=[bool]$riftDragRouteRecordsMatch;finalPass=[bool]$riftDragCompletionMatches;errorCount=@($riftDragErrors).Count;recordCount=$riftDragDiagnosticRecords.Count}
    # Legacy evidence readers use engineLog as the selected runtime-log path; logSource identifies its actual format.
    if ($riftDragDiagnosticLogs.Count -gt 0) { $riftDragLog = $riftDragDiagnosticLogs[0].file }
}
$riftDragSourcesUnchanged = $true
foreach ($riftDragPin in $riftDragPins) {
    $riftDragPinnedFile = Join-Path $riftDragRepo $riftDragPin.path
    if (!(Test-Path -LiteralPath $riftDragPinnedFile) -or (Get-FileHash -LiteralPath $riftDragPinnedFile -Algorithm SHA256).Hash.ToLowerInvariant() -ne $riftDragPin.sha256 -or (Get-Item -LiteralPath $riftDragPinnedFile).Length -ne $riftDragPin.bytes) {
        $riftDragSourcesUnchanged = $false
        $riftDragErrors += 'Source or Editor module changed during the native smoke: ' + $riftDragPin.path
    }
}
try {
    $riftDragCurrentSourcePins = @(Get-RiftDragPins $riftDragRepo (Get-RiftDragSourcePaths))
    if (($riftDragPins | ConvertTo-Json -Depth 5 -Compress) -ne ($riftDragCurrentSourcePins | ConvertTo-Json -Depth 5 -Compress)) {
        $riftDragSourcesUnchanged = $false
        $riftDragErrors += 'Native smoke source inventory changed during the run.'
    }
    $riftDragCurrentRuntimePins = @(Get-RiftDragPins $riftDragRuntimeRoot (Get-RiftDragRuntimePaths))
    $riftDragRuntimeUnchanged = ($riftDragRuntimePins | ConvertTo-Json -Depth 5 -Compress) -eq ($riftDragCurrentRuntimePins | ConvertTo-Json -Depth 5 -Compress)
} catch {
    $riftDragRuntimeUnchanged = $false
    $riftDragErrors += 'Native smoke source/runtime provenance check failed: ' + $_.Exception.Message
}
if (!$riftDragRuntimeUnchanged) { $riftDragErrors += 'Loaded native modules, configuration or Content changed during the run.' }
$riftDragExecutableUnchanged = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant() -eq $riftDragExecutableHash
$riftDragPassed = !$riftDragTimedOut -and $riftDragProcess.ExitCode -eq 0 -and $riftDragFresh -and $riftDragSourcesUnchanged -and $riftDragRuntimeUnchanged -and $riftDragExecutableUnchanged -and $null -ne $riftDragResult -and $riftDragResult.passed -eq $true -and $riftDragResult.version -eq $ExpectedVersion -and $riftDragResult.width -eq $Width -and $riftDragResult.height -eq $Height -and $riftDragResult.checkCount -eq $riftDragExpectedChecks -and @($riftDragResult.checks).Count -eq $riftDragExpectedChecks -and @($riftDragResult.checks | Where-Object { $_.passed -ne $true }).Count -eq 0 -and @($riftDragErrors).Count -eq 0
function Get-RiftDragRelative([string]$Path) {
    if ($Path.StartsWith($riftDragRepo + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { return [IO.Path]::GetRelativePath($riftDragRepo,$Path).Replace('\','/') }
    return 'external-runtime/' + [IO.Path]::GetFileName($Path)
}
foreach ($riftDragDiagnosticLog in $riftDragDiagnosticLogs) {
    $riftDragDiagnosticLog.file = Get-RiftDragRelative $riftDragDiagnosticLog.file
    $riftDragDiagnosticLog.sourceFile = Get-RiftDragRelative $riftDragDiagnosticLog.sourceFile
}
if ($riftDragDiagnosticVerification) { $riftDragDiagnosticVerification.saveRoot = Get-RiftDragRelative $riftDragDiagnosticVerification.saveRoot }
$riftDragVerification = [ordered]@{
    schemaVersion=1;version=$ExpectedVersion;name=$Name;utc=[DateTime]::UtcNow.ToString('o');passed=[bool]$riftDragPassed;
    executable=(Get-RiftDragRelative $Executable);executableSha256=$riftDragExecutableHash;editor=$riftDragIsEditor;
    report=(Get-RiftDragRelative $riftDragNativeReport);engineLog=(Get-RiftDragRelative $riftDragLog);
    logSource=$riftDragLogSource;engineLogAvailable=[bool]$riftDragEngineLogAvailable;nativeDiagnosticLogs=@($riftDragDiagnosticLogs);diagnosticVerification=$riftDragDiagnosticVerification;
    freshReport=[bool]$riftDragFresh;timedOut=$riftDragTimedOut;exitCode=$riftDragProcess.ExitCode;
    sourcePinsUnchanged=$riftDragSourcesUnchanged;executableUnchanged=$riftDragExecutableUnchanged;
    width=$Width;height=$Height;seconds=[math]::Round(([DateTime]::UtcNow - $riftDragStarted).TotalSeconds,2);
    checks=$(if ($riftDragResult) { @($riftDragResult.checks) } else { @() });checkCount=$(if ($riftDragResult) { $riftDragResult.checkCount } else { 0 });
    errors=@($riftDragErrors);nativeReportSha256=$(if ($riftDragFresh) { (Get-FileHash -LiteralPath $riftDragNativeReport -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null });
    sourcePins=$riftDragPins;
    runtimeRoot=(Get-RiftDragRelative $riftDragRuntimeRoot);runtimePins=$riftDragRuntimePins;runtimePinsUnchanged=[bool]$riftDragRuntimeUnchanged;
    runReport=(Get-RiftDragRelative (Join-Path $riftDragRoot 'verification.json'));
    scope='Real Slate mouse routing through the production UMG hand, legal single deployment and lossless cancellation. Editor errors use engine.log; Shipping errors use fresh structured FRiftDiagnostics game logs. This smoke does not certify art quality.'
}
$riftDragVerificationPath = Join-Path $riftDragRepo ('Artifacts/QA/CardDrag/' + $Name + '-verification.json')
[IO.File]::WriteAllText((Join-Path $riftDragRoot 'verification.json'),($riftDragVerification | ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($false))
# Keep the historical latest-report path while retaining immutable per-run evidence.
[IO.File]::WriteAllText($riftDragVerificationPath,($riftDragVerification | ConvertTo-Json -Depth 14),[Text.UTF8Encoding]::new($false))
Write-Output "Native hand drag checks: $($riftDragVerification.checkCount). Report: $riftDragVerificationPath"
if (!$riftDragPassed) { throw "Native hand drag smoke failed; inspect $riftDragVerificationPath" }

