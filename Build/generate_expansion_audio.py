"""Append two short authored expansion cues without rewriting released masters."""
import hashlib
import json
from pathlib import Path

import generate_audio as audio


def main():
    manifest_path = audio.OUT / "audio_manifest.json"
    original = json.loads(manifest_path.read_text(encoding="utf-8"))
    old_pins = {name: hashlib.sha256((audio.ROOT / item["file"]).read_bytes()).hexdigest()
                for name, item in original["sounds"].items() if name not in ("shield_break", "shadow_surge")}
    shield = audio.frames(.50)
    audio.layer(shield, "Kenney_impactPlate_light_002", .66, pitch=.72, limit=.27)
    audio.layer(shield, "Tinysized_sword-clash-03", .38, at=.035, pitch=.83, limit=.32)
    audio.layer(shield, "Tinysized_handcuffs-metal-lock-02", .23, at=.13, pitch=.70, limit=.18)
    audio.put(shield, audio.body(.26, 116, 57), gain=.11)
    audio.write("shield_break", audio.room(shield, .10), peak=.60, rms_db=-24)
    veil = audio.frames(.42, stereo=True)
    audio.layer(veil, "Tinysized_tube-plastic-whoosh-02", .42, pitch=.72, limit=.29)
    audio.put(veil, audio.glint(.32, [196, 294, 587], seed=1606), at=.03, gain=.35)
    audio.put(veil, audio.air(.28, 1607, 160, 1800, decay=12), gain=.024)
    audio.write("shadow_surge", audio.room(veil, .14), peak=.46, rms_db=-28)
    for name, item in audio.MANIFEST["sounds"].items():
        item["productionSource"] = "Build/generate_expansion_audio.py"
        original["sounds"][name] = item
    for name, expected in old_pins.items():
        if hashlib.sha256((audio.ROOT / original["sounds"][name]["file"]).read_bytes()).hexdigest() != expected:
            raise RuntimeError("Released audio master changed: " + name)
    manifest_path.write_text(json.dumps(original, indent=2) + "\n", encoding="utf-8")
    report = {"schema": 1, "passed": True, "existingMastersUnchanged": len(old_pins),
              "newCues": audio.MANIFEST["sounds"], "actualNumericMeasurements": True,
              "listeningAcceptance": False}
    destination = audio.ROOT / "Artifacts/QA/ExpansionAudio"
    destination.mkdir(parents=True, exist_ok=True)
    suffix = hashlib.sha256(manifest_path.read_bytes()).hexdigest()[:12]
    (destination / ("source-production-" + suffix + ".json")).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": True, "existingMastersUnchanged": len(old_pins), "newCues": list(audio.MANIFEST["sounds"])}))


if __name__ == "__main__":
    main()
