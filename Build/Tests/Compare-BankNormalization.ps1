param(
    [ValidateRange(1,1000000)][int]$FirstOrdinal = 626,
    [ValidateRange(1,1000000)][int]$LastOrdinal = 650
)
$ErrorActionPreference = 'Stop'
if ($LastOrdinal -lt $FirstOrdinal) { throw 'LastOrdinal must be at least FirstOrdinal.' }
$riftWitnessRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$riftWitnessPublic = Join-Path $riftWitnessRepo 'Unreal/RiftCrownArena/Source/RiftCrownArena/Public'
$riftWitnessPrivate = Join-Path $riftWitnessRepo 'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation'
$riftWitnessOutput = Join-Path $riftWitnessRepo ('Build/NativeTests/BankWitness-' + [Guid]::NewGuid().ToString('N'))
$riftWitnessVSWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$riftWitnessVS = & $riftWitnessVSWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$riftWitnessVS) { throw 'Visual Studio C++ x64 tools are required.' }
$riftWitnessVars = Join-Path $riftWitnessVS 'VC/Auxiliary/Build/vcvars64.bat'
$riftWitnessCurrent = [IO.File]::ReadAllText((Join-Path $riftWitnessPrivate 'RiftSimulation.cpp'))
$riftWitnessLine = 'state_.aether[t] = std::max(0.0, state_.aether[t] - c->cost);'
if (($riftWitnessCurrent.Split($riftWitnessLine).Count - 1) -ne 1) {
    throw 'Expected exactly one final post-payment normalization; production source was not modified.'
}
foreach ($riftWitnessMode in @('Before','Final')) {
    $riftWitnessCase = Join-Path $riftWitnessOutput $riftWitnessMode
    New-Item -ItemType Directory -Path $riftWitnessCase -Force | Out-Null
    $riftWitnessSource = if ($riftWitnessMode -eq 'Before') {
        $riftWitnessCurrent.Replace($riftWitnessLine,'state_.aether[t] -= c->cost;')
    } else { $riftWitnessCurrent }
    $riftWitnessMain = Join-Path $riftWitnessCase 'RiftSimulation.cpp'
    [IO.File]::WriteAllText($riftWitnessMain,$riftWitnessSource,[Text.UTF8Encoding]::new($false))
    $riftWitnessSources = @($riftWitnessMain) + @('RiftCombat.cpp','RiftPathfinding.cpp','RiftDeckAnalysis.cpp','RiftAI.cpp' | ForEach-Object { Join-Path $riftWitnessPrivate $_ })
    $riftWitnessSourceArgs = ($riftWitnessSources | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $riftWitnessExe = Join-Path $riftWitnessCase 'Witness.exe'
    $riftWitnessCmd = Join-Path $riftWitnessCase 'compile.cmd'
    $riftWitnessLines = @('@echo off',('call "' + $riftWitnessVars + '" >nul'),
        ('cl /nologo /std:c++20 /EHsc /W4 /O2 /fp:precise /I"' + $riftWitnessPublic + '" ' + $riftWitnessSourceArgs + ' "' + (Join-Path $PSScriptRoot 'RiftBankNormalizationWitness.cpp') + '" /Fe"' + $riftWitnessExe + '"'),
        'exit /b %errorlevel%')
    [IO.File]::WriteAllLines($riftWitnessCmd,$riftWitnessLines,[Text.UTF8Encoding]::new($false))
    Push-Location $riftWitnessCase
    try { & cmd.exe /d /c $riftWitnessCmd; if ($LASTEXITCODE -ne 0) { throw 'Witness compilation failed.' } }
    finally { Pop-Location }
    & $riftWitnessExe $FirstOrdinal $LastOrdinal | Set-Content -LiteralPath (Join-Path $riftWitnessCase 'trace.csv') -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw 'Witness simulation failed.' }
}
Write-Output "Historical comparison only; final production source remains unchanged. CSV traces: $riftWitnessOutput"
