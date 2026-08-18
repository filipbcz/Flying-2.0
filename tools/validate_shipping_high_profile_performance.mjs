#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const gatePath = "docs/validation/performance/shipping-high-profile-performance-gate.json";
const validationReportPath = "docs/validation/performance/shipping-high-profile-validation-report.json";

function fail(message) {
  console.error(`validate_shipping_high_profile_performance: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  try {
    return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function readJson(relativePath) {
  try {
    return JSON.parse(read(relativePath));
  } catch (error) {
    fail(`invalid JSON in ${relativePath}: ${error.message}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function valueAt(object, dottedPath) {
  return dottedPath.split(".").reduce((current, key) => current?.[key], object);
}

function requireToken(source, token, label) {
  requireCondition(source.includes(token), `${label} missing token: ${token}`);
}

function readBenchmarkReport(reportPath) {
  const resolvedPath = path.resolve(process.cwd(), reportPath);
  try {
    return JSON.parse(fs.readFileSync(resolvedPath, "utf8"));
  } catch (error) {
    if (error?.code === "ENOENT") {
      fail(`missing benchmark report: ${reportPath}`);
    }
    fail(`invalid benchmark report ${reportPath}: ${error.message}`);
  }
}

function parseArgs(argv) {
  const args = { report: null };
  for (let index = 0; index < argv.length; index += 1) {
    if (argv[index] === "--report") {
      args.report = argv[index + 1];
      index += 1;
    } else {
      fail(`unsupported argument: ${argv[index]}`);
    }
  }
  return args;
}

function validateBenchmarkReport(gate, reportPath) {
  const report = readBenchmarkReport(reportPath);
  const requirements = gate.benchmarkReportRequirements;
  requireCondition(
    report.schemaVersion === requirements.schemaVersion,
    "benchmark report schemaVersion mismatch",
  );

  for (const field of requirements.requiredFields) {
    requireCondition(valueAt(report, field) !== undefined, `benchmark report missing field: ${field}`);
  }

  const thresholds = gate.thresholds;
  requireCondition(report.build.platform === "Win64", "benchmark report build.platform must be Win64");
  requireCondition(report.build.configuration === "Shipping", "benchmark report build.configuration must be Shipping");
  requireCondition(report.build.signed === true, "benchmark report must come from signed Shipping build");
  requireCondition(report.graphicsProfile === "High", "benchmark report graphicsProfile must be High");
  requireCondition(report.resolution.width === 2560, "benchmark report width must be 2560");
  requireCondition(report.resolution.height === 1440, "benchmark report height must be 1440");
  requireCondition(report.startupTimeSeconds <= thresholds.startupTimeSecondsMax, "startup time exceeds threshold");
  requireCondition(report.cockpitLoadTimeSeconds <= thresholds.cockpitLoadTimeSecondsMax, "cockpit load time exceeds threshold");
  requireCondition(report.averageFps >= thresholds.averageFpsMin, "average FPS below threshold");
  requireCondition(report.onePercentLowFps >= thresholds.onePercentLowFpsMin, "1% low FPS below threshold");
  requireCondition(report.maxObservedHitchMilliseconds <= thresholds.maxObservedHitchMillisecondsMax, "hitch exceeds threshold");
  requireCondition(report.inputLatencyMillisecondsP95 <= thresholds.inputLatencyMillisecondsP95Max, "input latency exceeds threshold");
  requireCondition(report.coreSimMissedSteps <= thresholds.coreSimMissedStepsMax, "CoreSim missed steps exceed threshold");
  requireCondition(report.ramPeakGiB <= thresholds.ramPeakGiBMax, "RAM exceeds threshold");
  requireCondition(report.vramPeakGiB <= thresholds.vramPeakGiBMax, "VRAM exceeds threshold");

  for (const objectId of gate.graphicsProfileValidation.criticalObjectRetention.objects) {
    requireCondition(
      report.criticalObjectRetention?.[objectId] === "present",
      `critical object not retained by High profile: ${objectId}`,
    );
  }
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const gate = readJson(gatePath);
  const validationReport = readJson(validationReportPath);
  const defaultGame = read("unreal/Config/DefaultGame.ini");
  const defaultEngine = read("unreal/Config/DefaultEngine.ini");
  const scalability = read("unreal/Config/DefaultScalability.ini");
  const settingsHeader = read("unreal/Source/FlyingPresentation/Public/FlyingPresentationSettings.h");
  const terrainActor = read("unreal/Source/FlyingPresentation/Private/FlyingOfflinePilotTerrainActor.cpp");

  requireCondition(
    gate.schemaVersion === "flying.shipping-high-profile-performance-gate.v1",
    "gate schema mismatch",
  );
  requireCondition(gate.referenceEnvironment.os === "Windows 11", "reference OS must be Windows 11");
  requireCondition(gate.referenceEnvironment.platform === "Win64", "reference platform must be Win64");
  requireCondition(gate.referenceEnvironment.configuration === "Shipping", "configuration must be Shipping");
  requireCondition(gate.referenceEnvironment.graphicsProfile === "High", "graphics profile must be High");
  requireCondition(gate.referenceEnvironment.resolution.width === 2560, "width must be 2560");
  requireCondition(gate.referenceEnvironment.resolution.height === 1440, "height must be 1440");
  requireCondition(gate.benchmarkCommand.command.includes("Flying-Win64-Shipping.exe"), "benchmark command must run Shipping executable");
  requireToken(gate.benchmarkCommand.command, "flying.profile High", "benchmark command");
  requireToken(gate.benchmarkCommand.command, "flying.benchmark shipping-high-profile", "benchmark command");
  requireToken(gate.benchmarkCommand.command, "--offline", "benchmark command");
  requireToken(gate.benchmarkCommand.command, "--report Saved/Flying/Benchmarks/shipping-high-profile-report.json", "benchmark command");
  requireCondition(gate.benchmarkCommand.requiredBuild.signed === true, "benchmark command must require signed build");

  const thresholds = gate.thresholds;
  requireCondition(thresholds.startupTimeSecondsMax === 20, "startup threshold mismatch");
  requireCondition(thresholds.cockpitLoadTimeSecondsMax === 30, "cockpit load threshold mismatch");
  requireCondition(thresholds.averageFpsMin === 60, "average FPS threshold mismatch");
  requireCondition(thresholds.onePercentLowFpsMin === 45, "1% low FPS threshold mismatch");
  requireCondition(thresholds.maxObservedHitchMillisecondsMax === 100, "hitch threshold mismatch");
  requireCondition(thresholds.inputLatencyMillisecondsP95Max === 50, "latency threshold mismatch");
  requireCondition(thresholds.coreSimMissedStepsMax === 0, "missed-step threshold mismatch");
  requireCondition(thresholds.ramPeakGiBMax === 24, "RAM threshold mismatch");
  requireCondition(thresholds.vramPeakGiBMax === 10, "VRAM threshold mismatch");
  requireCondition(
    gate.mandatoryFailPolicy.decision === "fail_when_any_threshold_is_exceeded_or_any_required_field_is_missing",
    "mandatory fail policy mismatch",
  );
  requireCondition(gate.mandatoryFailPolicy.waiversAllowed === false, "performance gate must not allow waivers");

  for (const field of [
    "averageFps",
    "onePercentLowFps",
    "maxObservedHitchMilliseconds",
    "startupTimeSeconds",
    "cockpitLoadTimeSeconds",
    "ramPeakGiB",
    "vramPeakGiB",
    "inputLatencyMillisecondsP95",
    "coreSimMissedSteps",
    "criticalObjectRetention",
  ]) {
    requireCondition(
      gate.benchmarkReportRequirements.requiredFields.includes(field),
      `benchmark report requirement missing field: ${field}`,
    );
  }

  for (const token of [
    "HighGraphicsTargetFrameRate=60",
    "HighGraphicsMinimumOnePercentLowFps=45",
    "MaximumInputLatencyMilliseconds=50.0",
    "MaximumStreamingHitchMilliseconds=100.0",
    "MaximumSoakRamGiB=24.0",
    "MaximumSoakVramGiB=10.0",
    "HighGraphicsTerrainLodLevel=1",
    "MaxTerrainSectionsPerLoad=24",
    "MaxTerrainVerticesPerSection=262144",
  ]) {
    requireToken(defaultGame, token, "DefaultGame.ini");
  }

  for (const token of [
    "r.Streaming.PoolSize=8192",
    "r.TextureStreaming=True",
    "r.VirtualTextures=True",
    "r.OneFrameThreadLag=0",
  ]) {
    requireToken(defaultEngine, token, "DefaultEngine.ini");
  }

  for (const token of [
    "[ViewDistanceQuality@2]",
    "[TextureQuality@2]",
    "[FoliageQuality@2]",
    "foliage.DensityScale=0.65",
    "grass.DensityScale=0.65",
    "r.MotionBlurQuality=0",
  ]) {
    requireToken(scalability, token, "DefaultScalability.ini");
  }

  for (const token of [
    "HighGraphicsTargetFrameRate",
    "HighGraphicsMinimumOnePercentLowFps",
    "MaximumInputLatencyMilliseconds",
    "MaximumStreamingHitchMilliseconds",
    "MaximumSoakRamGiB",
    "MaximumSoakVramGiB",
    "HighGraphicsTerrainLodLevel",
    "MaxTerrainSectionsPerLoad",
    "MaxTerrainVerticesPerSection",
  ]) {
    requireToken(settingsHeader, token, "FlyingPresentationSettings.h");
  }

  for (const token of [
    "route-proximity-prioritized tiles",
    "Terrain section budget reached",
    "LoadTerrainTileCsv",
    "LoadImageryPackage",
  ]) {
    requireToken(terrainActor, token, "FlyingOfflinePilotTerrainActor.cpp");
  }

  requireCondition(
    validationReport.schemaVersion === "flying.shipping-high-profile-validation-report.v1",
    "validation report schema mismatch",
  );
  requireCondition(validationReport.gate === gatePath, "validation report must reference gate");
  requireCondition(
    validationReport.graphicsProfileValidation.result === "blocked",
    "graphics profile validation must remain blocked until Shipping benchmark evidence is supplied",
  );
  requireCondition(
    validationReport.graphicsProfileValidation.criticalObjectRetentionRequired?.source ===
      "shipping-high-profile-benchmark-report",
    "graphics profile validation must require benchmark-report critical object evidence",
  );
  requireCondition(
    validationReport.graphicsProfileValidation.criticalObjectRetentionRequired?.requiredState === "present",
    "graphics profile validation must require retained objects to be present",
  );
  for (const objectId of gate.graphicsProfileValidation.criticalObjectRetention.objects) {
    requireCondition(
      validationReport.graphicsProfileValidation.criticalObjectRetentionRequired.objects?.includes(objectId),
      `graphics profile validation missing required critical object: ${objectId}`,
    );
  }

  if (args.report) {
    validateBenchmarkReport(gate, args.report);
  }

  console.log("validate_shipping_high_profile_performance: ok");
}

main();
