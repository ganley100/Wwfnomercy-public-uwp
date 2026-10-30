# Builds SDL2 for UWP (SDL's WinRT backend: CoreWindow, Windows.Gaming.Input controllers, WASAPI audio).
# The port compiles against the desktop SDL2 devel headers of the same version (identical ABI) and links this build.
#
# Usage (from a VS developer PowerShell): .\uwp\build_sdl2.ps1 [-Version 2.30.3] [-OutDir build-sdl2-uwp]
# Then configure the port with -DNMR_UWP_SDL2_DIR=<OutDir>.
param(
    [string]$Version = "2.30.3",
    [string]$OutDir = "build-sdl2-uwp"
)
$ErrorActionPreference = "Stop"

$src = Join-Path $OutDir "SDL-$Version"
if (-not (Test-Path $src)) {
    git clone --depth 1 --branch "release-$Version" https://github.com/libsdl-org/SDL.git $src
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# SDL's own UWP project (VisualC-WinRT). Retarget it to the installed toolset and Windows SDK.
msbuild "$src\VisualC-WinRT\SDL-UWP.vcxproj" /m /nologo `
    /p:Configuration=Release /p:Platform=x64 `
    /p:PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$built = Get-ChildItem -Recurse "$src\VisualC-WinRT" -Filter SDL2.dll | Where-Object { $_.FullName -match '\\x64\\Release\\' } | Select-Object -First 1
if (-not $built) { throw "SDL2.dll not found under $src\VisualC-WinRT" }
Copy-Item (Join-Path $built.DirectoryName "SDL2.dll"), (Join-Path $built.DirectoryName "SDL2.lib") $OutDir
Write-Host "UWP SDL2 $Version -> $(Resolve-Path $OutDir)"
