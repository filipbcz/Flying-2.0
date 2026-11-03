param(
    [string]$UnrealRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string]$EvidenceDirectory = 'docs/validation/editor-baseline'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location $repo
$output = Join-Path $repo $EvidenceDirectory
New-Item -ItemType Directory -Force $output | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
function Save-Json($name, $value) {
    [IO.File]::WriteAllText((Join-Path $output $name), (($value | ConvertTo-Json -Depth 16) + [Environment]::NewLine), $utf8)
}
$baseCommit = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify base commit' }
$origin = (& git remote get-url origin).Trim()
if ($origin -notmatch '(?i)github.com[:/]filipbcz/Flying-2\.0(?:\.git)?$') { throw "Unexpected repository: $origin" }
$identity = [ordered]@{
    repository = $origin
    baseCommit = $baseCommit
    checkout = $repo
    workerLeaseId = Split-Path $repo -Leaf
    host = [Environment]::MachineName
    windowsSessionId = (Get-Process -Id $PID).SessionId
    project = 'unreal/Flying.uproject'
}
$checkpointPath = Join-Path $output 'checkpoint.json'
$completed = @()
if (Test-Path $checkpointPath) {
    $previous = Get-Content $checkpointPath -Raw | ConvertFrom-Json
    if ($previous.identity.baseCommit -ne $baseCommit -or $previous.identity.workerLeaseId -ne $identity.workerLeaseId) {
        throw 'Checkpoint belongs to a different checkout/session; preserve it before beginning a new acceptance run'
    }
    $completed = @($previous.completedSteps)
}
try {
    $versionFile = Join-Path $UnrealRoot 'Engine/Build/Build.version'
    $editor = Join-Path $UnrealRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    if (!(Test-Path $versionFile) -or !(Test-Path $editor)) { throw 'CAPABILITY: Unreal installation unavailable' }
    $version = Get-Content $versionFile -Raw | ConvertFrom-Json
    if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8 -or $version.PatchVersion -lt 2) {
        throw 'CAPABILITY: UE 5.8 patch >= 2 required'
    }
    $plugin = Get-ChildItem (Join-Path $UnrealRoot 'Engine/Plugins') -Filter CesiumForUnreal.uplugin -Recurse | Select-Object -First 1
    if (!$plugin) { throw 'CAPABILITY: Cesium unavailable' }
    $cesium = Get-Content $plugin.FullName -Raw | ConvertFrom-Json
    if ([version]$cesium.VersionName -lt [version]'2.28.0') { throw 'CAPABILITY: Cesium 2.28+ required' }
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (!(Test-Path $vswhere)) { throw 'CAPABILITY: Visual Studio locator unavailable' }
    $vs = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)[0]
    if (!$vs) { throw 'CAPABILITY: MSVC unavailable' }
    $compilers = @(Get-ChildItem (Join-Path $vs.installationPath 'VC/Tools/MSVC') -Directory | ForEach-Object {
        $cl = Join-Path $_.FullName 'bin/Hostx64/x64/cl.exe'
        if (Test-Path $cl) {
            [ordered]@{ toolset = $_.Name; executable = $cl; fileVersion = (Get-Item $cl).VersionInfo.FileVersion }
        }
    })
    $sdkRoot = "${env:ProgramFiles(x86)}/Windows Kits/10"
    $sdks = @(Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Where-Object {
        Test-Path (Join-Path $sdkRoot ("Lib/" + $_.Name + '/um/x64/kernel32.lib'))
    } | ForEach-Object { $_.Name })
    if (!$compilers.Count -or !$sdks.Count) { throw 'CAPABILITY: Compiler or Windows SDK unavailable' }
    $os = Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion'
    $gpus = @(Get-ItemProperty 'HKLM:/SYSTEM/CurrentControlSet/Control/Video/*/0000' -ErrorAction SilentlyContinue |
        Where-Object { $_.PSObject.Properties['DriverDesc'] -and $_.DriverDesc } |
        ForEach-Object { [ordered]@{ name = $_.DriverDesc; driverVersion = $_.DriverVersion } })
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FlyingBaselineDesktop {
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll")] public static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool GetDiskFreeSpaceEx(string path, out ulong available, out ulong total, out ulong free);
}
'@
    $desktop = [FlyingBaselineDesktop]::OpenInputDesktop(0, $false, 1)
    $interactive = [Environment]::UserInteractive -and $desktop -ne [IntPtr]::Zero
    if ($desktop -ne [IntPtr]::Zero) { [void][FlyingBaselineDesktop]::CloseDesktop($desktop) }
    [UInt64]$available = 0
    [UInt64]$total = 0
    [UInt64]$free = 0
    if (![FlyingBaselineDesktop]::GetDiskFreeSpaceEx($repo, [ref]$available, [ref]$total, [ref]$free)) {
        throw "Disk space probe failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    if (!$interactive -or !$gpus.Count) { throw 'CAPABILITY: Interactive GPU session unavailable' }
    $record = [ordered]@{
        schemaVersion = 'flying.editor-baseline-preflight.v1'
        recordedAtUtc = [DateTime]::UtcNow.ToString('o')
        identity = $identity
        status = 'pass'
        unreal = [ordered]@{ root = $UnrealRoot; editor = $editor; buildVersion = $version; buildVersionSha256 = (Get-FileHash $versionFile -Algorithm SHA256).Hash.ToLowerInvariant() }
        cesium = [ordered]@{ descriptor = $plugin.FullName; version = $cesium.VersionName; engineVersion = $cesium.EngineVersion; sha256 = (Get-FileHash $plugin.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        toolchain = [ordered]@{ visualStudioVersion = $vs.installationVersion; compilers = $compilers; windowsSDKs = $sdks; selectionEvidence = 'build-result.json stdout records the compiler and SDK actually selected by UBT' }
        windows = [ordered]@{ build = $os.CurrentBuild; revision = $os.UBR; displayVersion = $os.DisplayVersion; version = [Environment]::OSVersion.Version.ToString() }
        capabilities = [ordered]@{ gpu = $gpus; interactiveDesktop = $interactive; probe = 'OpenInputDesktop + registry display adapter; actual RHI device is also recorded by the Editor log' }
        disk = [ordered]@{ path = $repo; availableBytes = $available; totalBytes = $total; freeBytes = $free }
    }
    Save-Json 'preflight.json' $record
    if ($completed -notcontains 'preflight') { $completed += 'preflight' }
    Save-Json 'checkpoint.json' ([ordered]@{
        schemaVersion = 'flying.editor-baseline-checkpoint.v1'; identity = $identity
        status = 'in_progress'; completedSteps = $completed; nextStep = 'build FlyingEditor using run_unreal_authoring'
        updatedAtUtc = [DateTime]::UtcNow.ToString('o'); reason = $null
    })
    $record | ConvertTo-Json -Depth 16
} catch {
    $state = if ($_.Exception.Message.StartsWith('CAPABILITY:')) { 'waiting_for_capability' } else { 'failed' }
    Save-Json 'checkpoint.json' ([ordered]@{
        schemaVersion = 'flying.editor-baseline-checkpoint.v1'; identity = $identity
        status = $state; completedSteps = $completed; nextStep = 'rerun preflight, then resume the first incomplete step'
        updatedAtUtc = [DateTime]::UtcNow.ToString('o'); reason = $_.Exception.Message
    })
    throw
}
