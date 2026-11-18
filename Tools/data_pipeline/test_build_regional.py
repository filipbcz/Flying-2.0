"""Regressions for the acquired CUZK TIFF metadata repair."""
import shutil
import tempfile
import uuid
from contextlib import contextmanager
import unittest
from pathlib import Path

import numpy as np
import rasterio
from rasterio.transform import from_origin

from build_regional import Checkpoint, normalize_height, generate_tile, normals_for, seam_report
from regional_acceptance import image_placement, survey_comparison, verify_mesh
from acquire_sources import atomic_json, digest_file
from acquire_regional_evidence import manifest_digest


EXPORT_WKT = (
    'LOCAL_CS["S-JTSK / Krovak East North",'
    'UNIT["metre",1,AUTHORITY["EPSG","9001"]],'
    'AXIS["Easting",EAST],AXIS["Northing",NORTH],'
    'AUTHORITY["EPSG","5514"]]'
)


@contextmanager
def test_directory():
    # Use inherited checkout ACLs; Python 3.13 TemporaryDirectory applies a
    # private Windows ACL that excludes the runner's restricted process token.
    directory = Path(tempfile.gettempdir()) / ("regional-test-" + uuid.uuid4().hex)
    directory.mkdir(mode=0o755)
    try:
        yield directory
    finally:
        shutil.rmtree(directory)


class HeightNormalizationTest(unittest.TestCase):
    def normalize(self, crs):
        with test_directory() as directory:
            source = Path(directory) / "source.tif"
            destination = Path(directory) / "normalized.tif"
            heights = np.array([[350.125, 351.5], [352.75, 353.0]], dtype="float32")
            transform = from_origin(-605366, -1075291, 5.76, 5.76)
            with rasterio.open(source, "w", driver="GTiff", width=2, height=2,
                               count=1, dtype="float32", crs=crs, transform=transform) as dataset:
                dataset.write(heights, 1)
            if crs and crs.startswith("LOCAL_CS"):
                # GeoTIFF's writer drops LOCAL_CS authority tags. VRT preserves
                # the exact acquired WKT for this regression.
                vrt = Path(directory) / "source.vrt"
                vrt.write_text(
                    '<VRTDataset rasterXSize="2" rasterYSize="2">'
                    f'<SRS>{crs}</SRS>'
                    '<GeoTransform>-605366,5.76,0,-1075291,0,-5.76</GeoTransform>'
                    '<VRTRasterBand dataType="Float32" band="1"><SimpleSource>'
                    '<SourceFilename relativeToVRT="1">source.tif</SourceFilename>'
                    '<SourceBand>1</SourceBand>'
                    '</SimpleSource></VRTRasterBand></VRTDataset>', encoding="utf-8")
                source = vrt
            normalize_height(source, destination, "bare-earth")
            with rasterio.open(destination) as dataset:
                np.testing.assert_array_equal(dataset.read(1), heights)
                self.assertEqual(dataset.transform, transform)
                self.assertEqual(dataset.crs, rasterio.crs.CRS.from_string("EPSG:5514+8357"))
                self.assertEqual(dataset.tags()["sourceWkt"], rasterio.crs.CRS.from_user_input(crs).to_wkt())

    def test_known_cuzk_local_wkt_preserves_heights_and_coordinates(self):
        self.normalize(EXPORT_WKT)

    def test_standard_projected_crs_preserves_heights_and_coordinates(self):
        self.normalize("EPSG:5514")

    def test_conflicting_authority_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Source height authority"):
            self.normalize(EXPORT_WKT.replace('"5514"', '"5513"'))

    def test_missing_or_other_projected_crs_is_rejected(self):
        for crs in (None, "EPSG:3857"):
            with self.subTest(crs=crs), self.assertRaisesRegex(ValueError, "Source height authority"):
                self.normalize(crs)


class RegionalAcceptanceTest(unittest.TestCase):
    def test_nonlinear_mixed_lod_edges_and_corrupt_topology(self):
        with test_directory() as root:
            rr, cc = np.mgrid[:35, :35]
            heights = (350 + np.sin(rr*.7)*5 + np.cos(cc*.4)*3).astype("float32")
            affine = from_origin(-605366, -1075291, 5.76, 5.76)
            normals = normals_for(heights, affine)
            for rows in ((0, 16), (16, 34)):
                for columns in ((0, 16), (16, 34)):
                    generate_tile(root, rows, columns, heights, normals, affine)
            report = seam_report(root)
            self.assertEqual(len(report["pairs"]), 100)
            self.assertEqual(report["mixedLodStatus"], "pass")
            self.assertEqual(report["mixedLodPairCount"], 80)
            self.assertEqual(report["sameLodPairCount"], 20)
            with np.load(root / "data/generated/terrain/r0000-c0000/lod4.npz") as saved:
                payload = {key: saved[key] for key in saved.files}
            payload["mesh_triangles"] = payload["mesh_triangles"][1:]
            with self.assertRaisesRegex(ValueError, "boundary|hole"):
                verify_mesh(payload)

    def test_placement_needs_returned_extent_bound_to_image(self):
        with test_directory() as root:
            source = dict(checksum=dict(value="a"*64), provenance=dict(sourceUri="https://example.org/export"))
            with self.assertRaisesRegex(ValueError, "Missing"):
                image_placement(root, source, 2048, 2048)
            record = dict(imageSha256="a"*64, sourceUri="https://example.org/export",
                          evidenceKind="returned-export-response", width=2048, height=2048,
                          extent=dict(xmin=16.39, ymin=49.8, xmax=16.55, ymax=49.96,
                                      spatialReference=dict(wkid=4326)))
            response = {key: record[key] for key in ("width", "height", "extent")}
            response["href"] = "https://example.org/returned.jpg"
            atomic_json(root / "ortofoto/export-response.json", response)
            record.update(responsePath="ortofoto/export-response.json", href=response["href"],
                          responseSha256=digest_file(root / "ortofoto/export-response.json"))
            path = root / "ortofoto/export-placement.json"
            atomic_json(path, record)
            transform, crs, _ = image_placement(root, source, 2048, 2048)
            self.assertEqual(crs.to_epsg(), 4326)
            np.testing.assert_allclose(transform * (0, 0), (16.39, 49.96))
            np.testing.assert_allclose(transform * (2048, 2048), (16.55, 49.8))
            record["imageSha256"] = "b"*64
            atomic_json(path, record)
            with self.assertRaisesRegex(ValueError, "checksum"):
                image_placement(root, source, 2048, 2048)
            record["imageSha256"] = "a"*64
            record["extent"]["ymin"] = 49.7
            atomic_json(path, record)
            with self.assertRaisesRegex(ValueError, "actual returned"):
                image_placement(root, source, 2048, 2048)

    def test_documented_ground_controls_enforce_dataset_threshold(self):
        with test_directory() as root:
            with self.assertRaisesRegex(ValueError, "Missing independent"):
                survey_comparison(root)
            normalized = root / "data/normalized"
            normalized.mkdir(parents=True)
            with rasterio.open(normalized / "dmr.tif", "w", driver="GTiff", width=3, height=3,
                               count=1, dtype="float32", crs="EPSG:5514+8357",
                               transform=from_origin(-600000, -1000000, 1, 1)) as dataset:
                dataset.write(np.full((3, 3), 350, dtype="float32"), 1)
            atomic_json(normalized / "source-lineage.json",
                        dict(sourceGroups=dict(dmr=[dict(checksum=dict(value="a"*64))])))
            record = dict(crs="EPSG:5514+8357", independentSurvey=True, terrainSha256="a"*64,
                          provenance=dict(sourceUri="fixture-only", surveyId="unit-test"),
                          uncertaintyProvenance="test fixture", sourceUncertaintyM=.20,
                          datasetAcceptance=dict(maximumAbsoluteGroundResidualM=.30, basis="Declared fixture policy"),
                          points=[dict(id=str(i), eastM=east, northM=north, baltic1957M=349.71,
                                       controlType="fixture-control", markType="fixture-ground",
                                       controlOrder="fixture-order", controlUncertaintyM=None,
                                       comparability=dict(status="comparable", reason="Documented fixture ground mark"),
                                       markToGroundOffsetM=0.,
                                       groundRelationship=dict(sourceUri="fixture-only", description="Ground mark",
                                           offsetConvention="control-height-minus-ground-height"))
                                  for i, (east, north) in enumerate(
                                      [(-599999.5, -1000000.5), (-599998.5, -1000000.5),
                                       (-599999.5, -1000001.5)])])
            path = normalized / "validation/height-controls.json"
            atomic_json(path, record)
            self.assertEqual(survey_comparison(root)["status"], "pass")
            record["points"][1]["baltic1957M"] = 349.69
            atomic_json(path, record)
            self.assertEqual(survey_comparison(root)["status"], "fail")
            record["sourceUncertaintyM"] = -1
            atomic_json(path, record)
            with self.assertRaisesRegex(ValueError, "uncertainty"):
                survey_comparison(root)
            record.update(sourceUncertaintyM=.20, evidenceKind="cuzk-zvbp-layer-44",
                          groundEquivalenceVerified=False, controlUncertaintyM=None)
            for point in record["points"]:
                point["baltic1957M"] = 350
            atomic_json(path, record)
            # Missing uncertainty is allowed, even for a documented ground control.
            self.assertEqual(survey_comparison(root)["status"], "pass")
            for index, point in enumerate(record["points"]):
                point.update(baltic1957M=500. + index, groundRelationship=None, markToGroundOffsetM=None,
                             comparability=dict(status="not-comparable", reason="No ground relationship"),
                             markType="NB" if index < 2 else "unknown")
            atomic_json(path, record)
            report = survey_comparison(root)
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["groundAccuracyStatus"], "not-assessed-no-comparable-controls")
            self.assertIsNone(report["maximumAbsoluteGroundResidualM"])
            self.assertEqual(report["excludedControlCount"], 3)
            self.assertEqual([item["rawResidualM"] for item in report["controls"]], [-150., -151., -152.])
            self.assertEqual([group["count"] for group in report["groups"]], [2, 1])
            self.assertEqual(report["groups"][0]["rawResidualsM"], [-150., -151.])
            self.assertTrue(all(item["exclusionReason"] for item in report["controls"]))
            # A documented offset converts the mark height to ground; other
            # excluded marks cannot hide a failed comparable control.
            point = record["points"][0]
            point.update(baltic1957M=351., markToGroundOffsetM=1.,
                         groundRelationship=dict(sourceUri="fixture-only", description="Measured 1 m above ground",
                             offsetConvention="control-height-minus-ground-height"),
                         comparability=dict(status="comparable", reason="Measured fixture offset"))
            atomic_json(path, record)
            report = survey_comparison(root)
            self.assertEqual(report["status"], "pass")
            self.assertEqual(report["comparableControlCount"], 1)
            self.assertEqual(report["controls"][0]["rawResidualM"], -1.)
            self.assertEqual(report["controls"][0]["groundResidualM"], 0.)
            point["baltic1957M"] = 352.
            atomic_json(path, record)
            self.assertEqual(survey_comparison(root)["status"], "fail")
            point["groundRelationship"] = None
            atomic_json(path, record)
            with self.assertRaisesRegex(ValueError, "documented ground relationship"):
                survey_comparison(root)
            point["comparability"] = dict(status="not-comparable", reason="Cannot hide known offset")
            atomic_json(path, record)
            with self.assertRaisesRegex(ValueError, "must be evaluated"):
                survey_comparison(root)

    def test_evidence_recovery_retains_geometry_and_rejects_source_change(self):
        with test_directory() as root:
            lineage = root / "data/normalized/source-lineage.json"
            manifest = dict(sourceGroups=dict(dmr=[dict(checksum=dict(value="a"*64))]))
            atomic_json(lineage, manifest)
            payload = root / "data/generated/terrain/fixture.npz"
            payload.parent.mkdir(parents=True)
            payload.write_bytes(b"fixture-not-production")
            identity = dict(schemaVersion="flying.regional-payload.v2", sources=manifest_digest(manifest),
                            processor="same-code", acceptanceProcessor="same-checks", supplementalEvidence={})
            checkpoint = Checkpoint(root, identity)
            checkpoint.stage("source-lineage", [], lambda: [lineage])
            checkpoint.stage("terrain-0-0", [], lambda: [payload])
            checkpoint.stage("validation-reports", [], lambda: [lineage])
            checkpoint.stage("imagery-mips", [], lambda: [payload])
            checkpoint.stage("regional-evidence-manifest.json", [], lambda: [lineage])
            updated_manifest = dict(manifest, evidence=[dict(path="new-evidence")])
            updated = dict(identity, supplementalEvidence={"validation/height-controls.json": "b"*64}, sources="c"*64)
            with self.assertRaisesRegex(ValueError, "inputs/toolchain changed"):
                Checkpoint(root, updated)
            changed_source = dict(updated_manifest, sourceGroups=dict(dmr=[]))
            with self.assertRaisesRegex(ValueError, "acquired payloads"):
                Checkpoint(root, updated, refresh_evidence=True, manifest=changed_source)
            recovered = Checkpoint(root, updated, refresh_evidence=True, manifest=updated_manifest)
            self.assertEqual(list(recovered.stages), ["terrain-0-0", "imagery-mips"])
            recovered.stage("terrain-0-0", [], lambda: self.fail("Must not regenerate retained geometry"))
            self.assertEqual(recovered.reused, ["terrain-0-0"])
            self.assertTrue((root / "artifacts/regional-validation/checkpoint-before-evidence.json").is_file())

if __name__ == "__main__":
    unittest.main()
