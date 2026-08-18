#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";

function fail(message, code = 1) {
  console.error(`validate_soak_report: ${message}`);
  process.exit(code);
}

function parseArgs(argv) {
  const args = {
    minHours: 10,
    maxStreamingErrors: 0,
    maxHitchMilliseconds: 100,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--report") {
      args.report = argv[++index];
    } else if (arg === "--min-hours") {
      args.minHours = Number(argv[++index]);
    } else if (arg === "--max-streaming-errors") {
      args.maxStreamingErrors = Number(argv[++index]);
    } else if (arg === "--max-hitch-ms") {
      args.maxHitchMilliseconds = Number(argv[++index]);
    } else {
      fail(`unsupported argument: ${arg}`, 2);
    }
  }
  if (!args.report) {
    fail("usage: node Tools/Stability/validate_soak_report.mjs --report <soak-report.json>", 2);
  }
  return args;
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
  if (typeof value === "number" && Number.isFinite(schema.minimum) && value < schema.minimum) {
    errors.push(`${location} must be at least ${schema.minimum}`);
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

function stableIdentity(sample) {
  return JSON.stringify({
    coreSimVersion: sample.coreSimVersion,
    dataVersion: sample.dataVersion,
    configVersion: sample.configVersion,
    inputProfile: sample.inputProfile,
    weatherSeed: String(sample.weatherSeed),
    scenarioId: sample.scenarioId,
  });
}

function validateReplayHashes(report) {
  requireCondition(Array.isArray(report.replayHashes) && report.replayHashes.length >= 2, "at least two replay hash samples are required");
  const first = report.replayHashes[0];
  requireCondition(typeof first.hash === "string" && first.hash.startsWith("sha256:"), "replay hash must be a sha256 value");
  const expectedIdentity = stableIdentity({
    ...report.identity,
    scenarioId: report.scenarioId,
  });
  for (const sample of report.replayHashes) {
    requireCondition(stableIdentity(sample) === expectedIdentity, "replay hash sample identity differs");
    requireCondition(sample.hash === first.hash, "replay hash is not deterministic for identical identity and seed");
  }
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const report = readJson(args.report);
  const schema = readJson(path.resolve("Tools/Stability/schema/soak-report.schema.json"));
  const schemaErrors = validateJsonSchema(report, schema);
  requireCondition(schemaErrors.length === 0, `soak report schema validation failed: ${schemaErrors.join("; ")}`);

  requireCondition(report.schemaVersion === "flying.long-flight-soak-report.v1", "soak report schema mismatch");
  requireCondition(report.process?.crashed === false, "soak report records an application crash");
  requireCondition(report.process?.exitCode === 0, "soak command must exit with code 0");
  requireCondition(report.durationHoursRequired >= args.minHours, "soak command duration requirement is below ten hours");
  requireCondition(report.durationHoursActual >= args.minHours, "actual soak duration is below ten hours");
  requireCondition(Array.isArray(report.memorySamples) && report.memorySamples.length > 0, "memory samples are required");
  for (const sample of report.memorySamples) {
    requireCondition(Number.isFinite(sample.residentMiB ?? sample.workingSetMiB), "memory sample must record residentMiB or workingSetMiB");
  }
  requireCondition(Array.isArray(report.hitches), "hitch samples are required");
  for (const hitch of report.hitches) {
    requireCondition(Number.isFinite(hitch.durationMilliseconds), "hitch duration is required");
    requireCondition(hitch.durationMilliseconds <= args.maxHitchMilliseconds, "hitch exceeds acceptance budget");
  }
  requireCondition(Array.isArray(report.streamingErrors), "streaming error evidence is required");
  requireCondition(report.streamingErrors.length <= args.maxStreamingErrors, "streaming errors exceed acceptance budget");
  validateReplayHashes(report);

  console.log("validate_soak_report: ok");
}

main();
