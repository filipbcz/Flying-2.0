import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { validateEvidence } from "../Tools/UnrealBaseline/validate_evidence.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dir = "docs/validation/editor-baseline/";
const acceptance = JSON.parse(fs.readFileSync(path.join(root, dir, "acceptance.json")));
const temp = fs.mkdtempSync(path.join(os.tmpdir(), "flying-baseline-evidence-"));
const files = new Set([dir + "acceptance.json",
  ...acceptance.sourceInputs.map((a) => a.path),
  ...acceptance.evidence.map((a) => a.path), ...acceptance.artifacts.map((a) => a.path)]);
function restore(relative) {
  const destination = path.join(temp, relative);
  fs.mkdirSync(path.dirname(destination), { recursive: true });
  fs.copyFileSync(path.join(root, relative), destination);
}
function rejected(relative, mutate, reason) {
  mutate(path.join(temp, relative));
  assert.throws(() => validateEvidence(temp), undefined, reason + " must reject acceptance");
  restore(relative);
}
function editJson(file, edit) {
  const value = JSON.parse(fs.readFileSync(file));
  edit(value);
  fs.writeFileSync(file, JSON.stringify(value));
}
try {
  files.forEach(restore);
  assert.doesNotThrow(() => validateEvidence(temp));
  rejected(acceptance.artifacts[0].path, (p) => fs.unlinkSync(p), "Missing Unreal package");
  rejected(acceptance.artifacts[0].path, (p) => {
    const data = fs.readFileSync(p);
    data[data.length - 1] ^= 1;
    fs.writeFileSync(p, data);
  }, "Tampered Unreal package");
  rejected(dir + "preflight.json", (p) => editJson(p, (v) => {
    v.unreal.buildVersion.PatchVersion = 1;
  }), "Unsupported UE patch");
  rejected(dir + "editor-open.json", (p) => editJson(p, (v) => {
    v.identity.workerLeaseId = "00000000-0000-0000-0000-000000000000";
  }), "Evidence from another worker session");
  rejected(dir + "open-result.json", (p) => editJson(p, (v) => {
    v.exitCode = 3;
  }), "Failed Editor process");
  rejected(dir + "open-result.json", (p) => editJson(p, (v) => {
    v.stdout = "Editor exited without running the verification script";
  }), "Zero exit without Editor verification");
  rejected(dir + "checkpoint.json", (p) => editJson(p, (v) => {
    v.status = "waiting_for_capability";
  }), "Incomplete checkpoint");
  console.log("Editor baseline evidence: valid bundle passes; 7 negative cases rejected");
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
