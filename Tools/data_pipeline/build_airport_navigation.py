"""Assemble a bounded, private real-source package without promoting derived data."""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import sys
import zipfile
from pathlib import Path, PurePosixPath

import numpy as np
import pyproj
import rasterio
from bs4 import BeautifulSoup

from acquire_sources import atomic_json, digest_file, read_json, require
from build_regional import official_transform
from build_runway_surfaces import build_surface, local_enu, validate_mesh_indices
from import_runways import build_coverage_report, validate_against_schema
from build_navigation_tiles import build_tiles, verify_tiles


REGION = Path(__file__).resolve().parents[2] / "Config/Regions/ceska-trebova-pilot-region.json"
VERSION = "2026-11-19-private-3"
MARKER_PAIRS = [((286.5, 389), (261.5, 402)), ((374, 564), (350, 576)),
                ((474.5, 763), (451.5, 776)), ((569, 949), (544.5, 962)),
                ((666, 1141), (640, 1156))]


def text(path):
    soup = BeautifulSoup(path.read_text(encoding="utf-8"), "html.parser")
    for element in soup(["script", "style"]):
        element.decompose()
    return soup.get_text(" ", strip=True)


def check_sources(root):
    records = []
    for sidecar in sorted(root.rglob("*.provenance.json")):
        record = read_json(sidecar)
        payload = sidecar.with_name(sidecar.name.removesuffix(".provenance.json"))
        require(payload.is_file(), f"Missing source {payload}")
        require(digest_file(payload) == record["sha256"], f"Source checksum mismatch: {payload}")
        records.append(dict(path=payload.relative_to(root).as_posix(), sha256=record["sha256"],
                            sourceUri=record["sourceUrl"], retrievedAt=record["downloadedAt"]))
    require(len(records) >= 30, "Expected acquired CUZK and AIM/operator collection")
    return records


def fit_markers(aero):
    require(digest_file(aero / "ortofoto-airport.png") == "33b3f960f5237912799c7a3615f5a6de3109cfe9b719cefacd2ebd10e9be5b0e", "Curated marker selections require the original checksum-bound image")
    placement = read_json(aero / "ortofoto-export.json")
    extent = placement["extent"]
    resolution = (extent["xmax"] - extent["xmin"]) / placement["width"]
    with rasterio.open(aero / "ortofoto-airport.png") as image:
        require(image.width == placement["width"] and image.height == placement["height"], "Image placement dimensions differ")
        palette = image.colormap(1)
        colors = np.array([palette[index][:3] for index in range(256)])
        rgb = colors[image.read(1)]
    for pair in MARKER_PAIRS:
        for column, row in pair:
            neighborhood = rgb[int(row)-3:int(row)+4, int(column)-3:int(column)+4]
            require(np.any(np.all(neighborhood > [190, 190, 160], axis=2)), "Selected marker no longer exists in imagery")
    pixels = np.array(MARKER_PAIRS).mean(axis=1)
    points = np.column_stack((extent["xmin"] + pixels[:, 0] * resolution,
                              extent["ymax"] - pixels[:, 1] * resolution))
    center = points.mean(axis=0)
    _, _, directions = np.linalg.svd(points - center)
    direction = directions[0]
    if direction[1] > 0:
        direction *= -1
    residuals = np.abs((points - center) @ np.array([-direction[1], direction[0]]))
    require(float(residuals.max()) < 2, "Marker centerline fit exceeds 2 m")
    spans = (points - center) @ direction
    center += direction * (spans.min() + spans.max()) / 2
    span = float(np.ptp(spans))
    require(span >= 350, "Imagery marker corridor shorter than published runway")
    pairs = np.array(MARKER_PAIRS)
    widths = np.linalg.norm(pairs[:, 0] - pairs[:, 1], axis=1) * resolution
    return center, direction, dict(method="Least-squares axis through five paired bright ground markers in returned-extent CUZK Ortofoto; 350 m subset centered inside observed marker span",
        selectedMarkerPairsPixels=MARKER_PAIRS, markerSpanM=span, markerSeparationM=widths.tolist(),
        centerlineFitResidualsM=residuals.tolist(), imagePixelSizeM=resolution,
        imageSha256=digest_file(aero / "ortofoto-airport.png"), placementSha256=digest_file(aero / "ortofoto-export.json"),
        publishedDimensionsM=dict(length=350, width=20), simulationContactDimensionsM=dict(length=350, width=20),
        interpretation="Full published 350 x 20 m dimensions placed on the imagery-derived axis. Endpoints are derived, not surveyed physical thresholds; absolute accuracy remains unmeasured.",
        horizontalUncertaintyM=5, headingUncertaintyDeg=1, thresholdStationUncertaintyM=25,
        uncertaintyKind="Conservative engineering allowances, NOT measured absolute accuracy or survey precision",
        productionValidated=False, precisionAcceptance=dict(status="blocked-unmeasured",
            requiredMaximumErrors=dict(thresholdM=1, headingDeg=0.1, dimensionM=0.5),
            measuredErrors=None, independentControlSource=None,
            reason="No independent surveyed threshold/edge control in acquired sources. Fit residuals, pixel resolution and engineering allowances do not measure absolute accuracy."))


def sample_height(dataset, east, north):
    row, column = dataset.index(float(east), float(north))
    require(0 <= row < dataset.height and 0 <= column < dataset.width, "Height sample outside acquired patch")
    value = float(dataset.read(1, window=((row, row + 1), (column, column + 1)))[0, 0])
    require(math.isfinite(value) and value != dataset.nodata, "Invalid DMR sample")
    return value


def build_airport(aero, grids):
    operator = text(aero / "lkctre.html")
    for required in ("Plocha SLZ neověřená", "350x20", "15/33", "412"):
        require(required in operator, f"Operator evidence changed: {required}")
    require("12 NOV 26" in text(aero / "lkuo.html"), "AIM effective date differs from reviewed snapshot")
    center, direction, geometry = fit_markers(aero)
    transform = official_transform(grids)
    provenance = [dict(sourceId=name, sourceDocument=f"sources/airport-navigation/{name}",
                       checksum=dict(algorithm="sha256", value=digest_file(aero / name)))
                  for name in ("lkctre.html", "ortofoto-airport.png", "ortofoto-export.json", "dmr-airport.tif")]
    with rasterio.open(aero / "dmr-airport.tif") as dem:
        def position(east, north):
            height = sample_height(dem, east, north)
            longitude, latitude, ellipsoid = transform.transform(float(east), float(north), height)
            return dict(latDeg=latitude, lonDeg=longitude, elevationM=ellipsoid)
        origin = position(*center)
        ends = [dict(id=designator, threshold=dict(geometrySource="derived_ortofoto_marker_corridor",
                    physicalThreshold=dict(positionWgs84=position(*(center + direction * station)))) )
                for designator, station in (("15", -175), ("33", 175))]
        runway = dict(id="RWY-15-33", designator="15/33", dimensionsM=dict(length=350, width=20),
                      surface=dict(surfaceType="grass", material="turf"), ends=ends, provenance=provenance,
                      validation=dict(status="derived", manualVerification=dict(status="unverified")))
        airport = dict(id="LKCTRE", name="Česká Třebová", classification="slz_field", operationalStatus="active",
                       verificationStatus="unverified", sourcePublishedStatus="Plocha SLZ neověřená",
                       referencePointWgs84=origin, sourcePublishedArpWgs84=dict(latDeg=49+54/60+31/3600,
                           lonDeg=16+27/60+34/3600, elevationBaltic1957M=412),
                       frequenciesMHz=[125.830], runways=[runway], provenance=provenance)
        surface = build_surface(airport, runway)
        surface["slopes"]["source"] = "derived_DMR_samples_not_verified_threshold_geometry"
        surface["transitionBands"] = []
        surface["lod"] = [dict(level=0, maxErrorM=0.0)]
        positions = []
        heights = []
        for station in np.linspace(-175, 175, 9):
            for lateral in (-10, 0, 10):
                east, north = center + direction * station + np.array([-direction[1], direction[0]]) * lateral
                point = position(east, north)
                positions.append(point)
                heights.append(sample_height(dem, east, north))
        for key in ("visualMesh", "collisionMesh"):
            for vertex, point in zip(surface[key]["vertices"], positions):
                vertex["positionWgs84"] = point
                vertex["localEnuM"] = local_enu(origin, point)
                vertex["heightM"] = point["elevationM"]
            validate_mesh_indices(surface[key]["vertices"], surface[key]["indices"])
        longitudinal = (heights[-2] - heights[1]) / 350 * 100
        transverse = float(np.mean(np.array(heights).reshape(9, 3)[:, 2] - np.array(heights).reshape(9, 3)[:, 0])) / 20 * 100
        surface.update(longitudinalPercent=longitudinal, transversePercent=transverse)
        surface["slopes"].update(longitudinalPercent=longitudinal, transversePercent=transverse)
        center_index = 13
        safe_start = dict(id="LKCTRE-simulation-center", airportId="LKCTRE", runwayId=runway["id"],
            positionWgs84=positions[center_index], localEnuM=local_enu(origin, positions[center_index]),
            placementReference="wheel-contact point; aircraft gear geometry supplies body clearance",
            defaultEligible=False, requiresUnverifiedFieldOptIn=True, aviationSafetyClaim=False,
            lateralMarginM=10, longitudinalMarginM=175,
            safetyScope="Static contact-mesh placement only; no real-world obstacle clearance or aircraft takeoff performance approval")
        first, last = ends[0]["threshold"]["physicalThreshold"]["positionWgs84"], ends[1]["threshold"]["physicalThreshold"]["positionWgs84"]
        heading, _, _ = pyproj.Geod(ellps="WGS84").inv(first["lonDeg"], first["latDeg"], last["lonDeg"], last["latDeg"])
        safe_start["trueHeadingDeg"] = heading % 360
        geometry.update(trueHeadingDeg=heading % 360, dmrPixelSizeM=list(dem.res), sampledBaltic1957M=heights,
                        verticalAccuracyM=None, verticalAccuracyNote="DMR export sampling resolution is not survey accuracy",
                        transformationPipeline=transform.definition, transformationDeclaredAccuracyM=transform.accuracy,
                        horizontalFrame="ETRS89 from official grids, WGS84-compatible simulator coordinates; no epoch equivalence claim",
                        heightDatum="ellipsoidal for ENU vertices; source samples Baltic 1957")
    return airport, surface, safe_start, geometry


def feature(identifier, geometry, **properties):
    return dict(type="Feature", id=identifier, properties=properties, geometry=geometry)


def collection(features):
    return dict(type="FeatureCollection", features=features)


def local_navigation(payload, aero, normalized, airport):
    navigation = payload / "navigation"
    navigation.mkdir(exist_ok=True)
    for name in ("zabaged-roads", "zabaged-forest", "zabaged-water", "geonames-settlements"):
        source = normalized / "vectors" / f"{name}.geojson"
        if not source.exists():
            source = normalized / "navigation" / f"{name}.geojson"
        destination = navigation / f"{name}.geojson"
        if source.resolve() != destination.resolve():
            shutil.copyfile(source, destination)
    for name in ("power-masts", "power-lines"):
        source = aero / ("power-masts.geojson" if name == "power-masts" else "power-lines.geojson")
        content = read_json(source)
        require(content.get("type") == "FeatureCollection" and content["features"], "Missing real obstacle layer")
        shutil.copyfile(source, navigation / f"{name}.geojson")
    point = airport["sourcePublishedArpWgs84"]
    atomic_json(navigation / "airports.geojson", collection([feature("LKCTRE", dict(type="Point", coordinates=[point["lonDeg"], point["latDeg"]]),
        name=airport["name"], classification="slz_field", verificationStatus="unverified", sourcePublishedStatus=airport["sourcePublishedStatus"], source="lkctre.html")]))
    runway_points = [end["threshold"]["physicalThreshold"]["positionWgs84"] for end in airport["runways"][0]["ends"]]
    atomic_json(navigation / "runways.geojson", collection([feature("LKCTRE/15-33", dict(type="LineString", coordinates=[[point["lonDeg"], point["latDeg"]] for point in runway_points]),
        geometryStatus="derived-unverified", contactWidthM=20, publishedWidthM=20, source="geometry-report.json")]))
    longitude, latitude = 16+25/60+35/3600, 49+58/60+43/3600
    geod = pyproj.Geod(ellps="WGS84")
    ring = [list(geod.fwd(longitude, latitude, angle, 3*1852)[:2]) for angle in range(0, 361, 3)]
    atomic_json(navigation / "airspaces.geojson", collection([feature("LKUO-nominal-ATZ", dict(type="Polygon", coordinates=[ring]),
        source=["lkuo.html", "enr-2.html#2.4.1.2"], derivation="3 NM nominal geodesic circle; NOT clipped to controlled or temporary airspace",
        lower="SFC", upper="4000 ft AMSL", status="nominal-advisory-only", activation="Unknown; offline snapshot has no AUP/NOTAM updates")]))
    chapters = navigation / "reference"
    chapters.mkdir(exist_ok=True)
    for filename in ("aim-index.html", "lkuo.html", "lkuo-map.pdf", "enr-1.html", "enr-2.html", "enr-3.html", "lkctre.html"):
        shutil.copyfile(aero / filename, chapters / filename)
    atomic_json(navigation / "manifest.json", dict(regionId=read_json(REGION)["regionId"], runtimeNetworkRequired=False,
        **build_tiles(navigation, read_json(REGION)["bounds"]),
        coordinateFrames=dict(baseVectors="EPSG:5514", airportRunwayAirspaceObstacles="EPSG:4326"),
        layers=[dict(path=path.name, sha256=digest_file(path)) for path in sorted(navigation.glob("*.geojson"))],
        externalTiles=[], sourceReferencesAreNotRuntimeDependencies=True,
        limitations=["Private simulator map, not for real navigation", "Nominal ATZ is not a complete controlled/restricted/temporary airspace database",
                     "No live AUP, NOTAM, obstacle completeness or operational clearance claim", "Local reference HTML is an evidence copy; its remote links are not loaded by the map"]))


def safe_member(name):
    path = PurePosixPath(name)
    require(name and not path.is_absolute() and ".." not in path.parts and "\\" not in name
            and ":" not in name and path.as_posix() == name, f"Unsafe package path: {name}")
    return path


def verify(root):
    manifest = read_json(root / "install-manifest.json")
    expected = set(manifest["files"])
    actual = {path.relative_to(root).as_posix() for path in root.rglob("*") if path.is_file()} - {"install-manifest.json"}
    require(expected == actual, "Installed file inventory differs")
    for name, digest in manifest["files"].items():
        safe_member(name)
        require(digest_file(root / name) == digest, f"Checksum mismatch: {name}")
    database = read_json(root / "airports.json")
    require(len(database["aerodromes"]) == 1 and database["aerodromes"][0]["id"] == "LKCTRE", "Unexpected regional airport coverage")
    require(database["aerodromes"][0]["verificationStatus"] == "unverified", "Operator field was promoted")
    surfaces = read_json(root / "runway-surfaces.json")
    validate_against_schema(surfaces, REGION.parents[1].parent / "data_pipeline/schemas/pilot-runway-surfaces-package.schema.json", "private regional surfaces")
    surface = surfaces["surfaces"][0]
    require(surface["dimensionsM"]["width"] == database["aerodromes"][0]["runways"][0]["dimensionsM"]["width"] == 20,
            "Runway contact width differs from published width")
    geometry = read_json(root / "geometry-report.json")
    require(geometry["publishedDimensionsM"] == geometry["simulationContactDimensionsM"], "Inset contact geometry substituted")
    require(not geometry["productionValidated"], "Unmeasured geometry promoted to production")
    require(surface["slopes"]["source"] == "derived_DMR_samples_not_verified_threshold_geometry", "Mislabelled slope provenance")
    require(not surface["transitionBands"], "Unmeasured transition bands must not be emitted")
    require(surface["visualMesh"] == surface["collisionMesh"], "Visual/contact mesh divergence")
    require(len(surface["visualMesh"]["vertices"]) == 27, "Incomplete contact mesh")
    validate_mesh_indices(surface["visualMesh"]["vertices"], surface["visualMesh"]["indices"])
    start = read_json(root / "safe-starts.json")["starts"][0]
    require(not start["defaultEligible"] and start["requiresUnverifiedFieldOptIn"], "Unsafe automatic eligibility")
    require(start["positionWgs84"] == surface["collisionMesh"]["vertices"][13]["positionWgs84"], "Start is not on contact mesh")
    bounds = read_json(root / "region.json")["bounds"]
    for vertex in surface["collisionMesh"]["vertices"]:
        point = vertex["positionWgs84"]
        require(all(math.isfinite(value) for value in point.values()), "Nonfinite contact geometry")
        require(bounds["minLatDeg"] <= point["latDeg"] <= bounds["maxLatDeg"] and
                bounds["minLonDeg"] <= point["lonDeg"] <= bounds["maxLonDeg"], "Contact geometry outside configured region")
    for offset in range(0, len(surface["collisionMesh"]["indices"]), 3):
        triangle = [surface["collisionMesh"]["vertices"][index]["localEnuM"] for index in surface["collisionMesh"]["indices"][offset:offset+3]]
        first, second, third = [np.array([point["east"], point["north"], point["up"]]) for point in triangle]
        require(np.linalg.norm(np.cross(second-first, third-first)) > .01, "Degenerate contact triangle")
    navigation = read_json(root / "navigation/manifest.json")
    require(not navigation["runtimeNetworkRequired"] and not navigation["externalTiles"], "Runtime external map dependency")
    for layer in navigation["layers"]:
        require(digest_file(root / "navigation" / layer["path"]) == layer["sha256"], "Navigation layer checksum mismatch")
    tile_count = verify_tiles((root / "navigation").resolve(), navigation)
    catalogue = read_json(root / "project-catalogue.json")
    require(catalogue["includedRecords"][0]["verificationStatus"] == "unverified", "Catalogue promoted operator SLZ")
    require({record["id"] for record in catalogue["excludedRecords"]} == {"LKUO", "LKVM", "LKMK", "LKCHOC"}, "Incomplete reviewed exclusions")
    for record in surface["provenance"]:
        require(digest_file(root / safe_member(record["sourceDocument"])) == record["checksum"]["value"], "Unresolved surface provenance")
    return dict(status="pass", filesChecked=len(expected), airportCount=1, runwayCount=1, safeStartCount=1,
                offlineLayerCount=len(navigation["layers"]), offlineTileCount=tile_count,
                geometryPrecisionAccepted=False, scope="Private bounded package integrity, not RC or operational approval")


def production_gate(root):
    verify(root)
    precision = read_json(root / "geometry-report.json")["precisionAcceptance"]
    require(precision["status"] == "pass" and precision["independentControlSource"],
            "Production geometry blocked: independent measured threshold, heading and dimension accuracy is unavailable")
    errors = precision["measuredErrors"]
    for name, limit in dict(thresholdM=1, headingDeg=0.1, dimensionM=0.5).items():
        require(isinstance(errors[name], (int, float)) and math.isfinite(errors[name]) and 0 <= errors[name] <= limit,
                f"Production geometry exceeds {name} limit {limit}")
    return dict(status="pass")


def install(archive, destination):
    require(not destination.exists(), "Install destination must be new")
    with zipfile.ZipFile(archive) as package:
        names = [entry.filename for entry in package.infolist()]
        require(len(names) == len(set(names)), "Duplicate archive entries")
        for entry in package.infolist():
            safe_member(entry.filename)
            require(not entry.is_dir() and (entry.external_attr >> 16) & 0o170000 != 0o120000, "Archive links/directories are not allowed")
        require(sum(entry.file_size for entry in package.infolist()) < 2_000_000_000, "Package exceeds bounded extraction size")
        package.extractall(destination)
    return verify(destination)


def build(managed, output):
    require(not (output / "data/packages").exists(), "Use a fresh output root")
    sources = output / "data/sources"
    shutil.copytree(managed / "sources", sources, dirs_exist_ok=True)
    aero = sources / "airport-navigation"
    shutil.copytree(managed / "aero-final", aero, dirs_exist_ok=True)
    source_report = check_sources(sources)
    packages = output / "data/packages"
    payload = packages / "regional-airport-navigation"
    reports = output / "artifacts/airport-navigation"
    payload.mkdir(parents=True)
    reports.mkdir(parents=True)
    normalized = managed / "normalized"
    airport, surface, start, geometry = build_airport(aero, normalized / "grids")
    master = dict(schemaVersion="flying.regional-airport-master-list.v1", regionId=read_json(REGION)["regionId"],
        masterList=[dict(aerodromeId="LKCTRE", name=airport["name"], classification="slz_field", operationalStatus="active",
            requiredRunways=[dict(id="RWY-15-33", designator="15/33")], notes="Operator-published unverified SLZ; full published dimensions on derived axis; absolute accuracy not measured; not production validated")])
    atomic_json(payload / "master-list.json", master)
    database = dict(schemaVersion="flying.aerodrome-database.v1", databaseVersion=VERSION,
                    airac=dict(effectiveDate="2026-11-12", notes="AIM WEF; operator page has no declared AIRAC"), aerodromes=[airport])
    atomic_json(payload / "airports.json", database)
    atomic_json(payload / "runway-surfaces.json", dict(surfaces=[surface]))
    atomic_json(payload / "safe-starts.json", dict(starts=[start]))
    atomic_json(payload / "region.json", read_json(REGION))
    atomic_json(payload / "geometry-report.json", geometry)
    shutil.copytree(aero, payload / "sources/airport-navigation")
    local_navigation(payload, aero, normalized, airport)
    shutil.copytree(managed / "generated", payload / "generated")
    shutil.copytree(normalized / "grids", payload / "grids")
    shutil.copyfile(normalized / "source-lineage.json", payload / "terrain-source-lineage.json")
    coverage = build_coverage_report(payload / "master-list.json", master, database, None)
    coverage["approvedMasterList"]["sourcePath"] = "master-list.json"
    coverage.update(scope="technically-discovered-bounded-project-list", productionValidatedRunways=0,
        missingProductionValidatedProvenance=0, nationalCompletenessClaim=False, verifiedRecords=[], unverifiedRecords=["LKCTRE"])
    excluded = []
    for identifier, latitude, longitude in (("LKUO",49+58/60+43/3600,16+25/60+35/3600),
        ("LKVM",49+55/60+37/3600,16+11/60+9/3600), ("LKMK",49+47/60+54/3600,16+41/60+16/3600),
        ("LKCHOC",49+58/60+39/3600,16+11/60+7/3600)):
        source = BeautifulSoup((aero / f"{identifier.lower()}.html").read_text(encoding="utf-8"), "html.parser")
        arp = source.select_one("#aerodrome-poloha")
        require(arp is not None, f"No source ARP for {identifier}")
        coordinates = re.search(r"(\d+)\D+(\d+)'\s*(\d+)\"\s*N,\s*(\d+)\D+(\d+)'\s*(\d+)\"\s*E", arp.get_text(" ", strip=True).replace("\u00a0", " "))
        require(coordinates is not None, f"Cannot parse source ARP for {identifier}")
        degrees = list(map(int, coordinates.groups()))
        latitude = degrees[0] + degrees[1]/60 + degrees[2]/3600
        longitude = degrees[3] + degrees[4]/60 + degrees[5]/3600
        bounds = read_json(REGION)["bounds"]
        nearest_lon = min(max(longitude, bounds["minLonDeg"]), bounds["maxLonDeg"])
        nearest_lat = min(max(latitude, bounds["minLatDeg"]), bounds["maxLatDeg"])
        _, _, distance = pyproj.Geod(ellps="WGS84").inv(longitude, latitude, nearest_lon, nearest_lat)
        require(distance > 250, f"Reviewed airport unexpectedly in bounded margin: {identifier}")
        excluded.append(dict(id=identifier, state="excluded", verificationStatus="official-AIM-record", operationalStatus="active",
            source=f"{identifier.lower()}.html", referencePointWgs84=dict(latDeg=latitude,lonDeg=longitude),
            distanceToPilotBoundsM=distance, sourceArpText=arp.get_text(" ", strip=True),
            reason="Outside configured pilot polygon and 250 m margin; no runway/contact/start emitted; no two-airport flight claimed"))
    soup = BeautifulSoup((aero / "aim-index.html").read_text(encoding="utf-8"), "html.parser")
    indexed = {}
    for link in soup.select('a[href$="_text_cz.html"]'):
        identifier = Path(link["href"]).name.removesuffix("_text_cz.html").upper()
        indexed[identifier] = dict(id=identifier, name=link.get_text(" ", strip=True), href=link["href"],
            state="not-spatially-reviewed", reason="National index discovery only, not a spatial exclusion or evidence of absence. Only the nearby reviewed records are spatially classified.")
    for record in excluded:
        if record["id"] in indexed:
            indexed[record["id"]].update(state="excluded-outside-margin", reason=record["reason"])
    atomic_json(payload / "project-catalogue.json", dict(regionId=read_json(REGION)["regionId"], allowedMarginM=250,
        includedRecords=[dict(id="LKCTRE", state="included", verificationStatus="unverified", operationalStatus="active",
            sourcePublishedStatus=airport["sourcePublishedStatus"], source="sources/airport-navigation/lkctre.html")],
        excludedRecords=excluded, nationalIndexDisposition=list(indexed.values()), completeness="Technically discovered bounded project list, not national spatial completeness"))
    coverage.update(excludedRecords=excluded, indexDisposition=list(indexed.values()),
        limitations=["Empty bounded ZABAGED airport/runway responses are not evidence of absence of SLZ", "No national completeness or regulator verification assertion"],
        discoveryEvidence=["aim-index.html","lkctre.html","airport-areas.geojson","runway-boundaries.geojson"],
        allowedMarginM=250, regionBoundsUnchanged=True)
    atomic_json(reports / "coverage-review.json", coverage)
    atomic_json(reports / "geometry-report.json", geometry)
    atomic_json(reports / "provenance-report.json", dict(status="pass", privateNonDistributed=True,
        redistributionApprovalClaim=False, permissionIsCompletionGate=False, sources=source_report))
    return finalize(output)


def finalize(output):
    packages = output / "data/packages"
    payload = packages / "regional-airport-navigation"
    reports = output / "artifacts/airport-navigation"
    surfaces = read_json(payload / "runway-surfaces.json")["surfaces"]
    package = dict(schemaVersion="flying.pilot-runway-surfaces-package.v1", packageName="Private Ceska Trebova full-width derived runway",
        packageVersion=VERSION, packageId="flying-ceska-trebova-private-runway", airportDatabaseVersion=VERSION,
        coordinateFrame="airport-local-ENU-per-aerodrome", heightDatum="ellipsoidal", geometryStatus="derived-unverified-private-full-width",
        runtimeDependencies=dict(runtimeNetworkRequired=False, externalMapApis=[], remoteTileServerUrls=[]),
        collision=dict(authority="runway_override", runwayOverridePriority=1000, genericTerrainPriority=100, wheelContactToleranceM=.05),
        surfaces=surfaces, validation=dict(coverageReport="coverage-review.json", coordinateChecksIncluded=True,
            headingChecksIncluded=True, dimensionChecksIncluded=True, ortofotoAlignmentChecksIncluded=True,
            terrainTransitionChecksIncluded=False, provenanceChecksIncluded=True))
    for surface in surfaces:
        atomic_json(payload / surface["file"], surface)
    atomic_json(payload / "runway-surfaces.json", package)
    coverage = read_json(reports / "coverage-review.json")
    coverage["approvedMasterList"]["checksumSha256"] = digest_file(payload / "master-list.json")
    atomic_json(reports / "coverage-review.json", coverage)
    shutil.copyfile(reports / "coverage-review.json", payload / "coverage-review.json")
    atomic_json(payload / "install-manifest.json", dict(version=VERSION, regionId=read_json(REGION)["regionId"], runtimeNetworkRequired=False,
        files={path.relative_to(payload).as_posix(): digest_file(path) for path in sorted(payload.rglob("*"))
               if path.is_file() and path.name != "install-manifest.json"}))
    proof = verify(payload)
    archive = packages / f"ceska-trebova-airport-navigation-{VERSION}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED) as package:
        for path in sorted(payload.rglob("*")):
            if path.is_file():
                package.write(path, path.relative_to(payload).as_posix())
    destination = output / "relocation-proof/unrelated install path"
    require(not destination.exists(), "Relocation proof requires a fresh destination")
    relocated = install(archive, destination)
    atomic_json(reports / "checksum-report.json", dict(status="pass", archive=archive.name, sha256=digest_file(archive),
        sizeBytes=archive.stat().st_size, manifestSha256=digest_file(payload / "install-manifest.json")))
    atomic_json(reports / "relocation-report.json", dict(status="pass", archiveSha256=digest_file(archive),
        installedUnderUnrelatedPath=True, validation=relocated, sourceCheckoutRequired=False, networkUsed=False))
    atomic_json(reports / "production-gate-report.json", dict(status="blocked-production-geometry-precision", releaseCandidateAccepted=False,
        task="Complete Production Airport And Navigation Package", packageIntegrity=proof, regionChanged=False,
        limitations=["Unverified operator SLZ retained as unverified", "Full published 350x20 m dimensions on derived axis; mandatory absolute geometry precision remains unmeasured and blocked",
            "Nominal ATZ and reference chapters are not a complete operational airspace database", "No Unreal/runtime flight, two-airport flight or release gate acceptance claimed"],
        artifacts=dict(archive=archive.name, masterList="master-list.json", surfaces="runway-surfaces.json", starts="safe-starts.json")))
    from private_airport_acceptance import private_package_gate
    return private_package_gate(output)


def refresh(output):
    payload = output / "data/packages/regional-airport-navigation"
    reports = output / "artifacts/airport-navigation"
    verify_sources = payload / "sources/airport-navigation"
    airport, surface, start, geometry = build_airport(verify_sources, payload / "grids")
    database = read_json(payload / "airports.json")
    database.update(databaseVersion=VERSION, aerodromes=[airport])
    atomic_json(payload / "airports.json", database)
    atomic_json(payload / "runway-surfaces.json", dict(surfaces=[surface]))
    atomic_json(payload / "safe-starts.json", dict(starts=[start]))
    atomic_json(payload / "geometry-report.json", geometry)
    atomic_json(reports / "geometry-report.json", geometry)
    master = read_json(payload / "master-list.json")
    master["masterList"][0]["notes"] = "Operator-published unverified SLZ; full 350x20 m published dimensions on derived axis; mandatory absolute precision unmeasured"
    atomic_json(payload / "master-list.json", master)
    local_navigation(payload, verify_sources, payload, airport)
    atomic_json(reports / "navigation-report.json", read_json(payload / "navigation/manifest.json"))
    return finalize(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("build", "finalize", "refresh", "verify", "install", "production-gate", "private-package-gate"))
    parser.add_argument("--managed-input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--destination", type=Path)
    args = parser.parse_args()
    if args.command == "build":
        require(args.managed_input and args.output, "Build requires --managed-input and --output")
        report = build(args.managed_input, args.output)
    elif args.command == "private-package-gate":
        require(args.output, "Private gate requires --output with materialized data and reports")
        from private_airport_acceptance import private_package_gate
        report = private_package_gate(args.output)
    elif args.command in ("finalize", "refresh"):
        require(args.output, "Finalize requires --output")
        report = finalize(args.output) if args.command == "finalize" else refresh(args.output)
    elif args.command in ("verify", "production-gate"):
        require(args.root, "Verify requires --root")
        report = verify(args.root) if args.command == "verify" else production_gate(args.root)
    else:
        require(args.archive and args.destination, "Install requires --archive and --destination")
        report = install(args.archive, args.destination)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError, zipfile.BadZipFile) as error:
        print(f"Airport/navigation package failed: {error}", file=sys.stderr)
        raise SystemExit(1)
