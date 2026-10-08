"""Inspect real FBX-generated physics assets and their on-disk registry records.

Run in UnrealEditor-Cmd with -run=pythonscript -script=<this file> -NullRHI.
No meshes, physics bodies, or gameplay collisions are replaced or removed.
RIFT_PHYSICS_RESCAN=1 performs an explicit synchronous registry rescan.
"""
import json
import os
from pathlib import Path

import unreal

ROOT = Path(os.environ.get("RIFT_REPO_ROOT", Path(__file__).resolve().parents[1]))
MANIFEST = json.loads((ROOT / "Assets/asset_manifest.json").read_text(encoding="utf-8"))
REGISTRY = unreal.AssetRegistryHelpers.get_asset_registry()


def registry_record(path):
    data = REGISTRY.get_asset_by_object_path(path, True)
    return {"valid": data.is_valid(), "package": str(data.package_name),
            "class": str(data.asset_class_path)}


def inspect(card_id):
    mesh_path = f"/Game/Rift/Characters/{card_id}/SK_{card_id}.SK_{card_id}"
    physics_path = f"/Game/Rift/Characters/{card_id}/SK_{card_id}_PhysicsAsset.SK_{card_id}_PhysicsAsset"
    physics_file = ROOT / f"Unreal/RiftCrownArena/Content/Rift/Characters/{card_id}/SK_{card_id}_PhysicsAsset.uasset"
    record = {"id": card_id, "physicsFileExists": physics_file.is_file(),
              "physicsFileBytes": physics_file.stat().st_size if physics_file.is_file() else 0,
              "registryBeforeLoad": registry_record(physics_path)}
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    physics = mesh.get_editor_property("physics_asset") if mesh else None
    record["meshLoadable"] = mesh is not None
    record["physics"] = physics.get_path_name() if physics else None
    record["registryAfterLoad"] = registry_record(physics_path)
    if physics:
        try:
            bodies = physics.get_editor_property("skeletal_body_setups")
            record["bodyCount"] = len(bodies)
            record["constraintCount"] = len(physics.get_editor_property("constraint_setup"))
            record["bodies"] = []
            for body in bodies:
                geometry = body.get_editor_property("agg_geom")
                shapes = {}
                for field in ("sphere_elems", "box_elems", "sphyl_elems", "convex_elems"):
                    shapes[field] = len(geometry.get_editor_property(field))
                record["bodies"].append({"bone": str(body.get_editor_property("bone_name")), "shapes": shapes})
        except Exception as error:
            record["bodyInspectionLimited"] = str(error)
    return record


report = {"schema": 1, "rescanned": False, "characters": [], "errors": []}
before = {card: registry_record(f"/Game/Rift/Characters/{card}/SK_{card}_PhysicsAsset.SK_{card}_PhysicsAsset")
          for card in MANIFEST["characters"]}
report["registryBeforeScan"] = before
if os.environ.get("RIFT_PHYSICS_RESCAN") == "1":
    REGISTRY.scan_paths_synchronous(["/Game/Rift/Characters"], True)
    physics_files = [str(ROOT / f"Unreal/RiftCrownArena/Content/Rift/Characters/{card}/SK_{card}_PhysicsAsset.uasset")
                     for card in MANIFEST["characters"]]
    REGISTRY.scan_files_synchronous(physics_files, True)
    report["rescanned"] = True
for card_id in MANIFEST["characters"]:
    try:
        report["characters"].append(inspect(card_id))
    except Exception as error:
        report["errors"].append(f"{card_id}: {error}")
report["nativeInspection"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
output = ROOT / "Artifacts/QA/unreal_physics_inspection.json"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
unreal.log(f"Physics asset diagnostic saved: {output}; {len(report['errors'])} errors")
if report["errors"]:
    raise RuntimeError("Physics asset inspection could not complete; inspect the JSON")
