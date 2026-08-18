#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { MANDATORY_VISUAL_SCENES } from "./mandatory_visual_scenes.mjs";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const schemaPath = "Build/VisualEvidence/visual-evidence-manifest.schema.json";

function readJsonFile(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (error) {
    throw new Error(`invalid JSON in ${filePath}: ${error.message}`);
  }
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function isNonEmptyString(value) {
  return typeof value === "string" && value.trim().length > 0;
}

function isFiniteNumber(value) {
  return typeof value === "number" && Number.isFinite(value);
}

function isIsoUtc(value) {
  return typeof value === "string" && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(value);
}

function isRemotePath(value) {
  return /^[a-z][a-z0-9+.-]*:\/\//i.test(value);
}

function isWindowsDrivePath(value) {
  return /^[a-z]:[\\/]/i.test(value);
}

function isUncPath(value) {
  return /^\\\\|^\/\//.test(value);
}

function isPackageRelativePath(value) {
  if (!isNonEmptyString(value)) {
    return false;
  }
  if (isRemotePath(value) || path.isAbsolute(value) || isWindowsDrivePath(value) || isUncPath(value)) {
    return false;
  }
  const normalized = path.posix.normalize(value.replaceAll("\\", "/"));
  return normalized !== "." && !normalized.startsWith("../") && normalized !== "..";
}

function add(errors, message) {
  errors.push(message);
}

function requireCondition(errors, condition, message) {
  if (!condition) {
    add(errors, message);
  }
}

function collectForbiddenAssets(scan) {
  const registered = Array.isArray(scan?.registeredPlaceholderAssets) ? scan.registeredPlaceholderAssets : [];
  const primitives = Array.isArray(scan?.primitiveTestMeshes) ? scan.primitiveTestMeshes : [];
  return new Set([...registered, ...primitives].filter(isNonEmptyString));
}

function validateCapture(errors, capture, index, mandatoryScenes, forbiddenAssets) {
  const label = `captures[${index}]`;
  requireCondition(errors, isObject(capture), `${label} must be an object`);
  if (!isObject(capture)) {
    return;
  }

  requireCondition(errors, mandatoryScenes.has(capture.scenarioId), `${label}.scenarioId must reference a mandatory scene`);
  requireCondition(errors, capture.captureSource === "win64-unreal-shipping-runtime", `${label}.captureSource must be win64-unreal-shipping-runtime`);
  requireCondition(errors, capture.headless === false, `${label}.headless must be false`);
  requireCondition(errors, capture.syntheticRaster === false, `${label}.syntheticRaster must be false`);

  const artifacts = capture.artifacts;
  requireCondition(errors, Array.isArray(artifacts) && artifacts.length > 0, `${label}.artifacts must be a non-empty array`);
  if (Array.isArray(artifacts)) {
    const hasVisualArtifact = artifacts.some((artifact) => artifact?.type === "screenshot" || artifact?.type === "video");
    requireCondition(errors, hasVisualArtifact, `${label}.artifacts must include a screenshot or video`);
    artifacts.forEach((artifact, artifactIndex) => {
      const artifactLabel = `${label}.artifacts[${artifactIndex}]`;
      requireCondition(errors, isObject(artifact), `${artifactLabel} must be an object`);
      if (!isObject(artifact)) {
        return;
      }
      requireCondition(errors, ["screenshot", "video", "audio"].includes(artifact.type), `${artifactLabel}.type is unsupported`);
      requireCondition(errors, isPackageRelativePath(artifact.path), `${artifactLabel}.path must be a local package-relative path`);
    });
  }

  const metrics = capture.performanceMetrics;
  requireCondition(errors, isObject(metrics), `${label}.performanceMetrics must be present`);
  if (isObject(metrics)) {
    for (const key of ["averageFps", "onePercentLowFps", "streamingHitchMsMax", "frameTimeMsP95"]) {
      requireCondition(errors, isFiniteNumber(metrics[key]), `${label}.performanceMetrics.${key} must be a finite number`);
    }
    if (isFiniteNumber(metrics.averageFps)) {
      requireCondition(errors, metrics.averageFps > 0, `${label}.performanceMetrics.averageFps must be greater than zero`);
    }
    if (isFiniteNumber(metrics.onePercentLowFps)) {
      requireCondition(errors, metrics.onePercentLowFps > 0, `${label}.performanceMetrics.onePercentLowFps must be greater than zero`);
    }
    if (isFiniteNumber(metrics.streamingHitchMsMax)) {
      requireCondition(errors, metrics.streamingHitchMsMax >= 0, `${label}.performanceMetrics.streamingHitchMsMax must not be negative`);
    }
    if (isFiniteNumber(metrics.frameTimeMsP95)) {
      requireCondition(errors, metrics.frameTimeMsP95 > 0, `${label}.performanceMetrics.frameTimeMsP95 must be greater than zero`);
    }
  }

  requireCondition(errors, Array.isArray(capture.assetRefs), `${label}.assetRefs must be an array`);
  if (Array.isArray(capture.assetRefs)) {
    for (const assetRef of capture.assetRefs) {
      if (forbiddenAssets.has(assetRef)) {
        add(errors, `${label}.assetRefs contains registered placeholder or primitive test mesh: ${assetRef}`);
      }
      if (/\/(?:Engine\/BasicShapes|StarterContent)\//i.test(assetRef) || /(?:placeholder|test[_-]?mesh|primitive)/i.test(assetRef)) {
        add(errors, `${label}.assetRefs contains forbidden placeholder-like asset reference: ${assetRef}`);
      }
    }
  }
}

export function validateVisualEvidenceManifest(manifest, options = {}) {
  const errors = [];

  requireCondition(errors, isObject(manifest), "manifest must be a JSON object");
  if (!isObject(manifest)) {
    return { ok: false, errors };
  }

  requireCondition(errors, manifest.schemaVersion === "flying.visual-evidence-manifest.v1", "schemaVersion must be flying.visual-evidence-manifest.v1");
  requireCondition(errors, isIsoUtc(manifest.createdAt), "createdAt must be an ISO UTC timestamp");

  const build = manifest.build;
  requireCondition(errors, isObject(build), "build must be present");
  if (isObject(build)) {
    requireCondition(errors, isNonEmptyString(build.id), "build.id is required");
    requireCondition(errors, build.platform === "Win64", "build.platform must be Win64");
    requireCondition(errors, build.configuration === "Shipping", "build.configuration must be Shipping");
    requireCondition(errors, build.renderer === "Unreal", "build.renderer must be Unreal");
  }

  const profile = manifest.graphicsProfile;
  requireCondition(errors, isObject(profile), "graphicsProfile must be present");
  if (isObject(profile)) {
    requireCondition(errors, isNonEmptyString(profile.id), "graphicsProfile.id is required");
    requireCondition(errors, isNonEmptyString(profile.qualityPreset), "graphicsProfile.qualityPreset is required");
    requireCondition(errors, Number.isInteger(profile.resolution?.width) && profile.resolution.width > 0, "graphicsProfile.resolution.width must be a positive integer");
    requireCondition(errors, Number.isInteger(profile.resolution?.height) && profile.resolution.height > 0, "graphicsProfile.resolution.height must be a positive integer");
  }

  requireCondition(errors, Array.isArray(manifest.mandatoryScenes) && manifest.mandatoryScenes.length > 0, "mandatoryScenes must be a non-empty array");
  const mandatoryScenes = new Set();
  if (Array.isArray(manifest.mandatoryScenes)) {
    for (const scene of manifest.mandatoryScenes) {
      requireCondition(errors, isNonEmptyString(scene), "mandatoryScenes entries must be non-empty strings");
      mandatoryScenes.add(scene);
    }
  }
  const requiredScenes = new Set([...MANDATORY_VISUAL_SCENES, ...(options.requiredScenes ?? [])]);
  for (const scene of requiredScenes) {
    requireCondition(errors, mandatoryScenes.has(scene), `mandatoryScenes missing required scene: ${scene}`);
  }

  const baseline = manifest.baselineApproval;
  requireCondition(errors, isObject(baseline), "baselineApproval must be present");
  if (isObject(baseline)) {
    requireCondition(errors, baseline.state === "approved", "baselineApproval.state must be approved");
    requireCondition(errors, isNonEmptyString(baseline.approvedBy), "baselineApproval.approvedBy is required");
    requireCondition(errors, isIsoUtc(baseline.approvedAtUtc), "baselineApproval.approvedAtUtc must be an ISO UTC timestamp");
    requireCondition(errors, isNonEmptyString(baseline.baselineId), "baselineApproval.baselineId is required");
  }

  const scan = manifest.placeholderScan;
  requireCondition(errors, isObject(scan), "placeholderScan must be present");
  if (isObject(scan)) {
    requireCondition(errors, scan.result === "pass", "placeholderScan.result must be pass");
    requireCondition(errors, isIsoUtc(scan.scannedAtUtc), "placeholderScan.scannedAtUtc must be an ISO UTC timestamp");
    requireCondition(errors, Array.isArray(scan.registeredPlaceholderAssets), "placeholderScan.registeredPlaceholderAssets must be an array");
    requireCondition(errors, Array.isArray(scan.primitiveTestMeshes), "placeholderScan.primitiveTestMeshes must be an array");
    requireCondition(errors, Array.isArray(scan.findings) && scan.findings.length === 0, "placeholderScan.findings must be an empty array");
  }

  requireCondition(errors, Array.isArray(manifest.captures) && manifest.captures.length > 0, "captures must be a non-empty array");
  const coveredScenes = new Set();
  const forbiddenAssets = collectForbiddenAssets(scan);
  if (Array.isArray(manifest.captures)) {
    manifest.captures.forEach((capture, index) => {
      if (isObject(capture) && isNonEmptyString(capture.scenarioId)) {
        coveredScenes.add(capture.scenarioId);
      }
      validateCapture(errors, capture, index, mandatoryScenes, forbiddenAssets);
    });
  }

  for (const scene of mandatoryScenes) {
    requireCondition(errors, coveredScenes.has(scene), `missing capture for mandatory scene: ${scene}`);
  }

  return { ok: errors.length === 0, errors };
}

export function validateVisualEvidenceManifestFile(manifestPath, options = {}) {
  const resolvedPath = path.resolve(options.cwd ?? process.cwd(), manifestPath);
  const manifest = readJsonFile(resolvedPath);
  return validateVisualEvidenceManifest(manifest, options);
}

function parseArgs(argv) {
  const args = {
    manifest: null,
    expectFailure: false,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--manifest") {
      args.manifest = argv[index + 1];
      index += 1;
    } else if (arg === "--expect-failure") {
      args.expectFailure = true;
    } else {
      throw new Error(`unsupported argument: ${arg}`);
    }
  }
  if (!args.manifest) {
    throw new Error("usage: node Validation/VisualAcceptance/visual_acceptance_validator.mjs --manifest <path> [--expect-failure]");
  }
  return args;
}

function main() {
  let args;
  try {
    args = parseArgs(process.argv.slice(2));
  } catch (error) {
    console.error(`visual_acceptance_validator: ${error.message}`);
    process.exit(2);
  }

  if (!fs.existsSync(path.join(repoRoot, schemaPath))) {
    console.error(`visual_acceptance_validator: missing schema: ${schemaPath}`);
    process.exit(1);
  }

  let result;
  try {
    result = validateVisualEvidenceManifestFile(args.manifest);
  } catch (error) {
    console.error(`visual_acceptance_validator: ${error.message}`);
    process.exit(args.expectFailure ? 0 : 1);
  }

  if (args.expectFailure) {
    if (result.ok) {
      console.error("visual_acceptance_validator: expected validation failure but manifest passed");
      process.exit(1);
    }
    console.log("visual_acceptance_validator: expected failure");
    process.exit(0);
  }

  if (!result.ok) {
    for (const error of result.errors) {
      console.error(`visual_acceptance_validator: ${error}`);
    }
    process.exit(1);
  }

  console.log("visual_acceptance_validator: ok");
}

if (import.meta.url === `file://${process.argv[1]}`) {
  main();
}
