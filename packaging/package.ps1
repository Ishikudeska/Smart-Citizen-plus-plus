<#
.SYNOPSIS
    Builds the release artifacts into dist/:
      dist/SCX/                       installed-build folder (cmake --install)
      dist/SCX-<ver>-Setup.exe        Inno Setup installer (when ISCC is found)
      dist/SCX-Portable/              portable build folder
      dist/SCX-Portable-<ver>.zip     portable zip

.DESCRIPTION
    Uses the mingw-release and mingw-release-portable CMake presets, so the
    Qt/MinGW paths come from CMakePresets.json. Qt is deployed as DLLs by
    windeployqt (through cmake --install). Run from any directory:

        powershell -ExecutionPolicy Bypass -File packaging\package.ps1

.PARAMETER SkipPortable
    Only build the installed variant.
.PARAMETER SkipInstaller
    Do not run Inno Setup even if it is installed.
.PARAMETER Iscc
    Path to ISCC.exe (default: Inno Setup 6 in Program Files, or on PATH).
#>
param(
    [switch]$SkipPortable,
    [switch]$SkipInstaller,
    [string]$Iscc = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root "dist"

function Invoke-Checked([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe $($arguments -join ' ') failed with exit code $LASTEXITCODE" }
}

# The version is defined once, in cmake/AppIdentity.cmake.
$identity = Get-Content (Join-Path $root "cmake\AppIdentity.cmake") -Raw
if ($identity -notmatch 'set\(APP_VERSION\s+"([^"]+)"\)') { throw "APP_VERSION not found in cmake/AppIdentity.cmake" }
$version = $Matches[1]
if ($identity -notmatch 'set\(APP_EXE_NAME\s+"([^"]+)"\)') { throw "APP_EXE_NAME not found in cmake/AppIdentity.cmake" }
$name = $Matches[1]
Write-Host "Packaging $name $version"

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue)
if ($cmake) { $cmake = $cmake.Source } else { $cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe" }
if (-not (Test-Path $cmake)) { throw "cmake not found (on PATH or C:\Qt\Tools\CMake_64)" }
# windeployqt copies the MinGW runtime it finds on PATH.
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.12.0\mingw_64\bin;C:\Qt\Tools\Ninja;$env:PATH"

New-Item -ItemType Directory -Force $dist | Out-Null

function Build-Variant([string]$preset, [string]$folder) {
    $out = Join-Path $dist $folder
    if (Test-Path $out) { Remove-Item -Recurse -Force $out }
    Push-Location $root
    try {
        Invoke-Checked $cmake @("--preset", $preset)
        Invoke-Checked $cmake @("--build", "--preset", $preset, "--target", "scapp")
        Invoke-Checked $cmake @("--install", "build\$preset", "--prefix", $out)
    } finally {
        Pop-Location
    }
    if (-not (Test-Path (Join-Path $out "$name.exe"))) { throw "$name.exe missing from $out" }
    return $out
}

# ── installed build + installer ──
$installed = Build-Variant "mingw-release" $name
Write-Host "Installed build: $installed"

if (-not $SkipInstaller) {
    if (-not $Iscc) {
        foreach ($candidate in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
                                 "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
            if (Test-Path $candidate) { $Iscc = $candidate; break }
        }
        if (-not $Iscc) {
            $onPath = Get-Command iscc -ErrorAction SilentlyContinue
            if ($onPath) { $Iscc = $onPath.Source }
        }
    }
    if ($Iscc) {
        Invoke-Checked $Iscc @("/Q", "/DAppVersion=$version", "/DSourceDir=$installed", "/DOutputDir=$dist",
                               (Join-Path $PSScriptRoot "installer.iss"))
        Write-Host "Installer: $(Join-Path $dist "$name-$version-Setup.exe")"
    } else {
        Write-Warning "Inno Setup 6 (ISCC.exe) not found; skipping the installer. Install it from https://jrsoftware.org/isinfo.php or pass -Iscc."
    }
}

# ── portable build + zip ──
if (-not $SkipPortable) {
    $portable = Build-Variant "mingw-release-portable" "$name-Portable"
    $zip = Join-Path $dist "$name-Portable-$version.zip"
    if (Test-Path $zip) { Remove-Item -Force $zip }
    # Zip the folder itself, so it unpacks into one directory.
    Compress-Archive -Path $portable -DestinationPath $zip -CompressionLevel Optimal
    Write-Host "Portable zip: $zip"
}
