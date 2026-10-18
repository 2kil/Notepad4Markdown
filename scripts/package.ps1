#Requires -Version 5.1
<#
.SYNOPSIS
    Package the built Notepad4.exe into a distributable zip.

.DESCRIPTION
    The executable is self-contained: preview assets (HTML/CSS/JS) and the
    Simplified Chinese UI resources are compiled in, so the archive holds a
    single .exe plus documentation and license texts.

    Used by .github/workflows/build.yml and usable locally:

        powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -Version v1.2.0

.PARAMETER Version
    Version string used in the archive name. Defaults to "dev".
#>
param(
    [string]$Version = "dev",
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) {
    $OutDir = Join-Path $root "dist"
}

$exe = Join-Path $root "notepad4\build\bin\$Configuration\$Platform\Notepad4.exe"
if (-not (Test-Path $exe)) {
    throw "Notepad4.exe not found: $exe`nRun scripts\build.cmd (or the CI build step) first."
}

# Version strings must be filesystem-safe.
$safeVersion = ($Version -replace '[^0-9A-Za-z._-]', '_')
$name = "Notepad4Markdown-$safeVersion-$Platform"
$stage = Join-Path $root "build\dist\$name"

if (Test-Path $stage) {
    Remove-Item $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item $exe (Join-Path $stage "Notepad4.exe")

foreach ($doc in @("README.md", "LICENSE", "LICENSE-THIRD-PARTY.md")) {
    $path = Join-Path $root $doc
    if (Test-Path $path) {
        Copy-Item $path $stage
    }
}

# Upstream and Scintilla licenses are required for binary redistribution.
$upstreamLicense = Join-Path $root "notepad4\License.txt"
if (Test-Path $upstreamLicense) {
    Copy-Item $upstreamLicense (Join-Path $stage "License-Notepad4.txt")
}
$scintillaLicense = Join-Path $root "notepad4\scintilla\License.txt"
if (Test-Path $scintillaLicense) {
    Copy-Item $scintillaLicense (Join-Path $stage "License-Scintilla.txt")
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$zip = Join-Path $OutDir "$name.zip"
if (Test-Path $zip) {
    Remove-Item $zip -Force
}
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -CompressionLevel Optimal

$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
$hashFile = "$zip.sha256"
"$hash  $(Split-Path -Leaf $zip)" | Set-Content -Path $hashFile -Encoding ascii

Write-Host "[package] $zip"
Write-Host "[package] sha256 $hash"
Write-Host "[package] size   $((Get-Item $zip).Length) bytes"
