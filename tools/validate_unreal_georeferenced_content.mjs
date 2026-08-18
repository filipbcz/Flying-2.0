#!/usr/bin/env node

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const automationTestName = "Flying.Presentation.GeoreferencedContent.StartupScenarioRendersOfflineRegionalContent";
const canonicalEvidencePath = "docs/validation/visual/rc-startup-georef-rendering.json";

function fail(message) {
  console.error(`validate_unreal_georeferenced_content: ${message}`);
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

function writeJson(relativePath, value) {
  const absolutePath = path.join(repoRoot, relativePath);
  fs.mkdirSync(path.dirname(absolutePath), { recursive: true });
  fs.writeFileSync(absolutePath, `${JSON.stringify(value, null, 2)}\n`, "utf8");
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function isNonEmptyString(value) {
  return typeof value === "string" && value.trim().length > 0;
}

function isIsoUtc(value) {
  return typeof value === "string" && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(value);
}

function normalizedRepoPath(value) {
  if (!isNonEmptyString(value)) {
    return null;
  }
  if (/^[a-z][a-z0-9+.-]*:\/\//i.test(value) || /^[a-z]:[\\/]/i.test(value) || /^\\\\|^\/\//.test(value)) {
    return null;
  }
  const normalized = path.posix.normalize(value.replaceAll("\\", "/"));
  if (normalized === "." || normalized === ".." || normalized.startsWith("../") || normalized.startsWith("/")) {
    return null;
  }
  return normalized;
}

function requireRepoFile(value, label) {
  const relativePath = normalizedRepoPath(value);
  requireCondition(relativePath !== null, `${label} must be a repository-relative path`);
  requireCondition(!/(^|\/)(?:fixtures?|synthetic|declarations?)(?:\/|$)/i.test(relativePath), `${label} cannot be fixture, synthetic, or declaration evidence`);
  requireCondition(!/readme(?:\.[a-z0-9]+)?$/i.test(relativePath), `${label} cannot be README-only evidence`);
  requireCondition(fs.existsSync(path.join(repoRoot, relativePath)), `${label} does not exist: ${relativePath}`);
  return relativePath;
}

function sha256(relativePath) {
  return crypto.createHash("sha256").update(fs.readFileSync(path.join(repoRoot, relativePath))).digest("hex");
}

function runGit(args) {
  const result = spawnSync("git", args, {
    cwd: repoRoot,
    encoding: "utf8",
    shell: false,
    stdio: ["ignore", "pipe", "pipe"],
  });
  requireCondition(result.status === 0, `git ${args.join(" ")} failed: ${result.stderr ?? result.stdout}`);
  return result.stdout.trim();
}

function parseArgs(argv) {
  const args = {
    evidence: null,
    requireUnrealRuntime: false,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--evidence") {
      args.evidence = argv[index + 1];
      index += 1;
    } else if (arg === "--require-unreal-runtime") {
      args.requireUnrealRuntime = true;
    } else {
      fail(`unsupported argument: ${arg}`);
    }
  }
  if (!args.evidence) {
    fail("usage: node tools/validate_unreal_georeferenced_content.mjs --evidence <path> [--require-unreal-runtime]");
  }
  return args;
}

function stripCppComments(text) {
  return text
    .replace(/\/\*[\s\S]*?\*\//g, "")
    .replace(/(^|[^:])\/\/.*$/gm, "$1");
}

function requireCodePattern(text, pattern, label, message) {
  requireCondition(pattern.test(stripCppComments(text)), `${label} ${message}`);
}

function validateImplementation(evidence) {
  const implementation = evidence.implementation;
  requireCondition(isObject(implementation), "implementation must be present");

  const gameModePath = requireRepoFile(implementation.startupGameMode, "implementation.startupGameMode");
  const terrainActorPath = requireRepoFile(implementation.offlineContentActor, "implementation.offlineContentActor");
  const terrainHeaderPath = requireRepoFile(implementation.offlineContentActorHeader, "implementation.offlineContentActorHeader");
  const georefPath = requireRepoFile(implementation.georeferenceComponent, "implementation.georeferenceComponent");
  const settingsPath = requireRepoFile(implementation.presentationSettings, "implementation.presentationSettings");
  const engineConfigPath = requireRepoFile(implementation.engineConfig, "implementation.engineConfig");
  const gameConfigPath = requireRepoFile(implementation.gameConfig, "implementation.gameConfig");

  const gameMode = read(gameModePath);
  requireCondition(read(engineConfigPath).includes("GlobalDefaultGameMode=/Script/FlyingPresentation.FlyingPilotGameMode"), `${engineConfigPath} must bind the startup scenario game mode`);
  requireCondition(read(engineConfigPath).includes("GameDefaultMap=/Engine/Maps/Templates/OpenWorld"), `${engineConfigPath} must bind a startup map`);
  requireCondition(read(gameConfigPath).includes("bOfflineOnly=True"), `${gameConfigPath} must keep startup offline-only`);
  requireCondition(read(gameConfigPath).includes("bSpawnDefaultSceneOnBeginPlay=True"), `${gameConfigPath} must spawn the startup presentation scene`);
  requireCondition(read(gameConfigPath).includes("TerrainPackageManifestPath=Saved/Flying/PilotRegion/Terrain/terrain-package.json"), `${gameConfigPath} must configure the installed terrain package`);
  requireCondition(read(gameConfigPath).includes("PilotRegionPackageManifestPath=Saved/Flying/PilotRegion/GIS/pilot-region-package.json"), `${gameConfigPath} must configure the installed pilot region package`);
  requireCodePattern(gameMode, /if\s*\(\s*!Settings->bSpawnDefaultSceneOnBeginPlay\s*\|\|\s*!GetWorld\(\)\s*\)\s*\{[\s\S]*?return\s*;[\s\S]*?\}/, gameModePath, "must gate startup scene spawning through settings and a runtime world");
  requireCodePattern(gameMode, /GetWorld\(\)->SpawnActor<\s*AFlyingOfflinePilotTerrainActor\s*>\s*\(/, gameModePath, "must spawn the offline terrain actor in the startup scenario");
  requireCodePattern(gameMode, /Georeference->SetOriginLongitudeLatitudeHeight\s*\(/, gameModePath, "must configure the Cesium georeference origin");

  const terrainHeader = read(terrainHeaderPath);
  requireCodePattern(terrainHeader, /bool\s+LoadOfflinePackages\s*\(\s*\)\s*;/, terrainHeaderPath, "must expose the offline package load operation");
  requireCodePattern(terrainHeader, /int32\s+GetRenderedTerrainSectionCount\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose rendered section count for runtime validation");
  requireCodePattern(terrainHeader, /bool\s+HasRenderedTerrainSections\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose rendered section state for runtime validation");
  requireCodePattern(terrainHeader, /FString\s+GetLoadedTerrainPackageManifestPath\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose loaded terrain manifest path for runtime validation");
  requireCodePattern(terrainHeader, /FString\s+GetLoadedPilotRegionPackageManifestPath\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose loaded pilot region manifest path for runtime validation");
  requireCodePattern(terrainHeader, /int32\s+GetLoadedTerrainTileCount\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose observed terrain tile count for runtime validation");
  requireCodePattern(terrainHeader, /int32\s+GetLoadedImageryTileCount\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose observed imagery tile count for runtime validation");
  requireCodePattern(terrainHeader, /FVector\s+GetFirstRenderedEcefPositionMeters\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose an observed rendered ECEF position for runtime validation");
  requireCodePattern(terrainHeader, /FVector\s+GetFirstRenderedUnrealPosition\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose the Cesium-transformed Unreal position for runtime validation");
  requireCodePattern(terrainHeader, /bool\s+DidLastLoadUseRemoteMapDependencies\s*\(\s*\)\s+const\s*;/, terrainHeaderPath, "must expose runtime external map dependency status");
  requireCodePattern(terrainHeader, /UFlyingCesiumGeoreferenceComponent/, terrainHeaderPath, "must own the Cesium georeference component");

  const terrainActor = read(terrainActorPath);
  requireCondition(
    /const\s+FVector\s+UnrealPosition\s*=\s*GeoreferenceComponent->TransformEcefPositionToUnreal\(EcefPosition\)\s*;[\s\S]*?Vertices\.Add\(UnrealPosition\)/.test(stripCppComments(terrainActor)),
    "terrain vertices must be placed through the Cesium georeference transform",
  );
  requireCondition(
    /TerrainMesh->CreateMeshSection_LinearColor\([\s\S]*?Vertices[\s\S]*?Triangles[\s\S]*?Normals[\s\S]*?VertexColors[\s\S]*?true\)/.test(stripCppComments(terrainActor)),
    "offline regional content must be submitted to a visible procedural mesh section with collision",
  );
  requireCodePattern(terrainActor, /LoadTerrainTiles\([\s\S]*?TerrainTiles\)/, terrainActorPath, "must load offline terrain tiles");
  requireCodePattern(terrainActor, /LoadImageryPackage\([\s\S]*?ImageryTiles\)/, terrainActorPath, "must load offline imagery from the pilot region package");
  requireCodePattern(terrainActor, /HasDisallowedTerrainRuntimeDependency\([\s\S]*?TerrainManifest[\s\S]*?\)/, terrainActorPath, "must inspect terrain package streaming dependencies");
  requireCodePattern(terrainActor, /HasDisallowedPilotRuntimeDependency\([\s\S]*?PilotPackage[\s\S]*?\)/, terrainActorPath, "must inspect pilot package runtime dependencies");
  requireCodePattern(terrainActor, /RuntimeDependencySectionUsesRemoteContent\([\s\S]*?runtimeNetworkRequired[\s\S]*?externalMapApis[\s\S]*?remoteTileServerUrls[\s\S]*?\)/, terrainActorPath, "must derive remote dependency state from manifest metadata");
  requireCodePattern(terrainActor, /bLastLoadUsedRemoteMapDependencies\s*=\s*bTerrainManifestUsedRemoteMapDependencies\s*\|\|\s*bPilotManifestUsedRemoteMapDependencies\s*;/, terrainActorPath, "must report actual terrain and pilot manifest dependency state");
  requireCondition(
    !/bLastLoadUsedRemoteMapDependencies\s*=\s*false\s*;/.test(stripCppComments(terrainActor)),
    "runtime external map dependency status must not be hardcoded false",
  );
  requireCodePattern(terrainActor, /return\s+TerrainMesh\s*\?\s*TerrainMesh->GetNumSections\(\)\s*:\s*0\s*;/, terrainActorPath, "must report actual procedural mesh sections");

  const georef = read(georefPath);
  requireCodePattern(georef, /TransformEarthCenteredEarthFixedPositionToUnreal\s*\(/, georefPath, "must call the Cesium ECEF position transform");
  requireCodePattern(georef, /TransformEarthCenteredEarthFixedDirectionToUnreal\s*\(/, georefPath, "must call the Cesium ECEF direction transform");

  const settings = read(settingsPath);
  requireCondition(settings.includes("Saved/Flying/PilotRegion/Terrain/terrain-package.json"), `${settingsPath} must declare the installed terrain package setting`);
  requireCondition(settings.includes("Saved/Flying/PilotRegion/GIS/pilot-region-package.json"), `${settingsPath} must declare the installed pilot region package setting`);

  const automationTestPath = requireRepoFile(implementation.automationTest, "implementation.automationTest");
  const automationTest = read(automationTestPath);
  requireCondition(automationTest.includes(automationTestName), `${automationTestPath} must define the runtime automation test`);
  requireCondition(!/SpawnActor<\s*AFlyingOfflinePilotTerrainActor\s*>/.test(stripCppComments(automationTest)), `${automationTestPath} must not spawn a fallback terrain actor`);
  requireCodePattern(automationTest, /TActorIterator<\s*AFlyingOfflinePilotTerrainActor\s*>/, automationTestPath, "must inspect the terrain actor produced by the startup world");
  requireCodePattern(automationTest, /TerrainActor->LoadOfflinePackages\(\)/, automationTestPath, "must execute runtime offline package loading");
  requireCodePattern(automationTest, /TestTrue\([\s\S]*?bLoadedOfflinePackages[\s\S]*?\)/, automationTestPath, "must assert runtime offline package loading");
  requireCodePattern(automationTest, /TestTrue\([\s\S]*?TerrainActor->HasRenderedTerrainSections\(\)[\s\S]*?\)/, automationTestPath, "must assert actual rendered terrain mesh sections");
  requireCodePattern(automationTest, /terrainManifestPath=\\?"%s\\?"/, automationTestPath, "must emit observed terrain package manifest path for artifact generation");
  requireCodePattern(automationTest, /pilotRegionManifestPath=\\?"%s\\?"/, automationTestPath, "must emit observed pilot region package manifest path for artifact generation");
  requireCodePattern(automationTest, /usedRemoteMapDependencies=%s/, automationTestPath, "must emit observed external map dependency status for artifact generation");
  requireCodePattern(automationTest, /firstEcef=\(%.3f,%.3f,%.3f\)/, automationTestPath, "must emit an observed ECEF position for artifact generation");
  requireCodePattern(automationTest, /firstUnreal=\(%.3f,%.3f,%.3f\)/, automationTestPath, "must emit an observed Cesium-transformed Unreal position for artifact generation");

  if (Array.isArray(implementation.fileChecksums)) {
    for (const entry of implementation.fileChecksums) {
      requireCondition(isObject(entry), "implementation.fileChecksums entries must be objects");
      const relativePath = requireRepoFile(entry.path, "implementation.fileChecksums.path");
      requireCondition(/^[a-f0-9]{64}$/.test(entry.sha256 ?? ""), `invalid sha256 for ${relativePath}`);
      requireCondition(sha256(relativePath) === entry.sha256, `checksum mismatch for ${relativePath}`);
    }
  }
}

function validateCaptureArtifact(evidence) {
  const artifactPath = requireRepoFile(evidence.captureArtifact, "captureArtifact");
  const capture = readJson(artifactPath);
  const currentCommit = runGit(["rev-parse", "HEAD"]);
  requireCondition(capture.schemaVersion === "flying.georeferenced-content-render-capture.v1", "capture schemaVersion is invalid");
  requireCondition(capture.scenarioId === evidence.startupScenarioId, "capture scenarioId must match evidence startupScenarioId");
  requireCondition(capture.captureSource === "unreal-automation-test", "captureSource must be unreal-automation-test");
  requireCondition(capture.fixture === false, "capture.fixture must be false");
  requireCondition(capture.synthetic === false, "capture.synthetic must be false");
  requireCondition(capture.headlessRender === false, "capture.headlessRender must be false");
  requireCondition(capture.offlineOnly === true, "capture.offlineOnly must be true");
  requireCondition(capture.automationTest === automationTestName, "capture automationTest must match the runtime validation test");
  requireCondition(isObject(capture.build), "capture must include build provenance");
  requireCondition(isNonEmptyString(capture.build?.id), "capture build.id is required");
  requireCondition(isNonEmptyString(capture.build?.commit), "capture build.commit is required");
  requireCondition(/^[a-f0-9]{40}$/i.test(capture.build?.commit ?? ""), "capture build.commit must be a Git commit id");
  requireCondition(capture.build.commit === currentCommit, "capture build.commit must match the current repository commit");
  requireCondition(isObject(capture.provenance), "capture must include generation provenance");
  requireCondition(capture.provenance?.generatedBy === "tools/validate_unreal_georeferenced_content.mjs --require-unreal-runtime", "capture provenance generatedBy is invalid");
  requireCondition(capture.provenance?.exitCode === 0, "capture provenance exitCode must be zero");
  requireCondition(Number.isInteger(capture.observations?.renderedTerrainSectionCount), "capture observations.renderedTerrainSectionCount must be an integer");
  requireCondition(capture.observations.renderedTerrainSectionCount > 0, "capture observations.renderedTerrainSectionCount must be greater than zero");
  requireCondition(Number.isInteger(capture.observations?.terrainTileCount), "capture observations.terrainTileCount must be an integer");
  requireCondition(capture.observations.terrainTileCount > 0, "capture observations.terrainTileCount must be greater than zero");
  requireCondition(Number.isInteger(capture.observations?.imageryTileCount), "capture observations.imageryTileCount must be an integer");
  requireCondition(capture.observations.imageryTileCount > 0, "capture observations.imageryTileCount must be greater than zero");
  requireCondition(Number.isInteger(capture.observations?.renderedVertexCount), "capture observations.renderedVertexCount must be an integer");
  requireCondition(capture.observations.renderedVertexCount > 0, "capture observations.renderedVertexCount must be greater than zero");
  requireCondition(Number.isInteger(capture.observations?.renderedTriangleCount), "capture observations.renderedTriangleCount must be an integer");
  requireCondition(capture.observations.renderedTriangleCount > 0, "capture observations.renderedTriangleCount must be greater than zero");
  requireCondition(capture.observations?.usedRemoteMapDependencies === false, "capture observations.usedRemoteMapDependencies must be false");
  requireCondition(isObject(capture.offlinePackageManifests), "capture must include offlinePackageManifests");
  for (const key of ["terrain", "pilotRegion"]) {
    requireCondition(isObject(capture.offlinePackageManifests?.[key]), `capture offlinePackageManifests.${key} must be present`);
    requireCondition(isNonEmptyString(capture.offlinePackageManifests?.[key]?.path), `capture offlinePackageManifests.${key}.path is required`);
    requireCondition(/^[a-f0-9]{64}$/.test(capture.offlinePackageManifests?.[key]?.sha256 ?? ""), `capture offlinePackageManifests.${key}.sha256 must be a SHA-256 digest`);
    requireCondition(capture.offlinePackageManifests?.[key]?.source === "installed-offline-regional-package", `capture offlinePackageManifests.${key}.source must be installed-offline-regional-package`);
  }
  requireCondition(isObject(capture.georeferenceTransform), "capture must include georeferenceTransform");
  requireCondition(Array.isArray(capture.georeferenceTransform?.firstEcefPositionMeters), "capture georeferenceTransform.firstEcefPositionMeters must be an array");
  requireCondition(Array.isArray(capture.georeferenceTransform?.firstUnrealPosition), "capture georeferenceTransform.firstUnrealPosition must be an array");
  for (const [index, value] of (capture.georeferenceTransform?.firstEcefPositionMeters ?? []).entries()) {
    requireCondition(typeof value === "number" && Number.isFinite(value), `capture georeferenceTransform.firstEcefPositionMeters[${index}] must be finite`);
  }
  for (const [index, value] of (capture.georeferenceTransform?.firstUnrealPosition ?? []).entries()) {
    requireCondition(typeof value === "number" && Number.isFinite(value), `capture georeferenceTransform.firstUnrealPosition[${index}] must be finite`);
  }
  requireCondition(capture.georeferenceTransform.firstEcefPositionMeters.length === 3, "capture georeferenceTransform.firstEcefPositionMeters must contain 3 values");
  requireCondition(capture.georeferenceTransform.firstUnrealPosition.length === 3, "capture georeferenceTransform.firstUnrealPosition must contain 3 values");
  requireCondition(isObject(capture.externalMapApiRuntimeCheck), "capture must include externalMapApiRuntimeCheck");
  requireCondition(capture.externalMapApiRuntimeCheck.runtimeNetworkRequired === false, "capture externalMapApiRuntimeCheck.runtimeNetworkRequired must be false");
  requireCondition(Array.isArray(capture.externalMapApiRuntimeCheck.externalMapApis) && capture.externalMapApiRuntimeCheck.externalMapApis.length === 0, "capture externalMapApiRuntimeCheck.externalMapApis must be empty");
  requireCondition(Array.isArray(capture.externalMapApiRuntimeCheck.remoteTileServerUrls) && capture.externalMapApiRuntimeCheck.remoteTileServerUrls.length === 0, "capture externalMapApiRuntimeCheck.remoteTileServerUrls must be empty");
  requireCondition(Array.isArray(capture.renderedObjects) && capture.renderedObjects.length > 0, "capture must include rendered object observations");

  const terrainObservation = capture.renderedObjects.find((object) => object?.type === "offline-regional-terrain-mesh");
  requireCondition(isObject(terrainObservation), "capture must include an offline-regional-terrain-mesh rendered object");
  for (const key of ["sourceActor", "placementTransform", "renderSubmission", "contentSource"]) {
    requireCondition(isNonEmptyString(terrainObservation[key]), `terrain observation must include ${key}`);
  }
  requireCondition(
    terrainObservation.sourceActor === "AFlyingOfflinePilotTerrainActor",
    "rendered terrain object must come from AFlyingOfflinePilotTerrainActor",
  );
  requireCondition(
    terrainObservation.placementTransform === "UFlyingCesiumGeoreferenceComponent::TransformEcefPositionToUnreal",
    "terrain placement must use Cesium georeference position transforms",
  );
  requireCondition(
    terrainObservation.renderSubmission === "UProceduralMeshComponent::CreateMeshSection_LinearColor",
    "terrain render submission must create a procedural mesh section",
  );
  requireCondition(
    terrainObservation.contentSource === "installed-offline-regional-package",
    "terrain content source must be the installed offline regional package",
  );
}

function validateEvidence(relativeEvidencePath, options = {}) {
  const evidencePath = requireRepoFile(relativeEvidencePath, "evidence");
  const evidence = readJson(evidencePath);
  const currentCommit = runGit(["rev-parse", "HEAD"]);
  requireCondition(evidence.schemaVersion === "flying.georeferenced-content-rendering-evidence.v1", "schemaVersion is invalid");
  requireCondition(isIsoUtc(evidence.createdAt), "createdAt must be an ISO UTC timestamp");
  requireCondition(evidence.startupScenarioId === "startup.offline-regional-georeferenced-content", "startupScenarioId is invalid");
  requireCondition(evidence.evidenceSource === "current-repository-implementation-validation", "evidenceSource is invalid");
  requireCondition(evidence.fixture === false, "evidence.fixture must be false");
  requireCondition(evidence.synthetic === false, "evidence.synthetic must be false");
  requireCondition(evidence.readmeClaim === false, "evidence.readmeClaim must be false");
  requireCondition(evidence.offlineOnly === true, "evidence.offlineOnly must be true");
  requireCondition(isObject(evidence.build), "evidence must include build provenance");
  requireCondition(isNonEmptyString(evidence.build?.id), "evidence build.id is required");
  requireCondition(isNonEmptyString(evidence.build?.commit), "evidence build.commit is required");
  requireCondition(/^[a-f0-9]{40}$/i.test(evidence.build?.commit ?? ""), "evidence build.commit must be a Git commit id");
  requireCondition(evidence.build.commit === currentCommit, "evidence build.commit must match the current repository commit");
  requireCondition(isObject(evidence.runtimeValidation), "evidence must include runtimeValidation provenance");
  requireCondition(evidence.runtimeValidation?.automationTest === automationTestName, "runtimeValidation automationTest is invalid");
  requireCondition(
    evidence.runtimeValidation?.command === `node tools/validate_unreal_georeferenced_content.mjs --evidence ${canonicalEvidencePath} --require-unreal-runtime`,
    "runtimeValidation command must be the authoritative Unreal runtime validation command",
  );
  validateImplementation(evidence);
  if (options.validateCapture !== false) {
    validateCaptureArtifact(evidence);
  }
  return evidence;
}

function firstExisting(paths) {
  return paths.find((candidate) => candidate && fs.existsSync(candidate));
}

function findUnrealRoot() {
  const candidates = [
    process.env.UE_5_8_ROOT,
    process.env.UNREAL_ENGINE_5_8_ROOT,
    process.env.UNREAL_ENGINE_ROOT,
    process.env.UE_ROOT,
  ];
  if (process.platform === "win32") {
    candidates.push(
      "C:\\Program Files\\Epic Games\\UE_5.8",
      "C:\\Program Files\\Epic Games\\UE_5.8EA",
    );
  }
  return firstExisting(candidates);
}

function assertWindows11Host() {
  requireCondition(process.platform === "win32", "Unreal runtime validation requires Windows");
  const releaseParts = os.release().split(".").map((part) => Number.parseInt(part, 10));
  requireCondition((releaseParts[2] ?? 0) >= 22000, `Unreal runtime validation requires Windows 11, found ${os.release()}`);
}

function assertUnreal58(unrealRoot) {
  const versionPath = path.join(unrealRoot, "Engine", "Build", "Build.version");
  const version = JSON.parse(fs.readFileSync(versionPath, "utf8"));
  requireCondition(version.MajorVersion === 5 && version.MinorVersion === 8, `Unreal toolchain must be UE 5.8, found ${version.MajorVersion}.${version.MinorVersion}`);
  return version;
}

function hashAbsoluteFile(filePath, label) {
  requireCondition(fs.existsSync(filePath), `${label} does not exist: ${filePath}`);
  return crypto.createHash("sha256").update(fs.readFileSync(filePath)).digest("hex");
}

function parseRuntimeObservation(combinedOutput) {
  const pattern =
    /FlyingGeoreferencedContentRendering:\s*renderedTerrainSectionCount=(\d+)\s+terrainTileCount=(\d+)\s+imageryTileCount=(\d+)\s+renderedVertexCount=(\d+)\s+renderedTriangleCount=(\d+)\s+usedRemoteMapDependencies=(true|false)\s+terrainManifestPath="([^"]+)"\s+pilotRegionManifestPath="([^"]+)"\s+firstEcef=\(([-+0-9.eE]+),([-+0-9.eE]+),([-+0-9.eE]+)\)\s+firstUnreal=\(([-+0-9.eE]+),([-+0-9.eE]+),([-+0-9.eE]+)\)/;
  const match = combinedOutput.match(pattern);
  requireCondition(match, "Unreal automation output did not emit the full georeferenced content observation");

  return {
    renderedTerrainSectionCount: Number.parseInt(match[1], 10),
    terrainTileCount: Number.parseInt(match[2], 10),
    imageryTileCount: Number.parseInt(match[3], 10),
    renderedVertexCount: Number.parseInt(match[4], 10),
    renderedTriangleCount: Number.parseInt(match[5], 10),
    usedRemoteMapDependencies: match[6] === "true",
    terrainManifestPath: match[7],
    pilotRegionManifestPath: match[8],
    firstEcefPositionMeters: [Number(match[9]), Number(match[10]), Number(match[11])],
    firstUnrealPosition: [Number(match[12]), Number(match[13]), Number(match[14])],
  };
}

function validateRuntime(evidence) {
  assertWindows11Host();
  const unrealRoot = findUnrealRoot();
  requireCondition(unrealRoot, "UE 5.8 toolchain not found; set UE_5_8_ROOT or UNREAL_ENGINE_ROOT");
  const unrealVersion = assertUnreal58(unrealRoot);

  const editor = firstExisting([
    path.join(unrealRoot, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe"),
    path.join(unrealRoot, "Engine", "Binaries", "Win64", "UnrealEditor.exe"),
  ]);
  requireCondition(editor, "UnrealEditor-Cmd.exe not found in UE 5.8 root");

  const reportDir = path.join(repoRoot, "Saved", "Automation", "GeoreferencedContent");
  fs.mkdirSync(reportDir, { recursive: true });
  const projectPath = path.join(repoRoot, "unreal", "Flying.uproject");
  const result = spawnSync(
    editor,
    [
      projectPath,
      "-game",
      "-unattended",
      "-nopause",
      "-stdout",
      `-ReportOutputPath=${reportDir}`,
      `-ExecCmds=Automation RunTests ${automationTestName}; Quit`,
      "-testexit=Automation Test Queue Empty",
    ],
    {
      cwd: path.join(repoRoot, "unreal"),
      encoding: "utf8",
      shell: false,
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

  const combinedOutput = `${result.stdout ?? ""}\n${result.stderr ?? ""}`;
  requireCondition(result.status === 0, `Unreal automation runtime validation failed with exit code ${result.status}: ${combinedOutput}`);
  requireCondition(combinedOutput.includes(automationTestName), "Unreal automation output did not include the georeferenced content test");
  requireCondition(!/Error:/.test(combinedOutput), "Unreal automation output contains an error marker");
  requireCondition(evidence.runtimeValidation?.automationTest === automationTestName, "runtimeValidation evidence must name the executed automation test");

  const observation = parseRuntimeObservation(combinedOutput);
  requireCondition(observation.renderedTerrainSectionCount > 0, "Unreal automation rendered terrain section count must be greater than zero");
  requireCondition(observation.terrainTileCount > 0, "Unreal automation terrain tile count must be greater than zero");
  requireCondition(observation.imageryTileCount > 0, "Unreal automation imagery tile count must be greater than zero");
  requireCondition(observation.renderedVertexCount > 0, "Unreal automation rendered vertex count must be greater than zero");
  requireCondition(observation.renderedTriangleCount > 0, "Unreal automation rendered triangle count must be greater than zero");
  requireCondition(observation.usedRemoteMapDependencies === false, "Unreal automation must report no external map API or remote tile dependency use");

  const commit = runGit(["rev-parse", "HEAD"]);
  const captureArtifact = normalizedRepoPath(evidence.captureArtifact);
  requireCondition(captureArtifact !== null, "captureArtifact must be a repository-relative path");
  const capturedAt = new Date().toISOString().replace(/\.\d{3}Z$/, "Z");
  writeJson(captureArtifact, {
    schemaVersion: "flying.georeferenced-content-render-capture.v1",
    scenarioId: evidence.startupScenarioId,
    capturedAt,
    captureSource: "unreal-automation-test",
    fixture: false,
    synthetic: false,
    headlessRender: false,
    offlineOnly: true,
    automationTest: automationTestName,
    build: {
      id: evidence.build.id,
      commit,
      platform: "Win64",
      unrealEngine: `${unrealVersion.MajorVersion}.${unrealVersion.MinorVersion}`,
    },
    provenance: {
      generatedBy: "tools/validate_unreal_georeferenced_content.mjs --require-unreal-runtime",
      command: evidence.runtimeValidation.command,
      exitCode: result.status,
      reportOutputPath: "Saved/Automation/GeoreferencedContent",
      requires: ["windows", "unreal-engine-5.8", "installed-offline-regional-package"],
    },
    startupPath: {
      map: "/Engine/Maps/Templates/OpenWorld",
      gameMode: "/Script/FlyingPresentation.FlyingPilotGameMode",
      spawnedActor: "AFlyingOfflinePilotTerrainActor",
      loadMethod: "AFlyingOfflinePilotTerrainActor::LoadOfflinePackages",
    },
    offlineContent: {
      terrainManifestSetting: "Saved/Flying/PilotRegion/Terrain/terrain-package.json",
      pilotRegionManifestSetting: "Saved/Flying/PilotRegion/GIS/pilot-region-package.json",
      runtimeNetworkRequired: false,
    },
    offlinePackageManifests: {
      terrain: {
        path: observation.terrainManifestPath,
        sha256: hashAbsoluteFile(observation.terrainManifestPath, "terrain package manifest"),
        source: "installed-offline-regional-package",
      },
      pilotRegion: {
        path: observation.pilotRegionManifestPath,
        sha256: hashAbsoluteFile(observation.pilotRegionManifestPath, "pilot region package manifest"),
        source: "installed-offline-regional-package",
      },
    },
    externalMapApiRuntimeCheck: {
      runtimeNetworkRequired: false,
      externalMapApis: [],
      remoteTileServerUrls: [],
      source: "AFlyingOfflinePilotTerrainActor::LoadImageryPackage",
    },
    georeferenceTransform: {
      component: "UFlyingCesiumGeoreferenceComponent",
      positionTransform: "TransformEcefPositionToUnreal",
      firstEcefPositionMeters: observation.firstEcefPositionMeters,
      firstUnrealPosition: observation.firstUnrealPosition,
    },
    observations: {
      renderedTerrainSectionCount: observation.renderedTerrainSectionCount,
      terrainTileCount: observation.terrainTileCount,
      imageryTileCount: observation.imageryTileCount,
      renderedVertexCount: observation.renderedVertexCount,
      renderedTriangleCount: observation.renderedTriangleCount,
      usedRemoteMapDependencies: observation.usedRemoteMapDependencies,
    },
    renderedObjects: [
      {
        type: "offline-regional-terrain-mesh",
        sourceActor: "AFlyingOfflinePilotTerrainActor",
        contentSource: "installed-offline-regional-package",
        placementTransform: "UFlyingCesiumGeoreferenceComponent::TransformEcefPositionToUnreal",
        normalTransform: "UFlyingCesiumGeoreferenceComponent::TransformEcefDirectionToUnreal",
        renderSubmission: "UProceduralMeshComponent::CreateMeshSection_LinearColor",
        materialSource: "installed-offline-regional-imagery",
        collisionEnabled: true,
      },
    ],
    validatedImplementationRefs: [
      "unreal/Source/FlyingPresentation/Private/FlyingPilotGameMode.cpp",
      "unreal/Source/FlyingPresentation/Private/FlyingOfflinePilotTerrainActor.cpp",
      "unreal/Source/FlyingPresentation/Private/FlyingCesiumGeoreferenceComponent.cpp",
      "unreal/Source/FlyingPresentation/Private/FlyingGeoreferencedContentRenderingAutomation.cpp",
      "unreal/Config/DefaultEngine.ini",
      "unreal/Config/DefaultGame.ini",
    ],
  });

  validateCaptureArtifact(evidence);
}

const args = parseArgs(process.argv.slice(2));
const evidence = validateEvidence(args.evidence, {
  validateCapture: !args.requireUnrealRuntime,
});
if (args.requireUnrealRuntime) {
  validateRuntime(evidence);
}

console.log("validate_unreal_georeferenced_content: ok");
