$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Get-OsAssignedFreePort {
    $probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    try {
        $probe.Start()
        return ([System.Net.IPEndPoint]$probe.LocalEndpoint).Port
    }
    finally {
        try { $probe.Stop() } catch {}
    }
}

# Ask Windows for an available ephemeral port instead of assuming 8080-8090 are free.
# This also means old hidden Rift Crown server processes no longer block new launches.
try {
    $port = Get-OsAssignedFreePort
}
catch {
    Write-Host 'Windows could not allocate a free local port.' -ForegroundColor Red
    Write-Host $_.Exception.Message
    Read-Host 'Press Enter to close'
    exit 1
}

$serverScript = Join-Path $root 'local_server.ps1'
$arguments = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', ('"' + $serverScript + '"'),
    '-Port', $port,
    '-Root', ('"' + $root + '"')
) -join ' '

try {
    $server = Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru
}
catch {
    Write-Host 'Could not start the local Rift Crown server process.' -ForegroundColor Red
    Write-Host $_.Exception.Message
    Read-Host 'Press Enter to close'
    exit 1
}

$url = "http://127.0.0.1:$port/"
$ready = $false
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 200
    if ($server.HasExited) { break }
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync('127.0.0.1', $port)
        if ($task.Wait(180) -and $client.Connected) {
            $ready = $true
            break
        }
    } catch {} finally { $client.Close() }
}

if (-not $ready) {
    Write-Host 'The local Rift Crown server failed to start.' -ForegroundColor Red
    if (-not $server.HasExited) { try { Stop-Process -Id $server.Id -Force } catch {} }
    Write-Host 'No administrator privileges should be required. Try START_GAME.bat once more.'
    Read-Host 'Press Enter to close'
    exit 1
}

Start-Process $url
Write-Host "Rift Crown Arena opened at $url" -ForegroundColor Green
Write-Host "Local server PID: $($server.Id)" -ForegroundColor DarkGray
Write-Host 'You can close this launcher window; the game server will keep running for this session.'
Start-Sleep -Seconds 2
