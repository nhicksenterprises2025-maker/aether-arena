"""Seal only actual v1.5.0 acceptance into a compact, sanitized evidence ZIP.

Run: python Build/package_pulverizer_qa.py --spec <relative-input-spec.json>
The explicit spec supplies final portable/native/meta/import/Shipping/release
paths. --validate-only performs the same gates without creating outputs.
Existing outputs are immutable. No tests, imports, Git writes or APIs run here.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.5.0"
MAX_BYTES = 128 * 1024 * 1024
STANDARD_CLIPS = {"Idle", "Locomotion", "Attack", "Hit", "Death", "Deploy", "Status", "Turn", "Acquire"}
PRIOR_TESTS = {
    "Rift.Integration." + name for name in (
        "BalancePatch", "BattleInputRouting", "CardData", "ConnectedUI", "FullLengthReplay",
        "PausedResultAndReplayEvents", "Presentation", "ProfilePersistence", "ProjectilePresentation",
        "ReplayTimeline", "SnapshotRoundtrip", "SpellCastReplay", "TowerPathing", "Typography",
        "UnitCollision", "UnitMotion", "Pulverizer")
} | {"Rift.Meta.AggregationEconomy", "Rift.Meta.WorkerPauseAndRecovery"}
TEXT_EXTENSIONS = {".json", ".jsonl", ".log", ".txt", ".md", ".tsv", ".cpp", ".h", ".cs", ".ini", ".py", ".ps1", ".cmd", ".uproject"}


def need(ok: bool, message: str) -> None:
    if not ok:
        raise ValueError(message)


def digest(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def field(document: object, dotted: str) -> object:
    value = document
    for part in dotted.split("."):
        need(isinstance(value, dict) and part in value, "Missing actual report field: " + dotted)
        value = value[part]
    return value


def passed(document: dict, label: str) -> None:
    need(document.get("passed") is True, label + " did not pass")
    need(not document.get("errors") and not document.get("failures") and not document.get("failure"), label + " contains failures")


class Packet:
    def __init__(self) -> None:
        self.inputs: dict[str, dict] = {}
        self.include: set[str] = set()
        self.documents: dict[str, dict] = {}
        self.synthetic_meta: set[str] = set()
        self.derived: dict[str, bytes] = {}

    def path(self, value: str, exists: bool = True) -> Path:
        need(isinstance(value, str) and value and "\0" not in value, "Invalid evidence path")
        candidate = Path(value) if Path(value).is_absolute() else ROOT / value
        # Resolve the lexical path, then reject links before any file read.
        candidate = Path(os.path.abspath(candidate))
        need(candidate.is_relative_to(ROOT) and candidate != ROOT, "Evidence path leaves the workspace")
        for node in (candidate, *candidate.parents):
            if node == ROOT.parent:
                break
            if node.exists():
                info = node.lstat()
                need(not stat.S_ISLNK(info.st_mode) and not (getattr(info, "st_file_attributes", 0) & 0x400), "Evidence traverses a link")
        if exists:
            need(candidate.is_file(), "Missing actual evidence: " + candidate.relative_to(ROOT).as_posix())
        return candidate

    def identity(self, value: str) -> dict:
        path = self.path(value)
        h = hashlib.sha256()
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                h.update(chunk)
        return {"path": path.relative_to(ROOT).as_posix(), "bytes": path.stat().st_size, "sha256": h.hexdigest()}

    def pin(self, value: str, include: bool = False) -> dict:
        pin = self.identity(value)
        previous = self.inputs.setdefault(pin["path"], pin)
        need(previous == pin, "Input changed during acceptance: " + pin["path"])
        if include:
            self.include.add(pin["path"])
        return pin

    def bind(self, recorded: dict, include: bool = False) -> dict:
        need(isinstance(recorded, dict), "Missing recorded file identity")
        pin = self.pin(recorded.get("path", recorded.get("file")), include)
        need(recorded.get("sha256") == pin["sha256"], "Recorded SHA-256 differs: " + pin["path"])
        if "bytes" in recorded:
            need(type(recorded["bytes"]) is int and recorded["bytes"] == pin["bytes"], "Recorded length differs: " + pin["path"])
        return pin

    def read(self, value: str) -> dict:
        pin = self.pin(value, True)
        raw = self.path(pin["path"]).read_bytes()
        need(digest(raw) == pin["sha256"], "JSON changed before read")
        document = json.loads(raw.decode("utf-8-sig"), parse_constant=lambda x: (_ for _ in ()).throw(ValueError("Non-finite JSON: " + x)))
        need(isinstance(document, dict), "Expected an actual JSON object")
        self.documents[pin["path"]] = document
        return document

    def gates(self, document: dict, gates: list[dict]) -> None:
        for gate in gates:
            actual = field(document, gate["field"])
            if "equals" in gate:
                need(type(actual) is type(gate["equals"]) and actual == gate["equals"], "Actual metric differs: " + gate["field"])
            elif "minimum" in gate:
                need(type(actual) in (int, float) and actual >= gate["minimum"], "Actual metric below acceptance: " + gate["field"])
            else:
                raise ValueError("Metric gate needs equals or minimum")

    def recheck(self) -> None:
        for pin in self.inputs.values():
            need(self.identity(pin["path"]) == pin, "Accepted input changed before seal: " + pin["path"])


def sanitize_string(value: str) -> str:
    for prefix in sorted({str(ROOT), ROOT.as_posix(), str(ROOT).replace("/", "\\")}, key=len, reverse=True):
        if value.casefold() == prefix.casefold():
            return "[workspace]"
        if value.casefold().startswith(prefix.casefold() + "/") or value.casefold().startswith(prefix.casefold() + "\\"):
            return value[len(prefix):].lstrip("/\\").replace("\\", "/")
        value = re.sub(re.escape(prefix) + r"[/\\]?", "[workspace]/", value, flags=re.IGNORECASE)
    value = re.sub(r"[A-Za-z]:[/\\]+Users[/\\]+[^/\\\s\"']+[/\\]+", "[user]/", value, flags=re.IGNORECASE)
    value = re.sub(r"/(?:home|Users)/[^/\s\"']+/", "[user]/", value)
    return re.sub(r"\b[A-Za-z]:[/\\]+", "[external]/", value)


def sanitize(value: object) -> object:
    if isinstance(value, str):
        return sanitize_string(value)
    if isinstance(value, list):
        return [sanitize(item) for item in value]
    if isinstance(value, dict):
        secret_keys = {"authorization", "access_token", "refresh_token", "api_key", "password"}
        return {key: "[redacted]" if key.casefold() in secret_keys else sanitize(item) for key, item in value.items()}
    return value


def json_bytes(value: object) -> bytes:
    return (json.dumps(sanitize(value), indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def authored(packet: Packet) -> dict:
    folder = "Artifacts/QA/Pulverizer/"
    asset = packet.read(folder + "assets-source-qa.json")
    portrait = packet.read(folder + "card-portrait-qa.json")
    keep = packet.read(folder + "assets-preservation.json")
    review = packet.read(folder + "source-model-review.json")
    for label, report in (("Authored assets", asset), ("Portraits", portrait), ("Original assets", keep), ("Source visual review", review)):
        passed(report, label)
    need(asset["checkCount"] == len(asset["checks"]) and all(x["passed"] is True for x in asset["checks"]), "Incomplete authored asset checks")
    need(portrait["checkCount"] == len(portrait["checks"]) and len(portrait["cards"]) == 17 and all(x["passed"] is True for x in portrait["checks"]), "Incomplete17-card portrait audit")
    need(keep["originalFileCount"] == 586 and review["reviewed"] is True and not review["blockingIssues"], "Missing original asset/physical source acceptance")
    for report in (asset, portrait):
        for pin in report["provenance"].values():
            packet.bind(pin)
    for pin in review["pins"]:
        packet.bind(pin, Path(pin.get("path", pin.get("file", ""))).suffix.lower() in TEXT_EXTENSIONS)
    baseline = packet.read(packet.bind(keep["baseline"])["path"])
    need(len(baseline["files"]) == 586, "Original586-file baseline incomplete")
    for pin in baseline["files"]:
        packet.bind(pin)
    manifest = packet.read("Assets/asset_manifest.json")
    packet.bind(manifest["source"], True)
    model = manifest["characters"]["pulverizer"]
    need(manifest["cards"]["pulverizer"] == {"skeletal": "pulverizer"}, "Pulverizer binding changed")
    need({x["name"] for x in model["animations"]} == STANDARD_CLIPS, "Pulverizer requires all nine authored clips")
    for pin in [model["mesh"], *model["lods"], *model["animations"], manifest["illustrations"]["pulverizer"]]:
        packet.bind(pin, True)
    poses = packet.read(folder + "Poses/render-review.json")
    need(len(poses["renders"]) == 9 and all(x["model"] == "pulverizer" for x in poses["renders"]), "Missing nine compression pose renders")
    for pin in poses["renders"]:
        packet.bind(pin, True)
    for value in review["physicallyReviewedImages"]:
        packet.pin(value, True)
    return {"assetChecks": asset["checkCount"], "portraitChecks": portrait["checkCount"], "cards": 17, "originalAssetFiles": 586, "sourcePoseRenders": 9}


def portable(packet: Packet, value: str) -> dict:
    report = packet.read(value)
    passed(report, "Portable full simulation")
    packet.gates(report, [{"field": k, "equals": v} for k, v in {"version": VERSION, "focusedOnly": False, "sourcePinsUnchanged": True, "compileExit": 0, "focusedExit": 0, "fullExit": 0, "focusedScenarios": 6, "fullScenarios": 69}.items()])
    for pin in report["sourcePins"]:
        packet.bind(pin, True)
    executable = packet.pin(report["executable"])
    need(executable["sha256"] == report["executableSha256"], "Portable executable changed")
    log_pins = [packet.bind(pin, True) for pin in report["logPins"]]
    need(len(log_pins) == 3 and {Path(pin["path"]).name for pin in log_pins} == {"compile.log", "focused.log", "full.log"}, "Portable raw compile/focused/full log pins incomplete")
    for key in ("focusedLog", "fullLog"):
        need(packet.pin(report[key]) in log_pins, "Portable log differs from the accepted run")
    return {"focusedScenarios": 6, "fullScenarios": 69, "sourcePins": len(report["sourcePins"]), "report": packet.pin(value)}


def native(packet: Packet, spec: dict, module: dict) -> dict:
    report = packet.read(spec["report"])
    context = packet.read(spec["context"])
    packet.gates(context, [{"field": k, "equals": v} for k, v in {"completed": True, "sourcesUnchanged": True, "runtimeModulesUnchanged": True, "commandletExitCode": 0, "wrapperExitCode": 0}.items()])
    need(context["sourceReportSha256"] == packet.pin(spec["report"])["sha256"], "Full native report differs from its actual run")
    need(report["failed"] == report["notRun"] == report["inProcess"] == 0, "Incomplete native automation")
    tests = report["tests"]
    names = {x["fullTestPath"] for x in tests}
    need(len(names) == len(tests) and PRIOR_TESTS <= names and all(x["state"] in ("Success", "SuccessWithWarnings") for x in tests), "Full native suite lacks retained or Pulverizer tests")
    count = report["succeeded"] + report["succeededWithWarnings"]
    need(count == len(tests) == context["passedTests"], "Native test result counts differ")
    modules = [packet.bind(x) for x in context["runtimeModules"]]
    need(module in modules, "Full suite ran a different Editor module")
    for pin in context["sourceHashes"]:
        packet.bind(pin, Path(pin["path"]).suffix.lower() in TEXT_EXTENSIONS)
    packet.pin(context["commandletLog"], True)
    return {"tests": count, "failed": 0, "module": module, "context": packet.pin(spec["context"]), "report": packet.pin(spec["report"])}


def meta(packet: Packet, spec: dict, module: dict) -> dict:
    run = packet.read(spec["root"].rstrip("/\\") + "/run.json")
    context = packet.read(spec["root"].rstrip("/\\") + "/context.json")
    audit = packet.read(spec["audit"])
    passed(run, "Actual100-game native Meta run")
    need(run["games"] == context["requestedGames"] == 100 and run["runtimeSha256"] == context["runtimeSha256"] == module["sha256"], "Meta run is stale or incomplete")
    packet.gates(audit, [{"field": k, "equals": v} for k, v in {"observedGames": 100, "target": 100, "capturedCardCount": 17, "completeObserved": True, "invalid": 0, "economyInvalid": 0, "failedChecks": 0, "finalExportValidated": True}.items()])
    need(not audit["errors"] and audit["checks"] > 0, "Independent Meta aggregate failed")
    for pin in context["simulationSources"]:
        packet.bind(pin, True)
    for key in ("dataset", "export"):
        path = packet.path(run[key])
        need(path.is_relative_to(packet.path(spec["root"], False) / "UserData"), "Meta evidence is not from the isolated run")
        packet.synthetic_meta.add(path.relative_to(ROOT).as_posix())
        packet.read(run[key])
    need(packet.pin(run["dataset"])["sha256"] == run["datasetSha256"] and packet.path(audit["dataset"]) == packet.path(run["dataset"]), "Meta audit dataset identity differs")
    return {"games": 100, "capturedCards": 17, "checks": audit["checks"], "fingerprint": audit["fingerprint"], "runtimeSha256": module["sha256"], "audit": packet.pin(spec["audit"])}


def imported(packet: Packet, spec: dict, module: dict) -> dict:
    operations = []
    original_files = None
    final_import = None
    module_pins = None
    for key, operation, basename in (("run", "pulverizer-only-import", "unreal_asset_import.json"),
                                      ("auditRun", "readonly-audit", "unreal_asset_audit.json")):
        wrapper = packet.read(spec[key])
        passed(wrapper, "Final targeted native " + key)
        packet.gates(wrapper, [{"field": k, "equals": v} for k, v in {
            "schema": 2, "version": VERSION, "operation": operation, "exitCode": 0,
            "runtimeModulesUnchanged": True, "existingAssetCount": 322,
            "existingAssetsUnchanged": True, "changedExistingAssets": []}.items()])
        modules = [packet.bind(pin) for pin in wrapper["runtimeModules"]]
        need(len(modules) == 2 and len({pin["path"] for pin in modules}) == 2 and module in modules,
             "Native asset operation ran different Editor modules")
        if module_pins is not None:
            need(sorted(modules, key=lambda x: x["path"]) == module_pins, "Import and audit used different modules")
        module_pins = sorted(modules, key=lambda x: x["path"])
        files = wrapper["existingAssetPins"]
        need(isinstance(files, list) and len(files) == 322, "Imported322-file baseline incomplete")
        identities = []
        for pin in files:
            name = str(pin.get("path", ""))
            need(name.startswith("Unreal/RiftCrownArena/Content/") and name.endswith((".uasset", ".umap")),
                 "Unexpected native preservation input")
            identities.append(packet.bind(pin))
        need(len({pin["path"].casefold() for pin in identities}) == 322, "Duplicate native preservation pins")
        identities.sort(key=lambda x: x["path"])
        if original_files is not None:
            need(identities == original_files, "Import and audit do not preserve the same322 native files")
        original_files = identities
        folder = packet.path(spec[key]).parent.relative_to(ROOT).as_posix()
        raw_path = folder + "/" + basename
        raw = packet.read(raw_path)
        passed(raw, "Actual native " + basename)
        raw_pin = packet.pin(raw_path)
        need(raw["schema"] == 2 and raw_pin["sha256"] == wrapper["reportSha256"], "Native report differs from its operation")
        provenance = raw["provenance"]
        need(provenance["sourcesUnchanged"] is True, "Native asset source changed during operation")
        for name in ("assetManifest", "sourceBlend", "script", "helperScript"):
            packet.bind(provenance[name], name in ("script", "helperScript"))
        for pin in provenance["rawSources"]:
            packet.bind(pin)
        if key == "run":
            need(raw["mode"] == "PulverizerOnly", "Import rewrote assets outside the targeted selection")
            final_import = raw_pin
            validation = raw["validation"]
        else:
            passed(raw["sourceReadback"], "Native source readback")
            validation = raw["nativeValidation"]
        need(not validation["errors"] and len(validation["cards"]) == 17, "Native17-card asset validation incomplete")
        card = [row for row in validation["cards"] if row["id"] == "pulverizer"]
        need(len(card) == 1 and card[0]["illustration"] is True and card[0]["animations"] == 9
             and card[0]["bones"] == 11 and card[0]["lods"] == 4 and card[0]["physics"] is True,
             "Imported Pulverizer model/art/animation contract incomplete")
        for logfile in ("commandlet.log", "stdout.log"):
            packet.pin(folder + "/" + logfile, True)
        operations.append({"operation": operation, "run": packet.pin(spec[key]), "report": raw_pin})
    packet.pin("Build/Import-Pulverizer.ps1", True)
    stats = packet.read(spec["statPreservation"])
    passed(stats, "Prior16 native card-stat preservation")
    need(stats["schema"] == "rift.pulverizer.native-stat-preservation.v1" and stats["priorCards"] == 16
         and stats["addedCards"] == ["pulverizer"] and stats["checks"] > 0, "Native card-stat preservation incomplete")
    bound_stats = []
    for pin in stats["inputs"]:
        bound_stats.append(packet.bind(pin, Path(pin.get("path", pin.get("file", ""))).suffix.lower() not in (".zip", ".dll", ".exe")))
    need(any(pin["sha256"] == final_import["sha256"] and pin["bytes"] == final_import["bytes"]
             and Path(pin["path"]).name == "unreal_asset_import.json" for pin in bound_stats),
         "Card-stat preservation did not check the final imported report")
    return {"priorNativeFiles": 322, "priorCardDefinitions": 16, "statChecks": stats["checks"],
            "operations": operations, "runtimeModules": module_pins, "statPreservation": packet.pin(spec["statPreservation"])}


def captures(packet: Packet, spec: dict, executable: dict) -> dict:
    need(bool(spec["captures"]), "Actual Shipping capture cases are required")
    records = []
    for item in spec["captures"]:
        prefix = item["prefix"]
        run = packet.read(prefix + ".json")
        state = packet.read(prefix + ".state.json")
        passed(run, "Actual Shipping capture")
        packet.gates(run, [{"field": k, "equals": v} for k, v in {"version": VERSION, "editor": False, "captured": True, "stateCaptured": True, "exitCode": 0, "timedOut": False, "sourcePinsUnchanged": True, "executableUnchanged": True}.items()])
        need(run["executableSha256"] == executable["sha256"] and packet.path(run["executable"]) == packet.path(executable["path"]), "Capture used another executable")
        need(packet.path(run["state"]) == packet.path(prefix + ".state.json") and packet.path(run["screenshot"]) == packet.path(prefix + ".png"), "Capture triple paths differ")
        for pin in run["sourcePins"]:
            packet.bind(pin)
        packet.gates(state, item.get("stateGates", []))
        png = packet.pin(prefix + ".png", True)
        need(packet.path(png["path"]).read_bytes()[:8] == b"\x89PNG\r\n\x1a\n", "Capture is not a PNG")
        records.append({"name": run["name"], "png": png, "state": packet.pin(prefix + ".state.json"), "run": packet.pin(prefix + ".json")})
    review = packet.read(spec["physicalReview"])
    passed(review, "Actual independent Shipping model/decal/UI review")
    need(review.get("reviewed") is True or review.get("physicallyReviewed") is True, "Actual image inspection remains pending")
    packet.gates(review, spec.get("physicalReviewGates", []))
    # Final review must explicitly bind every actual image and both raw reports.
    text = json.dumps(review)
    for record in records:
        for kind in ("png", "state", "run"):
            need(record[kind]["sha256"] in text, "Physical review does not pin a final capture " + kind)
    return {"cases": records, "physicalReview": packet.pin(spec["physicalReview"]), "scope": "Observed stills and actual state/fixture checks; temporal behavior remains established by native/replay tests."}


def delivery(packet: Packet, spec: dict, executable: dict) -> dict:
    verification = packet.read(spec["verification"])
    manifest = packet.read(spec["manifest"])
    passed(verification, "Actual Windows delivery verification")
    need(verification["version"] == manifest["version"] == VERSION and verification["tag"] == "v" + VERSION, "Windows delivery is not1.5.0")
    root = spec.get("root", "Artifacts/Release").rstrip("/\\")
    payloads = {}
    for key in ("archive", "installer", "launcher"):
        need(Path(verification[key]).name == verification[key], "Unsafe delivery basename")
        pin = packet.pin(root + "/" + verification[key])
        need(pin["sha256"] == verification[key + "Sha256"], "Actual delivery payload changed")
        payloads[key] = pin
    need(packet.pin(spec["manifest"])["sha256"] == verification["manifestSha256"], "Delivery manifest changed")
    need(manifest["sha256"] == payloads["archive"]["sha256"] and manifest["size"] == payloads["archive"]["bytes"], "Manifest archive identity differs")
    files = manifest["files"]
    need(bool(files) and len({x["path"].casefold() for x in files}) == len(files) == verification["gameFiles"], "Payload inventory incomplete or duplicated")
    native_entry = [x for x in files if x["path"].endswith("Binaries/Win64/RiftCrownArena-Win64-Shipping.exe")]
    need(len(native_entry) == 1 and native_entry[0]["sha256"] == executable["sha256"] and native_entry[0]["size"] == executable["bytes"], "Manifest native executable differs")
    need(executable["path"].endswith(native_entry[0]["path"]), "Native payload root differs")
    payload_root = executable["path"][:-len(native_entry[0]["path"])]
    with zipfile.ZipFile(packet.path(payloads["archive"]["path"])) as archive:
        names = archive.namelist()
        need(len({name.casefold() for name in names}) == len(names), "Game ZIP contains duplicate entries")
        for name in names:
            need(name and not Path(name).is_absolute() and ".." not in Path(name).parts
                 and "\\" not in name and ":" not in name, "Unsafe game ZIP entry")
        stored_files = {entry.filename for entry in archive.infolist() if not entry.is_dir()}
        need(len(stored_files) == len(files), "Game ZIP has unlisted payload files")
        matched_files = set()
        for row in files:
            relative = row["path"]
            need(not Path(relative).is_absolute() and ".." not in Path(relative).parts and "\\" not in relative and ":" not in relative, "Unsafe game payload path")
            pin = packet.pin(payload_root + relative)
            need(pin["bytes"] == row["size"] and pin["sha256"] == row["sha256"], "Local game payload differs")
            need(relative in stored_files, "Game ZIP missing exact manifest payload entry")
            matched_files.add(relative)
            entry = archive.getinfo(relative);need(entry.file_size == pin["bytes"], "Game ZIP payload size differs")
            h = hashlib.sha256()
            with archive.open(entry) as stream:
                while chunk := stream.read(1024 * 1024):h.update(chunk)
            need(h.hexdigest() == pin["sha256"], "Game ZIP payload hash differs")
        need(matched_files == stored_files, "Game ZIP inventory differs from all delivered files")
    return {"verification": packet.pin(spec["verification"]), "manifest": packet.pin(spec["manifest"]), "payloads": payloads, "gameFiles": len(files)}


def seal(packet: Packet, summary: dict, summary_path: str, archive_path: str, source_context: dict) -> dict:
    target = packet.path(archive_path, False);document = packet.path(summary_path, False)
    need(VERSION in target.name and VERSION in document.name, "Only new versioned1.5.0 outputs may be written")
    need(not target.exists() and not document.exists(), "Immutable1.5.0 outputs already exist")
    entry_data = {}
    inventory = []
    for relative in sorted(packet.include):
        need("/UserData/" not in relative or relative in packet.synthetic_meta, "Only exact isolated synthetic Meta inputs may enter the packet")
        raw = packet.path(relative).read_bytes();pin = packet.inputs[relative]
        need(len(raw) == pin["bytes"] and digest(raw) == pin["sha256"], "Evidence changed before archive read")
        suffix = Path(relative).suffix.lower()
        if suffix == ".json":
            stored = json_bytes(json.loads(raw.decode("utf-8-sig")))
        elif suffix in TEXT_EXTENSIONS:
            stored = sanitize_string(raw.decode("utf-8-sig")).encode("utf-8")
        else:
            stored = raw
        entry_data[relative] = stored
        inventory.append({"entry": relative, "input": pin, "storedBytes": len(stored), "storedSHA256": digest(stored), "sanitized": stored != raw})
    for relative, raw in packet.derived.items():
        need(relative not in entry_data, "Derived evidence conflicts with an input")
        stored = sanitize_string(raw.decode("utf-8")).encode("utf-8")
        entry_data[relative] = stored
        inventory.append({"entry": relative, "input": None, "derivedRawBytes": len(raw), "derivedRawSHA256": digest(raw),
                          "storedBytes": len(stored), "storedSHA256": digest(stored), "sanitized": stored != raw})
    summary["inputs"] = list(packet.inputs.values());summary["sourceContext"] = source_context
    summary_raw = json_bytes(summary);summary_relative = document.relative_to(ROOT).as_posix()
    need(summary_relative not in entry_data, "Summary conflicts with an accepted evidence input")
    entry_data[summary_relative] = summary_raw
    inventory.append({"entry": summary_relative, "input": None, "storedBytes": len(summary_raw), "storedSHA256": digest(summary_raw), "sanitized": False, "generatedSummary": True})
    need(sum(len(x) for x in entry_data.values()) <= MAX_BYTES, "Evidence exceeds128MiB uncompressed cap")
    ledger = json_bytes({"schema": "rift.pulverizer.evidence-inventory.v1", "version": VERSION, "inputHashMeaning": "Raw actual file bytes before sanitization", "storedHashMeaning": "Exact sanitized bytes inside this ZIP", "entries": inventory})
    uncompressed_bytes = sum(len(x) for x in entry_data.values()) + len(ledger)
    need(uncompressed_bytes <= MAX_BYTES, "Evidence including inventory exceeds128MiB uncompressed cap")
    need("evidence-inventory.json" not in entry_data and len({x.casefold() for x in entry_data}) == len(entry_data), "Conflicting evidence paths")
    target.parent.mkdir(parents=True, exist_ok=True);document.parent.mkdir(parents=True, exist_ok=True)
    handle, staged = tempfile.mkstemp(prefix=target.name + ".", suffix=".tmp", dir=target.parent);os.close(handle)
    staged_path = Path(staged)
    created_summary = False
    try:
        with zipfile.ZipFile(staged_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for name, raw in {**entry_data, "evidence-inventory.json": ledger}.items():
                info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0));info.compress_type = zipfile.ZIP_DEFLATED;archive.writestr(info, raw)
        need(staged_path.stat().st_size <= MAX_BYTES, "Evidence ZIP exceeds128MiB")
        with zipfile.ZipFile(staged_path) as archive:
            need(len(archive.namelist()) == len(inventory) + 1 and archive.read("evidence-inventory.json") == ledger, "Evidence inventory differs")
            for row in inventory:
                raw = archive.read(row["entry"])
                need(len(raw) == row["storedBytes"] and digest(raw) == row["storedSHA256"], "Stored evidence identity differs")
        packet.recheck()
        need(not target.exists() and not document.exists(), "New outputs appeared during seal")
        # Never touch old packets, SHA256SUMS, update manifests or release APIs.
        with document.open("xb") as stream:
            stream.write(summary_raw)
        created_summary = True
        os.replace(staged_path, target)
    except Exception:
        if created_summary and not target.exists() and document.read_bytes() == summary_raw:
            document.unlink()
        raise
    finally:
        if staged_path.exists():staged_path.unlink()
    return {"passed": True, "version": VERSION, "archive": packet.identity(archive_path), "summary": packet.identity(summary_path), "entries": len(inventory), "uncompressedBytes": uncompressed_bytes, "scope": "Local immutable QA evidence only; no publication or original packet changes."}


def source_context(packet: Packet, spec: dict) -> dict:
    context = {"baseline": spec.get("baselineCommit"), "publicSourceCommit": spec.get("sourceCommit"),
               "identityScope": "Current files are independently pinned regardless of pre-commit Git status; commit metadata is optional."}
    for key in ("baselineCommit", "sourceCommit"):
        if spec.get(key):
            need(re.fullmatch(r"[a-f0-9]{40}", spec[key]) is not None, "Source context needs exact full commit SHA")
            result = subprocess.run(["git", "cat-file", "-e", spec[key] + "^{commit}"], cwd=ROOT, capture_output=True)
            need(result.returncode == 0, "Optional source commit does not exist locally")
    if spec.get("baselineCommit"):
        paths = sorted(path for path in packet.include if Path(path).suffix.lower() in TEXT_EXTENSIONS
                       and path.split("/", 1)[0] in {"Build", "Unreal", "Launcher", "Installer", "Docs"})
        need(bool(paths), "Source diff has no accepted source paths")
        command = ["git", "-c", "core.quotepath=false", "diff", "--no-ext-diff", "--no-textconv",
                   "--no-color", "--binary", spec["baselineCommit"]]
        if spec.get("sourceCommit"):
            command.append(spec["sourceCommit"])
        command += ["--", *paths]
        result = subprocess.run(command, cwd=ROOT, capture_output=True)
        need(result.returncode == 0, "Read-only source diff failed")
        result.stdout.decode("utf-8")
        relative = "source-diff.patch"
        packet.derived[relative] = result.stdout
        context["diff"] = {"entry": relative, "rawSHA256": digest(result.stdout), "rawBytes": len(result.stdout),
                           "command": command, "paths": paths,
                           "scope": "Tracked accepted text sources at the selected commit or current working tree. Untracked accepted files are archived and pinned separately."}
    return context


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", required=True)
    parser.add_argument("--summary", default="Docs/QA/pulverizer-1.5.0.json")
    parser.add_argument("--archive", default="Artifacts/Release/RiftCrownArena-Pulverizer-QAEvidence-1.5.0.zip")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args();packet = Packet();spec = packet.read(args.spec)
    need(spec.get("version") == VERSION, "An explicit1.5.0 input spec is required")
    packet.pin("Build/package_pulverizer_qa.py", True)
    module = packet.pin(spec["module"]);executable = packet.pin(spec["executable"])
    summary = {"schema": "rift.pulverizer.acceptance.v1", "version": VERSION, "passed": True, "generatedUTC": datetime.now(timezone.utc).isoformat(), "sourceModule": module, "nativeExecutable": executable,
               "authored": authored(packet), "portable": portable(packet, spec["portable"]), "native": native(packet, spec["native"], module), "meta": meta(packet, spec["meta"], module), "imported": imported(packet, spec["import"], module), "shipping": captures(packet, spec, executable), "windowsDelivery": delivery(packet, spec["release"], executable),
               "limits": ["Only actual completed runs are accepted; this writer does not perform or relabel game tests.", "Image observations describe stills; replay/native proofs establish motion and timing.", "Historical1.4.0 reports retain their original meaning and bytes."]}
    for value in spec.get("sourceFiles", []) + spec.get("extraEvidence", []):packet.pin(value, True)
    for role in spec.get("extraReports", []):
        report = packet.read(role["file"])
        if role.get("requiresPassed", True):passed(report, role["file"])
        packet.gates(report, role.get("gates", []))
    context = source_context(packet, spec)
    packet.recheck()
    if args.validate_only:
        print(json.dumps({"passed": True, "version": VERSION, "validatedInputs": len(packet.inputs), "includedInputs": len(packet.include), "scope": "Read-only preflight; no outputs sealed."}))
    else:
        print(json.dumps(seal(packet, summary, args.summary, args.archive, context)))


if __name__ == "__main__":
    main()
