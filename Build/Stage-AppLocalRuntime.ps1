param(
    [string]$GamePackage,
    [string]$ToolchainRoot,
    [string]$BuildLog,
    [string]$Report
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $GamePackage) { $GamePackage = Join-Path $repoRoot 'Artifacts/Game/Windows' }
$packageRoot = [IO.Path]::GetFullPath($GamePackage)
$nativeRoot = Join-Path $packageRoot 'RiftCrownArena/Binaries/Win64'
$native = @(Get-ChildItem -LiteralPath $nativeRoot -File -Filter 'RiftCrownArena*.exe')
if (-not (Test-Path -LiteralPath (Join-Path $packageRoot 'RiftCrownArena.exe') -PathType Leaf) -or $native.Count -ne 1) { throw 'A real staged Win64 game and bootstrap executable are required.' }
foreach ($directory in @((Get-Item -LiteralPath $packageRoot)) + @(Get-ChildItem -LiteralPath $packageRoot -Directory -Recurse)) {
    if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'The game package contains a directory link.' }
}

# Select the compiler recorded by the real build, rather than the globally installed CRT.
if (-not $BuildLog) { $BuildLog = Join-Path $repoRoot 'Artifacts/QA/native-shipping-build.log' }
if (-not $ToolchainRoot) {
    if (-not (Test-Path -LiteralPath $BuildLog -PathType Leaf)) { throw 'Supply the actual Shipping build log or its ToolchainRoot to identify the matching redistributable.' }
    $toolchainMatch = [regex]::Matches([IO.File]::ReadAllText($BuildLog), 'Using Visual Studio [\d.]+ toolchain \(([^\r\n]+)\) and Windows')
    if ($toolchainMatch.Count -eq 0) { throw 'The Shipping build log does not record an MSVC toolchain.' }
    $ToolchainRoot = $toolchainMatch[$toolchainMatch.Count - 1].Groups[1].Value
}
$compilerRoot = [IO.Path]::GetFullPath($ToolchainRoot)
$toolsetVersion = [Version]([IO.Path]::GetFileName($compilerRoot.TrimEnd('\','/')))
$vcRoot = [IO.Path]::GetFullPath((Join-Path $compilerRoot '../../..'))
$redistRoot = Join-Path $vcRoot ('Redist/MSVC/' + $toolsetVersion.ToString() + '/x64')
$crtFolders = @(Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object Name -match '^Microsoft\.VC\d+\.CRT$')
if ($crtFolders.Count -ne 1) { throw 'The matching installed x64 release CRT redistributable is unavailable.' }
$crtRoot = $crtFolders[0].FullName
$dumpbin = Join-Path $compilerRoot 'bin/Hostx64/x64/dumpbin.exe'
if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) { throw 'The recorded compiler lacks its PE inspection tool.' }
$runtimeFiles = @(Get-ChildItem -LiteralPath $crtRoot -File -Filter '*.dll')
foreach ($required in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    if ($required -notin $runtimeFiles.Name) { throw "The matching redistributable omits $required." }
}
$providers = @{}
$versions = @{}
foreach ($file in $runtimeFiles) {
    $runtimeVersion = [Version]$file.VersionInfo.FileVersion
    if ($runtimeVersion.Major -ne $toolsetVersion.Major -or $runtimeVersion.Minor -ne $toolsetVersion.Minor) { throw "The runtime family does not match the actual compiler: $($file.Name)." }
    $exports = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $exportOutput = & $dumpbin /NOLOGO /EXPORTS $file.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect runtime exports: $($file.Name)." }
    foreach ($line in $exportOutput) {
        if ($line -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $exports.Add($Matches[1]) | Out-Null }
    }
    if ($exports.Count -eq 0) { throw "The runtime has no readable exports: $($file.Name)." }
    $providers[$file.Name.ToLowerInvariant()] = $exports
    $versions[$file.Name] = $runtimeVersion.ToString()
}

# Verify imported CRT symbols from every actual PE, including third-party libraries.
$importsVerified = 0
$peFiles = @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Where-Object Extension -in @('.exe','.dll'))
$importEvidence = [Collections.Generic.List[object]]::new()
foreach ($pe in $peFiles) {
    $importOutput = & $dumpbin /NOLOGO /IMPORTS $pe.FullName
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect packaged PE imports: $($pe.FullName)." }
    $provider = ''
    $counts = @{}
    foreach ($line in $importOutput) {
        if ($line -match '^\s+(\S+\.dll)\s*$') { $provider = $Matches[1].ToLowerInvariant(); continue }
        if ($providers.ContainsKey($provider) -and $line -match '^\s+[0-9A-F]+\s+(\S+)\s*$') {
            $symbol = $Matches[1]
            if (-not $providers[$provider].Contains($symbol)) { throw "Missing runtime symbol $symbol in $provider, imported by $($pe.FullName)." }
            $importsVerified++
            if (-not $counts.ContainsKey($provider)) { $counts[$provider] = 0 }
            $counts[$provider]++
        }
    }
    if ($counts.Count) { $importEvidence.Add([ordered]@{ path = [IO.Path]::GetRelativePath($packageRoot,$pe.FullName).Replace('\','/'); providers = $counts }) }
}
if ($importsVerified -eq 0) { throw 'No actual game CRT imports were verified.' }
$staged = [Collections.Generic.List[object]]::new()
foreach ($destinationRoot in @($packageRoot,$nativeRoot)) {
    foreach ($file in $runtimeFiles) {
        $destination = Join-Path $destinationRoot $file.Name
        if ((Test-Path -LiteralPath $destination) -and ((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'A runtime destination is a link.' }
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sourceHash) { throw 'Runtime bytes changed during app-local staging.' }
        $staged.Add([ordered]@{ path = [IO.Path]::GetRelativePath($packageRoot,$destination).Replace('\','/'); version = $versions[$file.Name]; sha256 = $sourceHash; size = $file.Length })
    }
}
if (-not $Report) { $Report = Join-Path $repoRoot 'Artifacts/QA/app-local-runtime.json' }
$reportPath = [IO.Path]::GetFullPath($Report)
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($reportPath)) -Force | Out-Null
$evidence = [ordered]@{ passed = $true; utc = [DateTime]::UtcNow.ToString('o'); architecture = 'x64'; toolchainRoot = $compilerRoot; toolsetVersion = $toolsetVersion.ToString(); redistRoot = $crtRoot; packageRoot = $packageRoot; peFilesInspected = $peFiles.Count; importedSymbolsVerified = $importsVerified; imports = $importEvidence.ToArray(); files = $staged.ToArray() }
[IO.File]::WriteAllText($reportPath,($evidence | ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output "Staged $($staged.Count) matching app-local runtime files; verified $importsVerified CRT imports across $($peFiles.Count) packaged PE files. Evidence: $reportPath"
