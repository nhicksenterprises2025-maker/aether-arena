param([string]$Msi,[string]$UpgradeMsi)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $Msi) { $Msi = Join-Path $repoRoot 'Artifacts/Installer/RiftCrownArena-Setup.msi' }
$msiPath = [IO.Path]::GetFullPath($Msi)
if (-not (Test-Path -LiteralPath $msiPath -PathType Leaf)) { throw 'Compile the actual packaged-game MSI before running installer QA.' }

# Read only MSI product metadata. No player profile is discovered or read.
$windowsInstaller = New-Object -ComObject WindowsInstaller.Installer
function ComMethod($Object, [string]$Name, [object[]]$Arguments) {
    return ,($Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::InvokeMethod, $null, $Object, $Arguments))
}
function ComProperty($Object, [string]$Name, [object[]]$Arguments = @()) {
    return ,($Object.GetType().InvokeMember($Name, [Reflection.BindingFlags]::GetProperty, $null, $Object, $Arguments))
}
$database = ComMethod $windowsInstaller 'OpenDatabase' @($msiPath, 0)
function MsiProperty([string]$Name) {
    $view = ComMethod $database 'OpenView' @("SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = '$Name'")
    try {
        ComMethod $view 'Execute' @() | Out-Null
        $record = ComMethod $view 'Fetch' @()
        if (-not $record) { throw "MSI property $Name is missing." }
        try { return ComProperty $record 'StringData' @(1) }
        finally { [Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) | Out-Null }
    }
    finally { ComMethod $view 'Close' @() | Out-Null; [Runtime.InteropServices.Marshal]::FinalReleaseComObject($view) | Out-Null }
}
$productCode = MsiProperty 'ProductCode'
$upgradeCode = MsiProperty 'UpgradeCode'
$version = MsiProperty 'ProductVersion'
$upgradeProductCode = $null
if ($UpgradeMsi) {
    $upgradeMsiPath = [IO.Path]::GetFullPath($UpgradeMsi)
    if (-not (Test-Path -LiteralPath $upgradeMsiPath -PathType Leaf)) { throw 'Upgrade MSI is missing.' }
    [Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) | Out-Null
    $database = ComMethod $windowsInstaller 'OpenDatabase' @($upgradeMsiPath, 0)
    $upgradeProductCode = MsiProperty 'ProductCode'
    $upgradeVersion = MsiProperty 'ProductVersion'
    if ((MsiProperty 'UpgradeCode') -ne $upgradeCode -or $upgradeProductCode -eq $productCode -or [Version]$upgradeVersion -lt [Version]$version) { throw 'Upgrade QA requires a distinct product in the same upgrade family, with an equal or newer version.' }
}
$related = ComProperty $windowsInstaller 'RelatedProducts' @($upgradeCode)
try {
    if ((ComProperty $related 'Count') -gt 0) { throw 'Installer QA requires no existing Rift Crown Arena MSI installation, so an existing game is never upgraded or removed by this test.' }
}
finally { [Runtime.InteropServices.Marshal]::FinalReleaseComObject($related) | Out-Null }
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) | Out-Null
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($windowsInstaller) | Out-Null
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Rift Crown Arena'
if (Test-Path -LiteralPath $startMenu) { throw 'Installer QA will not overwrite an existing Rift Crown Arena Start menu folder.' }
$desktopShortcut = Join-Path ([Environment]::GetFolderPath('DesktopDirectory')) 'Rift Crown Arena.lnk'
if (Test-Path -LiteralPath $desktopShortcut) { throw 'Installer QA will not overwrite an existing Rift Crown Arena desktop shortcut.' }

$qaParent = [IO.Path]::GetFullPath((Join-Path $repoRoot 'Artifacts/QA'))
$qaRoot = [IO.Path]::GetFullPath((Join-Path $qaParent ('installer-' + [Guid]::NewGuid().ToString('N'))))
if (-not $qaRoot.StartsWith($qaParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Isolated installer QA path escaped the workspace.' }
$installRoot = Join-Path $qaRoot 'InstalledGame'
$saveRoot = Join-Path $qaRoot 'Saves'
New-Item -ItemType Directory -Path $saveRoot -Force | Out-Null
$saveFixture = Join-Path $saveRoot 'player_save.json'
[IO.File]::WriteAllText($saveFixture, '{"schemaVersion":1,"fixture":"isolated-installer-save","unknownLegacyField":{"preserve":true}}', [Text.UTF8Encoding]::new($false))
$saveHash = (Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash
$checks = [Collections.Generic.List[string]]::new()
$registered = $false
$registeredProductCode = $productCode
$primaryFailure = $null

function Require([bool]$Passed, [string]$Description) {
    if (-not $Passed) { throw $Description }
    $checks.Add($Description)
}
function RunMsi([string[]]$Arguments) {
    $process = Start-Process -FilePath (Join-Path $env:WINDIR 'System32/msiexec.exe') -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    if ($process.ExitCode -notin @(0, 3010)) { throw "Windows Installer failed with exit code $($process.ExitCode). See the verbose log in $qaRoot." }
    return $process.ExitCode
}
function RequireMaintenanceDirectory([string]$LogPath) {
    $directoryLines=@(Get-Content -LiteralPath $LogPath | Where-Object { $_ -match 'Dir \(target\): Key: INSTALLFOLDER\s*, Object:' })
    Require ($directoryLines.Count -gt 0) 'Windows Installer logs its actual maintenance target directory.'
    foreach($directoryLine in $directoryLines){if($directoryLine -notmatch 'Object: (.+)$'){throw 'Maintenance directory log is malformed.'};Require ([IO.Path]::GetFullPath($Matches[1].Trim()).TrimEnd('\','/') -eq [IO.Path]::GetFullPath($installRoot).TrimEnd('\','/')) 'Windows Installer maintenance remains inside the original isolated install directory.'}
}

try {
    $installExit = RunMsi @('/i', ('"' + $msiPath + '"'), '/qn', '/norestart', ('INSTALLFOLDER="' + $installRoot + '"'), 'ADDLOCAL=CoreGame,DesktopShortcutFeature', 'WIXUI_EXITDIALOGOPTIONALCHECKBOX=0', '/L*v', ('"' + (Join-Path $qaRoot 'install.log') + '"'))
    $registered = $true
    Require ($installExit -eq 0) 'Per-user unattended installation completed without a reboot request.'
    $pointerPath = Join-Path $installRoot 'Game/installed.json'
    Require (Test-Path -LiteralPath $pointerPath -PathType Leaf) 'Installed bundled-game pointer exists.'
    $pointer = Get-Content -LiteralPath $pointerPath -Raw | ConvertFrom-Json
    Require ($pointer.schemaVersion -eq 1 -and $pointer.version -eq $version) 'Bundled pointer schema and installed version agree with the MSI.'
    $gameRoot = [IO.Path]::GetFullPath((Join-Path $installRoot ('Game/' + $pointer.releasePath)))
    $gameParent = [IO.Path]::GetFullPath((Join-Path $installRoot 'Game'))
    Require ($gameRoot.StartsWith($gameParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) 'Bundled release resolves within the isolated installation.'
    foreach ($file in $pointer.manifest.files) {
        $path = [IO.Path]::GetFullPath((Join-Path $gameRoot $file.path))
        if (-not $path.StartsWith($gameRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Installed manifest path escaped its release.' }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Installed package file is missing: $($file.path)" }
        if ((Get-Item -LiteralPath $path).Length -ne $file.size -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw "Installed package file failed verification: $($file.path)" }
    }
    Require ($pointer.manifest.files.Count -gt 0) ("All $($pointer.manifest.files.Count) actual game files match their published lengths and SHA-256 hashes.")
    Require (Test-Path -LiteralPath (Join-Path $gameRoot 'RiftCrownArena.exe') -PathType Leaf) 'Actual native game executable is installed.'
    $launcher = Join-Path $installRoot 'Launcher/RiftCrownLauncher.exe'
    Require (Test-Path -LiteralPath $launcher -PathType Leaf) 'Self-contained launcher is installed.'
    $smoke = Start-Process -FilePath $launcher -ArgumentList @('--self-test', '--install-root', ('"' + $installRoot + '"'), '--save-root', ('"' + $saveRoot + '"')) -WindowStyle Hidden -PassThru
    if (-not $smoke.WaitForExit(90000)) { $smoke.Kill($true); $smoke.WaitForExit(10000) | Out-Null; throw 'Installed launcher smoke exceeded 90 seconds.' }
    Require ($smoke.ExitCode -eq 0) 'Installed launcher completed its actual WPF smoke check.'
    $smokeReport = Get-Content -LiteralPath (Join-Path $saveRoot 'launcher-self-test.json') -Raw | ConvertFrom-Json
    Require ($smokeReport.passed -and $smokeReport.installed -eq $pointer.version -and $smokeReport.commands -eq 10) 'Installed launcher discovers the bundled game and all ten connected command controls.'
    Require (Test-Path -LiteralPath (Join-Path $startMenu 'Rift Crown Arena.lnk') -PathType Leaf) 'Installer creates the Start menu launch shortcut.'
    Require (Test-Path -LiteralPath $desktopShortcut -PathType Leaf) 'Installer creates the selected optional desktop shortcut.'
    Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Installation and launcher smoke preserve the isolated legacy save byte for byte.'

    if ($UpgradeMsi) {
        $upgradeExit = RunMsi @('/i', ('"' + $upgradeMsiPath + '"'), '/qn', '/norestart', ('INSTALLFOLDER="' + $installRoot + '"'), 'ADDLOCAL=CoreGame,DesktopShortcutFeature', 'WIXUI_EXITDIALOGOPTIONALCHECKBOX=0', '/L*v', ('"' + (Join-Path $qaRoot 'upgrade.log') + '"'))
        $registeredProductCode = $upgradeProductCode
        Require ($upgradeExit -eq 0) 'Actual major upgrade replaces the prior MSI registration without requesting a reboot.'
        $pointer = Get-Content -LiteralPath $pointerPath -Raw | ConvertFrom-Json
        Require ($pointer.version -eq $upgradeVersion) 'Upgrade installs the new bundled release pointer.'
        $gameRoot = [IO.Path]::GetFullPath((Join-Path $installRoot ('Game/' + $pointer.releasePath)))
        Require ($gameRoot.StartsWith($gameParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) 'Upgraded release remains inside the isolated managed game tree.'
        foreach ($file in $pointer.manifest.files) {
            $path = [IO.Path]::GetFullPath((Join-Path $gameRoot $file.path))
            if (-not $path.StartsWith($gameRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $file.size -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) { throw "Upgraded package failed verification: $($file.path)" }
        }
        Require ($pointer.manifest.files.Count -gt 0) 'Every upgraded game file matches the installed manifest.'
        Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Actual MSI major upgrade preserves the separate isolated profile byte for byte.'
    }

    $installerRegistration=Get-ItemProperty -LiteralPath 'HKCU:\Software\RiftCrownArena\Installer'
    $validatedLauncherHash=(Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash.ToLowerInvariant()
    Require ([IO.Path]::GetFullPath($installerRegistration.InstallFolder).TrimEnd('\','/') -eq [IO.Path]::GetFullPath($installRoot).TrimEnd('\','/')) 'Installer remembers the exact isolated directory for later maintenance.'
    Require ([IO.Path]::GetFullPath($installerRegistration.GameCache).TrimEnd('\','/') -eq [IO.Path]::GetFullPath($gameParent).TrimEnd('\','/')) 'Installer remembered cache cleanup remains inside the isolated Game subtree.'
    $playSmoke = Start-Process -FilePath $launcher -ArgumentList @('--self-test-play', '--install-root', ('"' + $installRoot + '"'), '--save-root', ('"' + $saveRoot + '"')) -WindowStyle Hidden -PassThru
    if (-not $playSmoke.WaitForExit(180000)) { $playSmoke.Kill($true); $playSmoke.WaitForExit(10000) | Out-Null; throw 'Installed launcher Play smoke exceeded 180 seconds.' }
    Require ($playSmoke.ExitCode -eq 0) 'Actual installed WPF Play command completes its real bundled-game launch and waits for clean exit.'
    $playReport = Get-Content -LiteralPath (Join-Path $saveRoot 'launcher-play-self-test.json') -Raw | ConvertFrom-Json
    Require ($playReport.passed -and $playReport.routedCommand -eq 'PlayButton.Click' -and $playReport.launcherSha256 -eq $validatedLauncherHash -and $playReport.installed -eq $pointer.version -and $playReport.verifiedGameFiles -eq $pointer.manifest.files.Count -and $playReport.bootstrapExitCode -eq 0 -and $playReport.nativeExitCode -eq 0) 'Installed Play invokes the exact installed launcher routed command and verifies every manifest file before starting the native Shipping process.'
    $playNativeEntry = @($pointer.manifest.files | Where-Object { $_.path -match '^RiftCrownArena/Binaries/Win64/RiftCrownArena(-Win64-Shipping)?\.exe$' })
    Require ($playNativeEntry.Count -eq 1 -and [IO.Path]::GetFullPath($playReport.nativeExecutable) -eq [IO.Path]::GetFullPath((Join-Path $gameRoot $playNativeEntry[0].path)) -and $playReport.nativeSha256 -eq $playNativeEntry[0].sha256) 'Installed Play launches the exact native executable identified by the installed manifest and its SHA-256 hash.'
    Require ($playReport.width -eq 1280 -and $playReport.height -eq 720 -and (Test-Path -LiteralPath $playReport.capture -PathType Leaf) -and [IO.Path]::GetFullPath($playReport.capture).StartsWith([IO.Path]::GetFullPath($saveRoot) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) 'Installed Play produces a real 1280×720 Home capture inside its isolated save directory.'
    $playContexts = @($playReport.contexts | Where-Object { $_.build -eq 'Shipping' -and $_.processId -eq $playReport.nativeProcessId -and [IO.Path]::GetFullPath($_.saveRoot) -eq [IO.Path]::GetFullPath($saveRoot) -and [IO.Path]::GetFullPath($_.configRoot).StartsWith([IO.Path]::GetFullPath((Join-Path $saveRoot 'EngineUser')) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) })
    Require ($playReport.saveRootSource -eq 'RIFT_SAVE_ROOT environment' -and $playContexts.Count -gt 0 -and (Test-Path -LiteralPath (Join-Path $saveRoot 'ue_save.json') -PathType Leaf)) 'Installed Play forwards its environment save root through the bootstrap and isolates native profile and engine configuration writes.'
    Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Actual installed WPF Play preserves the isolated legacy save byte for byte.'
    [IO.File]::WriteAllText((Join-Path $qaRoot 'installed-launcher-play.json'), ($playReport | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    $gameCapture = Join-Path $qaRoot 'installed-native-home.png'
    $nativeExecutables = @($pointer.manifest.files | Where-Object { $_.path -match '^RiftCrownArena/Binaries/Win64/RiftCrownArena(-Win64-Shipping)?\.exe$' })
    Require ($nativeExecutables.Count -eq 1) 'Installed manifest contains the actual Win64 native game executable.'
    $nativeExecutable = Join-Path $gameRoot $nativeExecutables[0].path
    $gameArguments = @('/Game/Rift/Maps/Arena', '-RenderOffscreen', '-windowed', '-ResX=1280', '-ResY=720', ('-RiftSaveRoot="' + $saveRoot + '"'), ('-UserDir="' + (Join-Path $qaRoot 'EngineUser') + '"'), ('-RiftCapture="' + $gameCapture + '"'), '-RiftCapturePage=Home', '-RiftCaptureDelay=6', '-RiftQuitAfterCapture', '-unattended', '-nosplash', '-NoSound', '-NoVSync')
    $game = Start-Process -FilePath $nativeExecutable -ArgumentList $gameArguments -WorkingDirectory $gameRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $qaRoot 'native.stdout.log') -RedirectStandardError (Join-Path $qaRoot 'native.stderr.log')
    $runtimeModules = @()
    for ($sample = 0; $sample -lt 40 -and -not $game.HasExited; $sample++) {
        try { $game.Refresh(); $runtimeModules = @($game.Modules | Where-Object ModuleName -in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') | ForEach-Object { [ordered]@{ name = $_.ModuleName; path = $_.FileName; version = $_.FileVersionInfo.FileVersion } }) } catch { $runtimeModules = @() }
        if ($runtimeModules.Count -eq 3) { break }
        $game.WaitForExit(250) | Out-Null
    }
    if (-not $game.WaitForExit(120000)) { $game.Kill($true); $game.WaitForExit(10000) | Out-Null; throw 'Installed native game startup/capture exceeded 120 seconds.' }
    Require ($game.ExitCode -eq 0 -and (Test-Path -LiteralPath $gameCapture -PathType Leaf)) 'Actual installed native game renders its Home menu offscreen and exits cleanly.'
    Require ($runtimeModules.Count -eq 3) 'Actual Shipping process exposes all three required Visual C++ runtime modules.'
    foreach ($module in $runtimeModules) {
        $expectedModulePath = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($nativeExecutable)) $module.name))
        Require ([IO.Path]::GetFullPath($module.path) -eq $expectedModulePath) ("Shipping loads $($module.name) from its packaged application directory, independently of the host Visual C++ installation.")
        $runtimeEntry = @($pointer.manifest.files | Where-Object { $_.path -eq [IO.Path]::GetRelativePath($gameRoot,$module.path).Replace('\','/') })
        Require ($runtimeEntry.Count -eq 1 -and (Get-FileHash -LiteralPath $module.path -Algorithm SHA256).Hash -eq $runtimeEntry[0].sha256) ("Loaded $($module.name) matches the actual release manifest hash.")
    }
    [IO.File]::WriteAllText((Join-Path $qaRoot 'native-runtime-modules.json'),($runtimeModules | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    $captureBytes = [IO.File]::ReadAllBytes($gameCapture)
    $captureWidth = if ($captureBytes.Length -ge 24) { ([int]$captureBytes[16] -shl 24) -bor ([int]$captureBytes[17] -shl 16) -bor ([int]$captureBytes[18] -shl 8) -bor [int]$captureBytes[19] } else { 0 }
    $captureHeight = if ($captureBytes.Length -ge 24) { ([int]$captureBytes[20] -shl 24) -bor ([int]$captureBytes[21] -shl 16) -bor ([int]$captureBytes[22] -shl 8) -bor [int]$captureBytes[23] } else { 0 }
    Require ($captureBytes[0] -eq 137 -and $captureBytes[1] -eq 80 -and $captureBytes[2] -eq 78 -and $captureBytes[3] -eq 71 -and $captureWidth -eq 1280 -and $captureHeight -eq 720) 'Installed game capture is an actual 1280×720 PNG.'
    $gameLogs = @(Get-ChildItem -LiteralPath (Join-Path $saveRoot 'Logs') -Filter 'RiftGame-*.log' -File)
    Require ($gameLogs.Count -gt 0) 'Actual Shipping runtime produces its independent structured game diagnostics.'
    $gameEntries = @(foreach ($logFile in $gameLogs) { foreach ($line in [IO.File]::ReadLines($logFile.FullName)) { $line | ConvertFrom-Json } })
    $shippingContexts = @($gameEntries | Where-Object { $_.event -eq 'system_context' -and $_.build -eq 'Shipping' -and [IO.Path]::GetFullPath($_.saveRoot) -eq [IO.Path]::GetFullPath($saveRoot) })
    Require ($shippingContexts.Count -gt 0) 'Native diagnostics confirm a real Shipping build using the isolated save root.'
    $gameErrors = @($gameEntries | Where-Object level -in @('Error', 'Fatal') | ForEach-Object message)
    Require (@($gameErrors).Count -eq 0) 'Installed Shipping startup diagnostics contain no game Error or Fatal records.'
    Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Actual installed native startup preserves the isolated legacy profile byte for byte.'

    $repairTarget = Join-Path $gameRoot 'RiftCrownArena.exe'
    Remove-Item -LiteralPath $repairTarget
    $repairExit = RunMsi @('/fa', $registeredProductCode, '/qn', '/norestart', ('INSTALLFOLDER="' + $installRoot + '"'), '/L*v', ('"' + (Join-Path $qaRoot 'repair.log') + '"'))
    RequireMaintenanceDirectory (Join-Path $qaRoot 'repair.log')
    $expectedExecutable = $pointer.manifest.files | Where-Object path -eq 'RiftCrownArena.exe'
    Require ($repairExit -eq 0 -and (Test-Path -LiteralPath $repairTarget -PathType Leaf) -and (Get-FileHash -LiteralPath $repairTarget -Algorithm SHA256).Hash -eq $expectedExecutable.sha256) 'Windows Installer repair restores an actually missing native executable with its exact packaged hash.'
    Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Windows Installer repair preserves the separate isolated profile.'

    # These are managed-cache fixtures, never substituted for an actual game package.
    foreach ($relative in @('Game/releases/qa-obsolete-release/obsolete.bin', 'Game/staging/qa-interrupted-update/archive.part', 'Game/installed.previous.json', 'Game/update-journal.json', 'Game/update.lock', 'Game/installed.json.corrupt-qa')) {
        $fixture = Join-Path $installRoot $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($fixture)) -Force | Out-Null
        [IO.File]::WriteAllText($fixture, 'isolated updater cleanup fixture')
    }
    $uninstallExit = RunMsi @('/x', $registeredProductCode, '/qn', '/norestart', ('INSTALLFOLDER="' + $installRoot + '"'), '/L*v', ('"' + (Join-Path $qaRoot 'uninstall.log') + '"'))
    $registered = $false
    RequireMaintenanceDirectory (Join-Path $qaRoot 'uninstall.log')
    Require ($uninstallExit -eq 0) 'Unattended uninstall completed without a reboot request.'
    Require (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Game'))) 'Uninstall removes game files, obsolete releases, interrupted staging and managed update metadata.'
    Require (-not (Test-Path -LiteralPath $launcher)) 'Uninstall removes the launcher executable.'
    Require (-not (Test-Path -LiteralPath $startMenu)) 'Uninstall removes the installed Start menu shortcut folder.'
    Require (-not (Test-Path -LiteralPath $desktopShortcut)) 'Uninstall removes the selected optional desktop shortcut.'
    Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Uninstall preserves the separate isolated profile byte for byte.'
}
catch { $primaryFailure = $_.Exception.ToString() }
finally {
    if ($registered) {
        try { RunMsi @('/x', $registeredProductCode, '/qn', '/norestart', ('INSTALLFOLDER="' + $installRoot + '"'), '/L*v', ('"' + (Join-Path $qaRoot 'cleanup.log') + '"')) | Out-Null }
        catch { $primaryFailure += "`nIsolated MSI cleanup also failed: " + $_.Exception.Message }
    }
    $report = [ordered]@{ passed = ($null -eq $primaryFailure); utc = [DateTime]::UtcNow.ToString('o'); msi = $msiPath; msiSha256 = (Get-FileHash -LiteralPath $msiPath -Algorithm SHA256).Hash.ToLowerInvariant(); version = $version; productCode = $productCode; upgradeMsi = $UpgradeMsi; upgradeMsiSha256 = $(if($UpgradeMsi){(Get-FileHash -LiteralPath $upgradeMsiPath -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null}); upgradeProductCode = $upgradeProductCode; installedVersion = $pointer.version; installedArchiveSha256 = $pointer.manifest.sha256; installedGameFiles = $pointer.manifest.files.Count; installedLauncherSha256 = $validatedLauncherHash; isolatedRoot = $qaRoot; checks = $checks.ToArray(); checkCount = $checks.Count; failure = $primaryFailure }
    $reportJson = $report | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText((Join-Path $qaRoot 'report.json'), $reportJson, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $qaParent 'installer-tests.json'), $reportJson, [Text.UTF8Encoding]::new($false))
}
if ($primaryFailure) { throw $primaryFailure }
Write-Output "Actual installer QA passed $($checks.Count) checks. Evidence: $qaRoot"
