#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { parse } from "yaml";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  console.error(`validate_m0_legal_data_gate: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  try {
    return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function parseYaml(relativePath) {
  try {
    return parse(read(relativePath));
  } catch (error) {
    fail(`${relativePath}: ${error instanceof Error ? error.message : String(error)}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function requireString(value, label) {
  requireCondition(typeof value === "string" && value.trim().length > 0, `${label} must be a non-empty string`);
}

function requireArray(value, label) {
  requireCondition(Array.isArray(value) && value.length > 0, `${label} must be a non-empty array`);
}

function requireRequiredFields(record, fields, label) {
  for (const field of fields) {
    requireString(record[field], `${label}.${field}`);
  }
}

const evidence = parseYaml("docs/evidence/m0-legal-data-gate.yml");
const attribution = parseYaml("docs/licenses/source-attribution.yml");
const blockers = parseYaml("docs/blockers/external-inputs.yml");
const contract = JSON.parse(read("docs/contract/flying-2.0.yml"));
const contractRequirementIds = new Set(contract.requirements?.map((requirement) => requirement.id) ?? []);

requireCondition(contractRequirementIds.size > 0, "contract must define requirement ids");

requireArray(evidence.requirement_ids, "M0 evidence requirement_ids");
for (const requirementId of [
  "REQ-CONTRACT-GOVERNANCE",
  "REQ-EVIDENCE-TRACEABILITY",
  "REQ-RELEASE-GATES"
]) {
  requireCondition(
    evidence.requirement_ids.includes(requirementId),
    `M0 evidence requirement_ids missing ${requirementId}`
  );
}
for (const requirementId of evidence.requirement_ids) {
  requireCondition(
    contractRequirementIds.has(requirementId),
    `M0 evidence references unknown contract requirement ${requirementId}`
  );
}

requireArray(evidence.source_inventory, "M0 evidence source_inventory");
for (const source of evidence.source_inventory) {
  requireRequiredFields(
    source,
    ["source_id", "source_version", "license", "attribution", "redistribution_status", "effective_date"],
    `source_inventory[${source.source_id ?? "unknown"}]`
  );
}

requireCondition(evidence.aim_aip_vfr_gate?.consent_status === "not_recorded", "AIM/AIP/VFR consent status must be recorded");
requireCondition(
  evidence.aim_aip_vfr_gate?.fallback_status === "approved_process_recorded_not_complete",
  "AIM/AIP/VFR fallback status must be recorded"
);
requireString(evidence.aim_aip_vfr_gate?.evidence_or_blocker, "AIM/AIP/VFR evidence_or_blocker");
requireCondition(
  evidence.aircraft_data_rights_gate?.faithful_type_claims_allowed === false,
  "faithful type claims must remain disallowed until aircraft rights are recorded"
);
requireString(evidence.aircraft_data_rights_gate?.poh_afm_rights_status, "aircraft POH/AFM rights status");
requireString(evidence.aircraft_data_rights_gate?.validation_data_rights_status, "aircraft validation data rights status");
requireCondition(
  evidence.master_airport_list_gate?.status === "not_frozen_for_production",
  "master airport list production gate status must be recorded"
);

requireArray(attribution.records, "source attribution records");
for (const record of attribution.records) {
  requireRequiredFields(
    record,
    ["source_id", "source_version", "license", "attribution", "redistribution_status", "effective_date"],
    `attribution.records[${record.source_id ?? "unknown"}]`
  );
  requireArray(record.applies_to, `attribution.records[${record.source_id}].applies_to`);
}
requireCondition(
  attribution.records.some((record) => record.source_id === "aim-aip-vfr" && record.redistribution_status === "blocked_pending_written_permission"),
  "source attribution must record AIM/AIP/VFR redistribution blocker"
);
requireCondition(
  attribution.records.some((record) => record.source_id === "flying-trainer-one-aircraft-data"),
  "source attribution must record aircraft data rights status"
);

requireArray(blockers.blockers, "external blockers");
for (const blocker of blockers.blockers) {
  requireRequiredFields(
    blocker,
    ["blocker_id", "external_input", "source_version", "license", "attribution", "redistribution_status", "effective_date", "release_gate_effect"],
    `blockers[${blocker.blocker_id ?? "unknown"}]`
  );
  requireArray(blocker.requirement_ids, `blockers[${blocker.blocker_id}].requirement_ids`);
  for (const requirementId of blocker.requirement_ids) {
    requireCondition(
      contractRequirementIds.has(requirementId),
      `blockers[${blocker.blocker_id}].requirement_ids references unknown contract requirement ${requirementId}`
    );
  }
  requireArray(blocker.acceptable_resolution, `blockers[${blocker.blocker_id}].acceptable_resolution`);
}
for (const blockerId of [
  "aim-aip-vfr-permission",
  "aircraft-poh-afm-validation-rights",
  "production-master-airport-list"
]) {
  requireCondition(
    blockers.blockers.some((blocker) => blocker.blocker_id === blockerId && blocker.current_status === "blocked"),
    `external blockers missing active blocker ${blockerId}`
  );
}

console.log("validate_m0_legal_data_gate: parsed YAML records ok");
