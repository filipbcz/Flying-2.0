#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const automationTestName =
  "Flying.Presentation.Aircraft.EcefSnapshotDrivesCockpitAndExternalVisuals";

function fail(message) {
  console.error(`validate_ecef_aircraft_visuals: ${message}`);
  process.exit(1);
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function firstExisting(candidates) {
  return candidates.find((candidate) => candidate && fs.existsSync(candidate));
}

function findUnrealRoot() {
  return firstExisting([
    process.env.UE_5_8_ROOT,
    process.env.UNREAL_ENGINE_ROOT,
    "C:/Program Files/Epic Games/UE_5.8",
    "D:/Program Files/Epic Games/UE_5.8",
  ]);
}

function assertUnreal58(unrealRoot) {
  const versionPath = path.join(unrealRoot, "Engine", "Build", "Build.version");
  requireCondition(fs.existsSync(versionPath), "UE Build.version not found");
  const version = JSON.parse(fs.readFileSync(versionPath, "utf8"));
  requireCondition(
    version.MajorVersion === 5 && version.MinorVersion === 8,
    `Unreal toolchain must be UE 5.8, found ${version.MajorVersion}.${version.MinorVersion}`,
  );
}

function validateUnrealRuntime() {
  requireCondition(process.platform === "win32", "Unreal runtime validation requires Windows");
  const unrealRoot = findUnrealRoot();
  requireCondition(unrealRoot, "UE 5.8 toolchain not found; set UE_5_8_ROOT or UNREAL_ENGINE_ROOT");
  assertUnreal58(unrealRoot);

  const editor = firstExisting([
    path.join(unrealRoot, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe"),
    path.join(unrealRoot, "Engine", "Binaries", "Win64", "UnrealEditor.exe"),
  ]);
  requireCondition(editor, "UnrealEditor-Cmd.exe not found in UE 5.8 root");

  const reportDir = path.join(repoRoot, "Saved", "Automation", "AircraftEcefSnapshotVisuals");
  fs.mkdirSync(reportDir, { recursive: true });
  const result = spawnSync(
    editor,
    [
      path.join(repoRoot, "unreal", "Flying.uproject"),
      "-game",
      "-unattended",
      "-nopause",
      "-stdout",
      `-ReportOutputPath=${reportDir}`,
      `-ExecCmds=Automation RunTests ${automationTestName}; Quit`,
      "-testexit=Automation Test Queue Empty",
    ],
    {
      cwd: path.join(repoRoot, "unreal"),
      encoding: "utf8",
      shell: false,
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

  const combinedOutput = `${result.stdout ?? ""}\n${result.stderr ?? ""}`;
  requireCondition(
    result.status === 0,
    `Unreal aircraft ECEF snapshot automation failed with exit code ${result.status}: ${combinedOutput}`,
  );
  requireCondition(
    combinedOutput.includes(automationTestName),
    "Unreal automation output did not include the aircraft ECEF snapshot test",
  );
  requireCondition(!/Error:/.test(combinedOutput), "Unreal automation output contains an error marker");
}

const bridgeHeader = read("unreal/Source/FlyingCoreSimBridge/Public/FlyingCoreSimBridge.h");
for (const token of [
  "FFlyingCoreSimImmutableStateSnapshot",
  "EcefPositionMeters",
  "EcefVelocityMetersPerSecond",
  "BodyToEcef",
]) {
  requireCondition(bridgeHeader.includes(token), `bridge immutable snapshot missing ${token}`);
}

const componentHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCoreSimComponent.h");
requireCondition(
  componentHeader.includes('#include "FlyingCoreSimBridge.h"'),
  "UFlyingCoreSimComponent must expose the bridge snapshot contract",
);
requireCondition(
  componentHeader.includes("GetCurrentImmutableSnapshot() const") &&
    componentHeader.includes("FFlyingCoreSimImmutableStateSnapshot CurrentImmutableSnapshot"),
  "UFlyingCoreSimComponent must publish an immutable bridge snapshot",
);

const componentCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCoreSimComponent.cpp");
requireCondition(
  /FFlyingCoreSimImmutableStateSnapshot\s+SnapshotImmutable\(\)\s+const[\s\S]*?const AuthoritativeState& State = Simulator\.state\(\)/.test(componentCpp),
  "bridge implementation must derive immutable snapshots from authoritative CoreSim state",
);
for (const token of [
  "Snapshot.EcefPositionMeters = ToUnrealVector(State.ecef_position_m)",
  "Snapshot.EcefVelocityMetersPerSecond = ToUnrealVector(State.ecef_velocity_mps)",
  "Snapshot.BodyToEcef = ToUnrealQuat(State.body_to_ecef)",
  "CurrentImmutableSnapshot = Bridge ? Bridge->SnapshotImmutable() : FFlyingCoreSimImmutableStateSnapshot{}",
]) {
  requireCondition(componentCpp.includes(token), `immutable snapshot publication missing ${token}`);
}

const georefHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCesiumGeoreferenceComponent.h");
const georefCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCesiumGeoreferenceComponent.cpp");
requireCondition(
  georefHeader.includes("TransformBodyToUnrealRotator(") &&
    georefHeader.includes("FFlyingCoreSimImmutableStateSnapshot"),
  "Cesium georeference component must accept immutable CoreSim snapshots",
);
requireCondition(
  georefCpp.includes("TransformEarthCenteredEarthFixedPositionToUnreal") &&
    georefCpp.includes("TransformEarthCenteredEarthFixedDirectionToUnreal"),
  "Cesium georeference component must use Cesium ECEF transforms",
);
requireCondition(
  /TransformBodyToUnrealRotator\(\s*const FFlyingCoreSimImmutableStateSnapshot& Snapshot\)[\s\S]*?TransformBodyQuatToUnrealRotator\(\*this, Snapshot\.BodyToEcef\)/.test(georefCpp),
  "immutable snapshot attitude must be recomputed through the Cesium georeference",
);

const actorHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCoreSimAircraftActor.h");
const actorCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCoreSimAircraftActor.cpp");
const automationCpp = read(
  "unreal/Source/FlyingPresentation/Private/FlyingAircraftEcefSnapshotVisualsAutomation.cpp",
);
requireCondition(
  actorHeader.includes("UpdatePresentationFromImmutableSnapshot") &&
    actorHeader.includes("DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot") &&
    actorHeader.includes("DoesVisualPresentationMatchImmutableSnapshot"),
  "aircraft actor must expose immutable snapshot visual contract evidence",
);
requireCondition(
  /UpdatePresentationFromCoreSim\(\)[\s\S]*?UpdatePresentationFromImmutableSnapshot\(CoreSimComponent->GetCurrentImmutableSnapshot\(\)\)/.test(actorCpp),
  "aircraft actor must consume UFlyingCoreSimComponent immutable snapshots for visual transforms",
);
requireCondition(
  /UpdatePresentationFromImmutableSnapshot\(\s*const FFlyingCoreSimImmutableStateSnapshot& Snapshot\)[\s\S]*?TransformEcefPositionToUnreal\(Snapshot\.EcefPositionMeters\)[\s\S]*?TransformBodyToUnrealRotator\(Snapshot\)[\s\S]*?SetActorLocationAndRotation/.test(actorCpp),
  "aircraft actor must derive location and orientation from immutable ECEF snapshots",
);
requireCondition(
  /ApplyWorldOffset\([\s\S]*?if \(bWorldShift\)[\s\S]*?UpdatePresentationFromCoreSim\(\)/.test(actorCpp),
  "aircraft actor must recompute ECEF visual transforms after origin shifts",
);
requireCondition(
  actorCpp.includes("LastAppliedImmutableSnapshot = Snapshot") &&
    actorCpp.includes("LastAppliedUnrealLocation = UnrealLocation") &&
    actorCpp.includes("LastAppliedUnrealRotation = UnrealRotation") &&
    actorCpp.includes("bHasAppliedImmutableSnapshot = true"),
  "aircraft actor must retain the immutable snapshot and transform actually applied to visuals",
);
requireCondition(
  /DoesVisualPresentationMatchImmutableSnapshot\(\s*const FFlyingCoreSimImmutableStateSnapshot& Snapshot\) const[\s\S]*?SnapshotsMatch\(LastAppliedImmutableSnapshot, Snapshot\)[\s\S]*?GetActorLocation\(\)\.Equals\(LastAppliedUnrealLocation[\s\S]*?GetActorRotation\(\)\.Equals\(LastAppliedUnrealRotation[\s\S]*?DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot\(\)/.test(actorCpp),
  "aircraft actor must be able to verify runtime visuals against the last applied immutable snapshot",
);
requireCondition(
  /DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot\(\) const[\s\S]*?bHasAppliedImmutableSnapshot[\s\S]*?GetRootComponent\(\) == SceneRoot[\s\S]*?IsAttachedTo\(AircraftMesh, SceneRoot\)[\s\S]*?IsAttachedTo\(CockpitCamera, SceneRoot\)[\s\S]*?IsAttachedTo\(ExteriorCamera, SceneRoot\)/.test(actorCpp),
  "cockpit and external runtime visual components must share the snapshot-driven actor root",
);

requireCondition(
  automationCpp.includes("IMPLEMENT_SIMPLE_AUTOMATION_TEST") &&
    automationCpp.includes(automationTestName),
  "missing Unreal automation test for ECEF snapshot-driven cockpit and external visuals",
);
for (const token of [
  "World->SpawnActor<AFlyingCoreSimAircraftActor>()",
  "Aircraft->FindComponentByClass<UFlyingCoreSimComponent>()",
  "CoreSim->GetCurrentImmutableSnapshot()",
  "Aircraft->UpdatePresentationFromImmutableSnapshot(PublishedSnapshot)",
  "Aircraft->SetCockpitCameraMode(EFlyingCockpitCameraMode::Pilot)",
  "Aircraft->SetCockpitCameraMode(EFlyingCockpitCameraMode::ExteriorOrbit)",
  "Aircraft->DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot()",
  "Aircraft->DoesVisualPresentationMatchImmutableSnapshot(PublishedSnapshot)",
  "Aircraft->ApplyWorldOffset(",
  "Aircraft->DoesVisualPresentationMatchImmutableSnapshot(CoreSim->GetCurrentImmutableSnapshot())",
]) {
  requireCondition(
    automationCpp.includes(token),
    `Unreal automation test missing runtime assertion token: ${token}`,
  );
}
requireCondition(
  !/GetAttachParent\(\) == SceneRoot/.test(automationCpp),
  "runtime automation may not reduce cockpit/external evidence to direct source attachment checks",
);

if (process.argv.includes("--require-unreal-runtime")) {
  validateUnrealRuntime();
}

console.log("validate_ecef_aircraft_visuals: ok");
