[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$InstallerPath,
  [string]$InstallRoot = "",
  [string]$DataRoot = "",
  [string]$LogPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-InstalledFile([string]$Path) {
  if (-not (Test-Path $Path)) {
    throw "Expected installed file or directory is missing: $Path"
  }
}

$installerFull = (Resolve-Path $InstallerPath).Path
$runId = [System.Guid]::NewGuid().ToString("N")
if (-not $InstallRoot) {
  $InstallRoot = Join-Path ([System.IO.Path]::GetTempPath()) "FlyingRcInstall-$runId"
}
if (-not $DataRoot) {
  $DataRoot = Join-Path ([System.IO.Path]::GetTempPath()) "FlyingRcData-$runId"
}
if (-not $LogPath) {
  $LogPath = Join-Path ([System.IO.Path]::GetTempPath()) "FlyingRcInstall-$runId.log"
}

$process = Start-Process `
  -FilePath $installerFull `
  -ArgumentList @(
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/DIR=$InstallRoot",
    "/FLYINGDATAROOT=$DataRoot",
    "/LOG=$LogPath"
  ) `
  -Wait `
  -PassThru

if ($process.ExitCode -ne 0) {
  throw "Installer failed with exit code $($process.ExitCode). See $LogPath"
}

Assert-InstalledFile (Join-Path $InstallRoot "Flying.exe")
Assert-InstalledFile (Join-Path $InstallRoot "FlyingReleaseManifest.json")
Assert-InstalledFile (Join-Path $InstallRoot "Packaging\update-repair.ps1")
Assert-InstalledFile (Join-Path $DataRoot "ceska-trebova-pilot-region.json")
Assert-InstalledFile (Join-Path $DataRoot "Terrain")

Write-Host "validate_step28_clean_install: ok"
