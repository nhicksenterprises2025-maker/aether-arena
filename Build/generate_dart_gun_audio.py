"""Add Dart Gun's five short cues without rewriting any released sound master."""
import hashlib
import json

import generate_audio as a
from generate_roster_audio import PROFILES, recipe


def main():
    manifest_path = a.OUT / "audio_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    names = ["spawn_dart_gun", "spawn_dart_gun_v2", "spawn_dart_gun_v3", "dart_fire", "dart_hit"]
    preserved = {name: entry for name, entry in manifest["sounds"].items() if name not in names}
    for variation, name in enumerate(names[:3]):
        a.USED_SOURCES.clear()
        a.source.cache_clear()
        a.write(name, recipe("dart_gun", variation), peak=.60, rms_db=-23,
                role="deployment", sources=sorted(a.USED_SOURCES))
        a.MANIFEST["sounds"][name]["cardId"] = "dart_gun"
    for name in names[3:]:
        a.USED_SOURCES.clear()
        a.source.cache_clear()
        x = a.frames(.18 if name == "dart_fire" else .16)
        if name == "dart_fire":
            a.layer(x, "Tinysized_handcuffs-metal-lock-01", .30, pitch=1.5, limit=.075)
            a.layer(x, "Tinysized_arrow-feathers-01", .25, at=.012, pitch=1.8, limit=.11)
            a.put(x, a.air(.12, 1901, 600, 6000, decay=13), at=.008, gain=.055)
        else:
            a.layer(x, "Kenney_impactWood_medium_002", .55, pitch=1.65, limit=.10)
            a.layer(x, "Tinysized_arrow-feathers-01", .09, pitch=1.7, limit=.09)
        a.write(name, x, peak=.42, rms_db=-26, sources=sorted(a.USED_SOURCES))
    for entry in a.MANIFEST["sounds"].values():
        entry["productionSource"] = "Build/generate_dart_gun_audio.py"
    manifest["sounds"].update(a.MANIFEST["sounds"])
    manifest["spawnCueProfiles"]["dart_gun"] = {
        "identity": PROFILES["dart_gun"], "takes": names[:3],
        "trigger": "one card_play per playId; never entity_spawn"}
    for name, entry in preserved.items():
        if hashlib.sha256((a.ROOT / entry["file"]).read_bytes()).hexdigest() != entry["sha256"]:
            raise RuntimeError("Released master changed: " + name)
        if manifest["sounds"][name] != entry:
            raise RuntimeError("Released audio metadata changed: " + name)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    report = {"passed": True, "preservedMasters": len(preserved),
              "newMasters": a.MANIFEST["sounds"], "listeningAcceptance": "Not asserted by numerical authoring"}
    report_path = a.ROOT / "Artifacts/QA/DartGun190/Audio/production.json"
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"preservedMasters": len(preserved), "newMasters": len(names)}))


if __name__ == "__main__":
    main()
