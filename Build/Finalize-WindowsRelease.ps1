param([string]$Version='1.1.0')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$releaseRoot=Join-Path $repoRoot 'Artifacts/Release'
$qaRoot=Join-Path $repoRoot 'Artifacts/QA'
function ReadReport([string]$Name){
    $path=Join-Path $qaRoot $Name
    if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw "Measured release evidence is missing: $Name"}
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}
$manifest=Get-Content -LiteralPath (Join-Path $releaseRoot 'update-manifest.json') -Raw | ConvertFrom-Json
if($manifest.version -ne $Version -or $manifest.schemaVersion -ne 1 -or $manifest.platform -ne 'windows-x64'){throw 'The actual release manifest does not match the requested Windows version.'}
$archiveName='RiftCrownArena-Windows-x64-'+$Version+'.zip'
$archive=Join-Path $releaseRoot $archiveName
if(-not(Test-Path -LiteralPath $archive -PathType Leaf) -or (Get-Item -LiteralPath $archive).Length -ne $manifest.size -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $manifest.sha256){throw 'The actual release archive does not match its update manifest.'}
$expectedDownload='https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v'+$Version+'/'+$archiveName
if($manifest.downloadUrl -ne $expectedDownload -or $manifest.executable -ne 'RiftCrownArena.exe'){throw 'The release manifest does not point to the expected versioned GitHub game asset.'}
$core=ReadReport 'launcher-core-tests.json';$wpf=ReadReport 'launcher-published-smoke.json';$play=ReadReport 'launcher-play-smoke.json';$build=ReadReport 'installer-build.json';$installer=ReadReport 'installer-tests.json';$runtime=ReadReport 'app-local-runtime.json'
if(-not $core.passed -or $core.tests -ne 31 -or $core.failures -ne 0 -or -not $wpf.passed -or $wpf.commands -ne 10){throw 'Current published launcher compilation/control/update verification is incomplete.'}
$nativeEntries=@($manifest.files | Where-Object { $_.path -match '^RiftCrownArena/Binaries/Win64/RiftCrownArena(-Win64-Shipping)?\.exe$' })
if($nativeEntries.Count -ne 1 -or -not $play.passed -or $play.installed -ne $Version -or $play.nativeSha256 -ne $nativeEntries[0].sha256 -or $play.verifiedGameFiles -ne $manifest.files.Count -or -not $play.legacySavePreserved){throw 'The published WPF Play evidence does not match this actual native game package.'}
$msi=Join-Path $repoRoot 'Artifacts/Installer/RiftCrownArena-Setup.msi'
$msiHash=(Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash.ToLowerInvariant()
if(-not $build.passed -or $build.version -ne $Version -or $build.sha256 -ne $msiHash -or $build.gameArchiveSha256 -ne $manifest.sha256 -or $build.gameFiles -ne $manifest.files.Count){throw 'The compiled installer evidence does not match the actual release game and MSI bytes.'}
$testedMsiHash=if($installer.upgradeMsiSha256){$installer.upgradeMsiSha256}else{$installer.msiSha256}
if(-not $installer.passed -or $installer.checkCount -lt 51 -or $testedMsiHash -ne $msiHash -or $installer.installedVersion -ne $Version -or $installer.installedArchiveSha256 -ne $manifest.sha256 -or $installer.installedGameFiles -ne $manifest.files.Count -or $installer.installedLauncherSha256 -ne $build.launcherSha256){throw 'Actual installed Play/install/upgrade/repair/uninstall evidence does not match this release.'}
if(-not $runtime.passed){throw 'Matching application-local Visual C++ runtime verification did not pass.'}
$launcherSource=Join-Path $repoRoot 'Artifacts/Launcher/RiftCrownLauncher.exe'
$launcherHash=(Get-FileHash -LiteralPath $launcherSource -Algorithm SHA256).Hash.ToLowerInvariant()
if($launcherHash -ne $build.launcherSha256 -or $launcherHash -ne $wpf.launcherSha256 -or $launcherHash -ne $play.launcherSha256){throw 'The standalone launcher differs from the executable actually smoke-tested, used for published Play, packaged, or installed in this MSI.'}
$launcherRelease=Join-Path $releaseRoot 'RiftCrownLauncher.exe'
Copy-Item -LiteralPath $launcherSource -Destination $launcherRelease -Force
if((Get-FileHash -LiteralPath $launcherRelease -Algorithm SHA256).Hash.ToLowerInvariant() -ne $launcherHash){throw 'The standalone release launcher copy changed bytes.'}
$msiRelease=Join-Path $releaseRoot 'RiftCrownArena-Setup.msi'
Copy-Item -LiteralPath $msi -Destination $msiRelease -Force
if((Get-FileHash -LiteralPath $msiRelease -Algorithm SHA256).Hash.ToLowerInvariant() -ne $msiHash){throw 'The release installer copy changed bytes.'}
$manifestHash=(Get-FileHash -LiteralPath (Join-Path $releaseRoot 'update-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$checksums=$manifest.sha256+'  '+$archiveName+"`n"+$msiHash+'  RiftCrownArena-Setup.msi'+"`n"+$launcherHash+'  RiftCrownLauncher.exe'+"`n"+$manifestHash+'  update-manifest.json'+"`n"
[IO.File]::WriteAllText((Join-Path $releaseRoot 'SHA256SUMS.txt'),$checksums,[Text.UTF8Encoding]::new($false))
$report=[ordered]@{passed=$true;utc=[DateTime]::UtcNow.ToString('o');version=$Version;tag='v'+$Version;archive=$archiveName;archiveSha256=$manifest.sha256;installer='RiftCrownArena-Setup.msi';installerSha256=$msiHash;manifestSha256=$manifestHash;launcher='RiftCrownLauncher.exe';launcherSha256=$build.launcherSha256;gameFiles=$manifest.files.Count;launcherCoreChecks=$core.tests;wpfControls=$wpf.commands;installerChecks=$installer.checkCount;defaultManifestUrl='https://github.com/nhicksenterprises2025-maker/aether-arena/releases/latest/download/update-manifest.json';gameDownloadUrl=$manifest.downloadUrl;scope='Measured Windows packaging, published/installed WPF Play and MSI lifecycle verification'}
[IO.File]::WriteAllText((Join-Path $releaseRoot 'windows-release-verification.json'),($report|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output "Verified Windows release assets are ready in $releaseRoot for tag v$Version. This script does not publish to GitHub."
