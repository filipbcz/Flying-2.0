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
  if (args.expectStatus && !["complete", "partial", "blocked"].includes(args.expectStatus)) {
    fail("--expect-status must be complete, partial, or blocked", 2);
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

const REQUIRED_CLEAN_INSTALL_PHASES = Object.freeze([
  "install signed Shipping build",
  "select offline region package",
  "install regional data root",
  "verify regional data availability",
  "start cold-and-dark at included airport",
  "start engine",
  "taxi",
  "takeoff",
  "navigate cross-country in selected region",
  "land at another included airport",
  "shutdown",
  "replay flight",
  "export telemetry",
]);

const REQUIRED_CLEAN_INSTALL_EVIDENCE = Object.freeze([
  "installer log",
  "installed release manifest hashes",
  "regional package manifest hashes",
  "regional data-root install evidence",
  "offline-operation observation",
  "workflow telemetry export",
  "workflow replay artifact",
]);

function validateCleanInstallWorkflow(report) {
  const workflow = report.cleanInstallWorkflow;
  const errors = [];
  if (workflow === undefined) {
    return { status: "blocked", errors: ["cleanInstallWorkflow is required"] };
  }
  if (!isObject(workflow)) {
    return { status: "blocked", errors: ["cleanInstallWorkflow must be an object"] };
  }
  if (workflow.required !== true) {
    errors.push("cleanInstallWorkflow.required must be true");
  }
  if (workflow.result !== "pass") {
    errors.push("cleanInstallWorkflow.result must be pass");
  }
  if (workflow.environment?.os !== "Windows 11") {
    errors.push("cleanInstallWorkflow.environment.os must be Windows 11");
  }
  if (typeof workflow.environment?.regionPackage !== "string" || workflow.environment.regionPackage.length === 0) {
    errors.push("cleanInstallWorkflow.environment.regionPackage is required");
  }
  if (!String(workflow.environment?.networkState ?? "").toLowerCase().includes("offline")) {
    errors.push("cleanInstallWorkflow.environment.networkState must document offline operation");
  }
  const phases = new Set(Array.isArray(workflow.phases) ? workflow.phases : []);
  for (const phase of REQUIRED_CLEAN_INSTALL_PHASES) {
    if (!phases.has(phase)) {
      errors.push(`cleanInstallWorkflow.phases missing required phase: ${phase}`);
    }
  }
  const evidence = new Set(Array.isArray(workflow.requiredEvidence) ? workflow.requiredEvidence : []);
  for (const item of REQUIRED_CLEAN_INSTALL_EVIDENCE) {
    if (!evidence.has(item)) {
      errors.push(`cleanInstallWorkflow.requiredEvidence missing required item: ${item}`);
    }
  }
  return {
    status: errors.length === 0 ? "complete" : "blocked",
    errors,
  };
}

function collectMandatoryResults(report) {
  const results = [];
  for (const check of Array.isArray(report.mandatoryChecks) ? report.mandatoryChecks : []) {
    if (check?.mandatory === true) {
      results.push({
        id: check.id ?? "mandatoryChecks entry",
        result: check.result ?? "missing",
        releaseBlocking: true,
      });
    }
  }
  for (const gate of Array.isArray(report.dependencyGates) ? report.dependencyGates : []) {
    if (gate?.mandatory === true) {
      results.push({
        id: gate.id ?? "dependencyGates entry",
        result: gate.result ?? "missing",
        releaseBlocking: gate.releaseBlocking === true,
      });
    }
  }
  if (report.cleanInstallWorkflow?.required === true) {
    results.push({
      id: "clean-install-workflow",
      result: report.cleanInstallWorkflow.result ?? "missing",
      releaseBlocking: true,
    });
  }
  return results;
}

function deriveReleaseStatus(report, schemaErrors, visual, cleanInstall) {
  if (schemaErrors.length > 0 || visual.status === "blocked" || cleanInstall.status === "blocked") {
    return "blocked";
  }
  const mandatoryResults = collectMandatoryResults(report);
  const failing = mandatoryResults.filter((entry) => entry.result !== "pass");
  if (failing.some((entry) => entry.releaseBlocking || entry.result === "blocked" || entry.result === "missing")) {
    return "blocked";
  }
  if (failing.length > 0) {
    return "partial";
  }
  return "complete";
}

function main() {
  const args = parseArgs(process.argv.slice(2));
  const schema = readJson(releaseReportSchemaPath);
  const report = readJson(args.input);

  const schemaErrors = validateReleaseReportSchema(report, schema);
  const visual = validateVisualGate(report);
  const cleanInstall = validateCleanInstallWorkflow(report);
  const status = deriveReleaseStatus(report, schemaErrors, visual, cleanInstall);
  const statusErrors = [];
  if (report.status !== status) {
    statusErrors.push(`status must be ${status} for the supplied mandatory evidence`);
  }
  if (isObject(report.gateDecision) && report.gateDecision.decision !== status) {
    statusErrors.push(`gateDecision.decision must be ${status}`);
  }

  if (args.expectStatus && status !== args.expectStatus) {
    for (const error of schemaErrors) {
      console.error(`release-gate: ${error}`);
    }
    for (const error of visual.errors) {
      console.error(`release-gate: ${error}`);
    }
    for (const error of cleanInstall.errors) {
      console.error(`release-gate: ${error}`);
    }
    for (const error of statusErrors) {
      console.error(`release-gate: ${error}`);
    }
    fail(`expected status ${args.expectStatus}, got ${status}`);
  }

  if (statusErrors.length > 0) {
    for (const error of statusErrors) {
      console.error(`release-gate: ${error}`);
    }
    fail("release report status does not match mandatory evidence");
  }

  if (status === "blocked" && args.expectStatus !== "blocked") {
    for (const error of schemaErrors) {
      console.error(`release-gate: ${error}`);
    }
    for (const error of visual.errors) {
      console.error(`release-gate: ${error}`);
    }
    for (const error of cleanInstall.errors) {
      console.error(`release-gate: ${error}`);
    }
    fail("release status blocked");
  }

  console.log(`release-gate: ${status}`);
}

main();
