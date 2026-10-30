# Wraps a UWP build (cmake -DNMR_UWP=ON) into a signed MSIX for Xbox One Dev Mode.
#
# Usage (from a VS developer PowerShell, which puts makeappx/signtool on PATH):
#   .\uwp\package.ps1 -BuildDir build-uwp [-OutDir uwp-package] [-Version 0.2.0.0] [-Pfx cert.pfx -PfxPassword ...]
#
# Without -Pfx a self-signed code-signing certificate is created (CN=NoMercyRecompiled) and exported next to the
# package as NoMercyRecompiled.pfx/.cer; reuse that .pfx for later builds so updates install over the old version.
# Output: NoMercyRecompiled_<version>_x64.msix, the certificate, and Dependencies\Microsoft.VCLibs.x64.14.00.appx.
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$OutDir = "uwp-package",
    [string]$Version = "0.2.0.0",
    [string]$Pfx = "",
    [string]$PfxPassword = "nomercy"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$publisher = "CN=NoMercyRecompiled"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stage = Join-Path $OutDir "layout"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null

# A static import of a DLL the console doesn't have stops the app before any of its code runs (it just closes, with no
# log), so only the DLLs that are on every Xbox, the app CRT and SDL2 may be imported outright. Everything else must be
# delay-loaded (CMakeLists.txt, NMR_UWP), where a missing DLL is stubbed and logged instead.
$exe = Join-Path $BuildDir "NoMercyRecompiled.exe"
$deps = @()
$inStatic = $false
foreach ($line in (dumpbin /nologo /dependents $exe)) {
    if ($line -match 'Image has the following dependencies') { $inStatic = $true; continue }
    if ($line -match 'Image has the following delay load dependencies|^\s*Summary') { $inStatic = $false; continue }
    if ($inStatic -and $line -match '^\s+(\S+\.dll)\s*$') { $deps += $Matches[1] }
}
if (-not $deps) { throw "dumpbin /dependents listed no imports for $exe" }
Write-Host "Static imports: $($deps -join ', ')"
$allowed = '^(api-ms-win-.+|kernel32|sdl2|d3d12|dxgi|vcruntime140(_1)?_app|msvcp140(_[a-z0-9_]+)?_app)\.dll$'
$bad = @($deps | Where-Object { $_ -notmatch $allowed })
if ($bad) { throw "NoMercyRecompiled.exe imports DLLs the Xbox may not have: $($bad -join ', '). Delay-load them (CMakeLists.txt, NMR_UWP)." }

# Package layout: the exe and everything it loads relative to its folder (the app's working directory).
Copy-Item (Join-Path $BuildDir "NoMercyRecompiled.exe"), (Join-Path $BuildDir "SDL2.dll"), (Join-Path $BuildDir "recompcontrollerdb.txt") $stage
Copy-Item -Recurse (Join-Path $BuildDir "assets") (Join-Path $stage "assets")
# Tiles go in Images\, not Assets\: Windows paths ignore case, so Assets\ would land inside the game's assets\.
Copy-Item -Recurse (Join-Path $root "uwp\Assets") (Join-Path $stage "Images")
Copy-Item (Join-Path $root "COPYING") $stage
(Get-Content -Raw (Join-Path $root "uwp\Package.appxmanifest")).Replace("@VERSION@", $Version).Replace("@PUBLISHER@", $publisher) |
    Set-Content -Encoding UTF8 (Join-Path $stage "AppxManifest.xml")

# Never ship a ROM, saves or generated configs.
$stray = @(Get-ChildItem -Recurse $stage -Include *.z64, *.n64, *.v64, *.map, *.pdb, *.log)
if ($stray) { $stray | ForEach-Object { Write-Host "STRAY: $($_.FullName)" }; throw "stray files in the package layout" }

$msix = Join-Path $OutDir "NoMercyRecompiled_$($Version)_x64.msix"
makeappx pack /o /h SHA256 /d $stage /p $msix
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $Pfx) {
    $Pfx = Join-Path $OutDir "NoMercyRecompiled.pfx"
    if (-not (Test-Path $Pfx)) {
        $cert = New-SelfSignedCertificate -Type Custom -Subject $publisher -KeyUsage DigitalSignature `
            -FriendlyName "NoMercyRecompiled (Xbox Dev Mode)" -CertStoreLocation "Cert:\CurrentUser\My" `
            -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
        $password = ConvertTo-SecureString -String $PfxPassword -Force -AsPlainText
        Export-PfxCertificate -Cert $cert -FilePath $Pfx -Password $password | Out-Null
        Export-Certificate -Cert $cert -FilePath (Join-Path $OutDir "NoMercyRecompiled.cer") | Out-Null
        Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)"
    }
}
signtool sign /fd SHA256 /f $Pfx /p $PfxPassword $msix
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The app CRT framework package the manifest depends on (ships with Visual Studio's C++ UWP tools).
$vclibs = Get-ChildItem -Recurse "${env:ProgramFiles(x86)}\Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0" `
    -Filter "Microsoft.VCLibs.x64.14.00.appx" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match '\\Retail\\' } | Select-Object -First 1
if ($vclibs) {
    New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "Dependencies") | Out-Null
    Copy-Item $vclibs.FullName (Join-Path $OutDir "Dependencies")
}
else {
    Write-Warning "Microsoft.VCLibs.x64.14.00.appx not found (install VS's 'C++ Universal Windows Platform tools'); add it as a dependency by hand."
}

Remove-Item -Recurse -Force $stage
Write-Host "MSIX: $(Resolve-Path $msix)"
