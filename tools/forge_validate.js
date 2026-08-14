#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const contractPath = path.join(repoRoot, "docs", "contract", "flying-2.0.yml");
const ledgerSchemaPath = path.join(repoRoot, "docs", "evidence", "ledger.schema.json");
const reqIdPattern = /^REQ-[A-Z0-9]+(?:-[A-Z0-9]+)*$/;
const milestoneIdPattern = /^M[0-9]+$/;

class ValidationError extends Error {}

function readJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (error) {
    throw new ValidationError(
      `cannot read ${path.relative(repoRoot, filePath)}: ${error instanceof Error ? error.message : String(error)}`,
    );
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    throw new ValidationError(message);
  }
}

function validateLedgerSchema(schema) {
  requireCondition(
    schema.$schema === "https://json-schema.org/draft/2020-12/schema",
    "ledger schema must use JSON Schema draft 2020-12",
  );
  requireCondition(
    schema.properties?.schemaVersion?.const === "flying.evidence-ledger.v1",
    "ledger schemaVersion const mismatch",
  );

  const requirementSchema = schema.$defs?.requirementEvidence;
  requireCondition(requirementSchema, "ledger schema missing requirementEvidence definition");
  requireCondition(
    requirementSchema.properties?.requirementId?.pattern === reqIdPattern.source,
    "ledger requirement id pattern mismatch",
  );

  const requiredSets = new Set((requirementSchema.anyOf ?? []).map((item) => (item.required ?? []).join(",")));
  requireCondition(
    requiredSets.has("proofRefs") && requiredSets.has("blockerRef"),
    "ledger must reject requirements without proofRefs or blockerRef",
  );
  requireCondition(
    requirementSchema.properties?.proofRefs?.minItems === 1,
    "ledger proofRefs must require at least one proof",
  );
}

function validateContract() {
  const contract = readJson(contractPath);
  const schema = readJson(ledgerSchemaPath);

  requireCondition(contract.schemaVersion === "flying.contract.v1", "contract schemaVersion mismatch");
  requireCondition(contract.contractId === "flying-2.0", "contractId mismatch");
  requireCondition(contract.status === "immutable", "contract must be immutable");
  requireCondition(
    Array.isArray(contract.requirements) && contract.requirements.length > 0,
    "contract requirements must be a non-empty list",
  );
  requireCondition(
    Array.isArray(contract.milestones) && contract.milestones.length > 0,
    "contract milestones must be a non-empty list",
  );

  const milestoneIds = new Set();
  for (const milestone of contract.milestones) {
    requireCondition(milestoneIdPattern.test(milestone.id), `invalid milestone id: ${milestone.id}`);
    requireCondition(!milestoneIds.has(milestone.id), `duplicate milestone id: ${milestone.id}`);
    milestoneIds.add(milestone.id);
  }

  const requirementIds = new Set();
  for (const requirement of contract.requirements) {
    requireCondition(reqIdPattern.test(requirement.id), `invalid requirement id: ${requirement.id}`);
    requireCondition(!requirementIds.has(requirement.id), `duplicate requirement id: ${requirement.id}`);
    requirementIds.add(requirement.id);
    requireCondition(requirement.mandatory === true, `requirement must be mandatory: ${requirement.id}`);
    requireCondition(
      milestoneIds.has(requirement.milestone),
      `requirement maps to unknown milestone: ${requirement.id}`,
    );
    requireCondition(Boolean(requirement.evidenceGate), `requirement missing evidence gate: ${requirement.id}`);
  }

  const mappedRequirementIds = new Set();
  for (const milestone of contract.milestones) {
    for (const requirementId of milestone.requiredRequirementIds ?? []) {
      requireCondition(requirementIds.has(requirementId), `milestone maps unknown requirement: ${requirementId}`);
      mappedRequirementIds.add(requirementId);
    }
  }

  const missingMappings = [...requirementIds].filter((requirementId) => !mappedRequirementIds.has(requirementId));
  requireCondition(
    missingMappings.length === 0,
    `requirements missing milestone mapping: ${missingMappings.join(", ")}`,
  );
  requireCondition(
    contract.evidenceLedger?.schema === "docs/evidence/ledger.schema.json",
    "contract must link evidence ledger schema",
  );
  requireCondition(
    contract.evidenceLedger?.missingMandatoryEvidenceBehavior === "block_release",
    "missing mandatory evidence must block release",
  );

  validateLedgerSchema(schema);
  return contract;
}

function validateLedgerEntries(contract, ledger) {
  requireCondition(ledger.schemaVersion === "flying.evidence-ledger.v1", "ledger schemaVersion mismatch");
  requireCondition(ledger.contractId === contract.contractId, "ledger contractId mismatch");
  requireCondition(Array.isArray(ledger.requirements) && ledger.requirements.length > 0,
    "evidence ledger requirements must be a non-empty list");

  const requirementById = new Map(contract.requirements.map((requirement) => [requirement.id, requirement]));
  const seen = new Set();
  for (const entry of ledger.requirements) {
    const requirement = requirementById.get(entry.requirementId);
    requireCondition(requirement, `ledger references unknown requirement: ${entry.requirementId}`);
    requireCondition(!seen.has(entry.requirementId), `duplicate ledger evidence entry: ${entry.requirementId}`);
    seen.add(entry.requirementId);
    requireCondition(
      Array.isArray(entry.proofRefs) && entry.proofRefs.length > 0 || Boolean(entry.blockerRef),
      `ledger entry must link proofRefs or blockerRef: ${entry.requirementId}`,
    );
    requireCondition(entry.milestone === requirement.milestone, `ledger milestone mismatch: ${entry.requirementId}`);
    if (requirement.mandatory) {
      requireCondition(entry.mandatory === true, `mandatory flag mismatch: ${entry.requirementId}`);
    }
  }

  const missing = [...requirementById.keys()].filter((requirementId) => !seen.has(requirementId));
  requireCondition(missing.length === 0, `evidence ledger missing requirements: ${missing.join(", ")}`);
}

function resolveLedgerPath(args) {
  const optionIndex = args.indexOf("--ledger");
  const configuredPath = optionIndex >= 0 ? args[optionIndex + 1] : "docs/evidence/ledger.json";
  requireCondition(Boolean(configuredPath), "--ledger requires a repository-relative path");
  const resolved = path.resolve(repoRoot, configuredPath);
  requireCondition(
    resolved === repoRoot || resolved.startsWith(`${repoRoot}${path.sep}`),
    "ledger path must stay inside the repository",
  );
  return resolved;
}

function validateReleaseGate(args) {
  const contract = validateContract();
  const ledger = readJson(resolveLedgerPath(args));
  validateLedgerEntries(contract, ledger);
  const failures = ledger.requirements
    .filter((entry) => entry.mandatory === true && entry.status !== "pass")
    .map((entry) => entry.requirementId);
  requireCondition(failures.length === 0, `mandatory evidence is not passing: ${failures.join(", ")}`);
}

function validateMissingEvidenceSelftest() {
  const contract = validateContract();
  const invalidLedger = {
    schemaVersion: "flying.evidence-ledger.v1",
    contractId: contract.contractId,
    generatedAtUtc: "2026-08-11T00:00:00Z",
    requirements: contract.requirements.map((requirement) => ({
      requirementId: requirement.id,
      milestone: requirement.milestone,
      mandatory: requirement.mandatory,
      status: "pending",
    })),
  };

  try {
    validateLedgerEntries(contract, invalidLedger);
  } catch (error) {
    if (error instanceof ValidationError) {
      return;
    }
    throw error;
  }
  throw new ValidationError("missing mandatory evidence was accepted");
}

function main() {
  const [command, ...args] = process.argv.slice(2);
  switch (command) {
    case "contract":
      validateContract();
      console.log("forge_validate: contract ok");
      return;
    case "architecture":
    case "architectur":
      validateContract();
      console.log("forge_validate: architecture ok");
      return;
    case "release-gate":
      validateReleaseGate(args);
      console.log("forge_validate: release gate ok");
      return;
    case "missing-evidence-selftest":
      validateMissingEvidenceSelftest();
      console.log("forge_validate: missing mandatory evidence rejected");
      return;
    default:
      throw new ValidationError(`unknown command: ${command ?? "(missing)"}`);
  }
}

try {
  main();
} catch (error) {
  console.error(`forge_validate: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
}
