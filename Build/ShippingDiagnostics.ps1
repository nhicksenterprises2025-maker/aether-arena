# Shared native QA log verification. Never copy profiles/replays/config files.
function ConvertTo-RiftDiagnosticUtc([object]$Value) {
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime }
    return [DateTimeOffset]::Parse([string]$Value).UtcDateTime
}
function Get-RiftExpectedGameVersion([string]$RepoRoot,[string]$Override='') {
    if ($Override) { $version=$Override } else {
        $ini=Get-Content -LiteralPath (Join-Path $RepoRoot 'Unreal/RiftCrownArena/Config/DefaultGame.ini') -Raw
        $match=[regex]::Match($ini,'(?m)^ProjectVersion=(\d+\.\d+\.\d+)\s*$')
        if (!$match.Success) { throw 'Current ProjectVersion is missing; provide ExpectedVersion.' }
        $version=$match.Groups[1].Value
    }
    if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'ExpectedVersion must contain three numeric parts.' }
    return $version
}
function Get-RiftShippingDiagnostics {
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)][string]$DestinationRoot,
        [Parameter(Mandatory)][string]$SaveRoot,
        [Parameter(Mandatory)][string]$EngineUserRoot,
        [Parameter(Mandatory)][int]$ProcessId,
        [Parameter(Mandatory)][string]$ExpectedVersion,
        [Parameter(Mandatory)][object]$StartedUTC,
        [Parameter(Mandatory)][object]$FinishedUTC,
        [string]$ErrorPattern='Fatal error[:!]?|Unhandled Exception:|Assertion failed:|Authored .* missing'
    )
    $repo=[IO.Path]::GetFullPath($RepoRoot)
    $save=[IO.Path]::GetFullPath($SaveRoot)
    $user=[IO.Path]::GetFullPath($EngineUserRoot)
    $destination=[IO.Path]::GetFullPath((Join-Path $DestinationRoot 'Diagnostics'))
    foreach ($path in @($save,$user,$destination)) {
        if (!$path.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Native QA roots must remain in the workspace.' }
    }
    $started=ConvertTo-RiftDiagnosticUtc $StartedUTC
    $finished=ConvertTo-RiftDiagnosticUtc $FinishedUTC
    $errors=[Collections.Generic.List[string]]::new()
    $records=[Collections.Generic.List[object]]::new()
    $pins=[Collections.Generic.List[object]]::new()
    $directory=Join-Path $save 'Logs'
    $pattern='^RiftGame-\d{4}-\d{2}-\d{2}-'+$ProcessId+'-\d{2}-\d{2}-\d{2}-\d+\.log$'
    $files=@(if(Test-Path -LiteralPath $directory){Get-ChildItem -LiteralPath $directory -Filter 'RiftGame-*.log' -File|Where-Object {$_.Name -match $pattern}|Sort-Object Name})
    if ($files.Count -eq 0) { $errors.Add('Fresh Shipping diagnostics for the launched process are missing.') }
    foreach ($file in $files) {
        if ($file.LastWriteTimeUtc -lt $started -or $file.LastWriteTimeUtc -gt $finished) { $errors.Add('Shipping diagnostic file was not written during its actual run.') }
        try {
            $lines=[IO.File]::ReadAllLines($file.FullName)
            $fileRecords=@($lines|Where-Object {![string]::IsNullOrWhiteSpace($_)}|ForEach-Object {$_|ConvertFrom-Json -Depth 100})
            $contexts=@($fileRecords|Where-Object event -eq 'system_context')
            if ($contexts.Count -ne 1) { $errors.Add('Each Shipping diagnostic file must identify exactly one native process context.') }
            foreach ($context in $contexts) {
                $config=[IO.Path]::GetFullPath($context.configRoot).TrimEnd('\','/')
                if ($context.schemaVersion -ne 1 -or $context.project -ne 'RiftCrownArena' -or $context.build -ne 'Shipping' -or
                    $context.gameVersion -ne $ExpectedVersion -or $context.processId -ne $ProcessId -or
                    [IO.Path]::GetFullPath($context.saveRoot).TrimEnd('\','/') -ne $save.TrimEnd('\','/') -or
                    !$config.StartsWith($user.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
                    $errors.Add('Shipping diagnostic context differs from the launched version/process/build/save/config roots.')
                }
            }
            foreach ($record in $fileRecords) {
                $timestamp=ConvertTo-RiftDiagnosticUtc $record.timestamp
                if ($record.schemaVersion -ne 1 -or $timestamp -lt $started -or $timestamp -gt $finished) { $errors.Add('Shipping diagnostic record is outside its actual process execution.') }
                if ($record.level -in @('Error','Fatal') -or ($ErrorPattern -and $record.message -match $ErrorPattern)) {
                    $errors.Add(([string]$record.message).Replace($repo,'<repo>').Replace($env:USERPROFILE,'<user>'))
                }
                $records.Add($record)
            }
        } catch { $errors.Add('Shipping diagnostics have malformed JSON, timestamps, or context paths: '+$file.Name) }
        $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        New-Item -ItemType Directory -Path $destination -Force|Out-Null
        $copy=Join-Path $destination $file.Name
        if (Test-Path -LiteralPath $copy) { throw 'The immutable diagnostic evidence copy already exists.' }
        Copy-Item -LiteralPath $file.FullName -Destination $copy
        if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash -or (Get-Item -LiteralPath $copy).Length -ne $file.Length) { $errors.Add('Native diagnostic copy differs from its original isolated bytes.') }
        $pins.Add([pscustomobject][ordered]@{file=[IO.Path]::GetRelativePath($repo,$copy).Replace('\','/');sourceFile=[IO.Path]::GetRelativePath($repo,$file.FullName).Replace('\','/');sha256=$hash;bytes=$file.Length})
    }
    $verification=[pscustomobject][ordered]@{passed=$errors.Count -eq 0;processId=$ProcessId;startedUTC=$started.ToString('o');finishedUTC=$finished.ToString('o');
        saveRoot=[IO.Path]::GetRelativePath($repo,$save).Replace('\','/');engineUserRoot=[IO.Path]::GetRelativePath($repo,$user).Replace('\','/');
        gameVersion=$ExpectedVersion;build='Shipping';freshLog=$errors.Count -eq 0;contextMatches=$errors.Count -eq 0;errorCount=$errors.Count;recordCount=$records.Count}
    return [pscustomobject][ordered]@{diagnosticSource='shipping-frift-diagnostics';nativeDiagnosticLogs=$pins.ToArray();diagnosticVerification=$verification;
        engineLog=$(if($pins.Count){$pins[0].file}else{$null});errors=$errors.ToArray();passed=$verification.passed}
}
