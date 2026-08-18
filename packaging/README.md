# Win64 Release Packaging

This directory contains the reproducible Windows 11 x64 Shipping packaging
flow for Flying. The pipeline is offline-first: packaged terrain archives are
bundled beside the installer and installed under the configured
`FLYING_DATA_ROOT` regional data root so normal regional Czech VFR flight runs
without requiring network access.

## Required tools

- Unreal Engine 5.8 with `RunUAT.bat` available.
- Windows SDK signing tools (`signtool.exe`) for signed releases.
- Inno Setup 6 (`ISCC.exe`) for installer creation.
- Prebuilt regional terrain packages in the release terrain directory:
  - `Terrain/flying-cz-terrain.zip`
  - `GIS/flying-cz-gis.zip`
  - `Navigation/flying-cz-navigation.zip`

## Reproducible Shipping Build

Run from a Windows PowerShell prompt at the repository root:

```powershell
.\packaging\build-win64-shipping.ps1 `
  -EngineRoot "C:\Program Files\Epic Games\UE_5.8" `
  -BuildId "flying-1.0.0+20260807.1" `
  -Version "1.0.0" `
  -Commit "0000000000000000000000000000000000000000" `
  -TerrainRoot "D:\FlyingTerrain\CeskaTrebovaPilot" `
  -CertificateThumbprint "<code-signing-thumbprint>" `
  -TimestampUrl "http://timestamp.digicert.com"
```

The build script writes `unreal/Config/FlyingBuildMetadata.json`, invokes
Unreal Automation Tool with `BuildCookRun -targetplatform=Win64 -clientconfig=Shipping`,
copies required runtime content and terrain packages to `artifacts/win64/<BuildId>`,
signs application binaries, compiles the installer, signs it, and emits a
release manifest with SHA-256 hashes. The packaging flow verifies Authenticode
signatures after signing the staged binaries and again after signing the
installer; missing, invalid, or thumbprint-mismatched signatures fail the
release-candidate gate.

The release-candidate packaging command requires `-CertificateThumbprint`.
Unsigned artifacts are not valid release-candidate evidence.

## Installer

`FlyingInstaller.iss` installs:

- Win64 Shipping simulator binaries and cooked content.
- Required runtime metadata, including `FlyingBuildMetadata.json`.
- The selected regional terrain packages: terrain elevation, imagery/vector
  GIS, and offline navigation map data under the selected `FLYING_DATA_ROOT`.
- The installed region manifest, which records package bounds, source margin,
  data-root location and offline runtime compatibility.
- Start menu entries for Flying and the offline repair tool.

The installer does not configure Steam, store publishing, mandatory online
updates, or cloud telemetry.

## Update And Repair

`update-repair.ps1` verifies the installed release manifest, configured
`FLYING_DATA_ROOT`, installed region manifest and offline-launch compatibility,
then restores missing or corrupted simulator/terrain files from a local package
cache or removable media. It never downloads data by default. A network source
may only be supplied explicitly by the operator and is not required for normal
flight.

## Crash Diagnostics And Privacy

Shipping builds include Unreal CrashReportClient for local minidump capture.
Flying registers build metadata into crash context and writes
`Saved/Flying/Diagnostics/structured-log.jsonl` with schema
`flying.structured-log.v1` with no personal telemetry. `DefaultEngine.ini`
disables implicit crash upload, contact prompts, and log upload. Crash telemetry remains off unless
`bCrashTelemetryOptIn=True` is explicitly recorded in game config.
