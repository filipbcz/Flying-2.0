[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$InstallRoot,
  [string]$ManifestPath = "",
  [string]$PackageSource = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Sha256([string]$Path) {
  return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

$installRootFull = (Resolve-Path $InstallRoot).Path
if (-not $ManifestPath) {
  $ManifestPath = Join-Path $installRootFull "FlyingReleaseManifest.json"
}
if (-not (Test-Path $ManifestPath)) { throw "Release manifest not found: $ManifestPath" }

$manifest = Get-Content -Raw -Path $ManifestPath | ConvertFrom-Json
if ($manifest.schema -ne "flying.release-manifest.v1") {
  throw "Unsupported release manifest schema: $($manifest.schema)"
}

$sourceRoot = if ($PackageSource) { (Resolve-Path $PackageSource).Path } else { $installRootFull }
$repaired = 0
$failed = @()

foreach ($file in $manifest.files) {
  $relative = $file.path -replace "/", "\"
  $installed = Join-Path $installRootFull $relative
  $valid = (Test-Path $installed) -and ((Get-Sha256 $installed) -eq $file.sha256)
  if ($valid) { continue }

  $source = Join-Path $sourceRoot $relative
  if ((Test-Path $source) -and ((Get-Sha256 $source) -eq $file.sha256)) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $installed) | Out-Null
    Copy-Item -Force $source $installed
    $repaired += 1
  } else {
    $failed += $file.path
  }
}

if ($failed.Count -gt 0) {
  $failed | ForEach-Object { Write-Error "Missing or corrupt with no valid local repair source: $_" }
  exit 2
}

Write-Host "Flying repair complete. Repaired files: $repaired"
