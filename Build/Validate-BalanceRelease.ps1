param(
    [string]$Version = '',
    [string]$Manifest = '',
    [string]$Baseline = '',
    [string]$Resolution = '',
    [string]$GeneratedDefinitions = '',
    [string]$GeneratedArenaRules = '',
    [string]$Notes = '',
    [string]$CppNotes = '',
    [string]$DefaultGame = '',
    [string]$PythonExecutable = '',
    [switch]$PassThru
)
$ErrorActionPreference = 'Stop'
$riftReleaseRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Get-RiftReleaseInput([string]$Path, [string]$Default) {
    if (!$Path) { $Path = $Default }
    if (![IO.Path]::IsPathRooted($Path)) { $Path = Join-Path $riftReleaseRepo $Path }
    [IO.Path]::GetFullPath($Path)
}
$Manifest = Get-RiftReleaseInput $Manifest 'Assets/Data/card_manifest.json'
if(!$Baseline) {
    $riftReleaseManifestBaseline = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json -Depth 100
    $Baseline = if($riftReleaseManifestBaseline.baseline){[string]$riftReleaseManifestBaseline.baseline}else{'Assets/Data/Baselines/1.5.0.json'}
}
$Baseline = Get-RiftReleaseInput $Baseline 'Assets/Data/Baselines/1.5.0.json'
$Resolution = Get-RiftReleaseInput $Resolution 'Assets/Data/source_resolution.json'
$GeneratedDefinitions = Get-RiftReleaseInput $GeneratedDefinitions 'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCardManifest.generated.inc'
if(!$GeneratedArenaRules) {
    $riftReleaseDefaultDefinitions = Get-RiftReleaseInput '' 'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation/RiftCardManifest.generated.inc'
    $GeneratedArenaRules = if($GeneratedDefinitions -eq $riftReleaseDefaultDefinitions){
        'Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftArenaRules.generated.inc'
    }else{Join-Path ([IO.Path]::GetDirectoryName($GeneratedDefinitions)) 'RiftArenaRules.generated.inc'}
}
$GeneratedArenaRules = Get-RiftReleaseInput $GeneratedArenaRules 'Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Simulation/RiftArenaRules.generated.inc'
$Notes = Get-RiftReleaseInput $Notes 'Assets/Data/patch_notes.json'
$CppNotes = Get-RiftReleaseInput $CppNotes 'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/RiftPatchNotes.generated.inc'
$DefaultGame = Get-RiftReleaseInput $DefaultGame 'Unreal/RiftCrownArena/Config/DefaultGame.ini'
foreach ($riftReleaseInput in @($Manifest,$Baseline,$Resolution,$DefaultGame)) {
    if (!(Test-Path -LiteralPath $riftReleaseInput -PathType Leaf)) { throw "Release validation input is missing: $riftReleaseInput" }
}
$riftReleaseInitialPins = @(@($Manifest,$Baseline,$Resolution,$DefaultGame) | Sort-Object -Unique | ForEach-Object {
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length}
})
$riftReleaseManifest = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json -Depth 100
$riftReleaseResolution = Get-Content -LiteralPath $Resolution -Raw | ConvertFrom-Json -Depth 100
if ($riftReleaseManifest.status -cne 'approved' -or $riftReleaseResolution.status -cne 'approved' -or
    $riftReleaseResolution.unresolved -isnot [bool] -or $riftReleaseResolution.unresolved -ne $false) {
    throw 'Release blocked: the balance manifest and source resolution must both be approved with unresolved=false.'
}
$riftReleaseProjectVersions = @()
$riftReleaseSection = ''
foreach ($riftReleaseLine in Get-Content -LiteralPath $DefaultGame) {
    if ($riftReleaseLine -match '^\s*\[([^\]]+)\]\s*$') { $riftReleaseSection = $Matches[1]; continue }
    if ($riftReleaseSection -eq '/Script/EngineSettings.GeneralProjectSettings' -and $riftReleaseLine -match '^\s*ProjectVersion\s*=\s*(.*?)\s*$') {
        $riftReleaseProjectVersions += $Matches[1]
    }
}
if ($riftReleaseProjectVersions.Count -ne 1 -or $riftReleaseProjectVersions[0] -notmatch '^\d+\.\d+\.\d+(\.\d+)?$') {
    throw 'Release validation requires one numeric ProjectVersion in DefaultGame.ini GeneralProjectSettings.'
}
$riftReleaseProjectVersion = $riftReleaseProjectVersions[0]
if ($riftReleaseManifest.version -cne $riftReleaseProjectVersion -or ($Version -and $Version -cne $riftReleaseProjectVersion)) {
    throw "Release version mismatch: requested='$Version', ProjectVersion='$riftReleaseProjectVersion', manifest='$($riftReleaseManifest.version)'."
}
foreach ($riftReleaseInput in @($GeneratedDefinitions,$GeneratedArenaRules,$Notes,$CppNotes)) {
    if (!(Test-Path -LiteralPath $riftReleaseInput -PathType Leaf)) { throw "Approved release definitions/notes are missing: $riftReleaseInput" }
}
if (!$PythonExecutable) {
    $PythonExecutable = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
}
if (!(Test-Path -LiteralPath $PythonExecutable -PathType Leaf)) { throw 'Release validation Python is missing; supply -PythonExecutable with an installed Python executable.' }
$PythonExecutable = (Resolve-Path -LiteralPath $PythonExecutable).Path
$riftReleaseGenerator = Join-Path $PSScriptRoot 'generate_card_manifest.py'
$riftReleaseNotesGenerator = Join-Path $PSScriptRoot 'balance_patch_notes.py'
$riftReleasePinnedPaths = @($Manifest,$Baseline,$Resolution,$DefaultGame,$GeneratedDefinitions,$GeneratedArenaRules,$Notes,$CppNotes,$PythonExecutable,$PSCommandPath,$riftReleaseGenerator,$riftReleaseNotesGenerator)
$riftReleaseInputPins = @($riftReleasePinnedPaths | Sort-Object -Unique | ForEach-Object {
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length}
})
foreach ($riftReleaseInitialPin in $riftReleaseInitialPins) {
    $riftReleaseFrozenPin = @($riftReleaseInputPins | Where-Object path -eq $riftReleaseInitialPin.path)
    if ($riftReleaseFrozenPin.Count -ne 1 -or $riftReleaseFrozenPin[0].sha256 -ne $riftReleaseInitialPin.sha256 -or $riftReleaseFrozenPin[0].bytes -ne $riftReleaseInitialPin.bytes) {
        throw "Release input changed while approval/version data were read: $($riftReleaseInitialPin.path)"
    }
}
$riftReleaseNativeCheck = @(& $PythonExecutable -B $riftReleaseGenerator --manifest $Manifest --output $GeneratedDefinitions --arena-output $GeneratedArenaRules --check --require-approved 2>&1)
if ($LASTEXITCODE -ne 0) { throw ('Release canonical/generated-definition check failed: ' + ($riftReleaseNativeCheck -join [Environment]::NewLine)) }
$riftReleaseNotesCheck = @(& $PythonExecutable -B $riftReleaseNotesGenerator --before $Baseline --after $Manifest --resolution $Resolution --output $Notes --cpp-output $CppNotes --check 2>&1)
if ($LASTEXITCODE -ne 0) { throw ('Release exact patch-note check failed: ' + ($riftReleaseNotesCheck -join [Environment]::NewLine)) }
foreach ($riftReleasePin in $riftReleaseInputPins) {
    if (!(Test-Path -LiteralPath $riftReleasePin.path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $riftReleasePin.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $riftReleasePin.sha256 -or
        (Get-Item -LiteralPath $riftReleasePin.path).Length -ne $riftReleasePin.bytes) {
        throw "Release input changed during read-only validation: $($riftReleasePin.path)"
    }
}
$riftReleaseProof = [ordered]@{schema=1;passed=$true;operation='read-only-approved-balance-release-validation';
    version=$riftReleaseProjectVersion;balanceHash=$riftReleaseManifest.balanceHash;inputsUnchanged=$true;inputPins=$riftReleaseInputPins;
    canonicalCheck=($riftReleaseNativeCheck -join [Environment]::NewLine);notesCheck=($riftReleaseNotesCheck -join [Environment]::NewLine)}
if ($PassThru) { $riftReleaseProof } else { $riftReleaseProof | ConvertTo-Json -Depth 8 }
