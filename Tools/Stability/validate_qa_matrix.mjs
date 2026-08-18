#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";

const DEFAULT_REQUIRED_SUPPORTED_GPUS = ["nvidia-rtx-4070", "amd-rx-7800-xt"];
const DEFAULT_REQUIRED_SUPPORTED_DEVICES = ["keyboard-mouse", "xinput-gamepad"];

function addRequiredIds(existingIds, addedIds) {
  return [...new Set([...existingIds, ...addedIds])];
}

function fail(message, code = 1) {
  console.error(`validate_qa_matrix: ${message}`);
  process.exit(code);
}

function parseArgs(argv) {
  const args = {
    requiredGpus: DEFAULT_REQUIRED_SUPPORTED_GPUS,
    requiredDevices: DEFAULT_REQUIRED_SUPPORTED_DEVICES,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--matrix") {
      args.matrix = argv[++index];
    } else if (arg === "--required-gpus") {
      args.requiredGpus = addRequiredIds(args.requiredGpus, splitIds(argv[++index]));
    } else if (arg === "--required-devices") {
      args.requiredDevices = addRequiredIds(args.requiredDevices, splitIds(argv[++index]));
    } else {
      fail(`unsupported argument: ${arg}`, 2);
    }
  }
  if (!args.matrix) {
    fail("usage: node Tools/Stability/validate_qa_matrix.mjs --matrix <qa-matrix.json>", 2);
  }
  return args;
}

function splitIds(value) {
  return String(value ?? "")
    .split(",")
    .map((entry) => entry.trim())
    .filter(Boolean);
}

function readJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (error) {
    fail(`invalid JSON in ${filePath}: ${error.message}`);
  }
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function typeMatches(value, type) {
  if (Array.isArray(type)) {
    return type.some((entry) => typeMatches(value, entry));
  }
  if (type === "array") {
    return Array.isArray(value);
  }
  if (type === "object") {
    return isObject(value);
  }
  if (type === "integer") {
    return Number.isInteger(value);
  }
  if (type === "number") {
    return typeof value === "number" && Number.isFinite(value);
  }
  if (type === "null") {
    return value === null;
  }
  return typeof value === type;
}

function validateJsonSchema(value, schema, location = "$") {
  const errors = [];
  if (schema.const !== undefined && value !== schema.const) {
    errors.push(`${location} must equal ${JSON.stringify(schema.const)}`);
  }
  if (schema.type !== undefined && !typeMatches(value, schema.type)) {
    errors.push(`${location} must be type ${JSON.stringify(schema.type)}`);
    return errors;
  }
  if (typeof value === "string" && Number.isInteger(schema.minLength) && value.length < schema.minLength) {
    errors.push(`${location} must be at least ${schema.minLength} characters`);
  }
  if (Array.isArray(value)) {
    if (Number.isInteger(schema.minItems) && value.length < schema.minItems) {
      errors.push(`${location} must include at least ${schema.minItems} items`);
    }
    if (schema.items) {
      value.forEach((item, index) => errors.push(...validateJsonSchema(item, schema.items, `${location}[${index}]`)));
    }
  }
  if (isObject(value)) {
    for (const key of schema.required ?? []) {
      if (!(key in value)) {
        errors.push(`${location} missing required property: ${key}`);
      }
    }
    for (const [key, childSchema] of Object.entries(schema.properties ?? {})) {
      if (key in value) {
        errors.push(...validateJsonSchema(value[key], childSchema, `${location}.${key}`));
      }
    }
  }
  return errors;
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function evidenceById(entries) {
  return new Map((entries ?? []).map((entry) => [entry.id, entry]));
}

function validateLocalRelativeArtifact(artifactPath, matrixPath, label) {
  requireCondition(!path.isAbsolute(artifactPath), `${label} evidence artifact must be relative`);
  requireCondition(!/^[a-zA-Z]:[\\/]/.test(artifactPath), `${label} evidence artifact must not be drive-qualified`);
  requireCondition(!artifactPath.startsWith("\\\\") && !artifactPath.startsWith("//"), `${label} evidence artifact must not be UNC`);
  requireCondition(!/^[a-z][a-z0-9+.-]*:/i.test(artifactPath), `${label} evidence artifact must not be remote`);
  requireCondition(!artifactPath.split(/[\\/]+/).includes(".."), `${label} evidence artifact must be traversal-free`);
  const matrixDirectory = path.dirname(path.resolve(matrixPath));
  const resolved = path.resolve(matrixDirectory, artifactPath);
  requireCondition(resolved.startsWith(`${matrixDirectory}${path.sep}`), `${label} evidence artifact must stay within matrix directory`);
  requireCondition(fs.existsSync(resolved), `${label} evidence artifact is missing: ${artifactPath}`);
}

function validateRequiredCoverage(kind, requiredIds, requiredEntries, evidenceEntries, matrixPath) {
  requireCondition(Array.isArray(requiredEntries) && requiredEntries.length > 0, `${kind} required list is empty`);
  requireCondition(Array.isArray(requiredIds) && requiredIds.length > 0, `${kind} canonical required list is empty`);
  const requiredById = new Map(requiredEntries.map((entry) => [entry.id, entry]));
  for (const requiredId of requiredIds) {
    requireCondition(requiredById.has(requiredId), `${kind} required inventory missing: ${requiredId}`);
  }
  const evidence = evidenceById(evidenceEntries);
  for (const requiredId of requiredIds) {
    const required = requiredById.get(requiredId);
    requireCondition(required.supported === true, `${kind} ${required.id} must be marked supported`);
    const entry = evidence.get(required.id);
    requireCondition(entry, `${kind} evidence missing: ${required.id}`);
    requireCondition(entry.result === "pass", `${kind} evidence did not pass: ${required.id}`);
    requireCondition(typeof entry.evidenceArtifact === "string" && entry.evidenceArtifact.length > 0, `${kind} evidence artifact missing: ${required.id}`);
    validateLocalRelativeArtifact(entry.evidenceArtifact, matrixPath, `${kind} ${required.id}`);
    requireCondition(typeof entry.executedAtUtc === "string" && /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/.test(entry.executedAtUtc), `${kind} executedAtUtc invalid: ${required.id}`);
  }
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const matrix = readJson(args.matrix);
  const schema = readJson(path.resolve("Tools/Stability/schema/qa-matrix.schema.json"));
  const schemaErrors = validateJsonSchema(matrix, schema);
  requireCondition(schemaErrors.length === 0, `QA matrix schema validation failed: ${schemaErrors.join("; ")}`);

  requireCondition(matrix.schemaVersion === "flying.stability-qa-matrix.v1", "QA matrix schema mismatch");
  requireCondition(matrix.referenceEnvironment?.os === "Windows 11", "QA matrix must target Windows 11");
  validateRequiredCoverage("GPU", args.requiredGpus, matrix.requiredSupportedGpus, matrix.gpuEvidence, args.matrix);
  validateRequiredCoverage("device", args.requiredDevices, matrix.requiredSupportedDevices, matrix.deviceEvidence, args.matrix);

  console.log("validate_qa_matrix: ok");
}

main();
