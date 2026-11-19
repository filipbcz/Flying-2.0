"""Focused tests for the private regional packaging boundary."""

import hashlib
import json
import shutil
import uuid
from contextlib import contextmanager
import unittest
import zipfile
from pathlib import Path

from build_airport_navigation import check_sources, install, production_gate, safe_member, verify
from build_navigation_tiles import LAYERS, build_tiles, verify_tiles
from unittest.mock import patch
from import_runways import validate_json_schema_subset


@contextmanager
def temporary_tree():
    root = Path(__file__).resolve().parents[2] / ".forgemind-tmp" / "airport-tests" / uuid.uuid4().hex
    root.mkdir(parents=True)
    try:
        yield root
    finally:
        shutil.rmtree(root)


class AirportNavigationPackageTests(unittest.TestCase):
    def test_private_gate_rejects_synthetic_and_unidentified_sources(self):
        from private_airport_acceptance import require_real_source
        with temporary_tree() as root:
            source = root / "source.html"
            source.write_bytes(b"real snapshot")
            record = dict(sourceUri="https://aim.rlp.cz/vfrmanual/actual/ad_1_cz.html",
                          retrievedAt="2026-11-18T11:23:24Z",
                          sha256=hashlib.sha256(source.read_bytes()).hexdigest())
            require_real_source(record, source)
            with self.assertRaisesRegex(ValueError, "synthetic"):
                require_real_source(dict(record, synthetic=True), source)
            with self.assertRaisesRegex(ValueError, "identified real"):
                require_real_source(dict(record, sourceUri="https://example.invalid/fixture"), source)
            source.write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                require_real_source(record, source)

    def test_private_failure_replaces_stale_passing_report(self):
        from private_airport_acceptance import private_package_gate
        with temporary_tree() as root:
            report = root / "artifacts/airport-navigation/acceptance-report.json"
            report.parent.mkdir(parents=True)
            report.write_text('{"status": "pass"}', encoding="utf-8")
            with patch("private_airport_acceptance._validate", side_effect=ValueError("missing geometry")):
                with self.assertRaisesRegex(ValueError, "missing geometry"):
                    private_package_gate(root)
            self.assertEqual(json.loads(report.read_text())["status"], "fail")

    def test_private_geometry_comparison_cannot_use_uncertainty_as_tolerance(self):
        from private_airport_acceptance import equivalent
        with self.assertRaisesRegex(ValueError, "derived value differs"):
            equivalent(351, 350, "length")
        with self.assertRaisesRegex(ValueError, "value differs"):
            equivalent(True, False, "productionValidated")

    def test_missing_measured_precision_cannot_pass_production_gate(self):
        with temporary_tree() as root:
            (root / "geometry-report.json").write_text(json.dumps(dict(precisionAcceptance=dict(
                status="blocked-unmeasured", independentControlSource=None, measuredErrors=None))), encoding="utf-8")
            with patch("build_airport_navigation.verify"), self.assertRaisesRegex(ValueError, "independent measured"):
                production_gate(root)

    def test_real_raster_mbtiles_and_local_style(self):
        import pyproj
        import sqlite3
        with temporary_tree() as root:
            for name in LAYERS:
                coordinates = [16.45, 49.9]
                if name.startswith(("zabaged-", "geonames-")):
                    coordinates = list(pyproj.Transformer.from_crs(4326, 5514, always_xy=True).transform(*coordinates))
                (root / f"{name}.geojson").write_text(json.dumps(dict(type="FeatureCollection", features=[dict(
                    type="Feature", properties={}, geometry=dict(type="Point", coordinates=coordinates))])), encoding="utf-8")
            manifest = build_tiles(root, dict(minLonDeg=16.40, maxLonDeg=16.50, minLatDeg=49.85, maxLatDeg=49.95), 12, 12)
            self.assertGreater(verify_tiles(root.resolve(), manifest), 0)
            from contextlib import closing
            with closing(sqlite3.connect(root / "navigation.mbtiles")) as database:
                self.assertEqual(database.execute("SELECT tile_data FROM tiles LIMIT 1").fetchone()[0][:8], b"\x89PNG\r\n\x1a\n")
            (root / "style.json").write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "style checksum"):
                verify_tiles(root.resolve(), manifest)

    def test_unsafe_archive_names_fail(self):
        for name in ("../escape", "/absolute", "C:/absolute", "folder\\escape", "folder/../escape", "a//b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                safe_member(name)
        self.assertEqual(str(safe_member("navigation/airports.geojson")), "navigation/airports.geojson")

    def test_traversal_archive_fails_before_extraction(self):
        with temporary_tree() as temporary:
            root = Path(temporary)
            archive = root / "bad.zip"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("../escape.txt", "bad")
            with self.assertRaisesRegex(ValueError, "Unsafe package path"):
                install(archive, root / "installed")
            self.assertFalse((root / "escape.txt").exists())

    def test_installed_payload_tampering_fails(self):
        with temporary_tree() as temporary:
            root = Path(temporary)
            (root / "payload.json").write_text("changed", encoding="utf-8")
            (root / "install-manifest.json").write_text(json.dumps(dict(files={"payload.json": hashlib.sha256(b"original").hexdigest()})), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
                verify(root)

    def test_source_tampering_fails(self):
        with temporary_tree() as temporary:
            root = Path(temporary)
            (root / "source.html").write_text("changed", encoding="utf-8")
            (root / "source.html.provenance.json").write_text(json.dumps(dict(sha256=hashlib.sha256(b"original").hexdigest())), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Source checksum mismatch"):
                check_sources(root)

    def test_schema_conditional_preserves_non_private_gate(self):
        schema = dict(allOf=[dict(
            **{"if": dict(properties=dict(private=dict(const=True)), required=["private"]),
               "then": dict(properties=dict(transition=dict(const=False))),
               "else": dict(properties=dict(transition=dict(const=True)))})])
        self.assertFalse(validate_json_schema_subset(dict(private=True, transition=False), schema, schema, "$"))
        self.assertTrue(validate_json_schema_subset(dict(transition=False), schema, schema, "$"))

    def test_real_schema_keeps_production_transitions_mandatory(self):
        path = Path(__file__).resolve().parents[2] / "data_pipeline/schemas/pilot-runway-surfaces-package.schema.json"
        schema = json.loads(path.read_text(encoding="utf-8"))
        conditional = dict(allOf=schema["allOf"])
        production = dict(surfaces=[dict(transitionBands=[], slopes=dict(source="verified_threshold_geometry"))] * 2,
                          validation=dict(terrainTransitionChecksIncluded=True))
        self.assertTrue(validate_json_schema_subset(production, conditional, conditional, "$"))
        private = dict(geometryStatus="derived-unverified-private-inset-lane", surfaces=[dict(transitionBands=[])],
                       validation=dict(terrainTransitionChecksIncluded=False))
        self.assertFalse(validate_json_schema_subset(private, conditional, conditional, "$"))
        private["surfaces"][0]["transitionBands"] = [dict(side="left")]
        self.assertTrue(validate_json_schema_subset(private, conditional, conditional, "$"))


if __name__ == "__main__":
    unittest.main()
