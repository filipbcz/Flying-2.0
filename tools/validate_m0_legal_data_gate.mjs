#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

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

function requireToken(source, token, label) {
  if (!source.includes(token)) {
    fail(`${label} missing token: ${token}`);
  }
}

const evidence = read("docs/evidence/m0-legal-data-gate.yml");
const attribution = read("docs/licenses/source-attribution.yml");
const blockers = read("docs/blockers/external-inputs.yml");

for (const [source, label] of [
  [evidence, "M0 evidence"],
  [attribution, "source attribution"],
  [blockers, "external blockers"]
]) {
  for (const token of [
    "source_version:",
    "license:",
    "attribution:",
    "redistribution_status:",
    "effective_date:"
  ]) {
    requireToken(source, token, label);
  }
}

for (const token of [
  "REQ-LEGAL-DATA-GATE",
  "REQ-SOURCE-AUDITABILITY",
  "REQ-AIRCRAFT-DATA-RIGHTS",
  "aim_aip_vfr_gate:",
  "consent_status: not_recorded",
  "fallback_status: approved_process_recorded_not_complete",
  "faithful_type_claims_allowed: false",
  "master_airport_list_gate:",
  "status: not_frozen_for_production"
]) {
  requireToken(evidence, token, "M0 evidence");
}

for (const token of [
  "cuzk-open-data",
  "Creative Commons Attribution 4.0 International (CC BY 4.0)",
  "blocked_pending_written_permission",
  "Flying Trainer One is an unbranded project-authored trainer data set"
]) {
  requireToken(attribution, token, "source attribution");
}

for (const token of [
  "blocker_id: aim-aip-vfr-permission",
  "blocker_id: aircraft-poh-afm-validation-rights",
  "blocker_id: production-master-airport-list",
  "Blocks faithful-type aircraft claims",
  "Blocks importing, embedding, redistributing"
]) {
  requireToken(blockers, token, "external blockers");
}

console.log("validate_m0_legal_data_gate: ok");
