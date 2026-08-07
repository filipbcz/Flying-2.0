# Win64 Release Packaging

This directory contains the reproducible Windows 11 x64 Shipping packaging
flow for Flying. The pipeline is offline-first: packaged terrain archives are
bundled beside the installer and installed into `Saved/Flying/PilotRegion` so
normal Czech VFR flight runs without requiring network access.

## Required tools

- Unreal Engine 5.8 with `RunUAT.bat` available.
- Windows SDK signing tools (`signtool.exe`) for signed releases.
- Inno Setup 6 (`ISCC.exe`) for installer creation.
- Prebuilt Czech terrain packages in the release terrain directory:
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
  -TerrainRoot "D:\FlyingTerrain\CzechRepublic" `
  -CertificateThumbprint "<code-signing-thumbprint>" `
  -TimestampUrl "http://timestamp.digicert.com"
```

The build script writes `unreal/Config/FlyingBuildMetadata.json`, invokes
Unreal Automation Tool with `BuildCookRun -targetplatform=Win64 -clientconfig=Shipping`,
copies required runtime content and terrain packages to `artifacts/win64/<BuildId>`,
signs application binaries, compiles the installer, signs it, and emits a
release manifest with SHA-256 hashes.

If signing parameters are omitted, the script stops before publishing the
installer as a signed release. Unsigned artifacts are only valid for local
engineering runs.

## Installer

`FlyingInstaller.iss` installs:

- Win64 Shipping simulator binaries and cooked content.
- Required runtime metadata, including `FlyingBuildMetadata.json`.
- The selected Czech terrain packages: terrain elevation, imagery/vector GIS,
  and offline navigation map data.
- Start menu entries for Flying and the offline repair tool.

The installer does not configure Steam, store publishing, mandatory online
updates, or cloud telemetry.

## Update And Repair

`update-repair.ps1` verifies the installed release manifest and restores missing
or corrupted simulator/terrain files from a local package cache or removable
media. It never downloads data by default. A network source may only be supplied
explicitly by the operator and is not required for normal flight.

## Crash Diagnostics And Privacy

Shipping builds include Unreal CrashReportClient for local minidump capture.
Flying registers build metadata into crash context and writes
`Saved/Flying/Diagnostics/structured-log.jsonl` with schema
`flying.structured-log.v1` with no personal telemetry. `DefaultEngine.ini`
disables implicit crash upload, contact prompts, and log upload. Crash telemetry remains off unless
`bCrashTelemetryOptIn=True` is explicitly recorded in game config.
