param([Parameter(Mandatory=$true)][string]$OutputFile)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
# Code-native version of the blue spark already used by the Qt interface.
$piclocateSizes = @(16,24,32,48,64,128,256)
$piclocatePngs = @()
foreach ($piclocateSize in $piclocateSizes) {
    $piclocateBitmap = [Drawing.Bitmap]::new($piclocateSize,$piclocateSize,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $piclocateGraphics = [Drawing.Graphics]::FromImage($piclocateBitmap)
    $piclocateGraphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $piclocateGraphics.Clear([Drawing.Color]::Transparent)
    $piclocateGraphics.ScaleTransform($piclocateSize / 24.0, $piclocateSize / 24.0)
    $piclocateBackground = [Drawing.Drawing2D.GraphicsPath]::new()
    foreach ($piclocateArc in @(@(0,0,180),@(16,0,270),@(16,16,0),@(0,16,90))) {
        $piclocateBackground.AddArc($piclocateArc[0],$piclocateArc[1],8,8,$piclocateArc[2],90)
    }
    $piclocateBackground.CloseFigure()
    $piclocateBrush = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255,12,19,30))
    $piclocateGraphics.FillPath($piclocateBrush,$piclocateBackground)
    $piclocatePoints = [Drawing.PointF[]]@(
        [Drawing.PointF]::new(12,3), [Drawing.PointF]::new(14.5,9.5),
        [Drawing.PointF]::new(21,12), [Drawing.PointF]::new(14.5,14.5),
        [Drawing.PointF]::new(12,21), [Drawing.PointF]::new(9.5,14.5),
        [Drawing.PointF]::new(3,12), [Drawing.PointF]::new(9.5,9.5))
    $piclocatePen = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255,168,199,250),1.7)
    $piclocatePen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
    $piclocateGraphics.DrawPolygon($piclocatePen,$piclocatePoints)
    $piclocateStream = [IO.MemoryStream]::new()
    $piclocateBitmap.Save($piclocateStream,[Drawing.Imaging.ImageFormat]::Png)
    $piclocatePngs += ,$piclocateStream.ToArray()
    $piclocateStream.Dispose(); $piclocatePen.Dispose(); $piclocateBrush.Dispose()
    $piclocateBackground.Dispose(); $piclocateGraphics.Dispose(); $piclocateBitmap.Dispose()
}
$piclocateFile = [IO.File]::Create([IO.Path]::GetFullPath($OutputFile))
$piclocateWriter = [IO.BinaryWriter]::new($piclocateFile)
try {
    $piclocateWriter.Write([uint16]0); $piclocateWriter.Write([uint16]1)
    $piclocateWriter.Write([uint16]$piclocateSizes.Count)
    $piclocateOffset = 6 + 16 * $piclocateSizes.Count
    for ($piclocateIndex=0; $piclocateIndex -lt $piclocateSizes.Count; $piclocateIndex++) {
        $piclocateDimension = $piclocateSizes[$piclocateIndex] % 256
        $piclocateWriter.Write([byte]$piclocateDimension); $piclocateWriter.Write([byte]$piclocateDimension)
        $piclocateWriter.Write([uint16]0); $piclocateWriter.Write([uint16]1); $piclocateWriter.Write([uint16]32)
        $piclocateWriter.Write([uint32]$piclocatePngs[$piclocateIndex].Length)
        $piclocateWriter.Write([uint32]$piclocateOffset)
        $piclocateOffset += $piclocatePngs[$piclocateIndex].Length
    }
    foreach ($piclocatePng in $piclocatePngs) { $piclocateWriter.Write([byte[]]$piclocatePng) }
} finally { $piclocateWriter.Dispose(); $piclocateFile.Dispose() }
