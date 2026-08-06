#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  console.error(`validate_unreal_step18: ${message}`);
  process.exit(1);
}

function read(relativePath) {
  const absolutePath = path.join(repoRoot, relativePath);
  try {
    return fs.readFileSync(absolutePath, "utf8");
  } catch {
    fail(`missing required file: ${relativePath}`);
  }
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function requireTokens(source, tokens, label) {
  for (const token of tokens) {
    requireCondition(source.includes(token), `${label} missing token: ${token}`);
  }
}

const aircraftHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCoreSimAircraftActor.h");
const aircraftCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCoreSimAircraftActor.cpp");
const simHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCoreSimComponent.h");
const simCpp = read("unreal/Source/FlyingPresentation/Private/FlyingCoreSimComponent.cpp");
const snapshotHeader = read("unreal/Source/FlyingPresentation/Public/FlyingCoreSimStateSnapshot.h");
const standaloneSystems = read(
  "unreal/Source/FlyingPresentation/Private/CoreSimStandalone/aircraft_systems.cpp",
);

requireTokens(
  aircraftHeader,
  [
    "EFlyingCockpitControl::BatteryMaster",
    "Alternator",
    "AvionicsMaster",
    "FuelPump",
    "PitotHeat",
    "StandbyVacuum",
    "Magnetos",
    "Starter",
    "Throttle",
    "Mixture",
    "Propeller",
    "FuelSelector",
    "ParkingBrake",
    "Flaps",
    "Trim",
    "EmergencyFuelCutoff",
    "FireExtinguisher",
    "ReplayScrub",
    "InteractCockpitControl",
  ],
  "cockpit control contract",
);

requireTokens(
  aircraftCpp,
  [
    "BuildCockpitControls",
    "BuildInstrumentPanel",
    "AddLabel",
    "UTextRenderComponent",
    "SetCockpitNightLighting",
    "CockpitFloodLight",
    "InstrumentBacklight",
    "EFlyingCockpitCameraMode::Pilot",
    "EFlyingCockpitCameraMode::Instruments",
    "EFlyingCockpitCameraMode::ExteriorOrbit",
    "EFlyingCockpitCameraMode::ReplayInspection",
    "LeftWingMesh",
    "RightWingMesh",
    "TailMesh",
    "PropellerMesh",
  ],
  "aircraft and cockpit presentation",
);

requireTokens(
  snapshotHeader,
  [
    "FFlyingAircraftInstrumentSnapshot",
    "FFlyingEngineInstrumentSnapshot",
    "FFlyingElectricalInstrumentSnapshot",
    "FFlyingFuelInstrumentSnapshot",
    "IndicatedAirspeedMetersPerSecond",
    "VacuumSuctionInHg",
    "bGpsValid",
    "bPitotBlocked",
    "bStaticBlocked",
  ],
  "Unreal instrument snapshot",
);

requireTokens(
  simHeader,
  ["GetCurrentInstrumentSnapshot", "CurrentInstrumentSnapshot"],
  "CoreSim instrument publication",
);

requireTokens(
  simCpp,
  [
    "AircraftSystemsModel",
    "AircraftSystemsInput",
    "Systems.step",
    "ToUnreal(Systems.instruments())",
    "MakeSystemsTruth",
    "EstimateEngineRpm",
    "SetSystemSwitch",
    "SetFuelSelector",
    "SetFailure",
  ],
  "sensor/instrument API bridge",
);

requireTokens(
  simHeader,
  ["SetAircraftSystemSwitch", "SetAircraftFuelSelector", "SetAircraftFailure"],
  "cockpit-to-systems interaction API",
);

requireCondition(
  standaloneSystems.includes("../../../../../core_sim/src/aircraft_systems.cpp"),
  "Unreal module must compile the existing CoreSim aircraft systems source",
);

for (const token of [
  "EngineAudio",
  "PropellerAudio",
  "CabinAudio",
  "AirflowAudio",
  "DamageAudio",
  "SetVolumeMultiplier",
  "Instruments.Engine.Rpm",
  "Instruments.Engine.ManifoldPressureKpa",
  "Mixture",
  "IndicatedAirspeedMetersPerSecond",
  "Fuel.bEngineFuelStarved",
  "bPitotBlocked",
  "bStaticBlocked",
]) {
  requireCondition(aircraftCpp.includes(token), `state-driven audio missing token: ${token}`);
}

requireCondition(
  !/InstrumentReadouts\[[^\]]+\]->SetText[\s\S]{0,220}Ecef/.test(aircraftCpp),
  "cockpit displays must not render raw ECEF truth values",
);

console.log("validate_unreal_step18: ok");
