param(
    [string]$BuildDirectory = 'build/windows',
    [string]$OutputDirectory = 'dist',
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9._-]+$')]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$packageName = "cam86-view-windows-x64-$Version"
$packageDirectory = Join-Path $OutputDirectory $packageName
$zipPath = Join-Path $OutputDirectory "$packageName.zip"

# Verify all inputs before creating the package.
$inputs = @{
    'cam86-view.exe' = Join-Path $BuildDirectory 'Release/cam86-view.exe'
    'README.md' = 'README.md'
    'LICENSE' = 'LICENSE'
}
foreach ($source in $inputs.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required package file is missing: $source"
    }
}
# CMake's post-build windeployqt step supplies DLLs, plugins and the MSVC runtime.
$runtimeDirectory = Join-Path $BuildDirectory 'Release'
foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeDirectory $required) -PathType Leaf)) {
        throw "Qt runtime missing: $required. Rebuild cam86-view to run windeployqt."
    }
}
if (-not (Test-Path -LiteralPath 'licenses/Qt-NOTICE.txt' -PathType Leaf)) {
    throw 'Qt license notices are missing.'
}
if (Test-Path -LiteralPath $packageDirectory) {
    throw "Package directory already exists: $packageDirectory. Use a fresh output directory or version."
}
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
foreach ($entry in $inputs.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $packageDirectory $entry.Key)
}
Get-ChildItem -LiteralPath $runtimeDirectory -Filter '*.dll' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $packageDirectory
}
foreach ($pluginDirectory in @('platforms', 'styles', 'imageformats', 'iconengines', 'generic', 'networkinformation', 'tls')) {
    $source = Join-Path $runtimeDirectory $pluginDirectory
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination $packageDirectory -Recurse
    }
}
Copy-Item -LiteralPath 'licenses' -Destination $packageDirectory -Recurse
$qtConfig = Join-Path $runtimeDirectory 'qt.conf'
if (Test-Path -LiteralPath $qtConfig) { Copy-Item -LiteralPath $qtConfig -Destination $packageDirectory }
@'
CAM86-View - Windows x64 Release

Extract this ZIP and run cam86-view.exe.
Select D2XX / FT2232H and click CONNECT to use the camera.
Close other applications that are using the camera first.

Install the FTDI D2XX/CDM driver if it is not already installed:
https://ftdichip.com/drivers/d2xx-drivers/
Keep the FTDI driver used by the original CAM86 software.
Do not switch the camera interfaces to WinUSB.

Qt 6 runtime DLLs, platform plugins and the Microsoft C++ runtime are included.
Qt is dynamically linked. Qt licensing: https://www.qt.io/licensing/
Qt source code: https://download.qt.io/archive/qt/6.8/6.8.3/
The 64-bit ftd2xx.dll is loaded from the installed FTDI driver.
The original Delphi application's 32-bit DLL cannot be used here.
For operation without camera hardware, select Demo camera.

Settings (cam86.ini) and saved files use the working directory.
Run from a directory where you have write access.
'@ | Set-Content -LiteralPath (Join-Path $packageDirectory 'README-WINDOWS.txt') -Encoding utf8

Compress-Archive -Path $packageDirectory -DestinationPath $zipPath -CompressionLevel Optimal
Get-Item -LiteralPath $zipPath
