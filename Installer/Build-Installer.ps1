param([string]$Version='1.0.0',[string]$Distribution)
$ErrorActionPreference='Stop'
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $Distribution){$Distribution=Join-Path $repoRoot 'Artifacts/Distribution'}
$distributionRoot=[IO.Path]::GetFullPath($Distribution)
if(-not (Test-Path -LiteralPath (Join-Path $distributionRoot 'Launcher/RiftCrownLauncher.exe')) -or -not (Test-Path -LiteralPath (Join-Path $distributionRoot 'Game/installed.json'))){throw 'Actual packaged game and launcher distribution is required before compiling an installer.'}
if($Version -notmatch '^\d+\.\d+\.\d+(\.\d+)?$'){throw 'Invalid release version.'}
$xmlNamespace='http://wixtoolset.org/schemas/v4/wxs'
$document=[xml]('<Wix xmlns="'+$xmlNamespace+'"><Fragment><DirectoryRef Id="INSTALLFOLDER"/></Fragment><Fragment><ComponentGroup Id="DistributionFiles"/></Fragment></Wix>')
$directories=@{'']=$document.DocumentElement.FirstChild.FirstChild}
$componentGroup=$document.DocumentElement.LastChild.FirstChild
function FileId([string]$prefix,[string]$relative){$bytes=[Text.Encoding]::UTF8.GetBytes($relative.ToLowerInvariant());$hash=[Security.Cryptography.SHA256]::HashData($bytes);return $prefix+([Convert]::ToHexString($hash).Substring(0,24))}
foreach($file in Get-ChildItem -LiteralPath $distributionRoot -File -Recurse | Sort-Object FullName){
    if(($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Distribution contains a link.'}
    $relative=[IO.Path]::GetRelativePath($distributionRoot,$file.FullName).Replace('\','/')
    $parent=[IO.Path]::GetDirectoryName($relative).Replace('\','/')
    $parts=if($parent){$parent.Split('/')}else{@()}
    $built=''
    foreach($part in $parts){$next=if($built){$built+'/'+$part}else{$part};if(-not $directories.ContainsKey($next)){$node=$document.CreateElement('Directory',$xmlNamespace);$node.SetAttribute('Id',(FileId 'D_' $next));$node.SetAttribute('Name',$part);$directories[$built].AppendChild($node) | Out-Null;$directories[$next]=$node};$built=$next}
    $component=$document.CreateElement('Component',$xmlNamespace);$component.SetAttribute('Id',(FileId 'C_' $relative));$component.SetAttribute('Guid','*');$component.SetAttribute('Directory',$directories[$parent].GetAttribute('Id'))
    $element=$document.CreateElement('File',$xmlNamespace);$element.SetAttribute('Id',$(if($relative -eq 'Launcher/RiftCrownLauncher.exe'){'LauncherExecutable'}else{FileId 'F_' $relative}));$element.SetAttribute('Source',$file.FullName);$element.SetAttribute('KeyPath','yes');$component.AppendChild($element) | Out-Null;$componentGroup.AppendChild($component) | Out-Null
}
$generated=Join-Path $PSScriptRoot 'GeneratedFiles.wxs'
$document.Save($generated)
$output=Join-Path $repoRoot 'Artifacts/Installer'
& dotnet build (Join-Path $PSScriptRoot 'RiftCrownArena.wixproj') --configuration Release ('-p:ReleaseVersion='+$Version) ('-p:OutputPath='+$output)
if($LASTEXITCODE -ne 0){throw 'WiX MSI compilation failed.'}
Write-Output "Compiled Windows Installer artifacts: $output"
