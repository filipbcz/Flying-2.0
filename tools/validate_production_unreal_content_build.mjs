#!/usr/bin/env node

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const defaultManifestPath = "docs/validation/visual/production-content-build-manifest.json";
const defaultStartupReportPath = "docs/validation/visual/startup-map-validation-report.json";
const defaultPlaceholderReportPath = "docs/validation/visual/placeholder-scan-report.json";
const defaultInventoryPath = "docs/validation/visual/acceptance-scene-asset-inventory.json";
const defaultRuntimeValidationCommand = [
  "tools/validate_unreal_georeferenced_content.mjs",
  "--evidence",
  "docs/validation/visual/rc-startup-georef-rendering.json",
  "--require-unreal-runtime",
];
const requiredSceneContent = [
  "region",
  "georeference",
  "terrain",
  "imagery",
  "vectorLayers",
  "water",
  "vegetation",
  "airportContent",
  "weather",
  "aircraft",
  "cockpit",
];
const forbiddenAssetPatterns = [
  /\/Engine\/BasicShapes\//i,
  /\/StarterContent\//i,
  /(?:^|[\\/_-])placeholder(?:[\\/_-]|$)/i,
  /test[_-]?mesh/i,
  /(?:^|[\\/_-])primitive(?:[\\/_-]|$)/i,
];

function fail(message) {
  console.error(`validate_production_unreal_content_build: ${message}`);
  process.exit(1);
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
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

function parseArgs(argv) {
  const args = {
    manifest: defaultManifestPath,
    startupReport: defaultStartupReportPath,
    placeholderReport: defaultPlaceholderReportPath,
    assetInventory: null,
    validateAssetInventoryFixture: null,
    selfTest: false,
    requireUnrealRuntime: false,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--manifest") {
      args.manifest = argv[index + 1];
      index += 1;
    } else if (arg === "--startup-report") {
      args.startupReport = argv[index + 1];
      index += 1;
    } else if (arg === "--placeholder-report") {
      args.placeholderReport = argv[index + 1];
      index += 1;
    } else if (arg === "--asset-inventory") {
      args.assetInventory = argv[index + 1];
      index += 1;
    } else if (arg === "--validate-asset-inventory-fixture") {
      args.validateAssetInventoryFixture = argv[index + 1];
      index += 1;
    } else if (arg === "--self-test") {
      args.selfTest = true;
    } else if (arg === "--require-unreal-runtime") {
      args.requireUnrealRuntime = true;
    } else {
      fail(`unsupported argument: ${arg}`);
    }
  }
  return args;
}

function normalizedLocalPath(value, label, { allowFixtures = false } = {}) {
  requireCondition(isNonEmptyString(value), `${label} must be a non-empty repository-relative path`);
  requireCondition(!/^[a-z][a-z0-9+.-]*:\/\//i.test(value), `${label} must not be remote: ${value}`);
  requireCondition(!/^[a-z]:[\\/]/i.test(value), `${label} must not be drive-qualified: ${value}`);
  requireCondition(!/^\\\\|^\/\//.test(value), `${label} must not be UNC: ${value}`);
  const normalized = path.posix.normalize(value.replaceAll("\\", "/"));
  requireCondition(normalized !== "." && !normalized.startsWith("../") && !normalized.startsWith("/"), `${label} must stay inside the repository: ${value}`);
  requireCondition(
    allowFixtures || !/(^|\/)(?:fixtures?|synthetic|declarations?)(?:\/|$)/i.test(normalized),
    `${label} cannot be fixture, synthetic, or declaration proof: ${normalized}`,
  );
  return normalized;
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function isNonEmptyString(value) {
  return typeof value === "string" && value.trim().length > 0;
}

function normalizedRepoPath(value, label) {
  return normalizedLocalPath(value, label);
}

function requireRepoFile(value, label) {
  const normalized = normalizedRepoPath(value, label);
  requireCondition(fs.existsSync(path.join(repoRoot, normalized)), `${label} does not exist: ${normalized}`);
  return normalized;
}

function sha256(relativePath) {
  return crypto.createHash("sha256").update(fs.readFileSync(path.join(repoRoot, relativePath))).digest("hex");
}

function jsonPointers(value, base = "") {
  const entries = [];
  if (Array.isArray(value)) {
    for (let index = 0; index < value.length; index += 1) {
      entries.push(...jsonPointers(value[index], `${base}/${index}`));
    }
  } else if (isObject(value)) {
    for (const [key, child] of Object.entries(value)) {
      entries.push(...jsonPointers(child, `${base}/${key}`));
    }
  } else if (typeof value === "string") {
    entries.push([base, value]);
  }
  return entries;
}

function requireNoForbiddenAssetRefs(document, label) {
  for (const [pointer, value] of jsonPointers(document)) {
    for (const pattern of forbiddenAssetPatterns) {
      requireCondition(!pattern.test(value), `${label}${pointer} contains forbidden placeholder/default primitive reference: ${value}`);
    }
  }
}

function requireNoForbiddenAssetRef(assetRef, label) {
  requireCondition(isNonEmptyString(assetRef), `${label}.assetRef must be a non-empty string`);
  for (const pattern of forbiddenAssetPatterns) {
    requireCondition(!pattern.test(assetRef), `${label}.assetRef contains forbidden placeholder/default primitive reference: ${assetRef}`);
  }
}

function validateChecksummedInputs(manifest) {
  requireCondition(Array.isArray(manifest.buildInputs) && manifest.buildInputs.length > 0, "manifest.buildInputs must be non-empty");
  const ids = new Set();
  for (const input of manifest.buildInputs) {
    requireCondition(isObject(input), "manifest.buildInputs entries must be objects");
    requireCondition(isNonEmptyString(input.id), "build input id is required");
    requireCondition(!ids.has(input.id), `duplicate build input id: ${input.id}`);
    ids.add(input.id);
    const relativePath = requireRepoFile(input.path, `build input ${input.id}`);
    requireCondition(/^[a-f0-9]{64}$/.test(input.sha256 ?? ""), `build input ${input.id} must carry a SHA-256 digest`);
    requireCondition(sha256(relativePath) === input.sha256, `checksum mismatch for build input ${input.id}: ${relativePath}`);
  }
  for (const requiredId of [
    "unreal-project",
    "startup-engine-config",
    "startup-game-config",
    "startup-game-mode",
    "offline-terrain-actor",
    "terrain-package",
    "visual-package",
    "world-procedural-rules",
    "cockpit-layout",
    "primary-aircraft",
  ]) {
    requireCondition(ids.has(requiredId), `manifest.buildInputs missing required input: ${requiredId}`);
  }
}

function validateManifest(manifest, paths) {
  requireCondition(manifest.schemaVersion === "flying.production-unreal-content-build.v1", "manifest schemaVersion mismatch");
  requireCondition(manifest.contentMode === "authorized-reproducible-content-build", "manifest must use an authorized reproducible content-build");
  requireCondition(manifest.status === "pass", "manifest status must be pass");
  requireCondition(manifest.scope?.runtimeTarget?.platform === "Win64", "runtime target must be Win64");
  requireCondition(manifest.scope?.runtimeTarget?.configuration === "Shipping", "runtime target must be Shipping");
  requireCondition(manifest.scope?.runtimeTarget?.unrealEngine === "5.8", "runtime target must be UE 5.8");
  requireCondition(manifest.authorization?.prohibitedSubstitutesRejected === true, "prohibited substitutes must be rejected");
  validateChecksummedInputs(manifest);
  requireCondition(manifest.startupMap?.manualSetupRequired === false, "startup map must require no manual setup");
  requireCondition(manifest.startupMap?.offlineOnly === true, "startup map must be offline-only");
  requireCondition(manifest.startupMap?.gameMode === "/Script/FlyingPresentation.FlyingPilotGameMode", "startup map must use FlyingPilotGameMode");
  for (const content of requiredSceneContent) {
    requireCondition(manifest.requiredAcceptanceSceneContent?.includes(content), `manifest missing startup content: ${content}`);
    requireCondition(
      manifest.generatedRuntimeContent?.some((entry) => entry.outputs?.includes(content)),
      `manifest generated runtime content missing output: ${content}`,
    );
  }
  requireCondition(manifest.placeholderPolicy?.rejectMissingExternalFiles === true, "placeholder policy must reject missing external files");
  requireCondition(manifest.placeholderPolicy?.rejectRegisteredPlaceholderAssets === true, "placeholder policy must reject registered placeholders");
  requireCondition(manifest.placeholderPolicy?.rejectEngineBasicShapes === true, "placeholder policy must reject Engine BasicShapes");
  requireCondition(manifest.placeholderPolicy?.rejectStarterContent === true, "placeholder policy must reject StarterContent");
  requireCondition(manifest.placeholderPolicy?.rejectPrimitiveTestMeshes === true, "placeholder policy must reject primitive test meshes");
  requireCondition(manifest.placeholderPolicy?.assetDependencyInventory === (paths.assetInventory ?? defaultInventoryPath), "manifest must link the acceptance-scene asset dependency inventory");
  requireCondition(manifest.validationReports?.startupMap === paths.startupReport, "manifest must link the startup map report");
  requireCondition(manifest.validationReports?.placeholderScan === paths.placeholderReport, "manifest must link the placeholder scan report");
  requireNoForbiddenAssetRefs(manifest.generatedRuntimeContent, "manifest.generatedRuntimeContent");
}

function validateStartupReport(report, paths) {
  requireCondition(report.schemaVersion === "flying.startup-map-validation-report.v1", "startup report schemaVersion mismatch");
  requireCondition(report.result === "pass", "startup report result must be pass");
  requireCondition(report.validatedManifest === paths.manifest, "startup report must validate the production content manifest");
  requireCondition(report.manualSetupRequired === false, "startup report must require no manual setup");
  requireCondition(report.offlineOnly === true, "startup report must be offline-only");
  requireCondition(report.startupBindings?.gameMode === "/Script/FlyingPresentation.FlyingPilotGameMode", "startup report must bind FlyingPilotGameMode");
  requireCondition(report.startupBindings?.spawnDefaultSceneOnBeginPlay === true, "startup report must spawn the startup scene on BeginPlay");
  requireCondition(isObject(report.runtimeValidation), "startup report must include runtime validation");
  requireCondition(
    report.runtimeValidation?.automationTest === "Flying.Presentation.GeoreferencedContent.StartupScenarioRendersOfflineRegionalContent",
    "startup report runtime validation must name the startup automation test",
  );
  requireCondition(
    report.runtimeValidation?.command === "node tools/validate_production_unreal_content_build.mjs --require-unreal-runtime",
    "startup report runtime validation must use the production content-build runtime command",
  );
  requireCondition(
    report.runtimeValidation?.delegatesTo ===
      "node tools/validate_unreal_georeferenced_content.mjs --evidence docs/validation/visual/rc-startup-georef-rendering.json --require-unreal-runtime",
    "startup report runtime validation must delegate to the Unreal startup automation validator",
  );
  requireCondition(
    Array.isArray(report.runtimeValidation?.requiredCapabilities) &&
      report.runtimeValidation.requiredCapabilities.includes("windows") &&
      report.runtimeValidation.requiredCapabilities.includes("unreal-engine-5.8"),
    "startup report runtime validation must require Windows and UE 5.8",
  );
  for (const content of requiredSceneContent) {
    const entry = report.loadPath?.find((candidate) => candidate.content === content);
    requireCondition(entry, `startup report missing load path for ${content}`);
    if (!String(entry.source).includes("#")) {
      requireRepoFile(entry.source, `startup report source for ${content}`);
    }
    requireCondition(isNonEmptyString(entry.loader), `startup report loader missing for ${content}`);
  }
  requireCondition(report.requiredAssertions?.allRequiredContentPresent === true, "startup report must assert all required content");
  requireCondition(report.requiredAssertions?.noManualMapSetup === true, "startup report must assert no manual map setup");
  requireCondition(report.requiredAssertions?.runtimeNetworkRequired === false, "startup report must assert offline runtime");
  requireCondition(Array.isArray(report.requiredAssertions?.externalMapApis) && report.requiredAssertions.externalMapApis.length === 0, "startup report externalMapApis must be empty");
  requireCondition(Array.isArray(report.requiredAssertions?.remoteTileServerUrls) && report.requiredAssertions.remoteTileServerUrls.length === 0, "startup report remoteTileServerUrls must be empty");
  requireCondition(Array.isArray(report.requiredAssertions?.missingExternalFiles) && report.requiredAssertions.missingExternalFiles.length === 0, "startup report must have no missing external files");
  requireNoForbiddenAssetRefs(report.loadPath, "startupReport.loadPath");
}

function validateAssetDependencyInventory(inventory, label = "asset dependency inventory") {
  requireCondition(inventory.schemaVersion === "flying.acceptance-scene-asset-inventory.v1", `${label} schemaVersion mismatch`);
  requireCondition(Array.isArray(inventory.scenes) && inventory.scenes.length > 0, `${label} must include scenes`);
  const startupScene = inventory.scenes.find((scene) => scene?.sceneId === "startup.offline-regional-georeferenced-content");
  requireCondition(isObject(startupScene), `${label} must include the startup acceptance scene`);
  requireCondition(Array.isArray(startupScene.dependencies), `${label} startup scene dependencies must be an array`);
  const content = new Set();
  for (const dependency of startupScene.dependencies) {
    requireCondition(isObject(dependency), `${label} dependency entries must be objects`);
    requireCondition(isNonEmptyString(dependency.content), `${label} dependency content is required`);
    content.add(dependency.content);
    requireNoForbiddenAssetRef(dependency.assetRef, `${label} ${dependency.content}`);
    const dependencyPath = requireRepoFile(dependency.path, `${label} ${dependency.content}.path`);
    requireCondition(!/README(?:\.[a-z0-9]+)?$/i.test(dependencyPath), `${label} ${dependency.content}.path cannot be README-only evidence`);
  }
  for (const requiredContent of requiredSceneContent) {
    requireCondition(content.has(requiredContent), `${label} missing dependency for ${requiredContent}`);
  }
}

function validatePlaceholderReport(report, paths) {
  requireCondition(report.schemaVersion === "flying.production-placeholder-scan.v1", "placeholder report schemaVersion mismatch");
  requireCondition(report.result === "pass", "placeholder report result must be pass");
  const inventoryPath = report.assetDependencyInventory ?? paths.assetInventory;
  requireCondition(inventoryPath === paths.assetInventory, "placeholder report must link the selected asset dependency inventory");
  requireCondition(Array.isArray(report.registeredPlaceholderAssets) && report.registeredPlaceholderAssets.length === 0, "registeredPlaceholderAssets must be empty");
  requireCondition(Array.isArray(report.primitiveTestMeshes) && report.primitiveTestMeshes.length === 0, "primitiveTestMeshes must be empty");
  requireCondition(Array.isArray(report.findings) && report.findings.length === 0, "placeholder findings must be empty");
  requireCondition(Array.isArray(report.missingExternalFiles) && report.missingExternalFiles.length === 0, "missingExternalFiles must be empty");
  requireCondition(report.requiredAcceptanceScenes?.includes("startup.offline-regional-georeferenced-content"), "placeholder report must scan the startup acceptance scene");
  validateAssetDependencyInventory(readJson(inventoryPath), `placeholder report inventory ${inventoryPath}`);
}

function validateRuntimeStartupLoad() {
  const result = spawnSync("node", defaultRuntimeValidationCommand, {
    cwd: repoRoot,
    encoding: "utf8",
    shell: false,
    stdio: ["ignore", "pipe", "pipe"],
  });
  requireCondition(
    result.status === 0,
    `Unreal startup runtime validation failed with exit code ${result.status}: ${(result.stderr || result.stdout).trim()}`,
  );
}

function expectFixtureFailure(fixturePath, expectedPattern) {
  const result = spawnSync(
    "node",
    ["tools/validate_production_unreal_content_build.mjs", "--validate-asset-inventory-fixture", fixturePath],
    {
      cwd: repoRoot,
      encoding: "utf8",
      shell: false,
      stdio: ["ignore", "pipe", "pipe"],
    },
  );
  requireCondition(result.status !== 0, `${fixturePath} unexpectedly passed`);
  const output = `${result.stderr ?? ""}\n${result.stdout ?? ""}`;
  requireCondition(expectedPattern.test(output), `${fixturePath} failed for the wrong reason: ${output.trim()}`);
}

function runSelfTest() {
  expectFixtureFailure(
    "tests/visual_acceptance/production_content_build/missing-external-file.asset-inventory.json",
    /does not exist/,
  );
  expectFixtureFailure(
    "tests/visual_acceptance/production_content_build/engine-basic-shapes.asset-inventory.json",
    /forbidden placeholder\/default primitive/,
  );
  expectFixtureFailure(
    "tests/visual_acceptance/production_content_build/starter-content.asset-inventory.json",
    /forbidden placeholder\/default primitive/,
  );
  expectFixtureFailure(
    "tests/visual_acceptance/production_content_build/placeholder-mesh.asset-inventory.json",
    /forbidden placeholder\/default primitive/,
  );
}

const args = parseArgs(process.argv.slice(2));
if (args.validateAssetInventoryFixture) {
  const fixturePath = normalizedLocalPath(args.validateAssetInventoryFixture, "asset inventory fixture", { allowFixtures: true });
  validateAssetDependencyInventory(readJson(fixturePath), fixturePath);
  console.log("validate_production_unreal_content_build: ok");
  process.exit(0);
}
const paths = {
  manifest: normalizedRepoPath(args.manifest, "manifest"),
  startupReport: normalizedRepoPath(args.startupReport, "startup report"),
  placeholderReport: normalizedRepoPath(args.placeholderReport, "placeholder report"),
  assetInventory: normalizedRepoPath(args.assetInventory ?? defaultInventoryPath, "asset inventory"),
};
const manifest = readJson(paths.manifest);
const startupReport = readJson(paths.startupReport);
const placeholderReport = readJson(paths.placeholderReport);

validateManifest(manifest, paths);
validateStartupReport(startupReport, paths);
validatePlaceholderReport(placeholderReport, paths);
if (args.selfTest) {
  runSelfTest();
}
if (args.requireUnrealRuntime) {
  validateRuntimeStartupLoad();
}

console.log("validate_production_unreal_content_build: ok");
