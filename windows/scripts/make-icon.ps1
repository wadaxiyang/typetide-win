# Export transparent Windows icons from the SVG source; uses Windows System.Drawing.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$win = Split-Path -Parent $PSScriptRoot
[xml]$svg = Get-Content (Join-Path $win 'assets/brand.svg') -Raw
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
foreach ($shape in $svg.SelectNodes("//*[local-name()='path' or local-name()='circle']")) {
    if ($shape.LocalName -eq 'circle') {
        $r = [single]$shape.r
        $path.StartFigure()
        $path.AddEllipse([single]$shape.cx - $r, [single]$shape.cy - $r, 2 * $r, 2 * $r)
        $path.CloseFigure()
        continue
    }
    $tokens = [regex]::Matches($shape.d, '[A-Za-z]|-?\d+(?:\.\d+)?') | ForEach-Object { $_.Value }
    $x = [single]0; $y = [single]0
    for ($i = 0; $i -lt $tokens.Count;) {
        $op = $tokens[$i++]
        switch ($op) {
            'M' { $path.StartFigure(); $x = [single]$tokens[$i++]; $y = [single]$tokens[$i++] }
            'L' {
                $nx = [single]$tokens[$i++]; $ny = [single]$tokens[$i++]
                $path.AddLine($x, $y, $nx, $ny); $x = $nx; $y = $ny
            }
            'C' {
                $a = [single]$tokens[$i++]; $b = [single]$tokens[$i++]
                $c = [single]$tokens[$i++]; $d = [single]$tokens[$i++]
                $nx = [single]$tokens[$i++]; $ny = [single]$tokens[$i++]
                $path.AddBezier($x, $y, $a, $b, $c, $d, $nx, $ny); $x = $nx; $y = $ny
            }
            'Z' { $path.CloseFigure() }
            default { throw "Unsupported SVG path command: $op" }
        }
    }
}
$gradient = $svg.svg.defs.linearGradient
$stops = $gradient.stop
$colors = $stops | ForEach-Object { [Drawing.ColorTranslator]::FromHtml($_.'stop-color') }
$positions = $stops | ForEach-Object { if ($_.offset) { [single]$_.offset } else { [single]0 } }
$brush = New-Object Drawing.Drawing2D.LinearGradientBrush(
    (New-Object Drawing.PointF([single]$gradient.x1, [single]$gradient.y1)),
    (New-Object Drawing.PointF([single]$gradient.x2, [single]$gradient.y2)), $colors[0], $colors[-1])
$blend = New-Object Drawing.Drawing2D.ColorBlend
$blend.Colors = [Drawing.Color[]]$colors
$blend.Positions = [single[]]$positions
$brush.InterpolationColors = $blend
$pngs = @()
foreach ($s in @(16, 20, 24, 32, 40, 48, 64, 128, 256)) {
    $large = New-Object Drawing.Bitmap ($s * 4), ($s * 4)
    $g = [Drawing.Graphics]::FromImage($large)
    $g.Clear([Drawing.Color]::Transparent)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.ScaleTransform($s / 16.0, $s / 16.0)
    $g.FillPath($brush, $path)
    $g.Dispose()
    $bmp = New-Object Drawing.Bitmap $s, $s
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($large, 0, 0, $s, $s)
    $g.Dispose(); $large.Dispose()
    $ms = New-Object IO.MemoryStream
    $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png)
    $pngs += ,@($s, $ms.ToArray())
    $ms.Dispose(); $bmp.Dispose()
}
$brush.Dispose(); $path.Dispose()
$out = Join-Path $win 'assets/app.ico'
$fs = [IO.File]::Create($out)
$bw = New-Object IO.BinaryWriter $fs
try {
    $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$pngs.Count)
    $offset = 6 + 16 * $pngs.Count
    foreach ($entry in $pngs) {
        $s = $entry[0]; $data = $entry[1]
        $sizeByte = if ($s -eq 256) { 0 } else { $s }
        $bw.Write([byte]$sizeByte); $bw.Write([byte]$sizeByte)
        $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([uint16]1); $bw.Write([uint16]32)
        $bw.Write([uint32]$data.Length); $bw.Write([uint32]$offset)
        $offset += $data.Length
    }
    foreach ($entry in $pngs) { $bw.Write([byte[]]$entry[1]) }
} finally { $bw.Dispose(); $fs.Dispose() }
Write-Output "Exported transparent SVG icon: $out"
