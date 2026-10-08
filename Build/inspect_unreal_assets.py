"""Read current on-disk Rift assets; never import, replace or save packages.

Run through UnrealEditor-Cmd -run=pythonscript -script=<this file> -NullRHI.
The evidence report has its own filename and cannot overwrite import results.
"""
import collections
import json
from datetime import datetime, timezone
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[1]
REGISTRY = unreal.AssetRegistryHelpers.get_asset_registry()
REGISTRY.scan_paths_synchronous(["/Game/Rift"], True)
records = REGISTRY.get_assets_by_path("/Game/Rift", True, True)
report = {"schema": 1, "generatedUTC": datetime.now(timezone.utc).isoformat(),
          "method": "Read actual on-disk AssetRegistry records and load existing objects; no import/save",
          "assets": [], "classCounts": {}, "nativeValidation": {}, "errors": []}
counts = collections.Counter()
for record in sorted(records, key=lambda entry: str(entry.package_name)):
    path = str(record.package_name) + "." + str(record.asset_name)
    kind = str(record.asset_class_path.asset_name)
    entry = {"path": path, "class": kind, "onDisk": record.is_valid()}
    if kind == "World":
        # Loading a map is a different editor operation from loading an asset.
        # Its real runtime load is evidenced by the native capture logs/PNGs.
        entry["loadCheck"] = "World load verified separately by actual native Arena captures"
        counts[kind] += 1
        report["assets"].append(entry)
        continue
    try:
        obj = unreal.EditorAssetLibrary.load_asset(path)
        entry["loadable"] = obj is not None
        if obj is None:
            report["errors"].append("Cannot load registered asset: " + path)
        if kind == "StaticMesh" and obj:
            bounds = obj.get_bounds()
            entry["boundsOriginCm"] = str(bounds.origin)
            entry["boundsExtentCm"] = str(bounds.box_extent)
            entry["materialSlots"] = len(obj.get_editor_property("static_materials"))
        if kind == "NiagaraSystem" and obj:
            entry["exposedProperties"] = {}
            for name in ("fixed_bounds", "warmup_time"):
                try:
                    entry["exposedProperties"][name] = str(obj.get_editor_property(name))
                except Exception:
                    pass
    except Exception as error:
        entry["inspectionError"] = str(error)
        report["errors"].append(path + ": " + str(error))
    counts[kind] += 1
    report["assets"].append(entry)
report["classCounts"] = dict(sorted(counts.items()))
report["assetCount"] = len(report["assets"])
report["nativeValidation"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
report["errors"].extend(report["nativeValidation"].get("errors", []))
if len(report["nativeValidation"].get("cards", [])) != 14:
    report["errors"].append("Native card validation did not inspect all 14 original cards")
output = ROOT / "Artifacts/QA/unreal_asset_audit.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log(f"Readonly asset audit: {len(records)} assets, {len(report['errors'])} errors; {output}")
if report["errors"]:
    raise RuntimeError("Actual asset audit failed; inspect its populated JSON")
