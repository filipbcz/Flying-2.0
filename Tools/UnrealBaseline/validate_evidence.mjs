import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const defaultRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

export function validateEvidence(root = defaultRoot, { finalize = false, requireLocalBinaries = false } = {}) {
const dir = "docs/validation/editor-baseline/";
const read = (p) => fs.readFileSync(path.join(root, p));
const json = (p) => JSON.parse(read(dir + p));
const bytes = (p, mode = "raw") => mode === "utf8-lf"
  ? Buffer.from(read(p).toString("utf8").replaceAll("\r\n", "\n"), "utf8") : read(p);
const hash = (p, mode = "raw") => crypto.createHash("sha256").update(bytes(p, mode)).digest("hex");
const save = (p, value) => fs.writeFileSync(path.join(root, dir, p), JSON.stringify(value, null, 2) + "\n");
const entry = (p) => {
  const hashMode = /\.(dll|target|umap|uasset)$/.test(p) ? "raw" : "utf8-lf";
  return { path: p, sha256: hash(p, hashMode), sizeBytes: bytes(p, hashMode).length, hashMode };
};
function walk(relative) {
  return fs.readdirSync(path.join(root, relative), { withFileTypes: true }).flatMap((e) =>
    e.isDirectory() ? walk(relative + "/" + e.name) : [relative + "/" + e.name]);
}
function inputs() {
  return [...walk("unreal/Source"), ...walk("unreal/Config"),
    ...walk("core_sim/include"), ...walk("geo_terrain/include"),
    "core_sim/src/flight_dynamics.cpp", "unreal/Flying.uproject",
    "Tools/UnrealBaseline/materialize_startup.py"].sort();
}
function artifact(a) {
  assert.equal(typeof a.path, "string");
  assert(!path.isAbsolute(a.path) && !a.path.includes("..") && !a.path.includes("\\"),
    "Artifact must stay inside the repository");
  assert.equal(hash(a.path, a.hashMode), a.sha256, "Changed artifact: " + a.path);
  assert.equal(bytes(a.path, a.hashMode).length, a.sizeBytes, "Changed size: " + a.path);
}
function validateRun(run, tool, phase) {
  assert.equal(run.exitCode, 0, "Unreal process did not pass: " + run.checkId);
  assert.equal(run.authoring.tool, tool);
  assert.equal(run.authoring.phase, phase);
  assert.equal(run.authoring.projectRelativePath, "unreal/Flying.uproject");
  assert(Date.parse(run.startedAt) < Date.parse(run.completedAt), "Invalid process timestamps");
}
function verifyRecords() {
  const preflight = json("preflight.json");
  const checkpoint = json("checkpoint.json");
  const build = json("build-result.json");
  const author = json("author-result.json");
  const open = json("open-result.json");
  const materialized = json("materialization.json");
  const opened = json("editor-open.json");
  assert.equal(preflight.status, "pass");
  const id = preflight.identity;
  assert.match(id.repository, /^https:\/\/github.com\/filipbcz\/Flying-2\.0\.git$/i);
  assert.match(id.baseCommit, /^[a-f0-9]{40}$/);
  assert.match(id.workerLeaseId, /^[a-f0-9-]{36}$/);
  assert(id.checkout.replaceAll("\\", "/").endsWith("/" + id.workerLeaseId));
  assert(id.host && id.windowsSessionId > 0);
  assert.equal(id.project, "unreal/Flying.uproject");
  assert.deepEqual(checkpoint.identity, id);
  const ue = preflight.unreal.buildVersion;
  assert.equal(ue.MajorVersion, 5);
  assert.equal(ue.MinorVersion, 8);
  assert(ue.PatchVersion >= 2);
  const cesium = preflight.cesium.version.split(".").map(Number);
  assert(cesium[0] > 2 || (cesium[0] === 2 && cesium[1] >= 28));
  assert(preflight.toolchain.compilers.some((c) => c.fileVersion && c.toolset && c.executable));
  assert(preflight.toolchain.windowsSDKs.length > 0);
  assert(Number(preflight.windows.build) >= 22000);
  assert.equal(preflight.capabilities.interactiveDesktop, true);
  assert(preflight.capabilities.gpu.some((gpu) => gpu.name && gpu.driverVersion));
  assert(preflight.disk.availableBytes > 0);
  validateRun(build, "unreal-build", "build");
  assert.deepEqual(build.authoring.args.slice(0, 3), ["FlyingEditor", "Win64", "Development"]);
  assert.match(build.stdout, /Result: Succeeded/);
  assert.match(build.stdout, /Using Visual Studio .+toolchain.*Windows .+SDK/);
  validateRun(author, "unreal-editor", "author");
  validateRun(open, "unreal-editor", "author");
  assert(open.authoring.args.includes("-FlyingBaselineVerify"));
  assert(!open.authoring.args.some((a) => /^-nullrhi$/i.test(a)));
  assert.match(open.stdout, /LogRHI: Using Default RHI: D3D12/);
  for (const [report, mode, run] of [[materialized, "materialize", author], [opened, "verify", open]]) {
    assert.deepEqual(report.identity, id);
    assert.equal(report.status, "pass");
    assert.equal(report.mode, mode);
    assert(report.engineVersion.startsWith("5.8." + ue.PatchVersion + "-"));
    assert.equal(report.world, "/Game/Flying/Maps/FlyingStartup.FlyingStartup");
    assert.equal(report.nativeContentBuilder, "/Script/FlyingPresentation.FlyingPilotGameMode");
    assert.equal(report.gameMode, "/Game/Flying/Blueprints/BP_FlyingPilotGameMode.BP_FlyingPilotGameMode_C");
    assert.equal(report.georeferenceCount, 1);
    assert.match(run.stdout, new RegExp("FLYING_EDITOR_BASELINE_PASS " + mode));
    assert.equal(report.artifacts.length, 3);
    assert.deepEqual(report.artifacts.map((a) => a.path).sort(), [
      "unreal/Content/Flying/Blueprints/BP_FlyingPilotGameMode.uasset",
      "unreal/Content/Flying/Maps/FlyingStartup.umap",
      "unreal/Content/Flying/Materials/M_OfflineVertexColor.uasset",
    ]);
    for (const a of report.artifacts) {
      artifact(a);
      assert(a.sizeBytes > 1024);
      assert.equal(read(a.path).subarray(0, 4).toString("hex"), "c1832a9e");
    }
  }
  assert(Date.parse(build.completedAt) <= Date.parse(open.startedAt));
  assert(!/LogPython: Error|CDO Constructor/.test(open.stdout), "Project startup errors remain");
  assert(Date.parse(author.completedAt) <= Date.parse(open.startedAt), "Must reopen in a separate process");
  const config = read("unreal/Config/DefaultEngine.ini").toString();
  for (const key of ["GameDefaultMap", "EditorStartupMap"]) {
    assert(config.includes(key + "=/Game/Flying/Maps/FlyingStartup"));
  }
  return { preflight, build, author, open, opened };
}

  const records = verifyRecords();
  if (finalize) {
    const binaryPaths = ["unreal/Binaries/Win64/UnrealEditor-FlyingPresentation.dll",
      "unreal/Binaries/Win64/UnrealEditor-FlyingCoreSimBridge.dll",
      "unreal/Binaries/Win64/FlyingEditor.target"];
    const evidencePaths = ["preflight.json", "build-result.json", "author-result.json",
      "open-result.json", "materialization.json", "editor-open.json"];
    const checkpoint = json("checkpoint.json");
    checkpoint.status = "complete";
    checkpoint.completedSteps = ["preflight", "editor-build", "materialize-startup", "fresh-editor-open", "artifact-verification"];
    checkpoint.nextStep = null;
    checkpoint.reason = null;
    checkpoint.updatedAtUtc = new Date().toISOString();
    save("checkpoint.json", checkpoint);
    save("acceptance.json", {
      schemaVersion: "flying.editor-baseline-acceptance.v1",
      status: "pass", recordedAtUtc: new Date().toISOString(),
      identity: records.preflight.identity,
      buildId: records.preflight.identity.baseCommit + "-editor-" + records.preflight.identity.workerLeaseId,
      classification: "build-validation",
      engineDiagnostics: records.open.stdout.split(/\r?\n/).filter((line) => /LogClass: Error|LogAutomationTest: Error/.test(line)),
      sourceInputs: inputs().map(entry),
      artifacts: records.opened.artifacts,
      buildProducts: binaryPaths.map(entry),
      evidence: [...evidencePaths, "checkpoint.json"].map((p) => entry(dir + p)),
      scope: "Canonical Editor baseline; no Shipping, regional GIS or performance claim",
    });
  }
  const acceptance = json("acceptance.json");
  assert.equal(acceptance.status, "pass");
  assert.equal(json("checkpoint.json").status, "complete");
  assert.deepEqual(acceptance.identity, records.preflight.identity);
  assert.deepEqual(acceptance.sourceInputs.map((a) => a.path), inputs());
  [...acceptance.sourceInputs, ...acceptance.artifacts, ...acceptance.evidence].forEach(artifact);
  assert.equal(acceptance.buildProducts.length, 3);
  acceptance.buildProducts.forEach((a) => {
    assert.match(a.sha256, /^[a-f0-9]{64}$/);
    assert(a.sizeBytes > 1024);
    if (requireLocalBinaries) artifact(a);
  });
  return acceptance;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    validateEvidence(defaultRoot, {
      finalize: process.argv.includes("--finalize"),
      requireLocalBinaries: process.argv.includes("--require-local-binaries"),
    });
    console.log("Canonical Flying-2.0 Editor baseline evidence: PASS");
  } catch (error) {
    console.error("Editor baseline rejected: " + error.message);
    process.exitCode = 1;
  }
}
