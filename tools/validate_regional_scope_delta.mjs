#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const contractPath = "docs/contract/flying-2.0.yml";
const migrationPath = "docs/contract/migrations/0001-regional-scope-delta.json";

const requiredDeclaredFields = [
  "project.runtime",
  "project.terrainRuntime",
  "regionalDataScope",
  "invariants",
  "governance.prohibitedSubstitutes",
  "milestones[M0].requiredRequirementIds",
  "milestones[M2].requiredRequirementIds",
  "milestones[M2].exitGate",
  "milestones[M3].exitGate",
  "requirements[REQ-RELEASE-GATES].summary",
  "requirements[REQ-DMR-TERRAIN-COVERAGE]",
  "requirements[REQ-REGIONAL-DATA-PACKAGES]",
];

function fail(message) {
  console.error(`validate_regional_scope_delta: ${message}`);
  process.exit(1);
}

function readJson(relativePath) {
  try {
    return JSON.parse(fs.readFileSync(path.join(repoRoot, relativePath), "utf8"));
  } catch (error) {
    fail(`cannot read valid JSON from ${relativePath}: ${error.message}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function includesText(value, token, label) {
  requireCondition(typeof value === "string" && value.includes(token), `${label} must include "${token}"`);
}

function stable(value) {
  if (Array.isArray(value)) {
    return value.map(stable);
  }
  if (value && typeof value === "object") {
    return Object.fromEntries(Object.keys(value).sort().map((key) => [key, stable(value[key])]));
  }
  return value;
}

function sameValue(left, right) {
  return JSON.stringify(stable(left)) === JSON.stringify(stable(right));
}

function byId(items, label) {
  requireCondition(Array.isArray(items), `${label} must be an array`);
  const mapped = new Map();
  for (const item of items) {
    requireCondition(item && typeof item.id === "string", `${label} entry missing id`);
    requireCondition(!mapped.has(item.id), `${label} contains duplicate id ${item.id}`);
    mapped.set(item.id, item);
  }
  return mapped;
}

function diffPlain(base, current, prefix, changes) {
  if (sameValue(base, current)) {
    return;
  }
  if (!base || !current || typeof base !== "object" || typeof current !== "object" ||
      Array.isArray(base) || Array.isArray(current)) {
    changes.add(prefix);
    return;
  }

  const keys = new Set([...Object.keys(base), ...Object.keys(current)]);
  for (const key of keys) {
    diffPlain(base[key], current[key], prefix ? `${prefix}.${key}` : key, changes);
  }
}

function diffByIdArray(baseItems, currentItems, prefix, changes) {
  const baseMap = byId(baseItems, prefix);
  const currentMap = byId(currentItems, prefix);
  const ids = new Set([...baseMap.keys(), ...currentMap.keys()]);

  for (const id of ids) {
    const changePrefix = `${prefix}[${id}]`;
    if (!baseMap.has(id) || !currentMap.has(id)) {
      changes.add(changePrefix);
      continue;
    }
    diffPlain(baseMap.get(id), currentMap.get(id), changePrefix, changes);
  }
}

function contractChangedFields(base, current) {
  const changes = new Set();
  const keys = new Set([...Object.keys(base), ...Object.keys(current)]);
  keys.delete("requirements");
  keys.delete("milestones");

  for (const key of keys) {
    diffPlain(base[key], current[key], key, changes);
  }

  diffByIdArray(base.milestones, current.milestones, "milestones", changes);
  diffByIdArray(base.requirements, current.requirements, "requirements", changes);
  return [...changes].sort();
}

function isDeclaredChange(change, declared) {
  if (declared.has(change)) {
    return true;
  }
  for (const field of declared) {
    if (change.startsWith(`${field}.`) || change.startsWith(`${field}[`)) {
      return true;
    }
  }
  return false;
}

function assertOmittedRequirementsPreserved(base, current, migration) {
  const baseRequirements = byId(base.requirements, "base requirements");
  const currentRequirements = byId(current.requirements, "current requirements");
  const added = new Set(migration.addedRequirementIds ?? []);
  const declaredChanged = new Set(
    [...(migration.declaredDeltaFields ?? [])]
      .map((field) => /^requirements\[([^\]]+)\]/.exec(field)?.[1])
      .filter(Boolean),
  );

  for (const [requirementId, baseRequirement] of baseRequirements) {
    requireCondition(currentRequirements.has(requirementId), `active requirement was dropped: ${requirementId}`);
    if (!added.has(requirementId) && !declaredChanged.has(requirementId)) {
      requireCondition(
        sameValue(baseRequirement, currentRequirements.get(requirementId)),
        `omitted active requirement changed without declaration: ${requirementId}`,
      );
    }
  }
}

const contract = readJson(contractPath);
const migration = readJson(migrationPath);
const baseContract = migration.baseContractSnapshot;

requireCondition(migration.schemaVersion === "flying.contract-migration.v1", "migration schemaVersion mismatch");
requireCondition(migration.migrationId === "0001-regional-scope-delta", "migration ID mismatch");
requireCondition(migration.status === "applied", "migration must be applied");
requireCondition(baseContract && typeof baseContract === "object", "migration must include baseContractSnapshot");
requireCondition(migration.appliesToContractId === contract.contractId, "migration contract ID mismatch");
requireCondition(
  migration.validationCommand === "node tools/validate_regional_scope_delta.mjs",
  "migration must declare this validation command",
);

const declared = new Set(migration.declaredDeltaFields ?? []);
for (const field of requiredDeclaredFields) {
  requireCondition(declared.has(field), `migration does not declare changed field ${field}`);
}

const changedFields = contractChangedFields(baseContract, contract);
requireCondition(changedFields.length > 0, "migration must produce contract changes");
for (const change of changedFields) {
  requireCondition(isDeclaredChange(change, declared), `contract changed undeclared field: ${change}`);
}
assertOmittedRequirementsPreserved(baseContract, contract, migration);

const requirements = new Map((contract.requirements ?? []).map((requirement) => [requirement.id, requirement]));
for (const requirementId of migration.preservedActiveRequirementIds ?? []) {
  requireCondition(requirements.has(requirementId), `preserved active requirement missing: ${requirementId}`);
}
for (const requirementId of migration.addedRequirementIds ?? []) {
  requireCondition(requirements.has(requirementId), `added requirement missing from registry: ${requirementId}`);
  requireCondition(requirements.get(requirementId).mandatory === true, `added requirement must be mandatory: ${requirementId}`);
}

const m2 = (contract.milestones ?? []).find((milestone) => milestone.id === "M2");
requireCondition(m2, "M2 milestone missing");
const m0 = (contract.milestones ?? []).find((milestone) => milestone.id === "M0");
requireCondition(m0, "M0 milestone missing");
requireCondition(
  m0.requiredRequirementIds?.includes("REQ-AUTHORITATIVE-DOCS"),
  "M0 must preserve authoritative documentation requirement mapping",
);
for (const requirementId of ["REQ-DMR-TERRAIN-COVERAGE", "REQ-REGIONAL-DATA-PACKAGES", "REQ-PILOT-REGION"]) {
  requireCondition(m2.requiredRequirementIds?.includes(requirementId), `M2 must require ${requirementId}`);
}
includesText(m2.exitGate, "regional package evidence", "M2 exit gate");

const releaseGates = requirements.get("REQ-RELEASE-GATES");
includesText(releaseGates?.summary, "regional terrain packages", "release gate summary");
includesText(releaseGates?.summary, "regional airport coverage", "release gate summary");
includesText(releaseGates?.summary, "production visual quality", "release gate summary");
includesText(releaseGates?.summary, "pilot release package", "release gate summary");

const regionalPackage = requirements.get("REQ-REGIONAL-DATA-PACKAGES");
includesText(regionalPackage?.summary, "independently installable offline regional packages", "regional package requirement");
includesText(regionalPackage?.summary, "without hard-coded", "regional package requirement");

const dmrCoverage = requirements.get("REQ-DMR-TERRAIN-COVERAGE");
includesText(dmrCoverage?.summary, "offline DMR 5G-based terrain coverage", "DMR terrain requirement");
includesText(dmrCoverage?.summary, "10 x 10 km Ceska Trebova pilot region", "DMR terrain requirement");
includesText(dmrCoverage?.summary, "seam validation", "DMR terrain requirement");

const regionalScopeText = JSON.stringify(contract.regionalDataScope ?? {});
includesText(regionalScopeText, "regional by default", "regional data scope");
includesText(regionalScopeText, "not the default pipeline, storage or acceptance baseline", "regional data scope");
includesText(regionalScopeText, "must not be hard-coded", "regional data scope");

const invariants = contract.invariants ?? [];
requireCondition(Array.isArray(invariants) && invariants.length >= 4, "regional invariants missing");
requireCondition(
  invariants.some((item) => item.includes("independently installable offline regional packages")),
  "invariants must require independently installable offline regional packages",
);
requireCondition(
  invariants.some((item) => item.includes("must not hard-code Ceska Trebova")),
  "invariants must prohibit region-specific application logic",
);
requireCondition(
  invariants.some((item) => item.includes("must not depend on external map APIs")),
  "invariants must preserve offline runtime behavior",
);

const prohibited = contract.governance?.prohibitedSubstitutes ?? [];
for (const token of [
  "hard-coded Ceska Trebova coordinates",
  "whole-country source downloads",
  "non-Shipping builds as production visual acceptance evidence",
]) {
  requireCondition(
    prohibited.some((item) => item.includes(token)),
    `prohibited substitutes must include ${token}`,
  );
}

requireCondition(
  (migration.supersededReleaseCriteria ?? []).some((item) => item.includes("whole-Czech-Republic")),
  "migration must record superseded whole-Czech release criteria",
);
requireCondition(
  (migration.replacementReleaseCriteria ?? []).some((item) => item.includes("Ceska Trebova 10 x 10 km")),
  "migration must record pilot replacement release criteria",
);

console.log("validate_regional_scope_delta: ok");
