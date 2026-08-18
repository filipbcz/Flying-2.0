#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const presentationRoot = path.join(repoRoot, "unreal/Source/FlyingPresentation");
const bridgeRoot = path.join(repoRoot, "unreal/Source/FlyingCoreSimBridge");

function fail(message) {
  console.error(`validate_presentation_core_sim_separation: ${message}`);
  process.exit(1);
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function walk(root) {
  if (!fs.existsSync(root)) {
    return [];
  }
  const pending = [root];
  const files = [];
  while (pending.length > 0) {
    const current = pending.pop();
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      const absolute = path.join(current, entry.name);
      if (entry.isDirectory()) {
        pending.push(absolute);
      } else if (entry.isFile()) {
        files.push(absolute);
      }
    }
  }
  return files;
}

const forbiddenPresentationPath = /(?:^|[/\\])(?:CoreSimStandalone|GeoTerrainStandalone)(?:[/\\]|$)/;
for (const absolute of walk(presentationRoot)) {
  const relative = path.relative(repoRoot, absolute).replaceAll(path.sep, "/");
  requireCondition(
    !forbiddenPresentationPath.test(relative),
    `Presentation may not own native CoreSim/GeoTerrain implementation file: ${relative}`,
  );
  requireCondition(
    !/core_sim[/\\]src|geo_terrain[/\\]src/.test(read(relative)),
    `Presentation may not compile repository native implementation sources: ${relative}`,
  );
}

const project = JSON.parse(read("unreal/Flying.uproject"));
const modules = new Map((project.Modules ?? []).map((module) => [module.Name, module]));
requireCondition(
  modules.get("FlyingCoreSimBridge")?.Type === "Runtime",
  "FlyingCoreSimBridge must be declared as a runtime module",
);

const presentationBuild = read("unreal/Source/FlyingPresentation/FlyingPresentation.Build.cs");
requireCondition(
  presentationBuild.includes('"FlyingCoreSimBridge"'),
  "FlyingPresentation must depend on the explicit CoreSim bridge module",
);
requireCondition(
  /PublicDependencyModuleNames\.AddRange\([\s\S]*"FlyingCoreSimBridge"/.test(presentationBuild),
  "FlyingCoreSimBridge must be a public dependency because Presentation public headers expose bridge API types",
);
requireCondition(
  !/PublicIncludePaths\.Add\([\s\S]*"(?:core_sim|geo_terrain)"/.test(presentationBuild),
  "FlyingPresentation must not expose raw CoreSim or GeoTerrain include paths publicly",
);
requireCondition(
  /PrivateIncludePaths\.Add\([\s\S]*"core_sim"[\s\S]*PrivateIncludePaths\.Add\([\s\S]*"geo_terrain"/.test(
    presentationBuild,
  ),
  "FlyingPresentation may use CoreSim and GeoTerrain headers only as private implementation details",
);

const componentHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCoreSimComponent.h");
requireCondition(
  componentHeader.includes('#include "FlyingCoreSimBridge.h"'),
  "UFlyingCoreSimComponent public contract must include the explicit CoreSim bridge API",
);

const bridgeBuild = read("unreal/Source/FlyingCoreSimBridge/FlyingCoreSimBridge.Build.cs");
for (const token of ['"Core"', '"core_sim"', '"geo_terrain"', "FLYING_CORE_SIM_VERSION"]) {
  requireCondition(bridgeBuild.includes(token), `FlyingCoreSimBridge Build.cs missing ${token}`);
}

const bridgeHeader = read("unreal/Source/FlyingCoreSimBridge/Public/FlyingCoreSimBridge.h");
for (const token of [
  "FFlyingCoreSimImmutableStateSnapshot",
  "EcefPositionMeters",
  "BodyToEcef",
  "FFlyingCoreSimBridgeModule",
]) {
  requireCondition(bridgeHeader.includes(token), `FlyingCoreSimBridge public API missing ${token}`);
}

const bridgeModuleCpp = read("unreal/Source/FlyingCoreSimBridge/Private/FlyingCoreSimBridge.cpp");
requireCondition(
  bridgeModuleCpp.includes("IMPLEMENT_MODULE(FFlyingCoreSimBridgeModule, FlyingCoreSimBridge)"),
  "FlyingCoreSimBridge must provide a normal Unreal IMPLEMENT_MODULE source file",
);

for (const relative of [
  "unreal/Source/FlyingCoreSimBridge/Public/FlyingCoreSimBridge.h",
  "unreal/Source/FlyingCoreSimBridge/Private/FlyingCoreSimBridge.cpp",
  "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/simulator.cpp",
  "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/fixed_step.cpp",
  "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/aircraft_systems.cpp",
  "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/weather.cpp",
  "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/terrain_contact.cpp",
  "unreal/Source/FlyingCoreSimBridge/Private/GeoTerrainStandalone/geodesy.cpp",
]) {
  requireCondition(fs.existsSync(path.join(repoRoot, relative)), `missing bridge source: ${relative}`);
}

const actorCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCoreSimAircraftActor.cpp");
requireCondition(
  actorCpp.includes("UpdatePresentationFromSnapshot") &&
    actorCpp.includes("TransformEcefPositionToUnreal") &&
    actorCpp.includes("TransformBodyToUnrealRotator") &&
    actorCpp.includes("SetActorLocationAndRotation"),
  "AFlyingCoreSimAircraftActor must update its transform from immutable CoreSim snapshots",
);
requireCondition(
  !/CoreSimulator|Simulator\.advance|FixedStepAccumulator|AircraftSystemsModel|reset_simulator_to_scenario/.test(
    actorCpp,
  ),
  "AFlyingCoreSimAircraftActor may not own CoreSim simulation equations",
);

console.log("validate_presentation_core_sim_separation: ok");
