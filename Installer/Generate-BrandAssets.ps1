$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$output = Join-Path $PSScriptRoot 'BrandAssets'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$stone = [Drawing.Color]::FromArgb(18, 29, 32)
$brass = [Drawing.Color]::FromArgb(200, 173, 115)
$cyan = [Drawing.Color]::FromArgb(95, 181, 202)
$ivory = [Drawing.Color]::FromArgb(232, 229, 216)
$brassBrush = [Drawing.SolidBrush]::new($brass)
$cyanBrush = [Drawing.SolidBrush]::new($cyan)
$ivoryBrush = [Drawing.SolidBrush]::new($ivory)
$stoneBrush = [Drawing.SolidBrush]::new($stone)

function Crown([Drawing.Graphics]$Graphics, [single]$X, [single]$Y, [single]$Size) {
    $scale = $Size / 64
    [Drawing.PointF[]]$points = @(@(4,19),@(16,29),@(20,9),@(32,23),@(44,9),@(48,29),@(60,19),@(52,45),@(12,45)) | ForEach-Object { [Drawing.PointF]::new(($X + $_[0] * $scale), ($Y + $_[1] * $scale)) }
    $Graphics.FillPolygon($brassBrush, $points)
    [Drawing.PointF[]]$base = @(@(14,51),@(50,51),@(48,57),@(16,57)) | ForEach-Object { [Drawing.PointF]::new(($X + $_[0] * $scale), ($Y + $_[1] * $scale)) }
    $Graphics.FillPolygon($brassBrush, $base)
    [Drawing.PointF[]]$gem = @(@(27,31),@(32,25),@(37,31),@(32,42)) | ForEach-Object { [Drawing.PointF]::new(($X + $_[0] * $scale), ($Y + $_[1] * $scale)) }
    $Graphics.FillPolygon($cyanBrush, $gem)
}
function Bitmap([string]$Name, [int]$Width, [int]$Height, [scriptblock]$Paint) {
    $bitmap = [Drawing.Bitmap]::new($Width, $Height, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $graphics.Clear([Drawing.Color]::White)
        & $Paint $graphics
        $bitmap.Save((Join-Path $output $Name), [Drawing.Imaging.ImageFormat]::Bmp)
    }
    finally { $graphics.Dispose(); $bitmap.Dispose() }
}
try {
    Bitmap 'Banner.bmp' 493 58 {
        param($graphics)
        Crown $graphics 440 4 50
        $graphics.FillRectangle($brassBrush, 0, 56, 493, 2)
    }
    Bitmap 'Dialog.bmp' 493 312 {
        param($graphics)
        $graphics.FillRectangle($stoneBrush, 0, 0, 164, 312)
        $graphics.FillRectangle($brassBrush, 162, 0, 2, 312)
        Crown $graphics 38 75 88
        $format = [Drawing.StringFormat]::new()
        $format.Alignment = [Drawing.StringAlignment]::Center
        $font = [Drawing.Font]::new('Georgia', 15, [Drawing.FontStyle]::Bold)
        $subfont = [Drawing.Font]::new('Segoe UI', 10, [Drawing.FontStyle]::Regular)
        try {
            $graphics.DrawString("RIFT CROWN", $font, $ivoryBrush, [Drawing.RectangleF]::new(0,178,162,35), $format)
            $graphics.DrawString('A R E N A', $subfont, $brassBrush, [Drawing.RectangleF]::new(0,215,162,25), $format)
        }
        finally { $format.Dispose(); $font.Dispose(); $subfont.Dispose() }
    }
    $iconImages = [Collections.Generic.List[object]]::new()
    foreach ($dimension in @(16,24,32,48,64,128,256)) {
        $bitmap = [Drawing.Bitmap]::new($dimension, $dimension, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            $graphics.Clear([Drawing.Color]::Transparent)
            Crown $graphics 0 0 $dimension
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $iconImages.Add([pscustomobject]@{ size = $dimension; bytes = $stream.ToArray() })
        }
        finally { $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    }
    $iconStream = [IO.MemoryStream]::new()
    $writer = [IO.BinaryWriter]::new($iconStream)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$iconImages.Count)
        $offset = 6 + 16 * $iconImages.Count
        foreach ($image in $iconImages) {
            $encodedDimension = if ($image.size -eq 256) { 0 } else { $image.size }
            $writer.Write([byte]$encodedDimension); $writer.Write([byte]$encodedDimension); $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$image.bytes.Length); $writer.Write([uint32]$offset)
            $offset += $image.bytes.Length
        }
        foreach ($image in $iconImages) { $writer.Write([byte[]]$image.bytes) }
        $writer.Flush()
        $iconBytes = $iconStream.ToArray()
        $iconFile = Join-Path $output 'Application.ico'
        [IO.File]::WriteAllBytes($iconFile, $iconBytes)
        $gameIcon = Join-Path $PSScriptRoot '../Unreal/RiftCrownArena/Build/Windows/Application.ico'
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($gameIcon)) -Force | Out-Null
        if (-not (Test-Path -LiteralPath $gameIcon) -or (Get-FileHash -LiteralPath $gameIcon -Algorithm SHA256).Hash -ne [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($iconBytes))) { [IO.File]::WriteAllBytes($gameIcon, $iconBytes) }
    }
    finally { $writer.Dispose(); $iconStream.Dispose() }
}
finally { $brassBrush.Dispose(); $cyanBrush.Dispose(); $ivoryBrush.Dispose(); $stoneBrush.Dispose() }
Write-Output "Generated original launcher crown branding: $output"
