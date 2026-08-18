#!/usr/bin/env node

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const automationTestName = "Flying.Presentation.GeoreferencedContent.StartupScenarioRendersOfflineRegionalContent";

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
  requireCodePattern(terrainHeader, /UFlyingCesiumGeoreferenceComponent/, terrainHeaderPath, "must own the Cesium georeference component");

  const terrainActor = read(terrainActorPath);
  requireCondition(
    /Vertices\.Add\(\s*GeoreferenceComponent->TransformEcefPositionToUnreal\(EcefPosition\)\s*\)/.test(stripCppComments(terrainActor)),
    "terrain vertices must be placed through the Cesium georeference transform",
  );
  requireCondition(
    /TerrainMesh->CreateMeshSection_LinearColor\([\s\S]*?Vertices[\s\S]*?Triangles[\s\S]*?Normals[\s\S]*?VertexColors[\s\S]*?true\)/.test(stripCppComments(terrainActor)),
    "offline regional content must be submitted to a visible procedural mesh section with collision",
  );
  requireCodePattern(terrainActor, /LoadTerrainTiles\([\s\S]*?TerrainTiles\)/, terrainActorPath, "must load offline terrain tiles");
  requireCodePattern(terrainActor, /LoadImageryPackage\([\s\S]*?ImageryTiles\)/, terrainActorPath, "must load offline imagery from the pilot region package");
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
  requireCodePattern(automationTest, /renderedTerrainSectionCount=%d/, automationTestPath, "must emit observed rendered section count for artifact generation");

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
  requireCondition(isObject(capture.provenance), "capture must include generation provenance");
  requireCondition(capture.provenance?.generatedBy === "tools/validate_unreal_georeferenced_content.mjs --require-unreal-runtime", "capture provenance generatedBy is invalid");
  requireCondition(capture.provenance?.exitCode === 0, "capture provenance exitCode must be zero");
  requireCondition(Number.isInteger(capture.observations?.renderedTerrainSectionCount), "capture observations.renderedTerrainSectionCount must be an integer");
  requireCondition(capture.observations.renderedTerrainSectionCount > 0, "capture observations.renderedTerrainSectionCount must be greater than zero");
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
  requireCondition(isObject(evidence.runtimeValidation), "evidence must include runtimeValidation provenance");
  requireCondition(evidence.runtimeValidation?.automationTest === automationTestName, "runtimeValidation automationTest is invalid");
  requireCondition(
    evidence.runtimeValidation?.command === "node tools/validate_unreal_georeferenced_content.mjs --evidence Build/VisualEvidence/rc-startup-georef-rendering.json --require-unreal-runtime",
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

  const sectionCountMatch = combinedOutput.match(/FlyingGeoreferencedContentRendering:\s*renderedTerrainSectionCount=(\d+)/);
  requireCondition(sectionCountMatch, "Unreal automation output did not emit rendered terrain section count");
  const renderedTerrainSectionCount = Number.parseInt(sectionCountMatch[1], 10);
  requireCondition(renderedTerrainSectionCount > 0, "Unreal automation rendered terrain section count must be greater than zero");

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
    observations: {
      renderedTerrainSectionCount,
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
