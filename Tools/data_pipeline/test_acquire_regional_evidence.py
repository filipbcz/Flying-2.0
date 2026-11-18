"""Offline negative and provenance regressions; all payloads here are fixtures."""

import copy
import shutil
import unittest

import pyproj

from acquire_sources import atomic_json, digest_file, load_config, read_json
from acquire_regional_evidence import EVIDENCE_MANIFEST, control_record, requests, stage, verify, verified_download
from test_build_regional import test_directory


class RegionalEvidenceTest(unittest.TestCase):
    def fixture(self, root):
        config = load_config()
        source_root = root / "sources"
        evidence_root = root / "evidence"
        image_path = source_root / "ortofoto/ortofoto.jpg"
        image_path.parent.mkdir(parents=True)
        image_path.write_bytes(b"not-a-production-image")
        image = dict(checksum=dict(path="ortofoto/ortofoto.jpg", value=digest_file(image_path)),
                     provenance=dict(sourceUri=next(source["url"] for source in config["sources"] if source["group"] == "ortofoto"),
                                     retrievedAtUtc="2026-11-16T00:00:00Z"))
        manifest = dict(sourceGroups=dict(ortofoto=[image], dmr=[dict(checksum=dict(value="a"*64))]))
        atomic_json(source_root / "acquisition-manifest.json", manifest)
        transform = pyproj.Transformer.from_crs(4326, 5514, always_xy=True)
        features = []
        for index, geographic in enumerate(((16.44, 49.88), (16.45, 49.88), (16.44, 49.89))):
            east, north = transform.transform(*geographic)
            features.append(dict(geometry=dict(x=east, y=north), attributes=dict(
                OBJECTID=index, CISLO=f"fixture-{index}", Y=str(-east), X=str(-north),
                VYSKA=str(350.123 + index), PORAD="fixture-order",
                GEODETICKE_UDAJE="https://geoportal.cuzk.gov.cz/fixture?TYP=NB")))
        responses = [dict(width=2048, height=2048,
                          href="https://ags.cuzk.gov.cz/arcgis1/rest/directories/arcgisoutput/ORTOFOTO_MapServer/fixture.jpg",
                          extent=dict(xmin=16.396, ymin=49.83, xmax=16.546, ymax=49.98,
                                      spatialReference=dict(wkid=4326))),
                     dict(geometryType="esriGeometryPoint", spatialReference=dict(wkid=5514), features=features)]
        for request, response in zip(requests(config), responses):
            destination = evidence_root / request["path"]
            atomic_json(destination, response)
            atomic_json(destination.with_name(destination.name + ".provenance.json"), dict(
                sourceUrl=request["url"], finalUrl=request["url"],
                destinationRelativePath="pilot-evidence/" + request["path"],
                sizeBytes=destination.stat().st_size, sha256=digest_file(destination),
                downloadedAt="2026-11-18T00:00:00Z"))
        return config, source_root, evidence_root, responses, manifest

    def test_bound_evidence_preserves_returned_extent_and_all_control_heights(self):
        with test_directory() as root:
            config, source_root, evidence_root, responses, _ = self.fixture(root)
            self.assertEqual(stage(evidence_root, source_root, config)["controlCount"], 3)
            self.assertEqual(verify(source_root, config)["status"], "pass")
            placement = read_json(source_root / "ortofoto/export-placement.json")
            self.assertEqual(placement["extent"], responses[0]["extent"])
            self.assertEqual(placement["extent"]["ymin"], 49.83)
            controls = read_json(source_root / "validation/height-controls.json")
            self.assertEqual([point["baltic1957M"] for point in controls["points"]], [350.123, 351.123, 352.123])
            self.assertIsNone(controls["controlUncertaintyM"])
            self.assertFalse(controls["groundEquivalenceVerified"])
            self.assertEqual(controls["fieldAvailability"]["perPointSurveyUncertainty"], "not-provided")
            for point, feature in zip(controls["points"], responses[1]["features"]):
                self.assertEqual(point["sourceAttributes"], feature["attributes"])
                self.assertEqual(point["controlOrder"], "fixture-order")
                self.assertEqual(point["markType"], "NB")
                self.assertIsNone(point["physicalMarkType"])
                self.assertIsNone(point["controlUncertaintyM"])
                self.assertIsNone(point["markToGroundOffsetM"])
                self.assertEqual(point["comparability"]["status"], "not-comparable")
                self.assertTrue(point["comparability"]["reason"])
            self.assertNotIn("evidence", read_json(source_root / "acquisition-manifest.json"))
            self.assertEqual(read_json(source_root / EVIDENCE_MANIFEST)["acquisitionManifestSha256"],
                             digest_file(source_root / "acquisition-manifest.json"))
            controls["points"][0]["baltic1957M"] = 999
            atomic_json(source_root / "validation/height-controls.json", controls)
            with self.assertRaisesRegex(ValueError, "checksum"):
                verify(source_root, config)

    def test_receipt_tampering_and_truncated_query_fail_closed(self):
        with test_directory() as root:
            config, _, evidence_root, responses, _ = self.fixture(root)
            request = requests(config)[0]
            atomic_json(evidence_root / request["path"], dict(responses[0], width=512))
            with self.assertRaisesRegex(ValueError, "size|checksum"):
                verified_download(evidence_root, request, config)
            request = requests(config)[1]
            response = dict(responses[1], exceededTransferLimit=True)
            path = evidence_root / request["path"]
            atomic_json(path, response)
            receipt_path = path.with_name(path.name + ".provenance.json")
            receipt = read_json(receipt_path)
            receipt.update(sizeBytes=path.stat().st_size, sha256=digest_file(path))
            atomic_json(receipt_path, receipt)
            with self.assertRaisesRegex(ValueError, "truncated"):
                verified_download(evidence_root, request, config)

    def test_wrong_axis_semantics_are_rejected(self):
        with test_directory() as root:
            config, _, evidence_root, responses, manifest = self.fixture(root)
            _, receipt = verified_download(evidence_root, requests(config)[1], config)
            response = copy.deepcopy(responses[1])
            response["features"][0]["geometry"]["x"] *= -1
            with self.assertRaisesRegex(ValueError, "coordinate attributes"):
                control_record(response, receipt, manifest["sourceGroups"]["dmr"][0], config)
            response["spatialReference"]["wkid"] = 4326
            with self.assertRaisesRegex(ValueError, "S-JTSK"):
                control_record(response, receipt, manifest["sourceGroups"]["dmr"][0], config)

    def test_projected_query_fringe_is_excluded_by_location_never_height(self):
        with test_directory() as root:
            config, _, evidence_root, responses, manifest = self.fixture(root)
            _, receipt = verified_download(evidence_root, requests(config)[1], config)
            response = copy.deepcopy(responses[1])
            fringe = copy.deepcopy(response["features"][0])
            east, north = pyproj.Transformer.from_crs(4326, 5514, always_xy=True).transform(16.56, 49.90)
            fringe["attributes"].update(CISLO="fringe", OBJECTID=99, X=str(-north), Y=str(-east))
            fringe["geometry"] = dict(x=east, y=north)
            response["features"].append(fringe)
            response["features"][0]["attributes"]["VYSKA"] = "9999.123"
            record = control_record(response, receipt, manifest["sourceGroups"]["dmr"][0], config)
            self.assertEqual(len(record["points"]), 3)
            self.assertEqual(record["points"][0]["baltic1957M"], 9999.123)
            self.assertEqual(record["spatiallyExcluded"][0]["id"], "fringe")
            self.assertEqual(record["spatiallyExcluded"][0]["sourceAttributes"], fringe["attributes"])
            self.assertEqual(record["points"][0]["comparability"]["status"], "not-comparable")

    def test_normalized_overlay_keeps_previously_published_sources_immutable(self):
        with test_directory() as root:
            config, source_root, evidence_root, _, _ = self.fixture(root)
            original = digest_file(source_root / "acquisition-manifest.json")
            stage(evidence_root, source_root, config)
            overlay = read_json(source_root / EVIDENCE_MANIFEST)
            paths = [entry["path"] for entry in overlay["evidence"]] + [EVIDENCE_MANIFEST]
            paths += [request["path"] + ".provenance.json" for request in requests(config)]
            for path in paths:
                destination = root / "normalized" / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(source_root / path, destination)
            self.assertEqual(original, digest_file(source_root / "acquisition-manifest.json"))
            self.assertEqual(verify(source_root, config)["status"], "pass")


if __name__ == "__main__":
    unittest.main()
