param([string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$launcherProject=Join-Path $repoRoot 'Launcher/RiftCrown.Launcher/RiftCrown.Launcher.csproj'
$launcherOutput=Join-Path $repoRoot 'Artifacts/Launcher'
& dotnet publish $launcherProject --configuration $Configuration --runtime win-x64 --self-contained true --output $launcherOutput
if($LASTEXITCODE -ne 0){throw 'Self-contained launcher publishing failed.'}
$qaOutput=Join-Path $repoRoot 'Artifacts/QA'
New-Item -ItemType Directory -Path $qaOutput -Force | Out-Null
& dotnet run --configuration Release --project (Join-Path $repoRoot 'Launcher/RiftCrown.Tests/RiftCrown.Tests.csproj') -- (Join-Path $qaOutput 'launcher-core-tests.json')
if($LASTEXITCODE -ne 0){throw 'Launcher integration checks failed.'}
$smokeRoot=Join-Path $repoRoot ('Artifacts/QA/launcher-smoke-'+[Guid]::NewGuid().ToString('N'))
$smokeInstall=Join-Path $smokeRoot 'Install'
$smokeSaves=Join-Path $smokeRoot 'Saves'
New-Item -ItemType Directory -Path $smokeInstall,$smokeSaves -Force | Out-Null
$smokeProcess=Start-Process -FilePath (Join-Path $launcherOutput 'RiftCrownLauncher.exe') -ArgumentList @('--self-test','--install-root',('"'+$smokeInstall+'"'),'--save-root',('"'+$smokeSaves+'"')) -WindowStyle Hidden -Wait -PassThru
if($smokeProcess.ExitCode -ne 0){throw 'Published launcher smoke check failed.'}
Copy-Item -LiteralPath (Join-Path $smokeSaves 'launcher-self-test.json') -Destination (Join-Path $qaOutput 'launcher-published-smoke.json') -Force
Copy-Item -LiteralPath (Join-Path $smokeSaves 'launcher-preview.png') -Destination (Join-Path $qaOutput 'launcher-preview.png') -Force
Write-Output "Published self-contained launcher: $launcherOutput"
