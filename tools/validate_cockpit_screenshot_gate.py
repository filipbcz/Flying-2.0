#!/usr/bin/env python3
"""Validate cockpit screenshot captures against layout-declared baselines.

This gate intentionally consumes captures produced outside the repository by the
current Unreal cockpit screenshot automation. Repository-static capture images are
rejected so visual regressions cannot be hidden by checked-in fixtures.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(message)


def read_json(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except OSError as exc:
        fail(f"unavailable JSON file {path}: {exc}")
    except json.JSONDecodeError as exc:
        fail(f"invalid JSON file {path}: {exc}")


def layout_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def resolve_repo_path(repo_root: Path, base: Path, value: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    if value.startswith("Reports/") or value.startswith("Content/"):
        return repo_root / path
    return base / path


def load_ppm(path: Path) -> tuple[int, int, bytes]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        fail(f"unavailable screenshot image {path}: {exc}")

    index = 0

    def token() -> bytes:
        nonlocal index
        while index < len(data) and chr(data[index]).isspace():
            index += 1
        if index >= len(data):
            fail(f"truncated PPM header in {path}")
        if data[index:index + 1] == b"#":
            while index < len(data) and data[index:index + 1] not in (b"\n", b"\r"):
                index += 1
            return token()
        start = index
        while index < len(data) and not chr(data[index]).isspace():
            index += 1
        return data[start:index]

    magic = token()
    width = int(token())
    height = int(token())
    maximum = int(token())
    if width <= 0 or height <= 0 or maximum != 255 or magic not in (b"P3", b"P6"):
        fail(f"unsupported PPM screenshot format in {path}")

    if magic == b"P6":
        if index >= len(data) or not chr(data[index]).isspace():
            fail(f"missing P6 header delimiter in {path}")
        index += 1
        rgb = data[index:index + width * height * 3]
        if len(rgb) != width * height * 3:
            fail(f"truncated P6 pixel data in {path}")
    else:
        while index < len(data) and chr(data[index]).isspace():
            index += 1
        rgb_values = [int(part) for part in data[index:].split()]
        if len(rgb_values) != width * height * 3:
            fail(f"truncated P3 pixel data in {path}")
        if any(value < 0 or value > 255 for value in rgb_values):
            fail(f"invalid P3 pixel data in {path}")
        rgb = bytes(rgb_values)
    return width, height, rgb


def image_difference_percent(expected: bytes, captured: bytes) -> float:
    if len(expected) != len(captured) or not expected:
        return 100.0
    total = sum(abs(a - b) for a, b in zip(expected, captured))
    return total / (len(expected) * 255.0) * 100.0


def require_sized(image: tuple[int, int, bytes], minimum_width: int, minimum_height: int, label: str) -> None:
    width, height, pixels = image
    if width < minimum_width or height < minimum_height:
        fail(f"{label} is below required resolution: {width}x{height}")
    if len(pixels) != width * height * 3:
        fail(f"{label} pixel data does not match dimensions")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", required=True, type=Path)
    parser.add_argument("--capture-report", required=True, type=Path)
    parser.add_argument("--run-capture-command", action="store_true")
    args = parser.parse_args()

    repo_root = Path.cwd()
    layout_path = args.layout.resolve()
    layout = read_json(layout_path)
    capture_config = layout.get("screenshotCapture", {})
    render_command = capture_config.get("renderCommand", "")
    declared_report = capture_config.get("provenance", "")
    if not declared_report:
        fail("layout does not declare screenshotCapture.provenance")

    if args.run_capture_command:
        if not render_command:
            fail("layout does not declare screenshotCapture.renderCommand")
        subprocess.run(render_command, cwd=repo_root, shell=True, check=True)

    report_path = args.capture_report.resolve()
    declared_report_path = resolve_repo_path(
        repo_root, layout_path.parent, declared_report
    ).resolve()
    if report_path != declared_report_path:
        fail("capture report path does not match layout screenshotCapture.provenance")

    report = read_json(report_path)
    if report.get("schemaVersion") != "flying.cockpit.screenshot-captures.v1":
        fail("capture report schemaVersion is not flying.cockpit.screenshot-captures.v1")
    if report.get("layoutSha256") != layout_sha256(layout_path):
        fail("capture report layoutSha256 does not match current layout")
    if report.get("renderCommand") != render_command:
        fail("capture report renderCommand does not match layout render command")
    if not report.get("buildId") or not report.get("generatedAt"):
        fail("capture report must include buildId and generatedAt provenance")

    requirements = layout.get("screenshotRequirements", {})
    minimum_width = int(requirements.get("minimumWidth", 0))
    minimum_height = int(requirements.get("minimumHeight", 0))
    if minimum_width < 640 or minimum_height < 360:
        fail("layout screenshotRequirements must require production-sized images")

    baselines = {entry["id"]: entry for entry in layout.get("screenshotBaselines", [])}
    captures = {entry["id"]: entry for entry in report.get("captures", [])}
    required_ids = {"day_readability", *layout.get("lighting", {}).get("nightViewStates", [])}
    missing_baselines = sorted(required_ids - baselines.keys())
    missing_captures = sorted(required_ids - captures.keys())
    if missing_baselines:
        fail(f"missing screenshot baseline ids: {', '.join(missing_baselines)}")
    if missing_captures:
        fail(f"missing screenshot capture ids: {', '.join(missing_captures)}")

    content_root = (repo_root / "Content" / "Cockpit").resolve()
    report_base = report_path.parent
    layout_base = layout_path.parent
    for screenshot_id in sorted(required_ids):
        baseline = baselines[screenshot_id]
        capture = captures[screenshot_id]
        if capture.get("view") != baseline.get("view"):
            fail(f"capture view mismatch for {screenshot_id}")
        if capture.get("source") != "unreal-render-target":
            fail(f"capture source for {screenshot_id} is not unreal-render-target")

        baseline_path = resolve_repo_path(repo_root, layout_base, baseline["image"]).resolve()
        captured_path = resolve_repo_path(repo_root, report_base, capture["image"]).resolve()
        if content_root in captured_path.parents:
            fail(f"capture image for {screenshot_id} is repository content, not renderer output")

        expected_image = load_ppm(baseline_path)
        captured_image = load_ppm(captured_path)
        require_sized(expected_image, minimum_width, minimum_height, f"{screenshot_id} baseline")
        require_sized(captured_image, minimum_width, minimum_height, f"{screenshot_id} capture")
        if expected_image[:2] != captured_image[:2]:
            fail(f"screenshot dimensions differ for {screenshot_id}")

        difference = image_difference_percent(expected_image[2], captured_image[2])
        if difference > float(baseline.get("maxDifferencePercent", 0.0)):
            fail(f"{screenshot_id} screenshot difference {difference:.3f}% exceeds threshold")

    return 0


if __name__ == "__main__":
    sys.exit(main())
