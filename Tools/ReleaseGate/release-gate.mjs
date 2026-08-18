#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { validateVisualEvidenceManifestFile } from "../../Validation/VisualAcceptance/visual_acceptance_validator.mjs";
import { MANDATORY_VISUAL_SCENES } from "../../Validation/VisualAcceptance/mandatory_visual_scenes.mjs";

const releaseReportSchemaPath = "Tools/ReleaseGate/schemas/release-report.schema.json";

function fail(message, code = 1) {
  console.error(`release-gate: ${message}`);
  process.exit(code);
}

function parseArgs(argv) {
  const args = {
    input: null,
    expectStatus: null,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--input") {
      args.input = argv[index + 1];
      index += 1;
    } else if (arg === "--expect-status") {
      args.expectStatus = argv[index + 1];
      index += 1;
    } else {
      fail(`unsupported argument: ${arg}`, 2);
    }
  }
  if (!args.input) {
    fail("usage: node Tools/ReleaseGate/release-gate.mjs --input <release-report.json> [--expect-status complete|partial|blocked]", 2);
  }
  return args;
}

function readJson(relativeOrAbsolutePath) {
  const resolved = path.resolve(process.cwd(), relativeOrAbsolutePath);
  try {
    return JSON.parse(fs.readFileSync(resolved, "utf8"));
  } catch (error) {
    fail(`invalid JSON in ${relativeOrAbsolutePath}: ${error.message}`);
  }
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function validateJsonSchema(value, schema, location = "$") {
  const errors = [];

  if (schema.const !== undefined && value !== schema.const) {
    errors.push(`${location} must equal ${JSON.stringify(schema.const)}`);
  }
  if (schema.enum !== undefined && !schema.enum.includes(value)) {
    errors.push(`${location} must be one of ${schema.enum.map((entry) => JSON.stringify(entry)).join(", ")}`);
  }
  if (schema.type === "object" && !isObject(value)) {
    errors.push(`${location} must be an object`);
    return errors;
  }
  if (schema.type === "array" && !Array.isArray(value)) {
    errors.push(`${location} must be an array`);
    return errors;
  }
  if (schema.type === "string" && typeof value !== "string") {
    errors.push(`${location} must be a string`);
    return errors;
  }
  if (schema.type === "boolean" && typeof value !== "boolean") {
    errors.push(`${location} must be a boolean`);
    return errors;
  }
  if (schema.type === "integer" && !Number.isInteger(value)) {
    errors.push(`${location} must be an integer`);
    return errors;
  }
  if (schema.type === "number" && (typeof value !== "number" || !Number.isFinite(value))) {
    errors.push(`${location} must be a finite number`);
    return errors;
  }

  if (typeof value === "string" && Number.isInteger(schema.minLength) && value.length < schema.minLength) {
    errors.push(`${location} must be at least ${schema.minLength} characters`);
  }

  if (isObject(value)) {
    for (const key of schema.required ?? []) {
      if (!(key in value)) {
        errors.push(`${location} missing required property: ${key}`);
      }
    }
    const properties = schema.properties ?? {};
    if (schema.additionalProperties === false) {
      const allowedProperties = new Set(Object.keys(properties));
      for (const key of Object.keys(value)) {
        if (!allowedProperties.has(key)) {
          errors.push(`${location} has unsupported property: ${key}`);
        }
      }
    }
    for (const [key, propertySchema] of Object.entries(properties)) {
      if (key in value) {
        errors.push(...validateJsonSchema(value[key], propertySchema, `${location}.${key}`));
      }
    }
  }

  if (Array.isArray(value)) {
    if (Number.isInteger(schema.minItems) && value.length < schema.minItems) {
      errors.push(`${location} must include at least ${schema.minItems} items`);
    }
    if (schema.uniqueItems === true && new Set(value.map((item) => JSON.stringify(item))).size !== value.length) {
      errors.push(`${location} must contain unique items`);
    }
    if (schema.items) {
      value.forEach((item, index) => {
        errors.push(...validateJsonSchema(item, schema.items, `${location}[${index}]`));
      });
    }
  }

  return errors;
}

function validateReleaseReportSchema(report, schema) {
  const errors = validateJsonSchema(report, schema);
  const schemaScenes = schema.properties?.visualAcceptance?.properties?.mandatoryScenes?.items?.enum ?? [];
  if (schemaScenes.length > 0) {
    const schemaSceneSet = new Set(schemaScenes);
    for (const scene of MANDATORY_VISUAL_SCENES) {
      if (!schemaSceneSet.has(scene)) {
        errors.push(`release report schema mandatoryScenes enum missing required scene: ${scene}`);
      }
    }
    for (const scene of schemaSceneSet) {
      if (!MANDATORY_VISUAL_SCENES.includes(scene)) {
        errors.push(`release report schema mandatoryScenes enum contains unknown scene: ${scene}`);
      }
    }
  }
  return errors;
}

function validateVisualGate(report) {
  const errors = [];
  const visual = report.visualAcceptance;
  if (!isObject(visual)) {
    return {
      status: "blocked",
      errors: ["visualAcceptance is required"],
    };
  }

  if (visual.required !== true) {
    errors.push("visualAcceptance.required must be true");
  }
  if (typeof visual.evidenceManifest !== "string" || visual.evidenceManifest.length === 0) {
    errors.push("visualAcceptance.evidenceManifest is required");
  }
  if (visual.result !== "pass") {
    errors.push("visualAcceptance.result must be pass");
  }
  if (visual.baselineApproval !== "approved") {
    errors.push("visualAcceptance.baselineApproval must be approved");
  }
  if (visual.placeholderScan !== "pass") {
    errors.push("visualAcceptance.placeholderScan must be pass");
  }
  if (!Array.isArray(visual.mandatoryScenes)) {
    errors.push("visualAcceptance.mandatoryScenes must be an array");
  } else {
    const declaredScenes = new Set(visual.mandatoryScenes);
    for (const scene of MANDATORY_VISUAL_SCENES) {
      if (!declaredScenes.has(scene)) {
        errors.push(`visualAcceptance.mandatoryScenes missing required scene: ${scene}`);
      }
    }
  }

  if (typeof visual.evidenceManifest === "string" && visual.evidenceManifest.length > 0) {
    const validation = validateVisualEvidenceManifestFile(visual.evidenceManifest, {
      requiredScenes: MANDATORY_VISUAL_SCENES,
    });
    if (!validation.ok) {
      errors.push(...validation.errors.map((error) => `visual evidence: ${error}`));
    }
  }

  return {
    status: errors.length === 0 ? "complete" : "blocked",
    errors,
  };
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const schema = readJson(releaseReportSchemaPath);
  const report = readJson(args.input);

  const schemaErrors = validateReleaseReportSchema(report, schema);
  const visual = validateVisualGate(report);
  const mandatoryChecks = Array.isArray(report.mandatoryChecks) ? report.mandatoryChecks : [];
  const failedMandatory = mandatoryChecks.filter((check) => check?.mandatory === true && check?.result !== "pass");
  const status = schemaErrors.length > 0 || visual.status === "blocked" || failedMandatory.length > 0 ? "blocked" : "complete";

  if (args.expectStatus && status !== args.expectStatus) {
    for (const error of schemaErrors) {
      console.error(`release-gate: ${error}`);
    }
    for (const error of visual.errors) {
      console.error(`release-gate: ${error}`);
    }
    fail(`expected status ${args.expectStatus}, got ${status}`);
  }

  if (status === "blocked" && args.expectStatus !== "blocked") {
    for (const error of schemaErrors) {
      console.error(`release-gate: ${error}`);
    }
    for (const error of visual.errors) {
      console.error(`release-gate: ${error}`);
    }
    fail("release status blocked");
  }

  console.log(`release-gate: ${status}`);
}

main();
