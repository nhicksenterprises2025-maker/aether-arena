param([switch]$FocusedOnly, [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$OutputDirectory){$OutputDirectory=Join-Path $repo ('Artifacts/QA/Pulverizer/'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$public=Join-Path $repo 'Unreal/RiftCrownArena/Source/RiftCrownArena/Public'
$private=Join-Path $repo 'Unreal/RiftCrownArena/Source/RiftCrownArena/Private/Simulation'
$sources=@('RiftSimulation.cpp','RiftCombat.cpp','RiftPathfinding.cpp','RiftDeckAnalysis.cpp','RiftAI.cpp') | ForEach-Object {Join-Path $private $_}
$pins=@($sources)+@((Join-Path $public 'Simulation/RiftSimulation.h'),(Join-Path $PSScriptRoot 'RiftSimulationTests.cpp'),$PSCommandPath)
$before=@($pins | ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $_).Length}})
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio C++ x64 tools are required.'}
$vars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$exe=Join-Path $OutputDirectory 'RiftSimulationTests.exe'
$cmd=Join-Path $OutputDirectory 'compile.cmd'
$lines=@('@echo off',('call "'+$vars+'" >nul'),('cl /nologo /std:c++20 /EHsc /W4 /O2 /I"'+$public+'" '+(($sources | ForEach-Object {'"'+$_+'"'}) -join ' ')+' "'+(Join-Path $PSScriptRoot 'RiftSimulationTests.cpp')+'" /Fe"'+$exe+'"'),'exit /b %errorlevel%')
[IO.File]::WriteAllLines($cmd,$lines,[Text.UTF8Encoding]::new($false))
$timer=[Diagnostics.Stopwatch]::StartNew();$compileExit=-1;$focusedExit=-1;$fullExit=$null
Push-Location $OutputDirectory
try {
    & cmd.exe /d /c $cmd *> (Join-Path $OutputDirectory 'compile.log');$compileExit=$LASTEXITCODE
    if($compileExit -eq 0){
        & $exe 'current patch' *> (Join-Path $OutputDirectory 'focused.log');$focusedExit=$LASTEXITCODE
        if(!$FocusedOnly -and $focusedExit -eq 0){& $exe *> (Join-Path $OutputDirectory 'full.log');$fullExit=$LASTEXITCODE}
    }
} finally {Pop-Location}
$timer.Stop()
$unchanged=@($before | Where-Object {(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $_.sha256}).Count -eq 0
$focusedLog=if(Test-Path (Join-Path $OutputDirectory 'focused.log')){Get-Content -Raw (Join-Path $OutputDirectory 'focused.log')}else{''}
$fullLog=if(Test-Path (Join-Path $OutputDirectory 'full.log')){Get-Content -Raw (Join-Path $OutputDirectory 'full.log')}else{''}
$focusedScenarios=([regex]::Matches($focusedLog,'(?m)^PASS ')).Count
$fullScenarios=([regex]::Matches($fullLog,'(?m)^PASS ')).Count
$logPins=@(@('compile.log','focused.log','full.log') | ForEach-Object {
    $path=Join-Path $OutputDirectory $_
    if(Test-Path -LiteralPath $path){[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $path).Length}}
})
$passed=$compileExit -eq 0 -and $focusedExit -eq 0 -and $focusedScenarios -eq 6 -and $unchanged -and ($FocusedOnly -or ($fullExit -eq 0 -and $fullScenarios -eq 69 -and $fullLog -match 'SOAK 21 complete matches'))
$report=[ordered]@{schema='rift.portable.pulverizer.qa.v1';version='1.5.0';passed=$passed;focusedOnly=[bool]$FocusedOnly;sourcePins=$before;sourcePinsUnchanged=$unchanged;logPins=$logPins;compileExit=$compileExit;focusedExit=$focusedExit;fullExit=$fullExit;focusedScenarios=$focusedScenarios;fullScenarios=$fullScenarios;elapsedSeconds=$timer.Elapsed.TotalSeconds;executable=$exe;executableSha256=$(if(Test-Path $exe){(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()}else{''});focusedLog=(Join-Path $OutputDirectory 'focused.log');fullLog=(Join-Path $OutputDirectory 'full.log')}
$reportPath=Join-Path $OutputDirectory 'portable-verification.json'
$report|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $reportPath -Encoding utf8
[pscustomobject]$report|Select-Object passed,focusedScenarios,fullScenarios,elapsedSeconds,sourcePinsUnchanged|ConvertTo-Json
Write-Output $reportPath
if(!$passed){throw 'Authoritative Pulverizer/deployment/detection verification failed; raw compile and test logs are preserved.'}
