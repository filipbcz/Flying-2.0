"""Production evidence checks and explicit terrain boundary topology (offline)."""
from pathlib import Path

import numpy as np
import rasterio
from rasterio.transform import from_bounds

from acquire_sources import digest_file, read_json, require


def stitched_mesh(rows, columns, heights, normals, affine):
    """Keep every source sample on the perimeter at every LOD.

    Only coarse cells touching the perimeter are replaced by triangle fans.
    Interior cells retain the ordinary two-triangle topology. Boundary vertices
    are shared by index within the tile; neighbours use identical source samples.
    """
    rr, cc = np.meshgrid(rows, columns, indexing="ij")
    coords = list(zip(rr.ravel().tolist(), cc.ravel().tolist()))
    indices = {point: index for index, point in enumerate(coords)}
    triangles = []

    def vertex(row, column):
        point = (row, column)
        if point not in indices:
            indices[point] = len(coords)
            coords.append(point)
        return indices[point]

    edges = {
        "north": [vertex(rows[0], c) for c in range(columns[0], columns[-1] + 1)],
        "south": [vertex(rows[-1], c) for c in range(columns[0], columns[-1] + 1)],
        "west": [vertex(r, columns[0]) for r in range(rows[0], rows[-1] + 1)],
        "east": [vertex(r, columns[-1]) for r in range(rows[0], rows[-1] + 1)],
    }
    # Vectorize regular cells; loop only over the small perimeter.
    base = (np.arange(len(rows) - 1)[:, None] * len(columns) +
            np.arange(len(columns) - 1)).ravel()
    regular = np.stack((np.stack((base, base + len(columns), base + 1), axis=1),
                        np.stack((base + 1, base + len(columns), base + len(columns) + 1), axis=1)),
                       axis=1)
    keep = np.ones((len(rows) - 1, len(columns) - 1), dtype=bool)
    for i in range(len(rows) - 1):
        for j in range(len(columns) - 1):
            if i not in (0, len(rows) - 2) and j not in (0, len(columns) - 2):
                continue
            r0, r1, c0, c1 = rows[i], rows[i + 1], columns[j], columns[j + 1]
            if r1 - r0 == c1 - c0 == 1:
                continue
            keep[i, j] = False
            top = range(c0, c1) if i == 0 else [c0]
            right = range(r0, r1) if j == len(columns) - 2 else [r0]
            bottom = range(c1, c0, -1) if i == len(rows) - 2 else [c1]
            left = range(r1, r0, -1) if j == 0 else [r1]
            ring = ([vertex(r0, c) for c in top] + [vertex(r, c1) for r in right] +
                    [vertex(r1, c) for c in bottom] + [vertex(r, c0) for r in left])
            center = vertex((r0 + r1) / 2, (c0 + c1) / 2)
            # row increases south; reverse ring for outward/upward winding.
            triangles.extend((center, ring[(k + 1) % len(ring)], ring[k]) for k in range(len(ring)))
    topology = regular[keep.ravel()].reshape(-1, 3)
    if triangles:
        topology = np.concatenate((topology, np.asarray(triangles)), axis=0)
    samples = np.asarray(coords, dtype="float64")
    lower = np.floor(samples).astype(int)
    upper = np.ceil(samples).astype(int)
    fraction = samples - lower

    def interpolate(values):
        return (values[..., lower[:, 0], lower[:, 1]] * (1-fraction[:, 0]) * (1-fraction[:, 1]) +
                values[..., upper[:, 0], lower[:, 1]] * fraction[:, 0] * (1-fraction[:, 1]) +
                values[..., lower[:, 0], upper[:, 1]] * (1-fraction[:, 0]) * fraction[:, 1] +
                values[..., upper[:, 0], upper[:, 1]] * fraction[:, 0] * fraction[:, 1])

    xyz = np.stack((affine.c + (samples[:, 1] + .5) * affine.a,
                    affine.f + (samples[:, 0] + .5) * affine.e,
                    interpolate(heights)), axis=1)
    mesh_normals = interpolate(normals).T
    mesh_normals /= np.linalg.norm(mesh_normals, axis=1)[:, None]
    return dict(mesh_vertices_east_north_baltic_m=xyz,
                mesh_normal_enu=mesh_normals.astype("float32"),
                mesh_triangles=topology.astype("uint32"),
                **{f"edge_{key}": np.asarray(value, dtype="uint32") for key, value in edges.items()})


def verify_mesh(payload):
    xyz, triangles = payload["mesh_vertices_east_north_baltic_m"], payload["mesh_triangles"]
    require(np.isfinite(xyz).all() and triangles.ndim == 2 and triangles.shape[1] == 3 and
            len(triangles) > 0 and triangles.max() < len(xyz), "Invalid terrain mesh")
    # Single-use triangle edges must equal the full-resolution outer perimeter.
    sides = np.concatenate((triangles[:, [0, 1]], triangles[:, [1, 2]], triangles[:, [2, 0]]))
    sides.sort(axis=1)
    encoded = sides[:, 0].astype("uint64") * len(xyz) + sides[:, 1]
    unique, counts = np.unique(encoded, return_counts=True)
    require(np.all(counts <= 2), "Nonmanifold terrain mesh")
    boundary = []
    for direction in ("north", "south", "west", "east"):
        edge = payload[f"edge_{direction}"]
        require(len(edge) > 1 and edge.max() < len(xyz), "Invalid boundary indices")
        pairs = np.sort(np.stack((edge[:-1], edge[1:]), axis=1), axis=1)
        boundary.extend((pairs[:, 0].astype("uint64") * len(xyz) + pairs[:, 1]).tolist())
    require(np.array_equal(np.sort(boundary), unique[counts == 1]), "Unstitched boundary or mesh hole")
    a, b, c = xyz[triangles[:, 0]], xyz[triangles[:, 1]], xyz[triangles[:, 2]]
    area = (b[:, 0]-a[:, 0])*(c[:, 1]-a[:, 1]) - (b[:, 1]-a[:, 1])*(c[:, 0]-a[:, 0])
    require(np.all(area > 0), "Degenerate or inverted terrain triangles")


def image_placement(source_root, source, width, height):
    """Require returned export metadata tied to this JPEG; a request bbox is insufficient."""
    path = source_root / "ortofoto/export-placement.json"
    require(path.is_file(), "Missing ortofoto/export-placement.json with returned extent and image checksum")
    record = read_json(path)
    require(record["imageSha256"] == source["checksum"]["value"], "Image placement checksum mismatch")
    require(record["sourceUri"] == source["provenance"]["sourceUri"], "Image placement source mismatch")
    require(record["evidenceKind"] == "returned-export-response", "Placement must come from the returned export response")
    require(record["width"] == width and record["height"] == height, "Image placement dimensions mismatch")
    require(record.get("responsePath") and record.get("responseSha256") and record.get("href"),
            "Missing raw export-response provenance")
    response_path = source_root / record["responsePath"]
    require(response_path.resolve().is_relative_to(source_root.resolve()), "Export response path escapes source root")
    require(digest_file(response_path) == record["responseSha256"], "Export response checksum mismatch")
    response = read_json(response_path)
    require(all(response[key] == record[key] for key in ("extent", "width", "height", "href")),
            "Placement differs from actual returned export metadata")
    extent = record["extent"]
    bounds = [float(extent[key]) for key in ("xmin", "ymin", "xmax", "ymax")]
    require(np.isfinite(bounds).all() and bounds[0] < bounds[2] and bounds[1] < bounds[3],
            "Invalid returned image extent")
    crs = rasterio.crs.CRS.from_epsg(extent["spatialReference"].get("latestWkid") or extent["spatialReference"]["wkid"])
    return from_bounds(*bounds, width, height), crs, path


def survey_comparison(root):
    """Report raw mark residuals; gate only documented ground comparisons."""
    path = root / "data/normalized/validation/height-controls.json"
    require(path.is_file(), "Missing independent height-controls.json")
    record = read_json(path)
    lineage = read_json(root / "data/normalized/source-lineage.json")
    require(record["terrainSha256"] == lineage["sourceGroups"]["dmr"][0]["checksum"]["value"],
            "Survey evidence belongs to a different terrain source")
    require(record["crs"] == "EPSG:5514+8357" and record["independentSurvey"] is True,
            "Controls must be independently surveyed S-JTSK / Baltic 1957 points")
    require(record["provenance"]["sourceUri"] and record["provenance"]["surveyId"] and
            record["uncertaintyProvenance"], "Missing survey or dataset uncertainty provenance")
    uncertainty = float(record["sourceUncertaintyM"])
    require(np.isfinite(uncertainty) and uncertainty >= 0, "Invalid source uncertainty")
    threshold = float(record["datasetAcceptance"]["maximumAbsoluteGroundResidualM"])
    require(np.isfinite(threshold) and threshold >= 0 and record["datasetAcceptance"]["basis"],
            "Invalid documented dataset acceptance threshold")
    require(len(record["points"]) >= 3, "At least three independent controls are required")
    results, locations = [], []
    with rasterio.open(root / "data/normalized/dmr.tif") as dataset:
        values = dataset.read(1)
        for point in record["points"]:
            east, north, reference = (float(point[key]) for key in ("eastM", "northM", "baltic1957M"))
            require(np.isfinite([east, north, reference]).all(), "Nonfinite survey control")
            column, row = (~dataset.transform) * (east, north)
            column, row = column - .5, row - .5
            require(0 <= row <= values.shape[0]-1 and 0 <= column <= values.shape[1]-1,
                    "Survey control outside collision coverage")
            i, j = min(int(row), values.shape[0]-2), min(int(column), values.shape[1]-2)
            v, u = row-i, column-j
            if u + v <= 1:
                predicted = values[i,j]*(1-u-v) + values[i,j+1]*u + values[i+1,j]*v
            else:
                predicted = values[i+1,j+1]*(u+v-1) + values[i,j+1]*(1-v) + values[i+1,j]*(1-u)
            predicted = float(predicted)
            require(np.isfinite(predicted), "Invalid DMR height at survey control")
            raw_residual = predicted - reference
            classification = point["comparability"]
            require(classification["status"] in ("comparable", "not-comparable") and classification["reason"],
                    "Missing control comparability classification or reason")
            comparable = classification["status"] == "comparable"
            relationship = point.get("groundRelationship")
            offset = point.get("markToGroundOffsetM")
            control_uncertainty = point.get("controlUncertaintyM")
            if control_uncertainty is not None:
                require(np.isfinite(control_uncertainty) and control_uncertainty >= 0,
                        "Invalid per-point survey uncertainty")
            ground_reference = ground_residual = None
            if comparable:
                require(relationship and relationship.get("sourceUri") and relationship.get("description") and
                        relationship.get("offsetConvention") == "control-height-minus-ground-height" and
                        offset is not None and np.isfinite(offset),
                        "Comparable control requires a documented ground relationship and mark-to-ground offset")
                ground_reference = reference - offset
                ground_residual = predicted - ground_reference
            else:
                require(relationship is None and offset is None,
                        "Documented ground relationships must be evaluated, not excluded")
            locations.append((east, north))
            results.append(dict(id=point["id"], controlType=point["controlType"],
                markType=point.get("markType"), controlOrder=point.get("controlOrder"),
                markTypeSource=point.get("markTypeSource"), sourceAttributes=point.get("sourceAttributes", {}),
                sourceUri=point.get("sourceUri", record["provenance"]["sourceUri"]),
                surveyUri=point.get("surveyUri"), predictedBaltic1957M=predicted,
                referenceBaltic1957M=reference, rawResidualM=raw_residual,
                absoluteErrorM=abs(raw_residual), comparability=classification,
                includedInGroundAcceptance=comparable,
                exclusionReason=None if comparable else classification["reason"],
                controlUncertaintyM=control_uncertainty, markToGroundOffsetM=offset,
                groundRelationship=relationship, groundReferenceBaltic1957M=ground_reference,
                groundResidualM=ground_residual,
                groundAcceptance=("pass" if abs(ground_residual) <= threshold else "fail")
                    if comparable else "not-applicable"))
    require(len({point["id"] for point in record["points"]}) == len(results), "Duplicate control IDs")
    require(np.linalg.matrix_rank(np.asarray(locations)-locations[0]) == 2, "Controls must span a two-dimensional area")
    grouped = {}
    for item in results:
        key = (item["controlType"], item["markType"], item["controlOrder"])
        grouped.setdefault(key, []).append(item)
    groups = []
    for (control_type, mark_type, order), controls in grouped.items():
        residuals = np.array([item["rawResidualM"] for item in controls])
        groups.append(dict(controlType=control_type, markType=mark_type, controlOrder=order,
            controlIds=[item["id"] for item in controls], count=len(controls),
            comparableCount=sum(item["includedInGroundAcceptance"] for item in controls),
            rawResidualsM=residuals.tolist(), minimumRawResidualM=float(residuals.min()),
            maximumRawResidualM=float(residuals.max()), meanRawResidualM=float(residuals.mean()),
            rawResidualRmseM=float(np.sqrt(np.mean(residuals ** 2))),
            maximumAbsoluteRawResidualM=float(np.max(np.abs(residuals)))))
    comparable = [item for item in results if item["includedInGroundAcceptance"]]
    failed = [item["id"] for item in comparable if item["groundAcceptance"] == "fail"]
    maximum = max((abs(item["groundResidualM"]) for item in comparable), default=None)
    return dict(status="fail" if failed else "pass",
                validationKind="independent-vertical-plausibility",
                reason="Comparable ground controls exceed the declared dataset threshold: " + ", ".join(failed)
                    if failed else "",
                residualConvention="DMR minus source Bpv mark height; raw residuals are not bare-earth errors",
                sourceUncertaintyM=uncertainty, datasetAcceptance=record["datasetAcceptance"],
                maximumAbsoluteGroundResidualM=maximum,
                groundAccuracyStatus=("fail" if failed else "pass") if comparable else "not-assessed-no-comparable-controls",
                comparableControlCount=len(comparable), excludedControlCount=len(results)-len(comparable),
                failedControlIds=failed, groups=groups, controls=results,
                maximumAbsoluteRawResidualM=max(item["absoluteErrorM"] for item in results),
                controlCount=len(results), controlUncertaintyM=record.get("controlUncertaintyM"),
                groundEquivalenceVerified=len(comparable) == len(results),
                limitations=record.get("limitations", []), fieldAvailability=record.get("fieldAvailability", {}),
                spatiallyExcluded=record.get("spatiallyExcluded", []),
                uncertaintyProvenance=record["uncertaintyProvenance"],
                provenance=record["provenance"], evidenceSha256=digest_file(path),
                independentSurveyControlsAvailable=True)
