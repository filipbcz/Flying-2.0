#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  console.error(`validate_step30_release_candidate_gate: ${message}`);
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

function resultIsPass(result) {
  return result === "pass";
}

const gatePath = "docs/validation/release/step30-release-candidate-gate.json";
const gate = readJson(gatePath);
const qaEvidence = read("docs/release/qa-evidence.md");
const licenseInventory = read("docs/release/license-inventory.md");
const sbom = readJson("docs/release/sbom.spdx.json");
const aircraftReport = read("docs/validation/aircraft/flying_trainer_one/aircraft-validation-report.md");
const performanceGate = readJson("docs/validation/performance/step27-performance-gate.json");

requireCondition(
  gate.schemaVersion === "flying.step30-release-candidate-gate.v1",
  "release candidate gate schema mismatch",
);
requireCondition(gate.documentId === "REL-30-RC-ACCEPTANCE-GATE", "document ID mismatch");
requireCondition(gate.releaseCandidate?.platform === "Win64", "release candidate must target Win64");
requireCondition(gate.releaseCandidate?.configuration === "Shipping", "release candidate must be Shipping");
requireCondition(gate.releaseCandidate?.signedBuildRequired === true, "signed Shipping build must be required");
requireCondition(gate.releaseCandidate?.installerRequired === true, "installer must be required");
requireCondition(gate.releaseCandidate?.buildMetadataRequired === true, "build metadata must be required");

const cleanMachine = gate.cleanMachineAcceptance;
requireCondition(cleanMachine?.required === true, "clean-machine acceptance must be required");
requireCondition(cleanMachine.environment?.os === "Windows 11", "clean-machine OS must be Windows 11");
requireCondition(cleanMachine.environment?.terrainScope === "Czech Republic", "terrain scope must be Czech Republic");
requireCondition(cleanMachine.environment?.airportDatabase === "approved master list", "approved airport database must be required");
requireCondition(
  cleanMachine.environment?.aircraft === "Flying Trainer One validated production aircraft",
  "validated aircraft must be required",
);
requireToken(cleanMachine.environment?.networkState ?? "", "offline", "clean-machine network state");

for (const phase of [
  "install signed Shipping build",
  "install Czech terrain packages",
  "install approved airport database",
  "start cold-and-dark at an included Czech airport",
  "start engine",
  "taxi",
  "takeoff",
  "fly cross-country over streamed Czech terrain",
  "land at another included airport",
  "shutdown",
  "replay flight",
  "export telemetry",
]) {
  requireCondition(cleanMachine.workflow?.includes(phase), `clean-machine workflow missing phase: ${phase}`);
}

for (const evidence of [
  "installer log",
  "installed release manifest hashes",
  "terrain package manifest hashes",
  "airport database package hash",
  "aircraft package hash",
  "offline-operation observation",
  "workflow telemetry export",
  "workflow replay artifact",
]) {
  requireCondition(cleanMachine.requiredEvidence?.includes(evidence), `clean-machine evidence missing: ${evidence}`);
}

const suites = new Map((gate.automatedAcceptanceSuites ?? []).map((suite) => [suite.area, suite]));
for (const area of [
  "CoreSim",
  "geodesy",
  "terrain",
  "runway",
  "airport coverage",
  "weather",
  "replay",
  "packaging",
  "license",
  "performance",
  "soak",
]) {
  const suite = suites.get(area);
  requireCondition(suite, `mandatory suite missing: ${area}`);
  requireCondition(suite.mandatory === true, `suite must be mandatory: ${area}`);
  requireCondition(typeof suite.command === "string" && suite.command.length > 0, `suite command missing: ${area}`);
}

requireCondition(
  suites.get("license").command === "node tools/validate_step30_license_acceptance.mjs",
  "license suite must run the standalone Step 30 licensing acceptance validator",
);

const coverage = gate.coverageReportReview;
requireCondition(
  coverage?.requiredSchemaVersion === "flying.pilot-runway-coverage-report.v1",
  "coverage report schema requirement mismatch",
);
requireCondition(coverage.requiredMissingActiveRunways === 0, "coverage gate must require zero missing active runways");
requireCondition(
  coverage.requireProductionValidatedRunwayProvenance === true,
  "coverage gate must require production-validated runway provenance",
);
if (coverage.result === "pass") {
  requireCondition(coverage.actualMissingActiveRunways === 0, "passing coverage gate must record zero missing active runways");
  requireCondition(
    coverage.productionValidatedRunwaysWithIncompleteRequiredProvenance === 0,
    "passing coverage gate must record zero production-validated runways with incomplete required provenance",
  );
}

for (const report of [
  "docs/validation/m1/m1-validation-report.md",
  "docs/validation/aircraft/flying_trainer_one/aircraft-validation-report.md",
  "docs/validation/performance/step27-performance-gate.json",
  "docs/release/qa-evidence.md",
  "docs/release/license-inventory.md",
  "docs/release/sbom.spdx.json",
]) {
  requireCondition(gate.validationReportReview?.requiredReports?.includes(report), `validation report missing: ${report}`);
}

const perf = gate.performanceAndSoakEvidence;
requireCondition(perf?.referenceOs === "Windows 11", "performance evidence must target Windows 11");
requireCondition(perf.requiredAverageFpsMin === performanceGate.budgets.averageFpsMin, "average FPS budget mismatch");
requireCondition(perf.requiredOnePercentLowFpsMin === performanceGate.budgets.onePercentLowFpsMin, "1% low FPS budget mismatch");
requireCondition(perf.requiredStreamingHitchMsMax === performanceGate.budgets.streamingHitchMsMax, "streaming hitch budget mismatch");
requireCondition(perf.requiredCoreSimMissedStepsMax === performanceGate.budgets.coreSimMissedStepsMax, "CoreSim missed-step budget mismatch");
requireCondition(perf.requiredInputLatencyMsMax === performanceGate.budgets.inputLatencyMsMax, "input latency budget mismatch");
requireCondition(perf.requiredSoakHoursMin === performanceGate.budgets.soakDurationHours, "soak duration budget mismatch");
if (perf.result === "pass") {
  requireCondition(perf.actualAverageFps >= perf.requiredAverageFpsMin, "average FPS evidence below release budget");
  requireCondition(perf.actualOnePercentLowFps >= perf.requiredOnePercentLowFpsMin, "1% low FPS evidence below release budget");
  requireCondition(perf.actualStreamingHitchMsMax <= perf.requiredStreamingHitchMsMax, "streaming hitch evidence above release budget");
  requireCondition(perf.actualCoreSimMissedSteps <= perf.requiredCoreSimMissedStepsMax, "CoreSim missed-step evidence above release budget");
  requireCondition(perf.actualInputLatencyMs <= perf.requiredInputLatencyMsMax, "input latency evidence above release budget");
  requireCondition(perf.actualSoakHours >= perf.requiredSoakHoursMin, "soak evidence below release duration");
}

const policy = gate.mandatoryFailurePolicy;
for (const area of [
  "physics",
  "terrain",
  "airport",
  "licensing",
  "offline-operation",
  "installer",
  "stability",
  "performance",
]) {
  requireCondition(policy?.blockedAreas?.includes(area), `mandatory failure policy missing area: ${area}`);
}
requireCondition(policy.waiversAllowed === false, "mandatory waivers must be disallowed");
requireToken(policy.decisionRule ?? "", "blocked unless every mandatory", "mandatory failure policy");

const mandatoryResults = [
  cleanMachine.result,
  coverage.result,
  gate.validationReportReview?.result,
  perf.result,
  ...(gate.automatedAcceptanceSuites ?? []).filter((suite) => suite.mandatory).map((suite) => suite.result),
];
const allMandatoryPassed = mandatoryResults.every(resultIsPass);

if (allMandatoryPassed) {
  requireCondition(gate.status === "pass", "gate status must pass when every mandatory result passes");
  requireCondition(gate.gateDecision?.decision === "pass", "gate must pass when every mandatory result passes");
  requireCondition(typeof gate.gateDecision?.approvedBy === "string" && gate.gateDecision.approvedBy.length > 0,
    "passing gate must identify approver");
  requireCondition(typeof gate.operator === "string" && gate.operator.length > 0, "passing gate must identify operator");
  requireCondition(typeof cleanMachine.environment?.machineId === "string" && cleanMachine.environment.machineId.length > 0,
    "passing gate must identify clean-machine ID");
} else {
  requireCondition(gate.status === "blocked", "gate status must remain blocked until every mandatory result passes");
  requireCondition(gate.gateDecision?.decision === "blocked", "gate decision must block when mandatory evidence is not pass");
  requireCondition(gate.gateDecision?.approvedBy === null, "blocked gate must not identify an approver");
}

requireToken(qaEvidence, "Release Candidate Gate", "QA evidence");
requireToken(qaEvidence, gatePath, "QA evidence");
requireToken(licenseInventory, "Flying 1.0.0 Win64 offline release", "license inventory");
requireCondition(sbom.SPDXID === "SPDXRef-DOCUMENT", "SBOM document SPDXID mismatch");
requireToken(aircraftReport, "Flying Trainer One", "aircraft validation report");
requireToken(aircraftReport, "Status:", "aircraft validation report");

console.log("validate_step30_release_candidate_gate: ok");
