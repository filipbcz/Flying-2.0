import copy
import hashlib
import json
import shutil
import unittest
import uuid
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

import acquire_sources as acquisition
from validate_sources import run_self_test, sample_source, validate_source


@contextmanager
def test_directory(**kwargs):
    directory = acquisition.REPOSITORY / ".forgemind-tmp/managed" / ("acquisition-test-" + uuid.uuid4().hex)
    directory.mkdir(parents=True)
    try:
        yield str(directory)
    finally:
        shutil.rmtree(directory)


class AcquisitionTests(unittest.TestCase):
    def test_existing_source_validation_regressions(self):
        with patch("validate_sources.tempfile.TemporaryDirectory", test_directory):
            self.assertEqual(run_self_test(), 0)

    def test_storage_threshold(self):
        with test_directory() as directory:
            with self.assertRaises(ValueError):
                acquisition.storage_preflight(Path(directory), 99_999_999_999)
            self.assertEqual(acquisition.storage_preflight(Path(directory), 100_000_000_000), 100_000_000_000)

    def test_paths_and_domains(self):
        with self.assertRaises(ValueError):
            acquisition.persistent_root(str(acquisition.REPOSITORY / "data"))
        with test_directory() as directory:
            for logical in ("../secret", "/secret", "C:/secret", "safe/../../secret"):
                with self.assertRaises(ValueError):
                    acquisition.contained(Path(directory), logical)
        for url in ("http://ags.cuzk.gov.cz/", "https://localhost/", "https://user:pass@ags.cuzk.gov.cz/", "https://ags.cuzk.gov.cz/?token=secret"):
            with self.assertRaises(ValueError):
                acquisition.safe_url(url, ["ags.cuzk.gov.cz"])

    def test_scope(self):
        config = acquisition.load_config()
        altered = {key: value for key, value in config.items() if key != "region"}
        altered = copy.deepcopy(altered)
        altered["sources"][0]["url"] = altered["sources"][0]["url"].replace("16.396", "15.0")
        with test_directory() as directory:
            path = Path(directory) / "config.json"
            acquisition.atomic_json(path, altered)
            with self.assertRaises(ValueError):
                acquisition.load_config(path)

    def test_private_validation_keeps_technical_requirements(self):
        with test_directory() as directory:
            base = Path(directory)
            (base / "payload").write_bytes(b"payload")
            record = sample_source("dmr", "payload", hashlib.sha256(b"payload").hexdigest())
            del record["license"]
            del record["attribution"]
            findings = []
            validate_source(record, "$", base, True, findings, require_rights=False)
            self.assertEqual(findings, [])
            record["checksum"]["value"] = "0" * 64
            validate_source(record, "$", base, True, findings, require_rights=False)
            self.assertIn("checksum.mismatch", [finding.code for finding in findings])

    def test_managed_resume_and_integrity(self):
        config = acquisition.load_config()
        config["sources"] = [config["sources"][0]]
        source = config["sources"][0]
        with test_directory() as directory:
            root = Path(directory)
            payload = root / "data/sources" / source["path"]
            payload.parent.mkdir(parents=True)
            complete = b"II*\x00" + b"real-payload-test-fixture"
            payload.write_bytes(complete)
            receipt = {"sourceUrl": source["url"], "finalUrl": source["url"], "destinationRelativePath": "pilot/" + source["path"], "sizeBytes": len(complete), "sha256": hashlib.sha256(complete).hexdigest(), "downloadedAt": "2026-11-16T00:00:00Z"}
            acquisition.atomic_json(Path(str(payload) + ".provenance.json"), receipt)
            with patch.object(acquisition, "storage_preflight", return_value=100_000_000_000):
                self.assertEqual(acquisition.plan(config, root)["requests"], [])
                payload.write_bytes(complete[:10])
                queued = acquisition.plan(config, root)["requests"]
                self.assertEqual(len(queued), 1)
                self.assertEqual(queued[0]["destinationRelativePath"], "pilot/" + source["path"])
                self.assertEqual(queued[0]["minimumFreeSpaceBytes"], 100_000_000_000)
            with self.assertRaises(ValueError):
                acquisition.verify_receipt(root / "data/sources", source, config)
            payload.write_bytes(complete)
            staged = root / "data/sources"
            publication = root / "publication"
            acquisition.finalize(config, publication, staged)
            self.assertEqual(acquisition.verify(config, publication)["payloadCount"], 1)
            altered = acquisition.read_json(publication / "data/sources/acquisition-manifest.json")
            altered["purpose"] = "distributable"
            acquisition.atomic_json(publication / "data/sources/acquisition-manifest.json", altered)
            with self.assertRaises(ValueError):
                acquisition.verify(config, publication)

    def test_error_and_truncated_payloads(self):
        with test_directory() as directory:
            payload = Path(directory) / "payload"
            for value in ({"error": {"code": 403}}, {"type": "FeatureCollection", "features": [], "exceededTransferLimit": True}):
                payload.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    acquisition.validate_payload(payload, {"kind": "geojson"})
            with self.assertRaises(ValueError):
                acquisition.validate_payload(payload, {"kind": "tiff"})


if __name__ == "__main__":
    unittest.main()
