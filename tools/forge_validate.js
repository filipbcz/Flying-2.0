#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const contractPath = path.join(repoRoot, "docs", "contract", "flying-2.0.yml");
const contractMigrationsDir = path.join(repoRoot, "docs", "contract", "migrations");
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

  const evidenceOptions = requirementSchema.oneOf ?? requirementSchema.anyOf ?? [];
  const requiredSets = new Set(evidenceOptions.map((item) => (item.required ?? []).join(",")));
  requireCondition(
    requiredSets.has("proofRefs") && requiredSets.has("blockerRef"),
    "ledger must reject requirements without proofRefs or blockerRef",
  );
  requireCondition(
    Array.isArray(requirementSchema.oneOf),
    "ledger must make proofRefs and blockerRef mutually exclusive",
  );
  requireCondition(
    requirementSchema.properties?.proofRefs?.minItems === 1,
    "ledger proofRefs must require at least one proof",
  );
}

function refBase(ref) {
  return ref.split("#", 1)[0];
}

function validateProofRef(entry, proofRef) {
  requireCondition(proofRef && typeof proofRef === "object", `invalid proofRef: ${entry.requirementId}`);
  requireCondition(proofRef.status === "pass", `proofRef must be passing: ${entry.requirementId}`);
  requireCondition(proofRef.kind !== "external", `external references are blockers, not proof: ${entry.requirementId}`);
  requireCondition(
    !/(^|[/\\])(fixtures?|synthetic|declarations?)([/\\]|$)/i.test(proofRef.path),
    `synthetic, fixture, or declaration path cannot be production proof: ${entry.requirementId}`,
  );
  if (proofRef.kind !== "command") {
    const proofPath = path.resolve(repoRoot, refBase(proofRef.path));
    requireCondition(
      proofPath === repoRoot || proofPath.startsWith(`${repoRoot}${path.sep}`),
      `proofRef path must stay inside the repository: ${entry.requirementId}`,
    );
    requireCondition(fs.existsSync(proofPath), `proofRef path is missing: ${proofRef.path}`);
  }
}

function validateBlockerRef(entry) {
  requireCondition(
    entry.blockerRef.startsWith("docs/blockers/external-inputs.yml#"),
    `blockerRef must link the external blocker registry: ${entry.requirementId}`,
  );
  const blockerId = entry.blockerRef.slice("docs/blockers/external-inputs.yml#".length);
  const blockerRegistry = fs.readFileSync(path.join(repoRoot, "docs", "blockers", "external-inputs.yml"), "utf8");
  requireCondition(
    blockerRegistry.includes(`blocker_id: ${blockerId}`),
    `blockerRef does not exist in external blocker registry: ${entry.blockerRef}`,
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

function requirementMap(contractLike) {
  return new Map((contractLike.requirements ?? []).map((requirement) => [requirement.id, requirement]));
}

function milestoneMap(contractLike) {
  return new Map((contractLike.milestones ?? []).map((milestone) => [milestone.id, milestone]));
}

function validateRequirementNotWeakened(baseRequirement, currentRequirement, migrationId) {
  requireCondition(currentRequirement, `${migrationId}: active contract removed requirement ${baseRequirement.id}`);
  requireCondition(
    currentRequirement.mandatory === baseRequirement.mandatory,
    `${migrationId}: requirement mandatory flag changed for ${baseRequirement.id}`,
  );
  requireCondition(
    currentRequirement.milestone === baseRequirement.milestone,
    `${migrationId}: requirement milestone changed for ${baseRequirement.id}`,
  );
  requireCondition(
    typeof currentRequirement.evidenceGate === "string" && currentRequirement.evidenceGate.length >= baseRequirement.evidenceGate.length,
    `${migrationId}: requirement evidence gate was weakened for ${baseRequirement.id}`,
  );
  requireCondition(
    typeof currentRequirement.summary === "string" && currentRequirement.summary.length >= baseRequirement.summary.length,
    `${migrationId}: requirement summary was weakened for ${baseRequirement.id}`,
  );
}

function validateMigrationImpactNotes(migration) {
  if (migration.migrationId !== "0002-v4-production-evidence-delta") {
    return;
  }
  requireCondition(
    Array.isArray(migration.migrationImpactNotes) && migration.migrationImpactNotes.length >= 2,
    `${migration.migrationId}: migration impact notes are required`,
  );
  const notedAreas = new Set(migration.migrationImpactNotes.map((note) => note.area));
  requireCondition(notedAreas.has("content manifests"), `${migration.migrationId}: missing content manifest migration notes`);
  requireCondition(notedAreas.has("visual evidence records"), `${migration.migrationId}: missing visual evidence record migration notes`);
}

function validateContractMigrations() {
  const contract = validateContract();
  const activeRequirements = requirementMap(contract);
  const activeMilestones = milestoneMap(contract);
  const migrationNames = fs.readdirSync(contractMigrationsDir)
    .filter((name) => name.endsWith(".json"))
    .sort();

  requireCondition(migrationNames.length > 0, "no contract migrations found");
  for (const migrationName of migrationNames) {
    const migrationPath = path.join(contractMigrationsDir, migrationName);
    const migration = readJson(migrationPath);
    requireCondition(migration.schemaVersion === "flying.contract-migration.v1", `${migration.migrationId}: schemaVersion mismatch`);
    requireCondition(/^0[0-9]{3}-[a-z0-9-]+$/.test(migration.migrationId), `${migrationName}: invalid migrationId`);
    requireCondition(migration.status === "applied", `${migration.migrationId}: migration must be applied`);
    requireCondition(migration.appliesToContractId === contract.contractId, `${migration.migrationId}: contract id mismatch`);
    requireCondition(Array.isArray(migration.declaredDeltaFields) && migration.declaredDeltaFields.length > 0,
      `${migration.migrationId}: declaredDeltaFields are required`);
    requireCondition(migration.baseContractSnapshot?.contractId === contract.contractId,
      `${migration.migrationId}: baseContractSnapshot is required`);

    for (const requirementId of migration.preservedActiveRequirementIds ?? []) {
      requireCondition(activeRequirements.has(requirementId), `${migration.migrationId}: preserved requirement missing: ${requirementId}`);
    }
    for (const requirementId of migration.addedRequirementIds ?? []) {
      requireCondition(activeRequirements.has(requirementId), `${migration.migrationId}: added requirement missing: ${requirementId}`);
    }

    const baseRequirements = requirementMap(migration.baseContractSnapshot);
    for (const baseRequirement of baseRequirements.values()) {
      validateRequirementNotWeakened(baseRequirement, activeRequirements.get(baseRequirement.id), migration.migrationId);
    }

    for (const baseMilestone of migration.baseContractSnapshot.milestones ?? []) {
      const activeMilestone = activeMilestones.get(baseMilestone.id);
      requireCondition(activeMilestone, `${migration.migrationId}: active contract removed milestone ${baseMilestone.id}`);
      for (const requirementId of baseMilestone.requiredRequirementIds ?? []) {
        requireCondition(
          (activeMilestone.requiredRequirementIds ?? []).includes(requirementId),
          `${migration.migrationId}: milestone ${baseMilestone.id} no longer maps ${requirementId}`,
        );
      }
    }
    validateMigrationImpactNotes(migration);
  }

  const requiredV4Ids = [
    "REQ-PRODUCTION-UNREAL-CONTENT",
    "REQ-PRODUCTION-PILOT-REGIONAL-PACKAGE",
    "REQ-RUNTIME-SHIPPING-VISUAL-EVIDENCE",
    "REQ-AIRCRAFT-ECEF-VISUAL-AUTHORITY",
  ];
  requireCondition(contract.contractDeltaVersion === 4, "contractDeltaVersion must be 4");
  for (const requirementId of requiredV4Ids) {
    requireCondition(activeRequirements.has(requirementId), `active contract missing v4 requirement ${requirementId}`);
  }
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
    const hasProof = Array.isArray(entry.proofRefs) && entry.proofRefs.length > 0;
    const hasBlocker = Boolean(entry.blockerRef);
    requireCondition(hasProof !== hasBlocker, `ledger entry must link exactly one of proofRefs or blockerRef: ${entry.requirementId}`);
    requireCondition(entry.milestone === requirement.milestone, `ledger milestone mismatch: ${entry.requirementId}`);
    if (requirement.mandatory) {
      requireCondition(entry.mandatory === true, `mandatory flag mismatch: ${entry.requirementId}`);
    }
    if (hasProof) {
      requireCondition(entry.status === "pass", `proof-backed ledger entry must be pass: ${entry.requirementId}`);
      for (const proofRef of entry.proofRefs) {
        validateProofRef(entry, proofRef);
      }
    }
    if (hasBlocker) {
      requireCondition(entry.status === "blocked", `blocker-backed ledger entry must be blocked: ${entry.requirementId}`);
      validateBlockerRef(entry);
    }
  }

  const missing = [...requirementById.keys()].filter((requirementId) => !seen.has(requirementId));
  requireCondition(missing.length === 0, `evidence ledger missing requirements: ${missing.join(", ")}`);
}

function validateEvidenceLedger(args) {
  const contract = validateContract();
  const ledger = readJson(resolveLedgerPath(args));
  validateLedgerEntries(contract, ledger);
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

function validateMissingRequirementSelftest() {
  const contract = validateContract();
  const validEntries = contract.requirements.map((requirement) => ({
    requirementId: requirement.id,
    milestone: requirement.milestone,
    mandatory: requirement.mandatory,
    status: "blocked",
    blockerRef: "docs/blockers/external-inputs.yml#production-visual-shipping-evidence",
  }));
  const invalidLedger = {
    schemaVersion: "flying.evidence-ledger.v1",
    contractId: contract.contractId,
    generatedAtUtc: "2026-08-18T00:00:00Z",
    requirements: validEntries.slice(1),
  };

  try {
    validateLedgerEntries(contract, invalidLedger);
  } catch (error) {
    if (error instanceof ValidationError) {
      return;
    }
    throw error;
  }
  throw new ValidationError("missing requirement evidence was accepted");
}

function main() {
  const [command, ...args] = process.argv.slice(2);
  switch (command) {
    case "contract":
      validateContract();
      console.log("forge_validate: contract ok");
      return;
    case "contract-migrations":
      validateContractMigrations();
      console.log("forge_validate: contract migrations ok");
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
    case "evidence-ledger":
      validateEvidenceLedger(args);
      console.log("forge_validate: evidence ledger ok");
      return;
    case "missing-evidence-selftest":
      validateMissingEvidenceSelftest();
      console.log("forge_validate: missing mandatory evidence rejected");
      return;
    case "missing-requirement-selftest":
      validateMissingRequirementSelftest();
      console.log("forge_validate: missing requirement evidence rejected");
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
