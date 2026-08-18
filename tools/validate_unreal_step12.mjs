#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const unrealRoot = path.join(repoRoot, "unreal");

function fail(message) {
  console.error(`validate_unreal_step12: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  const absolutePath = path.join(repoRoot, relativePath);
  try {
    return fs.readFileSync(absolutePath, "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function loadUproject() {
  try {
    return JSON.parse(read("unreal/Flying.uproject"));
  } catch (error) {
    fail(`Flying.uproject is not valid JSON: ${error.message}`);
  }
}

function checkUproject() {
  const project = loadUproject();
  requireCondition(project.EngineAssociation === "5.8", "uproject must target UE 5.8");
  requireCondition(
    Array.isArray(project.TargetPlatforms) && project.TargetPlatforms.includes("Win64"),
    "uproject must target Win64",
  );

  const modules = new Map((project.Modules ?? []).map((module) => [module.Name, module]));
  requireCondition(
    modules.get("FlyingPresentation")?.Type === "Runtime",
    "FlyingPresentation runtime module is missing",
  );
  requireCondition(
    modules.get("FlyingCoreSimBridge")?.Type === "Runtime",
    "FlyingCoreSimBridge runtime module is missing",
  );

  const plugins = new Map((project.Plugins ?? []).map((plugin) => [plugin.Name, plugin]));
  requireCondition(
    plugins.get("CesiumForUnreal")?.Enabled === true,
    "CesiumForUnreal plugin must be enabled",
  );
  requireCondition(
    plugins.get("ProceduralMeshComponent")?.Enabled === true,
    "ProceduralMeshComponent plugin must be enabled",
  );
}

function checkTargetsAndBuildRules() {
  for (const relativePath of [
    "unreal/Source/Flying.Target.cs",
    "unreal/Source/FlyingEditor.Target.cs",
  ]) {
    requireCondition(fs.existsSync(path.join(repoRoot, relativePath)), `missing ${relativePath}`);
  }

  const buildCs = read("unreal/Source/FlyingPresentation/FlyingPresentation.Build.cs");
  for (const token of [
    "CppStandardVersion.Cpp20",
    "bEnableExceptions = true",
    '"FlyingCoreSimBridge"',
    '"CesiumRuntime"',
    '"ProceduralMeshComponent"',
    '"Json"',
    '"core_sim"',
    '"geo_terrain"',
  ]) {
    requireCondition(buildCs.includes(token), `Build.cs missing required token: ${token}`);
  }
}

function checkRuntimeConfig() {
  const defaultEngine = read("unreal/Config/DefaultEngine.ini");
  const defaultGame = read("unreal/Config/DefaultGame.ini");
  requireCondition(defaultGame.includes("bOfflineOnly=True"), "offline-only setting is missing");
  requireCondition(
    defaultGame.includes("TerrainPackageManifestPath="),
    "terrain package manifest setting is missing",
  );
  requireCondition(
    defaultGame.includes("PilotRegionPackageManifestPath="),
    "pilot region package manifest setting is missing",
  );

  const disallowed = /https?:\/\/|api[_-]?key|access[_-]?token|mapbox|ionasset/i;
  requireCondition(
    !disallowed.test(`${defaultEngine}\n${defaultGame}`),
    "Unreal config must not declare remote map APIs, tokens, or ion assets",
  );
}

function checkSourceContracts() {
  const requiredFiles = [
    "unreal/Source/FlyingPresentation/Public/FlyingCoreSimComponent.h",
    "unreal/Source/FlyingPresentation/Private/FlyingCoreSimComponent.cpp",
    "unreal/Source/FlyingPresentation/Public/FlyingCesiumGeoreferenceComponent.h",
    "unreal/Source/FlyingPresentation/Private/FlyingCesiumGeoreferenceComponent.cpp",
    "unreal/Source/FlyingPresentation/Public/FlyingCoreSimAircraftActor.h",
    "unreal/Source/FlyingPresentation/Private/FlyingCoreSimAircraftActor.cpp",
    "unreal/Source/FlyingPresentation/Public/FlyingOfflinePilotTerrainActor.h",
    "unreal/Source/FlyingPresentation/Private/FlyingOfflinePilotTerrainActor.cpp",
    "unreal/Source/FlyingCoreSimBridge/FlyingCoreSimBridge.Build.cs",
    "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/simulator.cpp",
    "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/determinism.cpp",
    "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/fixed_step.cpp",
    "unreal/Source/FlyingCoreSimBridge/Private/GeoTerrainStandalone/geodesy.cpp",
  ];
  for (const relativePath of requiredFiles) {
    requireCondition(
      fs.existsSync(path.join(repoRoot, relativePath)),
      `missing source contract file: ${relativePath}`,
    );
  }

  const georefCpp = read(
    "unreal/Source/FlyingPresentation/Private/FlyingCesiumGeoreferenceComponent.cpp",
  );
  requireCondition(
    georefCpp.includes("TransformEarthCenteredEarthFixedPositionToUnreal"),
    "Cesium ECEF position transform is missing",
  );
  requireCondition(
    georefCpp.includes("TransformEarthCenteredEarthFixedDirectionToUnreal"),
    "Cesium ECEF direction transform is missing",
  );
  requireCondition(
    georefCpp.includes("SetOriginLongitudeLatitudeHeight"),
    "Cesium georeference origin configuration is missing",
  );

  const aircraftCpp = read(
    "unreal/Source/FlyingPresentation/Private/FlyingCoreSimAircraftActor.cpp",
  );
  requireCondition(aircraftCpp.includes("ApplyWorldOffset"), "origin-shift handling is missing");
  requireCondition(
    aircraftCpp.includes("UpdatePresentationFromSnapshot"),
    "aircraft snapshot presentation update is missing",
  );

  const coreCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCoreSimComponent.cpp");
  requireCondition(coreCpp.includes("CoreSimulator"), "CoreSim standalone bridge is missing");
  requireCondition(coreCpp.includes("Simulator.advance"), "CoreSim advance call is missing");
  requireCondition(
    !coreCpp.includes("TransformEarthCenteredEarthFixedPositionToUnreal"),
    "CoreSim bridge must not depend on Cesium presentation transforms",
  );

  const terrainCpp = read(
    "unreal/Source/FlyingPresentation/Private/FlyingOfflinePilotTerrainActor.cpp",
  );
  for (const token of [
    "UProceduralMeshComponent",
    "LoadTerrainTiles",
    "LoadImageryPackage",
    "runtimeNetworkRequired",
    "externalMapApis",
    "remoteTileServerUrls",
    "LoadPpmP3",
    "CreateMeshSection_LinearColor",
  ]) {
    requireCondition(terrainCpp.includes(token), `offline terrain loader missing token: ${token}`);
  }

  for (const token of [
    "flying.terrain-package.v1",
    "flying.pilot-region-package.v1",
    "HasExpectedSchemaVersion",
    "ResolvePackageAssetPath",
    "IsPathInsidePackageRoot",
    "FPaths::IsRelative(ManifestRelativePath)",
    "Package asset path escapes its package root",
    "ResolvePackageAssetPath(PackageRoot, Tile.Path",
    "ResolvePackageAssetPath(PackageRoot, RelativePath",
    "TryReadBoundedJsonInt",
    "IsTerrainTileSampleCountWithinBounds",
    "kMaxTerrainTileSamples",
    "TryParseNonNegativeIntToken",
    "IsPpmPixelCountWithinBounds",
    "kMaxPpmPixels",
    "Configured pilot imagery package failed to load",
  ]) {
    requireCondition(
      terrainCpp.includes(token),
      `offline terrain loader missing safety contract token: ${token}`,
    );
  }

  requireCondition(
    /if\s*\(!LoadImageryPackage\(PilotManifestPath,\s*ImageryTiles\)\)\s*\{[\s\S]*?return false;\s*\}/.test(
      terrainCpp,
    ),
    "configured imagery package failures must fail LoadOfflinePackages",
  );

  const standaloneCpp = read(
    "unreal/Source/FlyingCoreSimBridge/Private/CoreSimStandalone/simulator.cpp",
  );
  requireCondition(
    standaloneCpp.includes("CoreSimulator::advance") &&
      standaloneCpp.includes("geo_terrain::ecef_to_geodetic"),
    "Unreal CoreSim bridge must compile the existing CoreSim simulator implementation",
  );
}

checkUproject();
checkTargetsAndBuildRules();
checkRuntimeConfig();
checkSourceContracts();

console.log("validate_unreal_step12: ok");
