<#
.SYNOPSIS
Assembles the release ZIP of the Portal Reloaded VR mod.

.DESCRIPTION
Combines the build output of the solution with everything else a release needs. The ZIP root is
the Portal Reloaded game folder, so players extract it there:

  bin\d3d9.dll, bin\openvr_api.dll               build output, thirdparty\openvr
  VR\                                            L4D2VR\config.txt, manifest, capsule, SteamVRActionManifest\
  Uninstall_Portal_Reloaded_VR_Mod.cmd           package-files\
  portal2_dlc3\                                  -PrivateFiles (modded portal gun models + sound cache)

Everything except portal2_dlc3\ is taken from the repository; the DLL must be built first
(RelWithDebInfo and Debug builds open a console and write l4d2vr_debug.log, so players get
Release). portal2_dlc3\ holds models derived from Valve's assets and is deliberately not in the
public repository: it comes from a private folder (a checkout of the private repository) whose
portal2_dlc3\ is copied into the ZIP root (pak01_dir.vpk, UpdateSoundCache.cmd,
maps\soundcache\_master.cache); nothing else of that folder (.git, README) goes into the ZIP.
Without it the portal gun is shown at the original model's position.
*.cmd files and config.txt are converted to CRLF, whatever the checkout looks like.

.PARAMETER Configuration
Build configuration to package (default Release).

.PARAMETER Version
Used in the ZIP name (default: git describe).

.PARAMETER IncludePdb
Also put bin\d3d9.pdb into the ZIP.

.PARAMETER PrivateFiles
Folder with the files that must not be in the public repository; its portal2_dlc3\ is copied into
the ZIP root. Default: a folder portal_reloaded_vr_private next to the repository, if it exists.
In GitHub CI this is a checkout of the private repository.

.EXAMPLE
tools\package-release.ps1 -Configuration Release -Version 0.2.0p1-reloaded.4
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo', 'Debug')][string]$Configuration = 'Release',
    [string]$Version = '',
    [switch]$IncludePdb,
    [string]$PrivateFiles = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Version) {
    $Version = (git -C $root describe --tags --always --dirty 2>$null)
    if (-not $Version) { $Version = 'dev' }
}

$dll = Join-Path $root "$Configuration\d3d9.dll"
if (-not (Test-Path $dll)) {
    throw "$dll not found. Build the $Configuration configuration first (x86)."
}

$name = "PortalReloadedVR-$Version"
if ($Configuration -ne 'Release') { $name += "-$Configuration" }
$stage = Join-Path $root "dist\$name"
$zip = Join-Path $root "dist\$name.zip"

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
if (Test-Path $zip) { Remove-Item -Force $zip }
New-Item -ItemType Directory -Force (Join-Path $stage 'bin'), (Join-Path $stage 'VR') | Out-Null

Copy-Item $dll (Join-Path $stage 'bin')
if ($IncludePdb) { Copy-Item (Join-Path $root "$Configuration\d3d9.pdb") (Join-Path $stage 'bin') }
Copy-Item (Join-Path $root 'thirdparty\openvr\bin\win32\openvr_api.dll') (Join-Path $stage 'bin')

foreach ($f in 'config.txt', 'manifest.vrmanifest', 'portal_reloaded_vr_capsule_main.png') {
    Copy-Item (Join-Path $root "L4D2VR\$f") (Join-Path $stage 'VR')
}
Copy-Item -Recurse (Join-Path $root 'L4D2VR\SteamVRActionManifest') (Join-Path $stage 'VR')

Copy-Item -Recurse (Join-Path $root 'package-files\*') $stage

if (-not $PrivateFiles) {
    $default = Join-Path (Split-Path -Parent $root) 'portal_reloaded_vr_private'
    if (Test-Path $default) { $PrivateFiles = $default }
}
if ($PrivateFiles) {
    $dlc = Join-Path $PrivateFiles 'portal2_dlc3'
    if (-not (Test-Path $dlc)) { throw "$dlc not found." }
    Copy-Item -Recurse $dlc $stage
} else {
    Write-Warning 'No -PrivateFiles folder: the ZIP has no portal2_dlc3 (modded portal gun models).'
}

# cmd.exe breaks on LF-only scripts and players edit config.txt with Notepad
Get-ChildItem $stage -Recurse -File | Where-Object { $_.Extension -eq '.cmd' -or $_.Name -eq 'config.txt' } | ForEach-Object {
    $text = [IO.File]::ReadAllText($_.FullName)
    [IO.File]::WriteAllText($_.FullName, ($text -replace "`r?`n", "`r`n"))
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)

$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try { $archive.Entries | Where-Object { $_.Length -gt 0 } | ForEach-Object { '{0,10}  {1}' -f $_.Length, $_.FullName } }
finally { $archive.Dispose() }
"-> $zip"
