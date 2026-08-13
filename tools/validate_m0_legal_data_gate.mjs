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

function indentation(line) {
  return line.match(/^ */)[0].length;
}

function parseScalar(value) {
  const trimmed = value.trim();
  if (trimmed === "true") {
    return true;
  }
  if (trimmed === "false") {
    return false;
  }
  if (/^-?\d+$/.test(trimmed)) {
    return Number(trimmed);
  }
  if (
    (trimmed.startsWith('"') && trimmed.endsWith('"')) ||
    (trimmed.startsWith("'") && trimmed.endsWith("'"))
  ) {
    return trimmed.slice(1, -1);
  }
  return trimmed;
}

function splitKeyValue(source) {
  const separator = source.indexOf(":");
  if (separator === -1) {
    fail(`invalid YAML entry: ${source}`);
  }
  return [source.slice(0, separator).trim(), source.slice(separator + 1).trim()];
}

function parseYaml(relativePath) {
  const lines = read(relativePath)
    .split(/\r?\n/)
    .filter((line) => line.trim() !== "" && !line.trimStart().startsWith("#"));

  function parseBlock(index, indent) {
    const first = lines[index];
    const isArray = first !== undefined && indentation(first) === indent && first.trimStart().startsWith("- ");
    const container = isArray ? [] : {};

    while (index < lines.length) {
      const line = lines[index];
      const currentIndent = indentation(line);
      if (currentIndent < indent) {
        break;
      }
      if (currentIndent > indent) {
        fail(`${relativePath}: unexpected indentation at line ${index + 1}`);
      }

      const trimmed = line.trimStart();
      if (isArray) {
        if (!trimmed.startsWith("- ")) {
          break;
        }
        const itemText = trimmed.slice(2).trim();
        index += 1;
        if (itemText === "") {
          const [child, nextIndex] = parseBlock(index, indent + 2);
          container.push(child);
          index = nextIndex;
          continue;
        }
        if (itemText.includes(":")) {
          const [key, value] = splitKeyValue(itemText);
          const item = { [key]: value === "" ? undefined : parseScalar(value) };
          if (value === "") {
            const [child, nextIndex] = parseBlock(index, indent + 2);
            item[key] = child;
            index = nextIndex;
          }
          if (index < lines.length && indentation(lines[index]) === indent + 2) {
            const [rest, nextIndex] = parseBlock(index, indent + 2);
            Object.assign(item, rest);
            index = nextIndex;
          }
          container.push(item);
          continue;
        }
        container.push(parseScalar(itemText));
        continue;
      }

      if (trimmed.startsWith("- ")) {
        break;
      }
      const [key, value] = splitKeyValue(trimmed);
      index += 1;
      if (value === "") {
        const [child, nextIndex] = parseBlock(index, indent + 2);
        container[key] = child;
        index = nextIndex;
      } else {
        container[key] = parseScalar(value);
      }
    }

    return [container, index];
  }

  const [parsed, nextIndex] = parseBlock(0, 0);
  if (nextIndex !== lines.length) {
    fail(`${relativePath}: trailing unparsed YAML at line ${nextIndex + 1}`);
  }
  return parsed;
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

requireArray(evidence.requirement_ids, "M0 evidence requirement_ids");
for (const requirementId of [
  "REQ-LEGAL-DATA-GATE",
  "REQ-SOURCE-AUDITABILITY",
  "REQ-AIRCRAFT-DATA-RIGHTS"
]) {
  requireCondition(
    evidence.requirement_ids.includes(requirementId),
    `M0 evidence requirement_ids missing ${requirementId}`
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
