#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  console.error(`validate_step28_packaging: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  try {
    return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function requireToken(source, token, label) {
  if (!source.includes(token)) {
    fail(`${label} missing token: ${token}`);
  }
}

const packagingReadme = read("packaging/README.md");
const buildScript = read("packaging/build-win64-shipping.ps1");
const signScript = read("packaging/sign-artifacts.ps1");
const repairScript = read("packaging/update-repair.ps1");
const installer = read("packaging/FlyingInstaller.iss");
const buildMetadataExample = JSON.parse(read("packaging/FlyingBuildMetadata.example.json"));
const target = read("unreal/Source/Flying.Target.cs");
const defaultGame = read("unreal/Config/DefaultGame.ini");
const defaultEngine = read("unreal/Config/DefaultEngine.ini");
const buildMetadataHeader = read("unreal/Source/FlyingPresentation/Public/FlyingBuildMetadata.h");
const buildMetadataCpp = read("unreal/Source/FlyingPresentation/Private/FlyingBuildMetadata.cpp");
const diagnosticsHeader = read("unreal/Source/FlyingPresentation/Public/FlyingDiagnosticsWidget.h");
const diagnosticsCpp = read("unreal/Source/FlyingPresentation/Private/FlyingDiagnosticsWidget.cpp");
const moduleCpp = read("unreal/Source/FlyingPresentation/Private/FlyingPresentation.cpp");

for (const token of [
  "BuildCookRun",
  "-targetplatform=Win64",
  "-clientconfig=Shipping",
  "-CrashReporter",
  "FlyingBuildMetadata.json",
  "flying.release-manifest.v1",
  "Get-FileHash -Algorithm SHA256",
]) {
  requireToken(buildScript, token, "Win64 Shipping build script");
}

const signingIndex = buildScript.indexOf("Binary signing failed");
const manifestIndex = buildScript.indexOf('schema = "flying.release-manifest.v1"');
if (signingIndex === -1 || manifestIndex === -1 || signingIndex > manifestIndex) {
  fail("release manifest must be generated after application binary signing so hashes match installed signed files");
}
requireToken(
  buildScript,
  'Copy-Item -Force $manifestPath (Join-Path $archiveRoot "Windows\\FlyingReleaseManifest.json")',
  "release manifest archive placement",
);

for (const token of [
  "signtool.exe",
  "/fd SHA256",
  "/tr",
  "/td SHA256",
  ".exe",
  ".dll",
]) {
  requireToken(signScript, token, "code signing script");
}

for (const token of [
  "ArchitecturesAllowed=x64compatible",
  "Components",
  "terrain\\elevation",
  "terrain\\gis",
  "terrain\\navigation",
  "FlyingReleaseManifest.json",
  "update-repair.ps1",
]) {
  requireToken(installer, token, "installer");
}

for (const token of [
  'DestDir: "{app}\\Flying\\Saved\\Flying\\PilotRegion\\Terrain"',
  'DestDir: "{app}\\Flying\\Saved\\Flying\\PilotRegion\\GIS"',
  'DestDir: "{app}\\Flying\\Saved\\Flying\\PilotRegion\\Navigation"',
]) {
  requireToken(installer, token, "installer terrain package destination");
}
for (const invalidToken of [
  'DestDir: "{app}\\Saved\\Flying\\PilotRegion\\Terrain"',
  'DestDir: "{app}\\Saved\\Flying\\PilotRegion\\GIS"',
  'DestDir: "{app}\\Saved\\Flying\\PilotRegion\\Navigation"',
]) {
  if (installer.includes(invalidToken)) {
    fail(`installer terrain package destination must preserve archived Flying path, found: ${invalidToken}`);
  }
}

for (const token of [
  "flying.release-manifest.v1",
  "Get-FileHash -Algorithm SHA256",
  "PackageSource",
  "Missing or corrupt with no valid local repair source",
]) {
  requireToken(repairScript, token, "update and repair script");
}

if (buildMetadataExample.schema !== "flying.build-metadata.v1") {
  fail("build metadata example schema mismatch");
}
if (buildMetadataExample.platform !== "Win64" || buildMetadataExample.configuration !== "Shipping") {
  fail("build metadata example must identify Win64 Shipping");
}

for (const token of [
  "bUseCrashReportClient = true",
]) {
  requireToken(target, token, "Unreal target");
}

for (const token of [
  "bCrashTelemetryOptIn=False",
  "StructuredLogPath=Saved/Flying/Diagnostics/structured-log.jsonl",
  "CrashDiagnosticsDirectory=Saved/Flying/Diagnostics/Crashes",
]) {
  requireToken(defaultGame, token, "DefaultGame diagnostics config");
}

for (const token of [
  "[CrashReportClient]",
  "bImplicitSend=False",
  "bAllowToBeContacted=False",
  "bSendLogFile=False",
]) {
  requireToken(defaultEngine, token, "CrashReportClient privacy config");
}

for (const token of [
  "GetBuildMetadata",
  "GetBuildId",
  "GetAboutBuildSummary",
]) {
  requireToken(buildMetadataHeader + buildMetadataCpp, token, "build metadata API");
}

for (const token of [
  "BuildId",
  "AboutBuildSummary",
  "UFlyingBuildMetadata::GetBuildId",
  "UFlyingBuildMetadata::GetAboutBuildSummary",
]) {
  requireToken(diagnosticsHeader + diagnosticsCpp, token, "diagnostics build ID publication");
}

for (const token of [
  "flying.structured-log.v1",
  "personalTelemetry\\\":false",
  "FGenericCrashContext::SetGameData",
  "FlyingBuildId",
  "FlyingCrashTelemetryOptIn",
  "GLog->AddOutputDevice",
]) {
  requireToken(moduleCpp, token, "structured logging and crash context");
}

for (const token of [
  "selected Czech terrain packages",
  "without requiring network access",
  "signed releases",
  "minidump",
  "no personal telemetry",
]) {
  requireToken(packagingReadme, token, "packaging documentation");
}

console.log("validate_step28_packaging: ok");
