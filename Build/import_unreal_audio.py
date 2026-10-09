"""Replace only cooked-source SoundWaves after generate_audio.py.

Invoke with UnrealEditor-Cmd -run=pythonscript -script=<absolute script path>.
This script never imports meshes/materials or touches presentation geometry.
"""
from pathlib import Path
import json
import unreal

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Assets/Source/Audio"
manifest = json.loads((SOURCE / "audio_manifest.json").read_text(encoding="utf-8"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.EditorAssetLibrary
report = {"schemaVersion": 1, "masterCount": len(manifest["sounds"]), "sounds": [], "errors": []}
for name, data in manifest["sounds"].items():
    task = unreal.AssetImportTask()
    task.filename = str(ROOT / data["file"])
    task.destination_path = "/Game/Rift/Audio"
    task.destination_name = "SFX_" + name
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = False
    tools.import_asset_tasks([task])
    sound = library.load_asset("/Game/Rift/Audio/SFX_" + name)
    if not isinstance(sound, unreal.SoundWave):
        raise RuntimeError("Audio import failed: " + name)
    sound.set_editor_property("looping", data["loop"])
    sound.set_editor_property("compression_quality", 90)
    sound.set_editor_property("priority", 100.0 if data["loop"] else 65.0 if data["role"] in ("announcement", "interface") else 40.0)
    sound.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.RETAIN_ON_LOAD if data["loop"] else unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
    if not library.save_loaded_asset(sound, False):
        raise RuntimeError("Audio save failed: " + name)
    report["sounds"].append({"name": name, "asset": sound.get_path_name(), "sourceSha256": data["sha256"], "channels": sound.get_editor_property("num_channels"), "duration": sound.get_editor_property("duration")})
target = ROOT / "Artifacts/QA/Audio/audio-import.json"
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
unreal.log("Rift audio import: " + str(len(report["sounds"])) + " mastered SoundWaves")
