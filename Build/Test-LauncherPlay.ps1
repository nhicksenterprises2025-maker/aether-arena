param([string]$InstallRoot,[string]$Launcher)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $InstallRoot){$InstallRoot=Join-Path $repoRoot 'Artifacts/Distribution'}
if(-not $Launcher){$Launcher=Join-Path $repoRoot 'Artifacts/Launcher/RiftCrownLauncher.exe'}
$installation=[IO.Path]::GetFullPath($InstallRoot);$launcherPath=[IO.Path]::GetFullPath($Launcher)
if(-not (Test-Path -LiteralPath $launcherPath -PathType Leaf) -or -not (Test-Path -LiteralPath (Join-Path $installation 'Game/installed.json') -PathType Leaf)){throw 'Actual published launcher and bundled game are required.'}
$qaRoot=Join-Path $repoRoot ('Artifacts/QA/launcher-play-'+[Guid]::NewGuid().ToString('N'))
$saves=Join-Path $qaRoot 'Saves';New-Item -ItemType Directory -Path $saves -Force | Out-Null
$fixture=Join-Path $saves 'player_save.json';[IO.File]::WriteAllText($fixture,'{"schemaVersion":1,"fixture":"isolated-real-launcher-play","preserve":true}',[Text.UTF8Encoding]::new($false));$fixtureHash=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash
$process=Start-Process -FilePath $launcherPath -ArgumentList @('--self-test-play','--install-root',('"'+$installation+'"'),'--save-root',('"'+$saves+'"')) -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(180000)){$process.Kill($true);$process.WaitForExit(10000) | Out-Null;throw 'Actual launcher Play smoke exceeded 180 seconds.'}
$reportPath=Join-Path $saves 'launcher-play-self-test.json'
if(-not (Test-Path -LiteralPath $reportPath -PathType Leaf)){throw 'Actual Play smoke did not produce its measured report.'}
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if($process.ExitCode -ne 0 -or -not $report.passed){throw ('Actual launcher Play failed: '+($report | ConvertTo-Json -Depth 8))}
if($report.routedCommand -ne 'PlayButton.Click' -or $report.nativeExitCode -ne 0 -or $report.bootstrapExitCode -ne 0 -or $report.width -ne 1280 -or $report.height -ne 720 -or $report.saveRootSource -ne 'RIFT_SAVE_ROOT environment' -or $report.contexts.Count -eq 0){throw 'Actual Play evidence is incomplete.'}
$contexts=@($report.contexts | Where-Object { $_.build -eq 'Shipping' -and $_.processId -eq $report.nativeProcessId -and [IO.Path]::GetFullPath($_.saveRoot) -eq [IO.Path]::GetFullPath($saves) -and [IO.Path]::GetFullPath($_.configRoot).StartsWith([IO.Path]::GetFullPath((Join-Path $saves 'EngineUser'))+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) })
if($contexts.Count -eq 0 -or [IO.Path]::GetFullPath($report.saveRoot) -ne [IO.Path]::GetFullPath($saves)){throw 'Actual Play diagnostics do not confirm the exact isolated save and engine configuration roots.'}
if((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne $fixtureHash){throw 'Actual launcher Play modified the isolated legacy fixture.'}
$report | Add-Member -NotePropertyName legacySavePreserved -NotePropertyValue $true
[IO.File]::WriteAllText((Join-Path $repoRoot 'Artifacts/QA/launcher-play-smoke.json'),($report | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath $report.capture -Destination (Join-Path $repoRoot 'Artifacts/QA/launcher-play-home.png') -Force
Write-Output "Actual published WPF Play launched the bundled Shipping game, rendered 1280×720, used isolated environment saves, and exited cleanly. Evidence: $qaRoot"
