param(
    [string]$GamePackage,
    [string]$Version='1.0.0',
    [string]$Repository='nhicksenterprises2025-maker/aether-arena',
    [string]$ToolchainRoot,
    [string]$BuildLog,
    [string]$PatchNotesFile,
    [string]$PatchNotes='Rift Crown Arena native Windows release. Exact QA and build evidence are included alongside this package.'
)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $GamePackage){$GamePackage=Join-Path $repoRoot 'Artifacts/Game/Windows'}
$packageRoot=[IO.Path]::GetFullPath($GamePackage)
if(-not (Test-Path -LiteralPath (Join-Path $packageRoot 'RiftCrownArena.exe') -PathType Leaf)){throw 'Actual packaged RiftCrownArena.exe is missing. Run Unreal Shipping packaging first.'}
& (Join-Path $PSScriptRoot 'Stage-AppLocalRuntime.ps1') -GamePackage $packageRoot -ToolchainRoot $ToolchainRoot -BuildLog $BuildLog
if($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$'){throw 'Release version must be numeric with three or four components.'}
if($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$'){throw 'Invalid GitHub repository name.'}
if($PatchNotesFile){$PatchNotes=Get-Content -LiteralPath $PatchNotesFile -Raw}
if($PatchNotes.Length -gt 131072){throw 'Patch notes exceed launcher limits.'}
$launcherRoot=Join-Path $repoRoot 'Artifacts/Launcher'
if(-not (Test-Path -LiteralPath (Join-Path $launcherRoot 'RiftCrownLauncher.exe'))){throw 'Build the self-contained launcher first.'}
$releaseRoot=Join-Path $repoRoot 'Artifacts/Release'
$distribution=Join-Path $repoRoot 'Artifacts/Distribution'
$distributionStage=Join-Path $repoRoot ('Artifacts/Distribution-stage-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $releaseRoot,$distributionStage -Force | Out-Null
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
# Verify the bytes actually archived, rather than assume the source stayed unchanged while copying.
$inventory=@{};foreach($file in $files){$inventory[$file.path]=$file}
$zip=[IO.Compression.ZipFile]::OpenRead($zipTemporary)
try {
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($entry in $zip.Entries){
        if($entry.FullName.EndsWith('/')){continue}
        if(-not $seen.Add($entry.FullName) -or -not $inventory.ContainsKey($entry.FullName)){throw 'Archived package inventory differs from the source manifest.'}
        $expected=$inventory[$entry.FullName]
        $entryStream=$entry.Open()
        try {$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($entryStream)).ToLowerInvariant()}
        finally {$entryStream.Dispose()}
        if($entry.Length -ne $expected.size -or $digest -ne $expected.sha256){throw "Package changed while archiving: $($entry.FullName)"}
    }
    if($seen.Count -ne $files.Count){throw 'The release archive omits source manifest files.'}
}
finally {$zip.Dispose()}
Move-Item -LiteralPath $zipTemporary -Destination $zipPath -Force
$archiveHash=(Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest=[ordered]@{schemaVersion=1;version=$Version;platform='windows-x64';downloadUrl=('https://github.com/'+$Repository+'/releases/download/v'+$Version+'/'+$zipName);sha256=$archiveHash;size=(Get-Item -LiteralPath $zipPath).Length;executable='RiftCrownArena.exe';patchNotes=$PatchNotes;minimumLauncherVersion='1.0.0';files=$files}
$manifestPath=Join-Path $releaseRoot 'update-manifest.json'
[IO.File]::WriteAllText($manifestPath,($manifest | ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
$relativeRelease='releases/'+$Version+'-'+$archiveHash.Substring(0,12)
$distributionGame=Join-Path $distributionStage ('Game/'+$relativeRelease)
New-Item -ItemType Directory -Path $distributionGame,(Join-Path $distributionStage 'Launcher') -Force | Out-Null
foreach($child in Get-ChildItem -LiteralPath $packageRoot){Copy-Item -LiteralPath $child.FullName -Destination $distributionGame -Recurse}
foreach($file in $files){$copied=Join-Path $distributionGame $file.path;if((Get-Item -LiteralPath $copied).Length -ne $file.size -or (Get-FileHash -LiteralPath $copied -Algorithm SHA256).Hash -ne $file.sha256){throw "Bundled package changed while copying: $($file.path)"}}
Copy-Item -LiteralPath (Join-Path $launcherRoot 'RiftCrownLauncher.exe') -Destination (Join-Path $distributionStage 'Launcher/RiftCrownLauncher.exe')
$pointer=[ordered]@{schemaVersion=1;version=$Version;releasePath=$relativeRelease;executable='RiftCrownArena.exe';manifest=$manifest}
[IO.File]::WriteAllText((Join-Path $distributionStage 'Game/installed.json'),($pointer | ConvertTo-Json -Depth 22),[Text.UTF8Encoding]::new($false))
if(Test-Path -LiteralPath $distribution){Move-Item -LiteralPath $distribution -Destination ($distribution+'.previous-'+[DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff'))}
Move-Item -LiteralPath $distributionStage -Destination $distribution
[IO.File]::WriteAllText((Join-Path $releaseRoot 'SHA256SUMS.txt'),($archiveHash+'  '+$zipName+"`n"),[Text.UTF8Encoding]::new($false))
Write-Output "Native game archive: $zipPath"
Write-Output "Verified manifest: $manifestPath"
Write-Output "Initial launcher distribution: $distribution"
