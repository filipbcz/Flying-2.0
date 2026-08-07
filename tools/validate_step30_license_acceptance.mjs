#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  console.error(`validate_step30_license_acceptance: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  try {
    return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function readJson(relativePath) {
  try {
    return JSON.parse(read(relativePath));
  } catch (error) {
    fail(`invalid JSON in ${relativePath}: ${error.message}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function requireToken(source, token, label) {
  requireCondition(source.includes(token), `${label} missing token: ${token}`);
}

const inventory = read("docs/release/license-inventory.md");
const notices = read("third_party/NOTICES.md");
const legalGate = read("docs/governance/m0-legal-data-gate.md");
const aircraftGate = read("docs/governance/m0-aircraft-data-gate.md");
const userManual = read("docs/release/user-manual.md");
const knownLimitations = read("docs/release/known-limitations.md");
const sbom = readJson("docs/release/sbom.spdx.json");

for (const token of [
  "Flying 1.0.0 Win64 offline release",
  "CUZK DMR 5G",
  "CUZK DMP 1G",
  "CUZK Ortofoto Ceske republiky",
  "CUZK ZABAGED",
  "CUZK Geonames",
  "CC BY 4.0",
  "AIM/AIP/VFR data",
  "Do not include AIM/AIP/VFR content unless written permission and notices are archived.",
  "Flying Trainer One aircraft data",
  "no type-fidelity notice",
  "In-App Attribution Review"
]) {
  requireToken(inventory, token, "license inventory");
}

for (const token of [
  "Canonical CUZK attribution",
  "AIM/AIP/VFR content",
  "must not be imported, embedded or redistributed",
  "Operator-provided airport/runway/SLZ notices"
]) {
  requireToken(notices, token, "third-party notices");
}

requireToken(legalGate, "licensed under Creative Commons Attribution 4.0 International", "legal data gate");
requireToken(legalGate, "must not scrape, import, embed, redistribute or package AIP", "legal data gate");
requireToken(aircraftGate, "No aircraft model may be marketed or documented as a faithful simulation", "aircraft data gate");
requireToken(userManual, "not an EASA, FAA or UCL-certified flight training device", "user manual");
requireToken(knownLimitations, "not certified or approved as an EASA, FAA, UCL, FSTD, FNPT, flight-training", "known limitations");

requireCondition(sbom.spdxVersion === "SPDX-2.3", "SBOM must be SPDX 2.3");
requireCondition(Array.isArray(sbom.packages), "SBOM packages missing");

const packages = new Map(sbom.packages.map((pkg) => [pkg.SPDXID, pkg]));
for (const [spdxId, license] of [
  ["SPDXRef-Package-CUZK-DMR5G", "CC-BY-4.0"],
  ["SPDXRef-Package-CUZK-DMP1G", "CC-BY-4.0"],
  ["SPDXRef-Package-CUZK-Ortofoto", "CC-BY-4.0"],
  ["SPDXRef-Package-CUZK-ZABAGED", "CC-BY-4.0"],
  ["SPDXRef-Package-CUZK-Geonames", "CC-BY-4.0"],
  ["SPDXRef-Package-FlyingTrainerOneData", "CC0-1.0"],
  ["SPDXRef-Package-JSBSim", "LGPL-2.1-or-later"]
]) {
  const pkg = packages.get(spdxId);
  requireCondition(pkg, `SBOM package missing: ${spdxId}`);
  requireCondition(pkg.licenseConcluded === license, `${spdxId} concluded license mismatch`);
  requireCondition(pkg.licenseDeclared === license, `${spdxId} declared license mismatch`);
}

console.log("validate_step30_license_acceptance: ok");
