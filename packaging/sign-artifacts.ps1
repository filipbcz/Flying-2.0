[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$Root,
  [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
  [string]$TimestampUrl = "http://timestamp.digicert.com",
  [string[]]$Extensions = @(".exe", ".dll")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$signtool = Get-Command signtool.exe -ErrorAction Stop
$rootFull = (Resolve-Path $Root).Path
$targets = Get-ChildItem -Path $rootFull -Recurse -File |
  Where-Object { $Extensions -contains $_.Extension.ToLowerInvariant() } |
  Sort-Object FullName

if ($targets.Count -eq 0) {
  throw "No signable artifacts were found under $rootFull"
}

foreach ($target in $targets) {
  & $signtool.Source sign /fd SHA256 /sha1 $CertificateThumbprint /tr $TimestampUrl /td SHA256 $target.FullName
  if ($LASTEXITCODE -ne 0) { throw "signtool failed for $($target.FullName)" }
}
