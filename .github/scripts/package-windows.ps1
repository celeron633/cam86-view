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
    'LICENSE-GLFW.md' = Join-Path $BuildDirectory '_deps/glfw-src/LICENSE.md'
    'LICENSE-ImGui.txt' = Join-Path $BuildDirectory '_deps/imgui-src/LICENSE.txt'
}
foreach ($source in $inputs.Values) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required package file is missing: $source"
    }
}
if (Test-Path -LiteralPath $packageDirectory) {
    throw "Package directory already exists: $packageDirectory. Use a fresh output directory or version."
}
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
foreach ($entry in $inputs.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $packageDirectory $entry.Key)
}
@'
CAM86-View - Windows x64 Release

Extract this ZIP and run cam86-view.exe.
Select D2XX / FT2232H and click CONNECT to use the camera.
Close other applications that are using the camera first.

Install the FTDI D2XX/CDM driver if it is not already installed:
https://ftdichip.com/drivers/d2xx-drivers/
Keep the FTDI driver used by the original CAM86 software.
Do not switch the camera interfaces to WinUSB.

This package uses a statically linked Microsoft C++ runtime.
The 64-bit ftd2xx.dll is loaded from the installed FTDI driver.
The original Delphi application's 32-bit DLL cannot be used here.
For operation without camera hardware, select Demo camera.

Settings (cam86.ini) and saved files use the working directory.
Run from a directory where you have write access.
'@ | Set-Content -LiteralPath (Join-Path $packageDirectory 'README-WINDOWS.txt') -Encoding utf8

Compress-Archive -Path $packageDirectory -DestinationPath $zipPath -CompressionLevel Optimal
Get-Item -LiteralPath $zipPath
