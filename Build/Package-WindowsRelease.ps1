param(
    [string]$GamePackage,
    [string]$Version='1.0.0',
    [string]$Repository='nhicksenterprises2025-maker/aether-arena',
    [string]$PatchNotesFile,
    [string]$PatchNotes='Rift Crown Arena native Windows release. Exact QA and build evidence are included alongside this package.'
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $GamePackage){$GamePackage=Join-Path $repoRoot 'Artifacts/Game/Windows'}
$packageRoot=[IO.Path]::GetFullPath($GamePackage)
if(-not (Test-Path -LiteralPath (Join-Path $packageRoot 'RiftCrownArena.exe') -PathType Leaf)){throw 'Actual packaged RiftCrownArena.exe is missing. Run Unreal Shipping packaging first.'}
if($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$'){throw 'Release version must be numeric with three or four components.'}
if($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Invalid GitHub repository name.'}
if($PatchNotesFile){$PatchNotes=Get-Content -LiteralPath $PatchNotesFile -Raw}
if($PatchNotes.Length -gt 131072){throw 'Patch notes exceed launcher limits.'}
$launcherRoot=Join-Path $repoRoot 'Artifacts/Launcher'
if(-not (Test-Path -LiteralPath (Join-Path $launcherRoot 'RiftCrownLauncher.exe'))){throw 'Build the self-contained launcher first.'}
$releaseRoot=Join-Path $repoRoot 'Artifacts/Release'
$distribution=Join-Path $repoRoot 'Artifacts/Distribution'
New-Item -ItemType Directory -Path $releaseRoot,$distribution -Force | Out-Null
$files=@(Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
    if(($_.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Game package contains a file link.'}
    [ordered]@{path=[IO.Path]::GetRelativePath($packageRoot,$_.FullName).Replace('\','/');size=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
if($files.Count -eq 0){throw 'Game package is empty.'}
foreach($folder in Get-ChildItem -LiteralPath $packageRoot -Directory -Recurse){if(($folder.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Game package contains a directory link.'}}
$zipName='RiftCrownArena-Windows-x64-'+$Version+'.zip'
$zipPath=Join-Path $releaseRoot $zipName
$zipTemporary=Join-Path $releaseRoot ($zipName+'.'+[Guid]::NewGuid().ToString('N')+'.tmp')
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($packageRoot,$zipTemporary,[IO.Compression.CompressionLevel]::Fastest,$false)
Move-Item -LiteralPath $zipTemporary -Destination $zipPath -Force
$archiveHash=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest=[ordered]@{schemaVersion=1;version=$Version;platform='windows-x64';downloadUrl=('https://github.com/'+$Repository+'/releases/download/v'+$Version+'/'+$zipName);sha256=$archiveHash;size=(Get-Item -LiteralPath $zipPath).Length;executable='RiftCrownArena.exe';patchNotes=$PatchNotes;minimumLauncherVersion='1.0.0';files=$files}
$manifestPath=Join-Path $releaseRoot 'update-manifest.json'
[IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
$relativeRelease='releases/'+$Version+'-'+$archiveHash.Substring(0,12)
$distributionGame=Join-Path $distribution ('Game/'+$relativeRelease)
New-Item -ItemType Directory -Path $distributionGame,(Join-Path $distribution 'Launcher') -Force | Out-Null
Copy-Item -Path (Join-Path $packageRoot '*') -Destination $distributionGame -Recurse -Force
Copy-Item -LiteralPath (Join-Path $launcherRoot 'RiftCrownLauncher.exe') -Destination (Join-Path $distribution 'Launcher/RiftCrownLauncher.exe') -Force
$pointer=[ordered]@{schemaVersion=1;version=$Version;releasePath=$relativeRelease;executable='RiftCrownArena.exe';manifest=$manifest}
[IO.File]::WriteAllText((Join-Path $distribution 'Game/installed.json'),($pointer | ConvertTo-Json -Depth 22),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $releaseRoot 'SHA256SUMS.txt'),($archiveHash+'  '+$zipName+"`n"),[Text.UTF8Encoding]::new($false))
Write-Output "Native game archive: $zipPath"
Write-Output "Verified manifest: $manifestPath"
Write-Output "Initial launcher distribution: $distribution"
