#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const manifestPath = path.join(repoRoot, "data_pipeline", "seeds", "detailed-airport-manifest.json");
const masterListPath = path.join(repoRoot, "data_pipeline", "seeds", "pilot-airport-master-list.json");

const REQUIRED_SCENERY_GROUPS = [
  "taxiways",
  "aprons",
  "stands",
  "runwayTaxiSigns",
  "buildings",
  "hangars",
  "windsocks",
  "lightingSystems",
  "markings",
  "significantObstacles",
];

const REQUIRED_CONTROLLED_PUBLIC_AIRPORTS = new Set([
  "LKPR",
  "LKTB",
  "LKMT",
  "LKKV",
  "LKPD",
  "LKCS",
]);

function fail(message) {
  console.error(`validate_detailed_airports: ${message}`);
  process.exit(1);
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function loadJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, "utf8"));
  } catch (error) {
    fail(`failed to read ${path.relative(repoRoot, filePath)}: ${error.message}`);
  }
}

function requireSourceReview(review, label) {
  requireCondition(review && typeof review === "object", `${label} missing sourceReview`);
  requireCondition(
    review.geometryStatus === "derived_with_review_approval" || review.geometryStatus === "approved_source",
    `${label} must be approved-source or derived with review approval`,
  );
  if (review.geometryStatus === "derived_with_review_approval") {
    requireCondition(
      review.reviewStatus === "reviewer_approved",
      `${label} derived geometry must carry reviewer approval`,
    );
  }
  requireCondition(typeof review.sourceVersion === "string" && review.sourceVersion.length > 0,
    `${label} missing source version`);
}

function requireFeatureArray(airport, groupName) {
  const features = airport.scenery?.[groupName];
  requireCondition(Array.isArray(features) && features.length > 0,
    `${airport.airportId} missing ${groupName}`);
  for (const feature of features) {
    requireCondition(typeof feature.id === "string" && feature.id.length > 0,
      `${airport.airportId} ${groupName} feature missing id`);
    requireSourceReview(feature.sourceReview, `${airport.airportId}/${feature.id}`);
  }
  return features;
}

function collectValidationScenarioAirports(masterList) {
  const locations = new Set();
  for (const aerodrome of masterList.aerodromes ?? []) {
    if (aerodrome.startLocationEligibility?.defaultStartLocation === true) {
      locations.add(aerodrome.id);
    }
  }
  return locations;
}

const manifest = loadJson(manifestPath);
const masterList = loadJson(masterListPath);

requireCondition(
  manifest.schemaVersion === "flying.detailed-airport-manifest.v1",
  "detailed airport manifest schema version mismatch",
);
requireCondition(
  manifest.sourcePolicy?.restrictedSourcePolicy === "no_aip_vfr_chart_geometry_without_permission",
  "manifest must keep restricted chart geometry out of seed data",
);
requireCondition(
  manifest.sourcePolicy?.reviewStatus === "reviewer_approved",
  "manifest source policy must carry review approval",
);

const coverageControlled = new Set(manifest.coverage?.controlledPublicAirportIds ?? []);
for (const airportId of REQUIRED_CONTROLLED_PUBLIC_AIRPORTS) {
  requireCondition(coverageControlled.has(airportId),
    `controlled public airport missing from coverage: ${airportId}`);
}

const expectedValidationAirports = collectValidationScenarioAirports(masterList);
const coverageValidation = new Set(manifest.coverage?.validationScenarioAirportIds ?? []);
for (const airportId of expectedValidationAirports) {
  requireCondition(coverageValidation.has(airportId),
    `validation scenario airport missing from coverage: ${airportId}`);
}

const airports = new Map((manifest.airports ?? []).map((airport) => [airport.airportId, airport]));
for (const airportId of [...coverageControlled, ...coverageValidation]) {
  requireCondition(airports.has(airportId), `covered airport missing detailed record: ${airportId}`);
}

for (const [airportId, airport] of airports) {
  requireCondition(Array.isArray(airport.roles) && airport.roles.length > 0,
    `${airportId} missing role assignment`);
  if (coverageControlled.has(airportId)) {
    requireCondition(airport.roles.includes("controlled_public_airport"),
      `${airportId} must be tagged as a controlled public airport`);
  }
  if (coverageValidation.has(airportId)) {
    requireCondition(airport.roles.includes("validation_scenario_airport"),
      `${airportId} must be tagged as a validation scenario airport`);
  }

  requireSourceReview(airport.sourceReview, airportId);

  const featureIds = new Set();
  for (const groupName of REQUIRED_SCENERY_GROUPS) {
    for (const feature of requireFeatureArray(airport, groupName)) {
      requireCondition(!featureIds.has(feature.id), `${airportId} duplicate feature id ${feature.id}`);
      featureIds.add(feature.id);
    }
  }

  const lightingSystems = airport.scenery.lightingSystems;
  requireCondition(
    lightingSystems.some((feature) => /runway|unlit/i.test(feature.type ?? feature.id)),
    `${airportId} must declare runway lighting state`,
  );
  requireCondition(
    airport.scenery.markings.some((feature) => feature.declaredDistanceStatus === "derived_review_approved" ||
      feature.declaredDistanceStatus === "approved_source"),
    `${airportId} markings must declare distance/marking source status`,
  );

  const wheelContactZones = airport.collisionIntegration?.wheelContactZones;
  requireCondition(Array.isArray(wheelContactZones) && wheelContactZones.length > 0,
    `${airportId} missing wheel contact collision zones`);
  for (const zone of wheelContactZones) {
    requireCondition(featureIds.has(zone.surfaceId), `${airportId} collision zone references unknown surface ${zone.surfaceId}`);
    requireCondition(zone.collisionSource === "detailed_airport_collision_mesh",
      `${airportId}/${zone.surfaceId} collision source must be detailed airport mesh`);
    requireCondition(zone.maxVisualCollisionDeltaM <= 0.05,
      `${airportId}/${zone.surfaceId} visual/collision delta exceeds wheel-contact tolerance`);
    requireCondition(zone.maxTerrainHeightDiscontinuityM <= 0.05,
      `${airportId}/${zone.surfaceId} terrain discontinuity exceeds wheel-contact tolerance`);
  }

  const terrainTransitions = airport.collisionIntegration?.terrainTransitions;
  requireCondition(Array.isArray(terrainTransitions) && terrainTransitions.length > 0,
    `${airportId} missing terrain transition checks`);
  for (const transition of terrainTransitions) {
    requireCondition(featureIds.has(transition.surfaceId),
      `${airportId} terrain transition references unknown surface ${transition.surfaceId}`);
    requireCondition(transition.status === "passed", `${airportId}/${transition.surfaceId} terrain transition must pass`);
    requireCondition(transition.maxStepM <= 0.05,
      `${airportId}/${transition.surfaceId} terrain transition step exceeds wheel-contact tolerance`);
    requireCondition(transition.blendWidthM > 0,
      `${airportId}/${transition.surfaceId} terrain transition blend width must be positive`);
  }
}

console.log("validate_detailed_airports: ok");
