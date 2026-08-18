[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$EngineRoot,
  [Parameter(Mandatory=$true)][string]$BuildId,
  [Parameter(Mandatory=$true)][string]$Version,
  [Parameter(Mandatory=$true)][string]$Commit,
  [Parameter(Mandatory=$true)][string]$TerrainRoot,
  [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
  [string]$Channel = "shipping",
  [string]$OutputRoot = "artifacts/win64",
  [string]$TimestampUrl = "http://timestamp.digicert.com",
  [string]$InnoSetupCompiler = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$Path) {
  if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
  return Join-Path $PSScriptRoot ".." $Path
}

function Get-Sha256([string]$Path) {
  return (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLowerInvariant()
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$uat = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
$uproject = Join-Path $repoRoot "unreal\Flying.uproject"
$stageRoot = Join-Path $repoRoot (Join-Path $OutputRoot $BuildId)
$archiveRoot = Join-Path $stageRoot "Package"
$installerOut = Join-Path $stageRoot "Installer"
$metadataPath = Join-Path $repoRoot "unreal\Config\FlyingBuildMetadata.json"
$terrainRootFull = Resolve-RepoPath $TerrainRoot

if (-not (Test-Path $uat)) { throw "RunUAT.bat was not found at $uat" }
if (-not (Test-Path $uproject)) { throw "Flying.uproject was not found at $uproject" }
if (-not (Test-Path $terrainRootFull)) { throw "TerrainRoot was not found at $terrainRootFull" }

New-Item -ItemType Directory -Force -Path $stageRoot,$archiveRoot,$installerOut | Out-Null

$metadata = [ordered]@{
  schema = "flying.build-metadata.v1"
  buildId = $BuildId
  version = $Version
  channel = $Channel
  commit = $Commit
  builtAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
  platform = "Win64"
  configuration = "Shipping"
}
$metadata | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 -Path $metadataPath

& $uat BuildCookRun `
  -project="$uproject" `
  -noP4 `
  -platform=Win64 `
  -targetplatform=Win64 `
  -clientconfig=Shipping `
  -build `
  -cook `
  -stage `
  -pak `
  -iostore `
  -compressed `
  -prereqs `
  -archive `
  -archivedirectory="$archiveRoot" `
  -CrashReporter `
  -utf8output
if ($LASTEXITCODE -ne 0) { throw "Unreal BuildCookRun failed with exit code $LASTEXITCODE" }

$runtimeConfig = Join-Path $archiveRoot "Windows\Flying\Config"
New-Item -ItemType Directory -Force -Path $runtimeConfig | Out-Null
Copy-Item -Force $metadataPath (Join-Path $runtimeConfig "FlyingBuildMetadata.json")

$terrainInstallRoot = Join-Path $archiveRoot "Windows\Flying\Saved\Flying\PilotRegion"
New-Item -ItemType Directory -Force -Path $terrainInstallRoot | Out-Null
Copy-Item -Recurse -Force (Join-Path $terrainRootFull "*") $terrainInstallRoot

& (Join-Path $PSScriptRoot "sign-artifacts.ps1") `
  -Root (Join-Path $archiveRoot "Windows") `
  -CertificateThumbprint $CertificateThumbprint `
  -TimestampUrl $TimestampUrl
if ($LASTEXITCODE -ne 0) { throw "Binary signing failed with exit code $LASTEXITCODE" }

& (Join-Path $PSScriptRoot "verify-signatures.ps1") `
  -Root (Join-Path $archiveRoot "Windows") `
  -ExpectedThumbprint $CertificateThumbprint
if ($LASTEXITCODE -ne 0) { throw "Binary signature verification failed with exit code $LASTEXITCODE" }

$releaseRoot = Join-Path $archiveRoot "Windows"
$files = Get-ChildItem -Path $releaseRoot -Recurse -File | Sort-Object FullName
$manifest = [ordered]@{
  schema = "flying.release-manifest.v1"
  buildId = $BuildId
  version = $Version
  channel = $Channel
  commit = $Commit
  createdAtUtc = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
  files = @($files | ForEach-Object {
    [ordered]@{
      path = $_.FullName.Substring($releaseRoot.Length + 1).Replace("\", "/")
      size = $_.Length
      sha256 = Get-Sha256 $_.FullName
    }
  })
}
$manifestPath = Join-Path $stageRoot "FlyingReleaseManifest.json"
$manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -Path $manifestPath
Copy-Item -Force $manifestPath (Join-Path $archiveRoot "Windows\FlyingReleaseManifest.json")

if (-not (Test-Path $InnoSetupCompiler)) { throw "ISCC.exe was not found at $InnoSetupCompiler" }
& $InnoSetupCompiler `
  /DSourceDir="$($archiveRoot)\Windows" `
  /DOutputDir="$installerOut" `
  /DAppVersion="$Version" `
  /DBuildId="$BuildId" `
  (Join-Path $PSScriptRoot "FlyingInstaller.iss")
if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed with exit code $LASTEXITCODE" }

& (Join-Path $PSScriptRoot "sign-artifacts.ps1") `
  -Root $installerOut `
  -CertificateThumbprint $CertificateThumbprint `
  -TimestampUrl $TimestampUrl `
  -Extensions ".exe"
if ($LASTEXITCODE -ne 0) { throw "Installer signing failed with exit code $LASTEXITCODE" }

& (Join-Path $PSScriptRoot "verify-signatures.ps1") `
  -Root $installerOut `
  -Extensions ".exe" `
  -ExpectedThumbprint $CertificateThumbprint
if ($LASTEXITCODE -ne 0) { throw "Installer signature verification failed with exit code $LASTEXITCODE" }

Write-Host "Flying Win64 Shipping release staged at $stageRoot"
