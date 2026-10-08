"""Rebuild the 17 generated Niagara system graphs; no FBX/material/card import."""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[1]
report = json.loads(unreal.RiftEditorAssetLibrary.build_presentation_assets_json())
output = ROOT / "Artifacts/QA/unreal_vfx_graph_repair.json"
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
if report["errors"] or len(report["effects"]) != 17 or any(not item["saved"] for item in report["effects"]):
    raise RuntimeError("Niagara graph repair failed; inspect populated report")
unreal.log("Saved all 17 generated Niagara systems through the engine editor graph utility")
