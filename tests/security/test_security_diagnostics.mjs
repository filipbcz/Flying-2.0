#!/usr/bin/env node

import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

import {
  loadJsonFileWithLastValidRecovery,
  validateSecurityPolicy,
  writeAtomicJsonFile,
} from "../../Tools/SecurityDiagnostics/security-diagnostics.mjs";

const root = path.resolve(import.meta.dirname, "../..");
const validator = path.join(root, "Tools/SecurityDiagnostics/security-diagnostics.mjs");
const fixtures = path.join(root, "Tools/SecurityDiagnostics/fixtures");
const schemaPath = path.join(root, "Tools/SecurityDiagnostics/security-policy.schema.json");
const atomicCategories = ["settings", "saves", "profiles", "scenarios", "consentState"];
const diagnosticArtifacts = ["sbom", "licenseInventory", "structuredLogs", "minidumpConfig", "buildId"];

function readFixture(name) {
  return JSON.parse(fs.readFileSync(path.join(fixtures, name), "utf8"));
}

function runValidator(fixture, extraArgs = []) {
  return spawnSync(process.execPath, [validator, "--policy", path.join(fixtures, fixture), ...extraArgs], {
    cwd: root,
    encoding: "utf8",
  });
}

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function assertFails(policy, expectedError) {
  const directErrors = validateSecurityPolicy(policy, { rootDir: root });
  assert(
    directErrors.some((error) => error.includes(expectedError)),
    `expected ${JSON.stringify(expectedError)} in ${JSON.stringify(directErrors)}`,
  );
}

function exerciseInterruptedAtomicPersistence(category) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), `flying-${category}-atomic-`));
  const target = path.join(dir, `${category}.json`);
  const original = {
    schemaVersion: `flying.${category}.v1`,
    category,
    sequence: 1,
    value: `last-valid-${category}`,
  };
  const replacement = {
    schemaVersion: `flying.${category}.v1`,
    category,
    sequence: 2,
    value: `interrupted-${category}`,
  };

  try {
    const firstWrite = writeAtomicJsonFile(target, original);
    const tempPath = `${target}.${process.pid}.tmp`;
    fs.writeFileSync(tempPath, JSON.stringify(replacement), "utf8");

    const afterInterruptedWrite = loadJsonFileWithLastValidRecovery(target);
    assert.deepEqual(afterInterruptedWrite.value, original);
    assert.equal(afterInterruptedWrite.recovered, false);
    assert.equal(afterInterruptedWrite.checksum, firstWrite.checksum);

    writeAtomicJsonFile(target, replacement);
    fs.writeFileSync(target, "{", "utf8");

    const recovered = loadJsonFileWithLastValidRecovery(target);
    assert.deepEqual(recovered.value, original);
    assert.equal(recovered.recovered, true);
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

{
  const schema = JSON.parse(fs.readFileSync(schemaPath, "utf8"));
  assert.deepEqual(schema.required, ["schemaVersion", "network", "telemetry", "atomicPersistence", "diagnostics"]);
  assert.deepEqual(schema.properties.atomicPersistence.required, atomicCategories);
  assert.deepEqual(schema.properties.diagnostics.required, diagnosticArtifacts);
}

{
  const result = runValidator("network-policy-valid.json");
  assert.equal(result.status, 0, result.stderr);
}

{
  const result = runValidator("network-policy-invalid-domain.json", ["--expect-failure"]);
  assert.equal(result.status, 0, result.stderr);
  const directErrors = validateSecurityPolicy(readFixture("network-policy-invalid-domain.json"), { rootDir: root });
  assert(directErrors.some((error) => error.includes("outside allowlist")));
}

{
  const policy = readFixture("telemetry-consent-valid.json");
  assert.deepEqual(validateSecurityPolicy(policy, { rootDir: root }), []);
  assert.equal(policy.telemetry.defaultUploadEnabled, false);
}

{
  const result = runValidator("telemetry-consent-invalid-personal-data.json", ["--expect-failure"]);
  assert.equal(result.status, 0, result.stderr);
  const directErrors = validateSecurityPolicy(readFixture("telemetry-consent-invalid-personal-data.json"), { rootDir: root });
  assert(directErrors.some((error) => error.includes("requires explicit consent")));
}

{
  const policy = readFixture("atomic-persistence-valid.json");
  for (const category of atomicCategories) {
    assert.equal(policy.atomicPersistence[category].interruptedWriteTested, true);
    assert.equal(policy.atomicPersistence[category].recoveredLastValid, true);
    assert.equal(policy.atomicPersistence[category].corruptWriteRejected, true);
    exerciseInterruptedAtomicPersistence(category);
  }
  assert.deepEqual(validateSecurityPolicy(policy, { rootDir: root }), []);
}

{
  const policy = readFixture("diagnostics-valid.json");
  for (const artifact of diagnosticArtifacts) {
    assert.equal(policy.diagnostics[artifact].present, true);
  }
  assert.deepEqual(validateSecurityPolicy(policy, { rootDir: root }), []);
}

{
  const basePolicy = readFixture("diagnostics-valid.json");
  const negativeCases = readFixture("negative-cases.json");
  for (const testCase of negativeCases.atomicPersistence) {
    const policy = clone(basePolicy);
    policy.atomicPersistence[testCase.category][testCase.field] = testCase.value;
    assertFails(policy, testCase.expectedError);
  }
  for (const testCase of negativeCases.diagnostics) {
    const policy = clone(basePolicy);
    if (testCase.mutation === "missing") {
      delete policy.diagnostics[testCase.artifact];
    } else if (testCase.mutation === "emptyPaths") {
      delete policy.diagnostics[testCase.artifact].path;
      policy.diagnostics[testCase.artifact].paths = [];
    } else if (testCase.mutation === "wrongPath") {
      delete policy.diagnostics[testCase.artifact].paths;
      policy.diagnostics[testCase.artifact].path = "packaging/README.md";
    } else if (testCase.mutation === "missingValue") {
      delete policy.diagnostics[testCase.artifact].value;
    } else {
      assert.fail(`unsupported negative mutation ${testCase.mutation}`);
    }
    assertFails(policy, testCase.expectedError);
  }
}

console.log("security diagnostics regression tests passed");
