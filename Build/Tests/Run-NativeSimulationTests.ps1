param([switch]$CompileOnly)
$ErrorActionPreference='Stop'
$taskRoot=[System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$nativePublic=Join-Path $taskRoot 'Unreal/RiftCrownArena/Source/RiftCrownArena/Public'
$nativePrivate=Join-Path $taskRoot 'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation'
$nativeOutput=Join-Path $taskRoot 'Build/NativeTests'
New-Item -ItemType Directory -Path $nativeOutput -Force | Out-Null
$nativeVSWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$nativeVS=& $nativeVSWhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $nativeVS){throw 'Visual Studio C++ x64 tools are required for native simulation tests.'}
$nativeVars=Join-Path $nativeVS 'VC/Auxiliary/Build/vcvars64.bat'
$nativeSources=@('RiftSimulation.cpp','RiftCombat.cpp','RiftPathfinding.cpp','RiftDeckAnalysis.cpp','RiftAI.cpp') | ForEach-Object {'"'+(Join-Path $nativePrivate $_)+'"'}
$nativeExe=Join-Path $nativeOutput 'RiftSimulationTests.exe'
$nativeCmd=Join-Path $nativeOutput 'compile.cmd'
$nativeLines=@('@echo off',('call "'+$nativeVars+'" >nul'),('cl /nologo /std:c++20 /EHsc /W4 /O2 /I"'+$nativePublic+'" '+($nativeSources -join ' ')+' "'+(Join-Path $PSScriptRoot 'RiftSimulationTests.cpp')+'" /Fe"'+$nativeExe+'"'), 'exit /b %errorlevel%')
[System.IO.File]::WriteAllLines($nativeCmd,$nativeLines,[System.Text.UTF8Encoding]::new($false))
Push-Location $nativeOutput
try { & cmd.exe /d /c $nativeCmd; if($LASTEXITCODE -ne 0){throw 'Native compilation failed.'} }
finally { Pop-Location }
if(-not $CompileOnly){ & $nativeExe; if($LASTEXITCODE -ne 0){throw 'Native simulation regression failed.'} }
