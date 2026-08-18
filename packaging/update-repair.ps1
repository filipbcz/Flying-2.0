[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$InstallRoot,
  [Parameter(Mandatory=$true)][string]$DataRoot,
  [string]$ManifestPath = "",
  [string]$PackageSource = "",
  [string]$OfflinePreflightExecutable = "",
  [switch]$RequireOfflineLaunch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Sha256([string]$Path) {
  return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Convert-ReleasePathToInstalledPath([string]$RelativePath) {
  $relative = $RelativePath -replace "/", "\"
  $regionalPrefix = "Flying\Saved\Flying\PilotRegion\"
  if ($relative.StartsWith($regionalPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    $regionalRelative = $relative.Substring($regionalPrefix.Length)
    return Join-Path $dataRootFull $regionalRelative
  }
  return Join-Path $installRootFull $relative
}

function Copy-VerifiedRepairSource([string]$Source, [string]$Installed) {
  $installedParent = Split-Path -Parent $Installed
  New-Item -ItemType Directory -Force -Path $installedParent | Out-Null
  Copy-Item -Force $Source $Installed
}

$installRootFull = (Resolve-Path $InstallRoot).Path
$dataRootFull = if (Test-Path $DataRoot) { (Resolve-Path $DataRoot).Path } else { $DataRoot }
if (-not $ManifestPath) {
  $ManifestPath = Join-Path $installRootFull "FlyingReleaseManifest.json"
}
if (-not (Test-Path $ManifestPath)) { throw "Release manifest not found: $ManifestPath" }
if (-not (Test-Path $dataRootFull)) { throw "Configured FLYING_DATA_ROOT data root not found: $dataRootFull" }

$manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json
if ($manifest.schema -ne "flying.release-manifest.v1") {
  throw "Unsupported release manifest schema: $($manifest.schema)"
}

$sourceRoot = if ($PackageSource) { (Resolve-Path $PackageSource).Path } else { $installRootFull }
$repaired = 0
$failed = @()

foreach ($file in $manifest.files) {
  $relative = $file.path -replace "/", "\"
  $installed = Convert-ReleasePathToInstalledPath $relative
  $valid = (Test-Path $installed) -and ((Get-Sha256 $installed) -eq $file.sha256)
  if ($valid) { continue }

  $source = Join-Path $sourceRoot $relative
  if ((Test-Path $source) -and ((Get-Sha256 $source) -eq $file.sha256)) {
    Copy-VerifiedRepairSource $source $installed
    $repaired += 1
  } else {
    $failed += $file.path
  }
}

if ($failed.Count -gt 0) {
  $failed | ForEach-Object { Write-Error "Missing or corrupt with no valid local repair source: $_" }
  exit 2
}

$regionManifestPath = Join-Path $dataRootFull "ceska-trebova-pilot-region.json"
if (-not (Test-Path $regionManifestPath)) { throw "Installed region manifest not found: $regionManifestPath" }
$regionManifest = Get-Content -Raw -Path $regionManifestPath | ConvertFrom-Json
if ($regionManifest.schemaVersion -ne "flying.region-manifest.v1") {
  throw "Unsupported region manifest schema: $($regionManifest.schemaVersion)"
}
if ($regionManifest.runtimeCompatibility.runtimeNetworkRequired -ne $false) {
  throw "Installed region package is not offline-launch compatible"
}

if ($RequireOfflineLaunch) {
  $env:FLYING_DATA_ROOT = $dataRootFull
  $flyingExe = if ($OfflinePreflightExecutable) { $OfflinePreflightExecutable } else { Join-Path $installRootFull "Flying.exe" }
  if (-not (Test-Path $flyingExe)) { throw "Offline launch preflight failed; executable not found: $flyingExe" }
  $preflight = Start-Process `
    -FilePath $flyingExe `
    -ArgumentList @("--offline-preflight", "--no-network", "--data-root", $dataRootFull, "--region-manifest", $regionManifestPath) `
    -Wait `
    -PassThru `
    -WindowStyle Hidden
  if ($preflight.ExitCode -ne 0) {
    throw "Offline launch preflight failed with exit code $($preflight.ExitCode)"
  }
}

Write-Host "Flying repair complete. Repaired files: $repaired"
