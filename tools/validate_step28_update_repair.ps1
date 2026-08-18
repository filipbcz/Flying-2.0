[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-Sha256([string]$Path) {
  return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

function Assert-FileHash([string]$Path, [string]$ExpectedHash) {
  if (-not (Test-Path $Path)) {
    throw "Expected repaired file is missing: $Path"
  }
  $actual = Get-Sha256 $Path
  if ($actual -ne $ExpectedHash) {
    throw "Expected $Path to have hash $ExpectedHash, got $actual"
  }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repairScript = Join-Path $repoRoot "packaging\update-repair.ps1"
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("flying-step28-update-repair-" + [System.Guid]::NewGuid().ToString("N"))

try {
  $installRoot = Join-Path $tempRoot "installed-app"
  $dataRoot = Join-Path $tempRoot "installed-data"
  $sourceRoot = Join-Path $tempRoot "release-media"
  $releaseRegionRoot = Join-Path $sourceRoot "Flying\Saved\Flying\PilotRegion"
  New-Item -ItemType Directory -Force -Path $installRoot,$dataRoot,$releaseRegionRoot | Out-Null

  $regionManifest = @'
{
  "schemaVersion": "flying.region-manifest.v1",
  "regionId": "ceska-trebova-pilot-10km",
  "runtimeCompatibility": {
    "runtimeNetworkRequired": false
  }
}
'@
  $payloads = @(
    @{ Path = "Flying/Saved/Flying/PilotRegion/ceska-trebova-pilot-region.json"; Content = $regionManifest },
    @{ Path = "Flying/Saved/Flying/PilotRegion/Terrain/elevation.bin"; Content = "terrain-elevation-pilot-package`n" },
    @{ Path = "Flying/Saved/Flying/PilotRegion/GIS/vector.bin"; Content = "gis-vector-pilot-package`n" },
    @{ Path = "Flying/Saved/Flying/PilotRegion/Navigation/map.bin"; Content = "navigation-pilot-package`n" }
  )

  $manifestFiles = @()
  foreach ($payload in $payloads) {
    $sourcePath = Join-Path $sourceRoot ($payload.Path -replace "/", "\")
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $sourcePath) | Out-Null
    Set-Content -Encoding UTF8 -Path $sourcePath -Value $payload.Content
    $manifestFiles += [ordered]@{
      path = $payload.Path
      size = (Get-Item $sourcePath).Length
      sha256 = Get-Sha256 $sourcePath
    }
  }

  New-Item -ItemType Directory -Force -Path (Join-Path $dataRoot "Terrain") | Out-Null
  Set-Content -Encoding UTF8 -Path (Join-Path $dataRoot "Terrain\elevation.bin") -Value "corrupt terrain`n"

  $releaseManifest = [ordered]@{
    schema = "flying.release-manifest.v1"
    buildId = "step28-update-repair-selftest"
    version = "1.0.0-test"
    files = $manifestFiles
  }
  $manifestPath = Join-Path $installRoot "FlyingReleaseManifest.json"
  $releaseManifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -Path $manifestPath

  $preflight = Join-Path $tempRoot "offline-preflight.cmd"
  @'
@echo off
set args=%*
echo %args% | findstr /C:"--offline-preflight" >nul
if errorlevel 1 exit /b 31
echo %args% | findstr /C:"--no-network" >nul
if errorlevel 1 exit /b 32
echo %args% | findstr /C:"--data-root" >nul
if errorlevel 1 exit /b 33
echo %args% | findstr /C:"--region-manifest" >nul
if errorlevel 1 exit /b 34
if "%FLYING_DATA_ROOT%"=="" exit /b 35
echo offline-ok>"%FLYING_DATA_ROOT%\offline-preflight.marker"
exit /b 0
'@ | Set-Content -Encoding ASCII -Path $preflight

  & $repairScript `
    -InstallRoot $installRoot `
    -DataRoot $dataRoot `
    -PackageSource $sourceRoot `
    -RequireOfflineLaunch `
    -OfflinePreflightExecutable $preflight
  if ($LASTEXITCODE -ne 0) {
    throw "update-repair.ps1 failed with exit code $LASTEXITCODE"
  }

  foreach ($file in $manifestFiles) {
    $relative = ($file.path -replace "Flying/Saved/Flying/PilotRegion/", "") -replace "/", "\"
    Assert-FileHash (Join-Path $dataRoot $relative) $file.sha256
  }
  if (-not (Test-Path (Join-Path $dataRoot "offline-preflight.marker"))) {
    throw "Offline preflight was not executed against the configured data root."
  }

  Write-Host "validate_step28_update_repair: ok"
} finally {
  if (Test-Path $tempRoot) {
    Remove-Item -Recurse -Force $tempRoot
  }
}
