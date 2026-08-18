#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function fail(message) {
  console.error(`validate_offline_navigation_map: ${message}`);
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

function requirePathExists(relativePath, message) {
  requireCondition(fs.existsSync(path.join(repoRoot, relativePath)), message);
}

function requirePathMissing(relativePath, message) {
  requireCondition(!fs.existsSync(path.join(repoRoot, relativePath)), message);
}

const widgetHeader = read(
  "unreal/Source/FlyingPresentation/Public/FlyingOfflineNavigationMapWidget.h",
);
const widgetCpp = read(
  "unreal/Source/FlyingPresentation/Private/FlyingOfflineNavigationMapWidget.cpp",
);
const style = read("unreal/Config/FlyingOfflineNavigationMapStyle.json");
const settings = read("unreal/Source/FlyingPresentation/Public/FlyingPresentationSettings.h");
const gameConfig = read("unreal/Config/DefaultGame.ini");
const packageSchema = read("data_pipeline/schemas/pilot-region-package.schema.json");
const pipeline = read("data_pipeline/src/pilot_region_packages.cpp");
const navMapBuilder = read("tools/data_pipeline/build_nav_map.py");
const navMapFixture = read("tests/data_pipeline/navigation_map_validation_fixture.json");

requirePathExists(
  "unreal/Source/FlyingPresentation/Private/FlyingOfflineNavigationMapWidget.cpp",
  "offline navigation map renderer must be under the canonical Unreal module Source path",
);
requirePathMissing(
  "unreal/source/FlyingPresentation/Private/FlyingOfflineNavigationMapWidget.cpp",
  "offline navigation map renderer must not exist only under a lowercase source path",
);

requireTokens(
  widgetHeader,
  [
    "UFlyingOfflineNavigationMapWidget",
    "EFlyingNavigationMapLayer::Airports",
    "Runways",
    "Obstacles",
    "Airspaces",
    "Labels",
    "AircraftPosition",
    "FlightPath",
    "ReplayTrack",
    "InitializeOfflineMap",
    "SetLayerVisible",
    "RenderMapToPaintContext",
    "AttributionText",
  ],
  "runtime offline map widget contract",
);

requireTokens(
  widgetCpp,
  [
    "runtimeNetworkRequired",
    "IsLocalTileArchivePath",
    "DoesFormatMatchArchivePath",
    "HasExpectedArchiveMagic",
    "LoadPmtilesTilePayload",
    "LoadMbtilesTilePayload",
    "LoadLocalTilePayload",
    "ReadUint64Le(Archive, 56)",
    "SELECT tile_data FROM tiles",
    "FFileHelper::LoadFileToArray",
    "FJsonSerializer::Deserialize",
    "RenderDecodedTileLayer",
    "ResolvedTileArchivePath",
    "FPaths::FileExists",
    ".pmtiles",
    ".mbtiles",
    "zabaged-base",
    "geonames-labels",
    "airports",
    "runways",
    "obstacles",
    "airspaces",
    "AttributionText",
    "DrawLine",
    "RenderTilePackageLayer",
  ],
  "runtime offline map initialization and renderer",
);

requireCondition(
  /for\s*\([^)]*LocalTilePackages[^)]*\)\s*\{[\s\S]*RenderTilePackageLayer/.test(widgetCpp),
  "runtime renderer must iterate loaded local tile packages",
);

for (const layer of ["airports", "runways", "obstacles", "airspaces", "geonames-labels"]) {
  requireCondition(
    widgetCpp.includes(`TilePackage.LayerId == TEXT("${layer}")`),
    `runtime renderer missing visible local package drawing for layer: ${layer}`,
  );
}

requireTokens(
  pipeline,
  [
    "render_navigation_map_tile_archive",
    "render_navigation_map_tile_payload",
    "append_varint(root_directory",
    "tile_data_offset",
    "tile_payload.size()",
    "write_binary_file",
    "'P', 'M', 'T', 'i', 'l', 'e', 's'",
    "offline-navigation-map.pmtiles",
    "navigation-map.json",
    "flying.navigation-vector-tiles.v1",
    "flying.navigation-vector-tile+json",
    "\\\"runtimeNetworkRequired\\\": false",
    "\\\"externalMapApis\\\": []",
    "\\\"remoteTileServerUrls\\\": []",
    "zabaged-base",
    "geonames-labels",
    "airports",
    "runways",
    "obstacles",
    "airspaces",
  ],
  "local vector tile generation integration",
);

requireTokens(
  navMapBuilder,
  [
    "--region-manifest",
    "validate_region_manifest",
    "regionManifest",
    "load_tile_payload",
    "load_mbtiles_payload",
    "runtimeNetworkRequired",
    "externalMapApis",
    "remoteTileServerUrls",
    "tile payload missing layer",
    "manifest must not reference remote tile servers",
    "resolve_package_artifact",
    "must remain inside installed map package",
    "artifact path must not include a drive or scheme",
    "run_validation_fixture",
  ],
  "regional navigation map validator",
);

const parsedFixture = JSON.parse(navMapFixture);
requireCondition(
  parsedFixture.schemaVersion === "flying.navigation-map-validation-fixture.v1" &&
    parsedFixture.explicitRegionManifest === "Config/Regions/ceska-trebova-pilot-region.json" &&
    ["airports", "runways", "obstacles", "airspaces"].every((layer) =>
      parsedFixture.failureCases?.missingRequiredLayers?.some((entry) => entry.layerId === layer),
    ) &&
    /^https:\/\//.test(parsedFixture.failureCases?.runtimeInternetDependency?.remoteTileServerUrl ?? "") &&
    parsedFixture.failureCases?.localArtifactEscapes?.some((entry) => entry.field === "tileArchive") &&
    parsedFixture.failureCases?.localArtifactEscapes?.some((entry) => entry.field === "style"),
  "navigation map validation fixture must cover explicit region, all mandatory aeronautical layers, remote dependency failures and package path escapes",
);

requireCondition(
  !/write_text_file\s*\(\s*tile_path\s*,/.test(pipeline),
  "PMTiles archive must not be written as JSON text",
);

requireCondition(
  !/append_uint64_le\s*\(\s*archive\s*,\s*0U\s*\)\s*;\s*\/\/ addressed tile count/.test(
    pipeline,
  ) &&
    !/append_uint64_le\s*\(\s*archive\s*,\s*0U\s*\)\s*;\s*\/\/ tile entry count/.test(
      pipeline,
    ) &&
    !/append_uint64_le\s*\(\s*archive\s*,\s*0U\s*\)\s*;\s*\/\/ tile content count/.test(
      pipeline,
    ) &&
    !/append_uint64_le\s*\(\s*archive\s*,\s*kNoSection\s*\)\s*;\s*\/\/ tile data offset/.test(
      pipeline,
    ) &&
    !/append_uint64_le\s*\(\s*archive\s*,\s*kNoSection\s*\)\s*;\s*\/\/ tile data length/.test(
      pipeline,
    ),
  "PMTiles archive must declare non-empty tile addressing and tile data sections",
);

requireCondition(
  /LoadPmtilesTilePayload[\s\S]*RenderDecodedTileLayer[\s\S]*TilePackage\.LayerId == TEXT\("geonames-labels"\)/.test(
    widgetCpp,
  ),
  "runtime renderer must read and decode local tile payloads before placeholder fallback",
);

requireCondition(
  /LoadLocalTilePayload[\s\S]*LoadMbtilesTilePayload[\s\S]*RenderDecodedTileLayer/.test(
    widgetCpp,
  ),
  "runtime renderer must decode MBTiles payloads before placeholder fallback",
);

requireTokens(
  packageSchema,
  [
    "\"navigationMap\"",
    "\"FlyingOfflineNavigationMapWidget\"",
    "\"pmtiles\"",
    "\"mbtiles\"",
    "\"attributionVisible\"",
  ],
  "navigation map package schema",
);

const parsedStyle = JSON.parse(style);
requireCondition(
  parsedStyle.runtimeNetworkRequired === false &&
    Array.isArray(parsedStyle.externalMapApis) &&
    parsedStyle.externalMapApis.length === 0 &&
    Array.isArray(parsedStyle.remoteTileServerUrls) &&
    parsedStyle.remoteTileServerUrls.length === 0,
  "style must declare offline-only runtime dependencies",
);

for (const layer of [
  "zabaged-base",
  "geonames-labels",
  "airports",
  "runways",
  "obstacles",
  "airspaces",
  "aircraft-position",
  "flight-path",
  "replay-track",
]) {
  requireCondition(parsedStyle.layers?.[layer], `style missing layer: ${layer}`);
}

for (const source of [style, settings, gameConfig]) {
  requireCondition(
    !/https?:\/\/|mapbox|access_token|api[_-]?key/i.test(source),
    "offline navigation map files must not contain external API or remote URL references",
  );
}

requireTokens(
  settings + gameConfig + read("unreal/Source/FlyingPresentation/FlyingPresentation.Build.cs"),
  ["NavigationMapManifestPath", "NavigationMapStylePath", "SQLiteCore"],
  "runtime configuration",
);

console.log("validate_offline_navigation_map: ok");
