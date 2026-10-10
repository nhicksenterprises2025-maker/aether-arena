"""Verify production-model card portrait provenance using only Python's stdlib.

Run from any directory: python Build/validate_card_portraits.py
The default report is Artifacts/QA/model-card-portraits.json. This checks source
files and render metadata; it does not replace review of the cooked game.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib


CARD_IDS = frozenset((
    "ironclad", "ember_archer", "archer_tower", "vampire_bats",
    "twin_blades", "boulderback", "arc_mage", "rambeast", "sky_manta",
    "frost_fang", "storm_raven", "bullet_burst", "nova_flask",
    "meteor_shards", "mini_stampede", "stampede",
))
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def finite_number(value: object) -> bool:
    return type(value) in (int, float) and math.isfinite(value)


def json_safe(value: object) -> object:
    """Allow failure evidence to be written even when input contains NaN/Inf."""
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {key: json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


class PortraitAudit:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.files: dict[str, bytes] = {}
        self.report = {
            "schema": 1,
            "generatedUTC": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "passed": False,
            "checks": [],
            "errors": [],
            "cards": {},
            "provenance": {},
            "scope": "Source/export/PNG provenance; cooked bindings and rendered game review are separate.",
        }

    def check(self, name: str, passed: bool, detail: object = None) -> bool:
        item = {"name": name, "passed": bool(passed)}
        if detail is not None:
            item["detail"] = detail
        self.report["checks"].append(item)
        if not passed:
            self.report["errors"].append(name)
        return bool(passed)

    def path(self, relative: str) -> Path:
        if not isinstance(relative, str) or "\\" in relative:
            raise ValueError("Expected a forward-slash relative source path")
        path = (self.root / relative).resolve()
        if Path(relative).is_absolute() or ".." in Path(relative).parts:
            raise ValueError("Expected a path within the repository")
        path.relative_to(self.root)
        return path

    def raw(self, relative: str) -> bytes:
        if relative not in self.files:
            self.files[relative] = self.path(relative).read_bytes()
        return self.files[relative]

    def file_info(self, relative: str) -> dict:
        raw = self.raw(relative)
        return {"file": relative, "bytes": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest()}

    def verify_file(self, entry: object, label: str) -> dict | None:
        if not self.check(label + " metadata", isinstance(entry, dict) and
                          isinstance(entry.get("file"), str) and
                          isinstance(entry.get("sha256"), str) and
                          type(entry.get("bytes")) is int):
            return None
        try:
            actual = self.file_info(entry["file"])
        except (OSError, ValueError):
            self.check(label + " exists within repository", False)
            return None
        self.check(label + " raw bytes and SHA-256", all(
            actual[key] == entry[key] for key in ("file", "bytes", "sha256")))
        return actual

    def load_json(self, relative: str) -> dict:
        document = json.loads(self.raw(relative).decode("utf-8-sig"))
        if not isinstance(document, dict):
            raise ValueError("Expected a JSON object")
        return document

    def audit(self) -> None:
        manifest_path = "Assets/asset_manifest.json"
        portrait_path = "Assets/Source/CardArt/model_portraits.json"
        manifest = self.load_json(manifest_path)
        portraits = self.load_json(portrait_path)
        for label, relative in (
            ("assetManifest", manifest_path),
            ("portraitReport", portrait_path),
            ("rendererScript", "Build/render_card_portraits.py"),
            ("validatorScript", "Build/validate_card_portraits.py"),
        ):
            self.report["provenance"][label] = self.file_info(relative)

        self.check("Manifest contains exactly the sixteen canonical cards",
                   set(manifest.get("cards", {})) == CARD_IDS)
        self.check("Manifest illustrations contain exactly the sixteen cards",
                   set(manifest.get("illustrations", {})) == CARD_IDS)
        self.check("Portrait report contains exactly the sixteen cards",
                   set(portraits.get("cards", {})) == CARD_IDS)
        self.check("Portrait PNG inventory contains exactly the sixteen cards",
                   {p.stem for p in (self.root / "Assets/Source/CardArt").glob("*.png")} == CARD_IDS)
        self.check("Portrait resolution is 768 by 960", portraits.get("resolution") == [768, 960])
        self.check("Portrait sourceBlend equals final export source",
                   portraits.get("sourceBlend") == manifest.get("source"))
        source_info = self.verify_file(manifest.get("source"), "Production Blender source")
        if source_info:
            self.report["provenance"]["sourceBlend"] = source_info
        if "rendererScript" in portraits:
            renderer = self.verify_file(portraits["rendererScript"], "Recorded renderer script")
            self.check("Recorded renderer identifies the current portrait script",
                       renderer == self.report["provenance"]["rendererScript"])
        for key in ("aiGeneratedRasterArtwork", "generatedArtwork"):
            if key in portraits:
                self.check(key + " is false", portraits[key] is False)

        models = {}
        for model_id, character in manifest.get("characters", {}).items():
            mesh = character["mesh"]
            models[mesh["file"]] = (model_id, "skeletal", mesh, character)
        for model_id, static in manifest.get("statics", {}).items():
            models[static["file"]] = (model_id, "static", static, None)

        for card_id in sorted(CARD_IDS):
            record = portraits.get("cards", {}).get(card_id)
            binding = manifest.get("cards", {}).get(card_id)
            illustration = manifest.get("illustrations", {}).get(card_id)
            if not self.check(card_id + " records exist", all(
                    isinstance(item, dict) for item in (record, binding, illustration))):
                continue
            image_info = self.verify_file(record, card_id + " PNG")
            self.check(card_id + " illustration binding matches portrait", all(
                record.get(key) == illustration.get(key) for key in ("file", "bytes", "sha256")))
            self.check(card_id + " canonical PNG path",
                       record.get("file") == f"Assets/Source/CardArt/{card_id}.png")
            if image_info:
                raw = self.raw(image_info["file"])
                header_ok = len(raw) >= 33 and raw[:8] == PNG_SIGNATURE and raw[8:16] == b"\x00\x00\x00\rIHDR"
                self.check(card_id + " PNG signature and IHDR", header_ok)
                if header_ok:
                    width, height = struct.unpack(">II", raw[16:24])
                    self.check(card_id + " IHDR dimensions", (width, height) == (768, 960), [width, height])
                    self.check(card_id + " IHDR checksum",
                               zlib.crc32(raw[12:29]) == struct.unpack(">I", raw[29:33])[0])
            pose = record.get("poseFrame")
            self.check(card_id + " positive integer pose frame", type(pose) is int and pose >= 1)
            scale = record.get("cameraOrthoScale")
            self.check(card_id + " finite positive camera scale", finite_number(scale) and scale > 0)
            bounds = record.get("boundsMeters")
            bounds_ok = isinstance(bounds, list) and len(bounds) == 2 and all(
                isinstance(point, list) and len(point) == 3 and all(finite_number(v) for v in point)
                for point in bounds)
            self.check(card_id + " finite ordered model bounds", bounds_ok and all(
                lo <= hi for lo, hi in zip(bounds[0], bounds[1])))
            sources = record.get("sources")
            if not self.check(card_id + " production subjects exist", isinstance(sources, list) and bool(sources)):
                continue
            expected_models = {binding[key] for key in ("skeletal", "static", "skeletalDecoration") if key in binding}
            observed_models = set()
            validated_sources = []
            for index, subject in enumerate(sources):
                label = f"{card_id} subject {index + 1}"
                model = subject.get("model") if isinstance(subject, dict) else None
                if not self.check(label + " recognized production export", isinstance(model, dict) and model.get("file") in models):
                    continue
                model_id, kind, exported, character = models[model["file"]]
                observed_models.add(model_id)
                self.check(label + " matches final export metadata", all(
                    model.get(key) == exported.get(key) for key in ("file", "bytes", "sha256")))
                verified = self.verify_file(model, label + " FBX")
                source = {"modelId": model_id, "kind": kind, "model": verified or {"file": model["file"]}}
                if kind == "skeletal":
                    action = subject.get("action")
                    clip = next((clip for clip in character["animations"] if clip["name"] == action), None)
                    phase = subject.get("phase")
                    self.check(label + " recognized authored action", clip is not None)
                    phase_ok = finite_number(phase) and 0 <= phase <= 1
                    self.check(label + " normalized finite action phase", phase_ok)
                    if clip is not None and phase_ok:
                        expected_frame = 1 + round((clip["frames"] - 1) * phase)
                        self.check(label + " pose frame matches action phase", pose == expected_frame)
                        source.update({"action": action, "phase": phase, "poseFrame": expected_frame})
                else:
                    self.check(label + " static subject has no skeletal pose",
                               "action" not in subject and "phase" not in subject)
                validated_sources.append(source)
            self.check(card_id + " subjects match native card model bindings", observed_models == expected_models,
                       {"expected": sorted(expected_models), "observed": sorted(observed_models)})
            if validated_sources and all(source["kind"] == "static" for source in validated_sources):
                self.check(card_id + " static portrait uses the base frame", pose == 1)
            self.report["cards"][card_id] = {
                "image": image_info, "poseFrame": pose,
                "cameraOrthoScale": scale, "boundsMeters": bounds,
                "sources": validated_sources,
            }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, default=Path("Artifacts/QA/model-card-portraits.json"))
    args = parser.parse_args()
    audit = PortraitAudit(args.root)
    try:
        audit.audit()
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as error:
        # Keep machine/user paths out of a report that may become public evidence.
        audit.check("Audit input structure and readable source files", False, type(error).__name__)
    report = audit.report
    report["checkCount"] = len(report["checks"])
    report["passed"] = not report["errors"] and len(report["cards"]) == len(CARD_IDS)
    output = args.output if args.output.is_absolute() else audit.root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(json_safe(report), indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"RIFT_CARD_PORTRAIT_QA: {len(report['cards'])} cards, {report['checkCount']} checks, {len(report['errors'])} failures")
    for failure in report["errors"]:
        print("RIFT_CARD_PORTRAIT_ERROR:", failure)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
