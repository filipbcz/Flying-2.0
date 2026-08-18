[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Root,
  [string[]]$Extensions = @(".exe", ".dll"),
  [string]$ExpectedThumbprint = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rootFull = (Resolve-Path $Root).Path
$normalizedExtensions = @($Extensions | ForEach-Object { $_.ToLowerInvariant() })
$targets = Get-ChildItem -Path $rootFull -Recurse -File |
  Where-Object { $normalizedExtensions -contains $_.Extension.ToLowerInvariant() } |
  Sort-Object FullName

if ($targets.Count -eq 0) {
  throw "No signed artifacts were found under $rootFull"
}

$failures = @()
foreach ($target in $targets) {
  $signature = Get-AuthenticodeSignature -FilePath $target.FullName
  if ($signature.Status -ne "Valid") {
    $failures += "$($target.FullName): signature status $($signature.Status)"
    continue
  }

  if ($ExpectedThumbprint) {
    $actualThumbprint = $signature.SignerCertificate.Thumbprint
    if ($actualThumbprint -ne $ExpectedThumbprint) {
      $failures += "$($target.FullName): signed by $actualThumbprint, expected $ExpectedThumbprint"
    }
  }
}

if ($failures.Count -gt 0) {
  $failures | ForEach-Object { Write-Error $_ }
  exit 3
}

Write-Host "Flying signature verification complete. Verified files: $($targets.Count)"
