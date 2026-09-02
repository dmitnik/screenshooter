param(
    [string]$EdgePath = 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$projectRoot = Split-Path -Parent $PSScriptRoot
$sourceDirectory = Join-Path $projectRoot 'resources\tabler'
$outputDirectory = Join-Path $sourceDirectory 'generated'
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) 'screenshooter-tabler-render'

if (-not (Test-Path -LiteralPath $EdgePath)) {
    throw "Microsoft Edge не найден: $EdgePath"
}

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null

$icons = [ordered]@{
    'pointer'        = [Drawing.Color]::FromArgb(51, 102, 204)
    'rectangle'      = [Drawing.Color]::FromArgb(226, 82, 76)
    'line'           = [Drawing.Color]::FromArgb(126, 87, 194)
    'oval'           = [Drawing.Color]::FromArgb(28, 145, 122)
    'arrow-up-right' = [Drawing.Color]::FromArgb(226, 82, 76)
    'highlight'      = [Drawing.Color]::FromArgb(239, 169, 18)
    'letter-t'       = [Drawing.Color]::FromArgb(51, 102, 204)
    'blur'           = [Drawing.Color]::FromArgb(48, 130, 196)
    'crop'           = [Drawing.Color]::FromArgb(28, 145, 122)
    'copy'           = [Drawing.Color]::FromArgb(51, 102, 204)
    'device-floppy'  = [Drawing.Color]::FromArgb(28, 145, 122)
}
$sizes = @(20, 25, 30, 40)

foreach ($entry in $icons.GetEnumerator()) {
    $images = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        $source = Join-Path $sourceDirectory ($entry.Key + '.svg')
        $rendered = Join-Path $temporaryDirectory ($entry.Key + '-' + $size + '.png')
        $profile = Join-Path $temporaryDirectory ('edge-' + $entry.Key + '-' + $size)
        $scale = ([double]$size / 24.0).ToString([Globalization.CultureInfo]::InvariantCulture)
        $arguments = @(
            '--headless=new'
            '--disable-gpu'
            '--hide-scrollbars'
            '--default-background-color=00000000'
            "--force-device-scale-factor=$scale"
            '--window-size=24,24'
            "--user-data-dir=$profile"
            "--screenshot=$rendered"
            ([Uri]$source).AbsoluteUri
        )
        $process = Start-Process -FilePath $EdgePath -ArgumentList $arguments `
            -Wait -PassThru -WindowStyle Hidden
        if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $rendered)) {
            throw "Не удалось отрисовать $($entry.Key) в размере $size"
        }

        $sourceBitmap = [Drawing.Bitmap]::FromFile($rendered)
        try {
            if ($sourceBitmap.Width -ne $size -or $sourceBitmap.Height -ne $size) {
                throw "Неверный размер $($entry.Key): $($sourceBitmap.Width)x$($sourceBitmap.Height)"
            }
            $colored = [Drawing.Bitmap]::new(
                $size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
            try {
                for ($y = 0; $y -lt $size; $y++) {
                    for ($x = 0; $x -lt $size; $x++) {
                        $alpha = $sourceBitmap.GetPixel($x, $y).A
                        $colored.SetPixel($x, $y, [Drawing.Color]::FromArgb(
                            $alpha, $entry.Value.R, $entry.Value.G, $entry.Value.B))
                    }
                }
                $stream = [IO.MemoryStream]::new()
                try {
                    $colored.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
                    $images.Add($stream.ToArray())
                } finally {
                    $stream.Dispose()
                }
            } finally {
                $colored.Dispose()
            }
        } finally {
            $sourceBitmap.Dispose()
        }
    }

    $target = Join-Path $outputDirectory ($entry.Key + '.ico')
    $file = [IO.File]::Create($target)
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$images.Count)
        $offset = 6 + (16 * $images.Count)
        for ($index = 0; $index -lt $images.Count; $index++) {
            $writer.Write([byte]$sizes[$index])
            $writer.Write([byte]$sizes[$index])
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$images[$index].Length)
            $writer.Write([uint32]$offset)
            $offset += $images[$index].Length
        }
        foreach ($image in $images) {
            $writer.Write($image)
        }
    } finally {
        $writer.Dispose()
        $file.Dispose()
    }
}

Write-Host "Tabler icons generated in $outputDirectory"
