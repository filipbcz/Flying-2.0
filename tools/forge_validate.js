#!/usr/bin/env node

const fs = require("node:fs");
const path = require("node:path");

const repoRoot = path.resolve(__dirname, "..");
const contractPath = path.join(repoRoot, "docs", "contract", "flying-2.0.yml");
const ledgerSchemaPath = path.join(repoRoot, "docs", "evidence", "ledger.schema.json");
const reqIdPattern = /^REQ-[A-Z0-9]+(?:-[A-Z0-9]+)*$/;
const milestoneIdPattern = /^M[0-9]+$/;

function fail(message) {
  console.error(`forge_validate: ${message}`);
  process.exit(1);
}

function readJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (error) {
    fail(`cannot read ${path.relative(repoRoot, filePath)}: ${error.message}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
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
  requireCondition(Array.isArray(contract.requirements) && contract.requirements.length > 0,
    "contract requirements must be a non-empty list");
  requireCondition(Array.isArray(contract.milestones) && contract.milestones.length > 0,
    "contract milestones must be a non-empty list");

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
    requireCondition(milestoneIds.has(requirement.milestone), `requirement maps to unknown milestone: ${requirement.id}`);
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
  requireCondition(missingMappings.length === 0, `requirements missing milestone mapping: ${missingMappings.join(", ")}`);
  requireCondition(contract.evidenceLedger?.schema === "docs/evidence/ledger.schema.json",
    "contract must link evidence ledger schema");
  requireCondition(contract.evidenceLedger?.missingMandatoryEvidenceBehavior === "block_release",
    "missing mandatory evidence must block release");

  validateLedgerSchema(schema);
  return contract;
}

function main() {
  const command = process.argv[2];
  if (command === "contract") {
    validateContract();
    console.log("forge_validate: contract ok");
    return;
  }
  if (command === "architecture" || command === "architectur") {
    validateContract();
    console.log("forge_validate: architecture ok");
    return;
  }

  fail(`unknown command: ${command ?? "(missing)"}`);
}

main();
