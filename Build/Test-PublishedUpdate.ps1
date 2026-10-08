param([string]$Launcher,[string]$ExpectedManifest)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $Launcher){$Launcher=Join-Path $repoRoot 'Artifacts/Release/RiftCrownLauncher.exe'}
if(-not $ExpectedManifest){$ExpectedManifest=Join-Path $repoRoot 'Artifacts/Release/update-manifest.json'}
$launcherPath=[IO.Path]::GetFullPath($Launcher);$manifestPath=[IO.Path]::GetFullPath($ExpectedManifest)
if(-not(Test-Path -LiteralPath $launcherPath -PathType Leaf) -or -not(Test-Path -LiteralPath $manifestPath -PathType Leaf)){throw 'The actual final standalone launcher and expected manifest are required.'}
$expected=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$qaRoot=Join-Path $repoRoot ('Artifacts/QA/published-update-'+[Guid]::NewGuid().ToString('N'))
$installation=Join-Path $qaRoot 'Install';$saves=Join-Path $qaRoot 'Saves'
New-Item -ItemType Directory -Path $installation,$saves -Force | Out-Null
$fixture=Join-Path $saves 'player_save.json';[IO.File]::WriteAllText($fixture,'{"schemaVersion":1,"fixture":"isolated-public-update-check","preserve":true}',[Text.UTF8Encoding]::new($false));$fixtureHash=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash
$process=Start-Process -FilePath $launcherPath -ArgumentList @('--self-test-update','--install-root',('"'+$installation+'"'),'--save-root',('"'+$saves+'"'),'--expected-manifest',('"'+$manifestPath+'"')) -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(60000)){$process.Kill($true);$process.WaitForExit(10000) | Out-Null;throw 'Actual public launcher update check exceeded 60 seconds.'}
$reportPath=Join-Path $saves 'launcher-update-self-test.json'
if(-not(Test-Path -LiteralPath $reportPath -PathType Leaf)){throw 'Actual public update check produced no measured report.'}
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if($process.ExitCode -ne 0 -or -not $report.passed){throw ('Actual public update check failed: '+($report | ConvertTo-Json -Depth 8))}
if($report.routedCommand -ne 'CheckButton.Click' -or $report.endpoint -ne 'https://github.com/nhicksenterprises2025-maker/aether-arena/releases/latest/download/update-manifest.json' -or -not $report.expectedFinalManifestMatched -or $report.releaseVersion -ne $expected.version -or $report.archiveSha256 -ne $expected.sha256 -or $report.archiveSize -ne $expected.size -or $report.gameFiles -ne $expected.files.Count -or -not $report.patchNotesDisplayed -or -not $report.installAvailable -or -not $report.gameInstallationUnchanged -or $report.gameDownloaded -or $report.gameInstalled){throw 'Actual public update evidence does not match the expected final release or read-only check.'}
if($report.launcherSha256 -ne (Get-FileHash -LiteralPath $launcherPath -Algorithm SHA256).Hash){throw 'The public update report does not identify the exact final standalone launcher executable.'}
if((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne $fixtureHash){throw 'The public update check changed the isolated legacy save fixture.'}
$report | Add-Member -NotePropertyName legacySavePreserved -NotePropertyValue $true
[IO.File]::WriteAllText((Join-Path $repoRoot 'Artifacts/QA/published-update-check.json'),($report | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output "Actual standalone WPF Check for Updates fetched the default public HTTPS latest manifest, matched the final inventory and notes, and preserved game/save data. Evidence: $qaRoot"
