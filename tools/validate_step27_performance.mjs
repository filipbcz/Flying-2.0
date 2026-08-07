#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  console.error(`validate_step27_performance: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  try {
    return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function requireToken(source, token, label) {
  requireCondition(source.includes(token), `${label} missing token: ${token}`);
}

function parseIniNumber(source, key) {
  const match = source.match(new RegExp(`^${key}=([0-9]+(?:\\.[0-9]+)?)$`, "m"));
  requireCondition(match, `DefaultGame.ini missing numeric ${key}`);
  return Number(match[1]);
}

const settingsHeader = read("unreal/Source/FlyingPresentation/Public/FlyingPresentationSettings.h");
const coreHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCoreSimComponent.h");
const coreCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCoreSimComponent.cpp");
const diagnosticsHeader = read("unreal/Source/FlyingPresentation/Public/FlyingDiagnosticsWidget.h");
const diagnosticsCpp = read("unreal/Source/FlyingPresentation/Private/FlyingDiagnosticsWidget.cpp");
const terrainHeader = read("unreal/Source/FlyingPresentation/Public/FlyingOfflinePilotTerrainActor.h");
const terrainCpp = read("unreal/Source/FlyingPresentation/Private/FlyingOfflinePilotTerrainActor.cpp");
const defaultGame = read("unreal/Config/DefaultGame.ini");
const defaultEngine = read("unreal/Config/DefaultEngine.ini");
const scalability = read("unreal/Config/DefaultScalability.ini");
const cmake = read("tests/CMakeLists.txt");
const timingTest = read("tests/core_sim/performance_timing_test.cpp");
const gate = JSON.parse(read("docs/validation/performance/step27-performance-gate.json"));

requireCondition(gate.schemaVersion === "flying.step27-performance-gate.v1", "gate schema mismatch");
requireCondition(gate.referenceHardwareClass.os === "Windows 11", "reference OS must be Windows 11");
requireCondition(gate.referenceHardwareClass.resolution.width === 2560, "reference width must be 2560");
requireCondition(gate.referenceHardwareClass.resolution.height === 1440, "reference height must be 1440");
requireCondition(gate.budgets.averageFpsMin >= 60, "average FPS budget must be at least 60");
requireCondition(gate.budgets.onePercentLowFpsMin >= 45, "1% low FPS budget must be at least 45");
requireCondition(gate.budgets.inputLatencyMsMax <= 50, "input latency budget must be <= 50 ms");
requireCondition(gate.budgets.streamingHitchMsMax <= 100, "streaming hitch budget must be <= 100 ms");
requireCondition(gate.budgets.coreSimMissedStepsMax === 0, "CoreSim missed-step budget must be zero");
requireCondition(gate.budgets.ramGiBMax <= 24, "RAM budget must be <= 24 GiB");
requireCondition(gate.budgets.vramGiBMax <= 10, "VRAM budget must be <= 10 GiB");
requireCondition(gate.budgets.soakDurationHours >= 10, "soak duration must be at least 10 hours");
requireCondition(
  gate.inputLatencyEvidence?.measurement === "external input-to-photon capture at 60 FPS",
  "input latency evidence must require external input-to-photon capture",
);
requireCondition(
  gate.inputLatencyEvidence?.runtimeProxyField === "LastCoreSimInputProcessingMilliseconds",
  "input latency runtime proxy must be named as CoreSim input processing time",
);

for (const [key, expected] of Object.entries({
  HighGraphicsTargetFrameRate: 60,
  HighGraphicsMinimumOnePercentLowFps: 45,
  MaximumInputLatencyMilliseconds: 50,
  MaximumStreamingHitchMilliseconds: 100,
  MaximumSoakRamGiB: 24,
  MaximumSoakVramGiB: 10,
  SoakDurationHours: 10,
})) {
  requireCondition(parseIniNumber(defaultGame, key) === expected, `${key} must equal ${expected}`);
  requireToken(settingsHeader, key, "presentation settings");
}

for (const token of [
  "r.Streaming.PoolSize=8192",
  "r.TextureStreaming=True",
  "r.VirtualTextures=True",
  "r.OneFrameThreadLag=0",
]) {
  requireToken(defaultEngine, token, "renderer settings");
}

for (const token of [
  "[FoliageQuality@2]",
  "foliage.DensityScale=0.65",
  "grass.DensityScale=0.65",
  "r.Streaming.PoolSize=8192",
  "r.MotionBlurQuality=0",
]) {
  requireToken(scalability, token, "high scalability profile");
}

for (const token of [
  "CoreSimMissedStepCount",
  "MaxCoreSimStepsPerFrame",
  "AverageFrameRate",
  "OnePercentLowFrameRate",
  "MaxObservedHitchMilliseconds",
  "LastCoreSimInputProcessingMilliseconds",
]) {
  requireToken(coreHeader, token, "CoreSim performance API");
  requireToken(coreCpp, token, "CoreSim performance implementation");
  requireToken(diagnosticsHeader, token, "diagnostics performance fields");
  requireToken(diagnosticsCpp, token, "diagnostics performance publication");
}

for (const token of [
  "HighGraphicsTerrainLodLevel",
  "MaxTerrainSectionsPerLoad",
  "MaxTerrainVerticesPerSection",
  "TerrainStreamingFocusLocalMeters",
  "route-proximity-prioritized tiles",
  "Terrain section budget reached",
]) {
  requireToken(terrainHeader + terrainCpp + settingsHeader + defaultGame, token, "terrain streaming budget");
}

for (const token of [
  "flying_core_sim_performance_timing_tests",
  "flying.core_sim.performance_timing",
]) {
  requireToken(cmake, token, "performance timing CMake target");
}

for (const token of [
  "10.0 * 60.0 * 60.0",
  "kExpectedTotalSteps",
  "missed_steps == 0",
  "report.steps_executed != 4",
]) {
  requireToken(timingTest, token, "CoreSim ten-hour timing test");
}

console.log("validate_step27_performance: ok");
