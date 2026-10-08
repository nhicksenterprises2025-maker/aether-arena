param(
    [int]$Port = 8080,
    [string]$Root = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath($Root)
$utf8 = New-Object System.Text.UTF8Encoding($false)
# HTTP Content-Length counts bytes. A single-byte reader preserves that count
# even when the JSON body contains a UTF-8 username.
$httpEncoding = [Text.Encoding]::GetEncoding(28591)
if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { $saveDir = Join-Path $Root 'save_data' } else { $saveDir = Join-Path $env:LOCALAPPDATA 'RiftCrownArena' }
[IO.Directory]::CreateDirectory($saveDir) | Out-Null
$savePath = Join-Path $saveDir 'player_save.json'
$labPath = Join-Path $saveDir 'lab_v15.json'

function Get-ContentType([string]$Path) {
    switch ([IO.Path]::GetExtension($Path).ToLowerInvariant()) {
        '.html' { 'text/html; charset=utf-8' }
        '.js'   { 'text/javascript; charset=utf-8' }
        '.css'  { 'text/css; charset=utf-8' }
        '.json' { 'application/json; charset=utf-8' }
        '.glb'  { 'model/gltf-binary' }
        '.gltf' { 'model/gltf+json' }
        '.png'  { 'image/png' }
        '.jpg'  { 'image/jpeg' }
        '.jpeg' { 'image/jpeg' }
        '.svg'  { 'image/svg+xml' }
        '.ico'  { 'image/x-icon' }
        default { 'application/octet-stream' }
    }
}

function Send-Response($Stream, [int]$Status, [string]$StatusText, [byte[]]$Body, [string]$ContentType) {
    $header = "HTTP/1.1 $Status $StatusText`r`n" +
              "Content-Type: $ContentType`r`n" +
              "Content-Length: $($Body.Length)`r`n" +
              "Cache-Control: no-store`r`n" +
              "Access-Control-Allow-Origin: *`r`n" +
              "Connection: close`r`n`r`n"
    $headerBytes = $utf8.GetBytes($header)
    $Stream.Write($headerBytes, 0, $headerBytes.Length)
    if ($Body.Length -gt 0) { $Stream.Write($Body, 0, $Body.Length) }
    $Stream.Flush()
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $Port)
$listener.Start()
Write-Host "Rift Crown Arena server running at http://127.0.0.1:$Port"

try {
    while ($true) {
        $client = $listener.AcceptTcpClient()
        $reader = $null
        $stream = $null
        try {
            $client.ReceiveTimeout = 5000
            $stream = $client.GetStream()
            $reader = New-Object System.IO.StreamReader($stream, $httpEncoding, $false, 4096, $true)
            $requestLine = $reader.ReadLine()
            if ([string]::IsNullOrWhiteSpace($requestLine)) { continue }

            $headers = @{}
            while ($true) {
                $line = $reader.ReadLine()
                if ([string]::IsNullOrEmpty($line)) { break }
                $idx = $line.IndexOf(':')
                if ($idx -gt 0) {
                    $headers[$line.Substring(0,$idx).Trim().ToLowerInvariant()] = $line.Substring($idx+1).Trim()
                }
            }

            $parts = $requestLine.Split(' ')
            if ($parts.Length -lt 2) {
                Send-Response $stream 400 'Bad Request' ($utf8.GetBytes('Bad request')) 'text/plain; charset=utf-8'
                continue
            }
            $method = $parts[0].ToUpperInvariant()
            $rawPath = $parts[1].Split('?')[0]
            $decoded = [Uri]::UnescapeDataString($rawPath)

            if ($decoded -eq '/api/save' -or $decoded -eq '/api/lab') {
                if ($decoded -eq '/api/lab') { $apiSavePath = $labPath; $maxApiBytes = 16777216 }
                else { $apiSavePath = $savePath; $maxApiBytes = 262144 }
                $apiTempPath = "$apiSavePath.$PID.tmp"
                if ($method -eq 'GET') {
                    if ([IO.File]::Exists($apiSavePath)) { $bytes = [IO.File]::ReadAllBytes($apiSavePath) }
                    else { $bytes = $utf8.GetBytes('{}') }
                    Send-Response $stream 200 'OK' $bytes 'application/json; charset=utf-8'
                    continue
                }
                if ($method -eq 'POST') {
                    $length = 0
                    if (-not $headers.ContainsKey('content-length') -or
                        -not [int]::TryParse($headers['content-length'], [ref]$length) -or $length -lt 0) {
                        Send-Response $stream 400 'Bad Request' ($utf8.GetBytes('{"ok":false,"error":"invalid content length"}')) 'application/json; charset=utf-8'
                        continue
                    }
                    if ($length -gt $maxApiBytes) {
                        Send-Response $stream 413 'Payload Too Large' ($utf8.GetBytes('{"ok":false}')) 'application/json; charset=utf-8'
                        # Drain queued bytes before closing to avoid resetting the
                        # connection while the client receives the 413 response.
                        try {
                            $discard = New-Object char[] 4096
                            $remaining = $length
                            while ($remaining -gt 0) {
                                $read = $reader.Read($discard,0,[Math]::Min($remaining,$discard.Length))
                                if ($read -eq 0) { break }
                                $remaining -= $read
                            }
                        } catch {}
                        continue
                    }
                    $body = ''
                    if ($length -gt 0) {
                        $buffer = New-Object char[] $length
                        $read = $reader.ReadBlock($buffer,0,$length)
                        if ($read -ne $length) {
                            Send-Response $stream 400 'Bad Request' ($utf8.GetBytes('{"ok":false,"error":"incomplete body"}')) 'application/json; charset=utf-8'
                            continue
                        }
                        $body = $utf8.GetString($httpEncoding.GetBytes((-join $buffer)))
                    }
                    try {
                        if ([string]::IsNullOrWhiteSpace($body)) { throw 'Empty JSON' }
                        $body | ConvertFrom-Json -ErrorAction Stop | Out-Null
                    } catch {
                        Send-Response $stream 400 'Bad Request' ($utf8.GetBytes('{"ok":false,"error":"invalid json"}')) 'application/json; charset=utf-8'
                        continue
                    }
                    try {
                        [IO.File]::WriteAllText($apiTempPath,$body,$utf8)
                        if ([IO.File]::Exists($apiSavePath)) { [IO.File]::Replace($apiTempPath,$apiSavePath,$null) }
                        else { [IO.File]::Move($apiTempPath,$apiSavePath) }
                        Send-Response $stream 200 'OK' ($utf8.GetBytes('{"ok":true}')) 'application/json; charset=utf-8'
                    } catch {
                        if ([IO.File]::Exists($apiTempPath)) { [IO.File]::Delete($apiTempPath) }
                        Send-Response $stream 500 'Internal Server Error' ($utf8.GetBytes('{"ok":false,"error":"save failed"}')) 'application/json; charset=utf-8'
                    }
                    continue
                }
                Send-Response $stream 405 'Method Not Allowed' ($utf8.GetBytes('Method not allowed')) 'text/plain; charset=utf-8'
                continue
            }

            if ($method -ne 'GET') {
                Send-Response $stream 405 'Method Not Allowed' ($utf8.GetBytes('Method not allowed')) 'text/plain; charset=utf-8'
                continue
            }

            if ($decoded -eq '/') { $decoded = '/index.html' }
            $relative = $decoded.TrimStart('/').Replace('/', [IO.Path]::DirectorySeparatorChar)
            $fullPath = [IO.Path]::GetFullPath([IO.Path]::Combine($Root, $relative))

            $rootWithSep = $Root.TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
            if (-not ($fullPath + [IO.Path]::DirectorySeparatorChar).StartsWith($rootWithSep, [StringComparison]::OrdinalIgnoreCase) -and -not $fullPath.Equals($Root,[StringComparison]::OrdinalIgnoreCase)) {
                Send-Response $stream 403 'Forbidden' ($utf8.GetBytes('Forbidden')) 'text/plain; charset=utf-8'
                continue
            }
            if ([IO.Directory]::Exists($fullPath)) { $fullPath = [IO.Path]::Combine($fullPath, 'index.html') }
            if (-not [IO.File]::Exists($fullPath)) {
                Send-Response $stream 404 'Not Found' ($utf8.GetBytes('Not found')) 'text/plain; charset=utf-8'
                continue
            }
            $bytes = [IO.File]::ReadAllBytes($fullPath)
            Send-Response $stream 200 'OK' $bytes (Get-ContentType $fullPath)
        }
        catch {
            try { if ($stream) { Send-Response $stream 500 'Internal Server Error' ($utf8.GetBytes('Server error')) 'text/plain; charset=utf-8' } } catch {}
        }
        finally {
            if ($reader) { $reader.Dispose() }
            if ($stream) { $stream.Dispose() }
            $client.Close()
        }
    }
}
finally { $listener.Stop() }
