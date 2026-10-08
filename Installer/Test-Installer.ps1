param([string]$Msi)
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
$related = ComProperty $windowsInstaller 'RelatedProducts' @($upgradeCode)
try {
    if ((ComProperty $related 'Count') -gt 0) { throw 'Installer QA requires no existing Rift Crown Arena MSI installation, so an existing game is never upgraded or removed by this test.' }
}
finally { [Runtime.InteropServices.Marshal]::FinalReleaseComObject($related) | Out-Null }
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($database) | Out-Null
[Runtime.InteropServices.Marshal]::FinalReleaseComObject($windowsInstaller) | Out-Null
$startMenu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Rift Crown Arena'
if (Test-Path -LiteralPath $startMenu) { throw 'Installer QA will not overwrite an existing Rift Crown Arena Start menu folder.' }

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

try {
    $installExit = RunMsi @('/i', ('"' + $msiPath + '"'), '/qn', '/norestart', ('INSTALLFOLDER="' + $installRoot + '"'), 'ADDLOCAL=CoreGame', 'WIXUI_EXITDIALOGOPTIONALCHECKBOX=0', '/L*v', ('"' + (Join-Path $qaRoot 'install.log') + '"'))
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
    if (-not $smoke.WaitForExit(90000)) { $smoke.Kill(); throw 'Installed launcher smoke exceeded 90 seconds.' }
    Require ($smoke.ExitCode -eq 0) 'Installed launcher completed its actual WPF smoke check.'
    $smokeReport = Get-Content -LiteralPath (Join-Path $saveRoot 'launcher-self-test.json') -Raw | ConvertFrom-Json
    Require ($smokeReport.passed -and $smokeReport.installed -eq $pointer.version -and $smokeReport.commands -eq 10) 'Installed launcher discovers the bundled game and all ten connected command controls.'
    Require (Test-Path -LiteralPath (Join-Path $startMenu 'Rift Crown Arena.lnk') -PathType Leaf) 'Installer creates the Start menu launch shortcut.'
    Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Installation and launcher smoke preserve the isolated legacy save byte for byte.'

    # These are managed-cache fixtures, never substituted for an actual game package.
    foreach ($relative in @('Game/releases/qa-obsolete-release/obsolete.bin', 'Game/staging/qa-interrupted-update/archive.part', 'Game/installed.previous.json', 'Game/update-journal.json', 'Game/update.lock', 'Game/installed.json.corrupt-qa')) {
        $fixture = Join-Path $installRoot $relative
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($fixture)) -Force | Out-Null
        [IO.File]::WriteAllText($fixture, 'isolated updater cleanup fixture')
    }
    $uninstallExit = RunMsi @('/x', $productCode, '/qn', '/norestart', '/L*v', ('"' + (Join-Path $qaRoot 'uninstall.log') + '"'))
    $registered = $false
    Require ($uninstallExit -eq 0) 'Unattended uninstall completed without a reboot request.'
    Require (-not (Test-Path -LiteralPath (Join-Path $installRoot 'Game'))) 'Uninstall removes game files, obsolete releases, interrupted staging and managed update metadata.'
    Require (-not (Test-Path -LiteralPath $launcher)) 'Uninstall removes the launcher executable.'
    Require (-not (Test-Path -LiteralPath $startMenu)) 'Uninstall removes the installed Start menu shortcut folder.'
    Require ((Get-FileHash -LiteralPath $saveFixture -Algorithm SHA256).Hash -eq $saveHash) 'Uninstall preserves the separate isolated profile byte for byte.'
}
catch { $primaryFailure = $_.Exception.ToString() }
finally {
    if ($registered) {
        try { RunMsi @('/x', $productCode, '/qn', '/norestart', '/L*v', ('"' + (Join-Path $qaRoot 'cleanup.log') + '"')) | Out-Null }
        catch { $primaryFailure += "`nIsolated MSI cleanup also failed: " + $_.Exception.Message }
    }
    $report = [ordered]@{ passed = ($null -eq $primaryFailure); utc = [DateTime]::UtcNow.ToString('o'); msi = $msiPath; msiSha256 = (Get-FileHash -LiteralPath $msiPath -Algorithm SHA256).Hash.ToLowerInvariant(); version = $version; productCode = $productCode; isolatedRoot = $qaRoot; checks = $checks.ToArray(); checkCount = $checks.Count; failure = $primaryFailure }
    $reportJson = $report | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText((Join-Path $qaRoot 'report.json'), $reportJson, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $qaParent 'installer-tests.json'), $reportJson, [Text.UTF8Encoding]::new($false))
}
if ($primaryFailure) { throw $primaryFailure }
Write-Output "Actual installer QA passed $($checks.Count) checks. Evidence: $qaRoot"
