import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { test } from "node:test";

function run(args) {
  return spawnSync(process.execPath, args, {
    cwd: new URL("../..", import.meta.url),
    encoding: "utf8",
  });
}

test("valid soak report records required ten-hour stability evidence", () => {
  const result = run([
    "Tools/Stability/validate_soak_report.mjs",
    "--report",
    "Tools/Stability/fixtures/soak-report-valid.json",
  ]);

  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /validate_soak_report: ok/);
});

test("soak report fails when identical replay identity produces a different hash", () => {
  const result = run([
    "Tools/Stability/validate_soak_report.mjs",
    "--report",
    "Tools/Stability/fixtures/soak-report-invalid-replay.json",
  ]);

  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /replay hash is not deterministic/);
});

test("soak report fails when replay sample identity contradicts report identity", () => {
  const result = run([
    "Tools/Stability/validate_soak_report.mjs",
    "--report",
    "Tools/Stability/fixtures/soak-report-contradictory-identity.json",
  ]);

  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /replay hash sample identity differs/);
});

test("valid QA matrix covers all required supported GPUs and devices", () => {
  const result = run([
    "Tools/Stability/validate_qa_matrix.mjs",
    "--matrix",
    "Tools/Stability/fixtures/qa-matrix-valid.json",
  ]);

  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /validate_qa_matrix: ok/);
});

test("QA matrix fails when canonical required GPU is omitted from the matrix inventory", () => {
  const result = run([
    "Tools/Stability/validate_qa_matrix.mjs",
    "--matrix",
    "Tools/Stability/fixtures/qa-matrix-missing-gpu-inventory.json",
  ]);

  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /GPU required inventory missing: amd-rx-7800-xt/);
});

test("QA matrix CLI requirements add to canonical inventory instead of replacing it", () => {
  const result = run([
    "Tools/Stability/validate_qa_matrix.mjs",
    "--matrix",
    "Tools/Stability/fixtures/qa-matrix-missing-gpu-inventory.json",
    "--required-gpus",
    "nvidia-rtx-4070",
  ]);

  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /GPU required inventory missing: amd-rx-7800-xt/);
});

test("QA matrix fails when required supported device evidence is missing", () => {
  const result = run([
    "Tools/Stability/validate_qa_matrix.mjs",
    "--matrix",
    "Tools/Stability/fixtures/qa-matrix-missing-device.json",
  ]);

  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /device evidence missing: xinput-gamepad/);
});
