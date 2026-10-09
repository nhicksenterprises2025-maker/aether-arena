"""Import only Rift UI fonts; never rebuild gameplay or model assets.

Run using UnrealEditor-Cmd -run=pythonscript -script=<this file> -unattended
-NullRHI. RIFT_REPO_ROOT may override the repository root. Four unmodified TTFs
are verified against the pinned provenance before creating font-face assets.
"""
import hashlib
import json
import os
from pathlib import Path
import unreal

ROOT = Path(os.environ.get("RIFT_REPO_ROOT", Path(__file__).resolve().parents[1]))
SOURCE = ROOT / "Assets/Fonts/BarlowSemiCondensed"
PROVENANCE = json.loads((SOURCE / "provenance.json").read_text(encoding="utf-8"))
DESTINATION = "/Game/Rift/Fonts"
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary
REPORT = {"family": PROVENANCE["family"], "sourceCommit": PROVENANCE["commit"],
          "license": PROVENANCE["license"], "faces": [], "font": None}

if not unreal.RiftEditorAssetLibrary.ensure_font_import_slate():
    raise RuntimeError("Unreal font services could not initialize for commandlet import")

for record in PROVENANCE["files"]:
    source = SOURCE / record["name"]
    data = source.read_bytes()
    if len(data) != record["bytes"] or hashlib.sha256(data).hexdigest() != record["sha256"]:
        raise RuntimeError("Font source bytes differ from pinned provenance: " + source.name)

for weight in ("Regular", "Medium", "SemiBold", "Bold"):
    source = SOURCE / ("BarlowSemiCondensed-" + weight + ".ttf")
    name = "FF_BarlowSemiCondensed_" + weight
    factory = unreal.FontFileImportFactory()
    factory.set_editor_property("batch_create_font_asset", unreal.BatchCreateFontAsset.NO)
    task = unreal.AssetImportTask()
    for key, value in {
        "filename": str(source), "destination_path": DESTINATION,
        "destination_name": name, "automated": True, "replace_existing": True,
        "replace_existing_settings": True, "save": False, "factory": factory,
    }.items():
        task.set_editor_property(key, value)
    TOOLS.import_asset_tasks([task])
    face = LIB.load_asset(DESTINATION + "/" + name)
    if not isinstance(face, unreal.FontFace):
        raise RuntimeError("Expected a FontFace import for " + weight)
    # Inline stores face data in the cooked asset; no source TTF or system font
    # installation is needed when the player runs the packaged game.
    face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
    face.set_editor_property("hinting", unreal.FontHinting.AUTO)
    face.set_editor_property("layout_method", unreal.FontLayoutMethod.METRICS)
    if not LIB.save_loaded_asset(face, False):
        raise RuntimeError("Failed to save font face: " + weight)
    REPORT["faces"].append({"weight": weight, "path": face.get_path_name(),
                            "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                            "loadingPolicy": "Inline"})

# UE 5.8 does not expose FFontData/Typeface structs to Python. The narrow editor
# helper builds their asset references directly and reads back the saved font.
composition = json.loads(unreal.RiftEditorAssetLibrary.build_ui_font_json())
if not composition["passed"] or composition["errors"]:
    raise RuntimeError("Failed to save composite UI font: " + str(composition["errors"]))
if [entry["weight"] for entry in composition["faces"]] != ["Regular", "Medium", "SemiBold", "Bold"]:
    raise RuntimeError("Saved typeface does not contain all four named weights")
for original, saved in zip(REPORT["faces"], composition["faces"]):
    if saved["path"] != original["path"] or saved["loadingPolicy"] != "Inline" or saved["bytes"] <= 0:
        raise RuntimeError("Saved typeface has a missing or external font face")
REPORT["font"] = composition["font"]
REPORT["passed"] = True
report_path = ROOT / "Artifacts/QA/font-import.json"
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(REPORT, indent=2) + "\n", encoding="utf-8")
unreal.log("Rift UI font import passed: " + REPORT["font"])
