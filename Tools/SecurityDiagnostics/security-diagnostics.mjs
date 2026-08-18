#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";

const REQUIRED_ATOMIC_CATEGORIES = [
  "settings",
  "saves",
  "profiles",
  "scenarios",
  "consentState",
];

const REQUIRED_DIAGNOSTICS = [
  "sbom",
  "licenseInventory",
  "structuredLogs",
  "minidumpConfig",
  "buildId",
];

const REQUIRED_DIAGNOSTIC_TOKENS = {
  sbom: ["\"SPDXID\"", "\"SPDXRef-DOCUMENT\"", "\"spdxVersion\""],
  licenseInventory: ["Flying 1.0.0 Win64 offline release", "Required release action"],
  structuredLogs: [
    "StructuredLogPath=Saved/Flying/Diagnostics/structured-log.jsonl",
    "flying.structured-log.v1",
    "personalTelemetry",
  ],
  minidumpConfig: [
    "[CrashReportClient]",
    "bImplicitSend=False",
    "bSendLogFile=False",
    "bUseCrashReportClient = true",
    "CrashDiagnosticsDirectory",
    "SetGameData",
  ],
};

function fail(message, code = 1) {
  console.error(`security-diagnostics: ${message}`);
  process.exit(code);
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function readJson(filePath) {
  const resolved = path.resolve(process.cwd(), filePath);
  try {
    return JSON.parse(fs.readFileSync(resolved, "utf8"));
  } catch (error) {
    fail(`invalid JSON in ${filePath}: ${error.message}`);
  }
}

function checksumText(text) {
  let hash = 2166136261;
  for (const character of text) {
    hash ^= character.charCodeAt(0);
    hash = Math.imul(hash, 16777619) >>> 0;
  }
  return `fnv1a32:${hash.toString(16).padStart(8, "0")}`;
}

function ensureParentDirectory(filePath) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
}

function tempPathFor(filePath) {
  return `${filePath}.${process.pid}.tmp`;
}

function backupPathFor(filePath) {
  return `${filePath}.last-valid`;
}

export function writeAtomicJsonFile(filePath, value) {
  const target = path.resolve(filePath);
  const temp = tempPathFor(target);
  const backup = backupPathFor(target);
  const payload = `${JSON.stringify(value, null, 2)}\n`;
  ensureParentDirectory(target);
  if (fs.existsSync(target)) {
    fs.copyFileSync(target, backup);
  }
  fs.writeFileSync(temp, payload, "utf8");
  fs.renameSync(temp, target);
  return {
    path: target,
    checksum: checksumText(payload),
  };
}

export function loadJsonFileWithLastValidRecovery(filePath) {
  const target = path.resolve(filePath);
  const backup = backupPathFor(target);
  for (const candidate of [target, backup]) {
    try {
      const text = fs.readFileSync(candidate, "utf8");
      return {
        recovered: candidate === backup,
        value: JSON.parse(text),
        checksum: checksumText(text),
      };
    } catch {
      continue;
    }
  }
  throw new Error(`no valid JSON payload or last-valid backup for ${target}`);
}

function parseArgs(argv) {
  const args = {
    policy: null,
    expectFailure: false,
  };

  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--policy") {
      args.policy = argv[index + 1];
      index += 1;
    } else if (arg === "--expect-failure") {
      args.expectFailure = true;
    } else {
      fail(`unsupported argument: ${arg}`, 2);
    }
  }

  if (!args.policy) {
    fail("usage: node Tools/SecurityDiagnostics/security-diagnostics.mjs --policy <policy.json> [--expect-failure]", 2);
  }

  return args;
}

function normalizeDomain(domain) {
  return typeof domain === "string" ? domain.trim().toLowerCase() : "";
}

function hostForDownload(download) {
  if (!isObject(download) || typeof download.url !== "string") {
    return null;
  }
  try {
    return new URL(download.url).hostname.toLowerCase();
  } catch {
    return null;
  }
}

function artifactPaths(artifact) {
  if (Array.isArray(artifact.paths)) {
    return artifact.paths;
  }
  if (typeof artifact.path === "string") {
    return [artifact.path];
  }
  return [];
}

function validateArtifactPaths(rootDir, artifactName, artifact, errors) {
  const paths = artifactPaths(artifact);
  if (paths.length === 0 && artifactName !== "buildId") {
    errors.push(`diagnostics.${artifactName}.path or paths is required`);
    return "";
  }

  let combinedText = "";
  for (const artifactPath of paths) {
    if (typeof artifactPath !== "string" || artifactPath.length === 0) {
      errors.push(`diagnostics.${artifactName}.paths entries must be non-empty strings`);
      continue;
    }
    if (path.isAbsolute(artifactPath)) {
      errors.push(`diagnostics.${artifactName}.path must be repository-relative`);
      continue;
    }
    const resolved = path.resolve(rootDir, artifactPath);
    if (!fs.existsSync(resolved)) {
      errors.push(`diagnostics.${artifactName}.path does not exist: ${artifactPath}`);
      continue;
    }
    try {
      combinedText += `\n${fs.readFileSync(resolved, "utf8")}`;
    } catch (error) {
      errors.push(`diagnostics.${artifactName}.path cannot be read: ${artifactPath}: ${error.message}`);
    }
  }
  return combinedText;
}

export function validateSecurityPolicy(policy, options = {}) {
  const rootDir = path.resolve(options.rootDir ?? process.cwd());
  const errors = [];

  if (!isObject(policy)) {
    return ["policy must be an object"];
  }

  if (policy.schemaVersion !== "flying.security-diagnostics.v1") {
    errors.push("schemaVersion must be flying.security-diagnostics.v1");
  }

  const network = policy.network;
  if (!isObject(network)) {
    errors.push("network policy is required");
  } else {
    const allowedDomains = Array.isArray(network.allowedDomains)
      ? network.allowedDomains.map(normalizeDomain).filter(Boolean)
      : [];
    const allowlist = new Set(allowedDomains);
    if (allowedDomains.length === 0) {
      errors.push("network.allowedDomains must contain at least one explicit domain");
    }
    if (allowlist.size !== allowedDomains.length) {
      errors.push("network.allowedDomains must be unique");
    }
    if (!Array.isArray(network.observedDownloads)) {
      errors.push("network.observedDownloads must be an array");
    } else {
      for (const [index, download] of network.observedDownloads.entries()) {
        const host = hostForDownload(download);
        if (!host) {
          errors.push(`network.observedDownloads[${index}] must include a valid URL`);
          continue;
        }
        if (!allowlist.has(host)) {
          errors.push(`network download host outside allowlist: ${host}`);
        }
      }
    }
  }

  const telemetry = policy.telemetry;
  if (!isObject(telemetry)) {
    errors.push("telemetry policy is required");
  } else {
    if (telemetry.defaultUploadEnabled !== false) {
      errors.push("telemetry.defaultUploadEnabled must be false");
    }
    if (telemetry.personalDataConsentRequired !== true) {
      errors.push("telemetry.personalDataConsentRequired must be true");
    }
    if (!Array.isArray(telemetry.fields)) {
      errors.push("telemetry.fields must be an array");
    } else {
      for (const field of telemetry.fields) {
        if (!isObject(field) || typeof field.name !== "string" || field.name.length === 0) {
          errors.push("telemetry field entries must have a name");
          continue;
        }
        if (field.personalData === true && field.requiresExplicitConsent !== true) {
          errors.push(`telemetry personal-data field requires explicit consent: ${field.name}`);
        }
      }
    }
  }

  const atomic = policy.atomicPersistence;
  if (!isObject(atomic)) {
    errors.push("atomicPersistence evidence is required");
  } else {
    for (const category of REQUIRED_ATOMIC_CATEGORIES) {
      const evidence = atomic[category];
      if (!isObject(evidence)) {
        errors.push(`atomicPersistence.${category} evidence is required`);
        continue;
      }
      if (evidence.interruptedWriteTested !== true) {
        errors.push(`atomicPersistence.${category}.interruptedWriteTested must be true`);
      }
      if (evidence.recoveredLastValid !== true) {
        errors.push(`atomicPersistence.${category}.recoveredLastValid must be true`);
      }
      if (evidence.corruptWriteRejected !== true) {
        errors.push(`atomicPersistence.${category}.corruptWriteRejected must be true`);
      }
      if (typeof evidence.lastValidChecksum !== "string" || evidence.lastValidChecksum.length === 0) {
        errors.push(`atomicPersistence.${category}.lastValidChecksum is required`);
      }
      if (typeof evidence.interruptedWriteProbe !== "string" || evidence.interruptedWriteProbe.length === 0) {
        errors.push(`atomicPersistence.${category}.interruptedWriteProbe is required`);
      }
    }
  }

  const diagnostics = policy.diagnostics;
  if (!isObject(diagnostics)) {
    errors.push("diagnostics evidence is required");
  } else {
    for (const artifactName of REQUIRED_DIAGNOSTICS) {
      const artifact = diagnostics[artifactName];
      if (!isObject(artifact)) {
        errors.push(`diagnostics.${artifactName} evidence is required`);
        continue;
      }
      if (artifact.present !== true) {
        errors.push(`diagnostics.${artifactName}.present must be true`);
      }
      const combinedArtifactText = validateArtifactPaths(rootDir, artifactName, artifact, errors);
      for (const token of REQUIRED_DIAGNOSTIC_TOKENS[artifactName] ?? []) {
        if (!combinedArtifactText.includes(token)) {
          errors.push(`diagnostics.${artifactName} missing required content token: ${token}`);
        }
      }
      if (artifactName === "buildId" && (typeof artifact.value !== "string" || artifact.value.length === 0)) {
        errors.push("diagnostics.buildId.value is required");
      }
    }
  }

  return errors;
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const policy = readJson(args.policy);
  const errors = validateSecurityPolicy(policy);
  const failed = errors.length > 0;

  if (args.expectFailure) {
    if (!failed) {
      fail("expected validation failure, got pass");
    }
    console.log("security-diagnostics: expected failure");
    return;
  }

  if (failed) {
    for (const error of errors) {
      console.error(`security-diagnostics: ${error}`);
    }
    fail("validation failed");
  }

  console.log("security-diagnostics: pass");
}

if (import.meta.url === `file://${process.argv[1]}`) {
  main();
}
