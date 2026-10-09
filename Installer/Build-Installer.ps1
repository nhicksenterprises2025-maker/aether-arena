param([string]$Version='1.1.0',[string]$Distribution)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $Distribution){$Distribution=Join-Path $repoRoot 'Artifacts/Distribution'}
$distributionRoot=[IO.Path]::GetFullPath($Distribution)
if(-not (Test-Path -LiteralPath (Join-Path $distributionRoot 'Launcher/RiftCrownLauncher.exe')) -or -not (Test-Path -LiteralPath (Join-Path $distributionRoot 'Game/installed.json'))){throw 'Actual packaged game and launcher distribution is required before compiling an installer.'}
if($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$'){throw 'Invalid release version.'}
$pointer=Get-Content -LiteralPath (Join-Path $distributionRoot 'Game/installed.json') -Raw | ConvertFrom-Json
$pointerHash=(Get-FileHash -LiteralPath (Join-Path $distributionRoot 'Game/installed.json') -Algorithm SHA256).Hash.ToLowerInvariant()
$launcherHash=(Get-FileHash -LiteralPath (Join-Path $distributionRoot 'Launcher/RiftCrownLauncher.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
if($pointer.schemaVersion -ne 1 -or $pointer.version -ne $Version -or $pointer.manifest.version -ne $Version -or $pointer.executable -ne 'RiftCrownArena.exe' -or $pointer.manifest.files.Count -eq 0){throw 'The actual bundled native release pointer does not match this installer version.'}
$gameParent=[IO.Path]::GetFullPath((Join-Path $distributionRoot 'Game'))
$gameRelease=[IO.Path]::GetFullPath((Join-Path $gameParent $pointer.releasePath))
if(-not $gameRelease.StartsWith($gameParent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath (Join-Path $gameRelease 'RiftCrownArena.exe') -PathType Leaf)){throw 'The bundled native release is missing or outside the managed Game subtree.'}
foreach($file in $pointer.manifest.files){$installedFile=[IO.Path]::GetFullPath((Join-Path $gameRelease $file.path));if(-not $installedFile.StartsWith($gameRelease+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $installedFile -PathType Leaf) -or (Get-Item -LiteralPath $installedFile).Length -ne $file.size -or (Get-FileHash -LiteralPath $installedFile -Algorithm SHA256).Hash -ne $file.sha256){throw "Actual installer payload fails release verification: $($file.path)"}}
foreach($folder in Get-ChildItem -LiteralPath $distributionRoot -Directory -Recurse){if(($folder.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Distribution contains a directory link.'}}
& (Join-Path $PSScriptRoot 'Generate-BrandAssets.ps1')
$xmlNamespace='http://wixtoolset.org/schemas/v4/wxs'
$document=[xml]('<Wix xmlns="'+$xmlNamespace+'"><Fragment><DirectoryRef Id="INSTALLFOLDER"/></Fragment><Fragment><ComponentGroup Id="DistributionFiles"/></Fragment></Wix>')
$directories=@{''=$document.DocumentElement.FirstChild.FirstChild}
$componentGroup=$document.DocumentElement.LastChild.FirstChild
function FileId([string]$prefix,[string]$relative){$bytes=[Text.Encoding]::UTF8.GetBytes($relative.ToLowerInvariant());$hash=[Security.Cryptography.SHA256]::HashData($bytes);return $prefix+([Convert]::ToHexString($hash).Substring(0,24))}
function ComponentGuid([string]$relative){$namespaceBytes=[Convert]::FromHexString('B7D18D6523D54F298F98C066E091C6E8');$nameBytes=[Text.Encoding]::UTF8.GetBytes('rift-crown-arena/component/registry-v2/'+$relative.ToLowerInvariant());$digest=[Security.Cryptography.SHA1]::HashData([byte[]]($namespaceBytes+$nameBytes));$digest[6]=($digest[6] -band 15) -bor 80;$digest[8]=($digest[8] -band 63) -bor 128;return [Guid]::ParseExact([Convert]::ToHexString($digest[0..15]),'N').ToString('B')}
foreach($file in Get-ChildItem -LiteralPath $distributionRoot -File -Recurse | Sort-Object FullName){
    if(($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Distribution contains a link.'}
    $relative=[IO.Path]::GetRelativePath($distributionRoot,$file.FullName).Replace('\','/')
    $parent=[IO.Path]::GetDirectoryName($relative).Replace('\','/')
    $parts=if($parent){$parent.Split('/')}else{@()}
    $built=''
    foreach($part in $parts){$next=if($built){$built+'/'+$part}else{$part};if(-not $directories.ContainsKey($next)){$node=$document.CreateElement('Directory',$xmlNamespace);$node.SetAttribute('Id',(FileId 'D_' $next));$node.SetAttribute('Name',$part);$directories[$built].AppendChild($node) | Out-Null;$directories[$next]=$node};$built=$next}
    $component=$document.CreateElement('Component',$xmlNamespace);$component.SetAttribute('Id',(FileId 'C_' $relative));$component.SetAttribute('Guid',(ComponentGuid $relative));$component.SetAttribute('Directory',$directories[$parent].GetAttribute('Id'))
    $element=$document.CreateElement('File',$xmlNamespace);$element.SetAttribute('Id',$(if($relative -eq 'Launcher/RiftCrownLauncher.exe'){'LauncherExecutable'}else{FileId 'F_' $relative}));$element.SetAttribute('Source',$file.FullName);if($relative.EndsWith('/Engine/Content/SlateDebug/Fonts/LastResort.ttf',[StringComparison]::OrdinalIgnoreCase)){$element.SetAttribute('DefaultLanguage','0')};$component.AppendChild($element) | Out-Null
    $registration=$document.CreateElement('RegistryValue',$xmlNamespace);$registration.SetAttribute('Root','HKCU');$registration.SetAttribute('Key','Software\RiftCrownArena\Installer\Components');$registration.SetAttribute('Name',$component.GetAttribute('Id'));$registration.SetAttribute('Type','string');$registration.SetAttribute('Value',$relative);$registration.SetAttribute('KeyPath','yes');$component.AppendChild($registration) | Out-Null;$componentGroup.AppendChild($component) | Out-Null
}
$cleanup=$document.CreateElement('Component',$xmlNamespace);$cleanup.SetAttribute('Id','DistributionDirectoryCleanup');$cleanup.SetAttribute('Guid','*');$cleanup.SetAttribute('Directory','INSTALLFOLDER')
$cleanupKey=$document.CreateElement('RegistryValue',$xmlNamespace);$cleanupKey.SetAttribute('Root','HKCU');$cleanupKey.SetAttribute('Key','Software\RiftCrownArena\Installer');$cleanupKey.SetAttribute('Name','DirectoryCleanup');$cleanupKey.SetAttribute('Type','integer');$cleanupKey.SetAttribute('Value','1');$cleanupKey.SetAttribute('KeyPath','yes');$cleanup.AppendChild($cleanupKey) | Out-Null
foreach($relativeDirectory in $directories.Keys | Sort-Object){if(-not $relativeDirectory){continue};$remove=$document.CreateElement('RemoveFolder',$xmlNamespace);$remove.SetAttribute('Id',(FileId 'R_' $relativeDirectory));$remove.SetAttribute('Directory',$directories[$relativeDirectory].GetAttribute('Id'));$remove.SetAttribute('On','uninstall');$cleanup.AppendChild($remove) | Out-Null}
$componentGroup.AppendChild($cleanup) | Out-Null
$generated=Join-Path $PSScriptRoot 'GeneratedFiles.wxs'
$document.Save($generated)
$output=Join-Path $repoRoot 'Artifacts/Installer'
$compileTimer=[Diagnostics.Stopwatch]::StartNew()
& dotnet build (Join-Path $PSScriptRoot 'RiftCrownArena.wixproj') --configuration Release ('-p:ReleaseVersion='+$Version) ('-p:OutputPath='+$output)
if($LASTEXITCODE -ne 0){throw 'WiX MSI compilation failed.'}
$compileTimer.Stop()
if((Get-FileHash -LiteralPath (Join-Path $distributionRoot 'Game/installed.json') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pointerHash -or (Get-FileHash -LiteralPath (Join-Path $distributionRoot 'Launcher/RiftCrownLauncher.exe') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $launcherHash){throw 'The distribution changed during installer compilation. Rebuild from a stable packaged release.'}
foreach($file in $pointer.manifest.files){$sourceFile=Join-Path $gameRelease $file.path;if(-not (Test-Path -LiteralPath $sourceFile -PathType Leaf) -or (Get-Item -LiteralPath $sourceFile).Length -ne $file.size -or (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash -ne $file.sha256){throw "The native payload changed during installer compilation: $($file.path)"}}
$msi=Join-Path $output 'RiftCrownArena-Setup.msi'
if(-not (Test-Path -LiteralPath $msi -PathType Leaf)){throw 'WiX reported success without producing the actual installer.'}
$msiHash=(Get-FileHash -LiteralPath $msi -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS.txt'),($msiHash+'  RiftCrownArena-Setup.msi'+"`n"),[Text.UTF8Encoding]::new($false))
$qaRoot=Join-Path $repoRoot 'Artifacts/QA';New-Item -ItemType Directory -Path $qaRoot -Force | Out-Null
$report=[ordered]@{passed=$true;utc=[DateTime]::UtcNow.ToString('o');version=$Version;msi=$msi;sha256=$msiHash;size=(Get-Item -LiteralPath $msi).Length;compileSeconds=$compileTimer.Elapsed.TotalSeconds;gameArchiveSha256=$pointer.manifest.sha256;gameFiles=$pointer.manifest.files.Count;launcherSha256=$launcherHash;pointerSha256=$pointerHash;databaseValidation='Standard ICE validation passed with warnings treated as errors';documentedExceptions=@('ICE61: intentional same-version major upgrades','ICE91: fixed per-user installation scope','WiX1101: neutral language metadata for private LastResort.ttf')}
[IO.File]::WriteAllText((Join-Path $qaRoot 'installer-build.json'),($report | ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output "Compiled Windows Installer artifacts: $output"
