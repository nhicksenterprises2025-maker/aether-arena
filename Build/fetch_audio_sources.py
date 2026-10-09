"""Fetch the small, pinned CC0 palette used by generate_audio.py.

This is an authoring step, never a dependency of the packaged game. Existing
files are hash-verified and retained. Source provenance ships beside the files.
"""
from pathlib import Path
import hashlib
import json
import shutil
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Assets/Source/Audio/Palette"
CACHE = ROOT / "Build/Tools/AudioDownloads"


def download(url, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        request = urllib.request.Request(url, headers={"User-Agent": "RiftCrownAudioAuthoring/1.1"})
        with urllib.request.urlopen(request, timeout=60) as reply, target.open("wb") as stream:
            shutil.copyfileobj(reply, stream)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    prior_path = OUT / "sources.json"
    if prior_path.exists():
        prior = json.loads(prior_path.read_text(encoding="utf-8"))
        for record in prior["files"]:
            target = OUT / record["file"]
            if not target.exists() and "archiveMember" in record:
                archive = CACHE / (record["author"] + ".zip")
                download(record["url"], archive)
                with zipfile.ZipFile(archive) as source:
                    target.write_bytes(source.read(record["archiveMember"]))
            else:
                download(record["url"], target)
            if hashlib.sha256(target.read_bytes()).hexdigest() != record["sha256"]:
                raise RuntimeError(f"Upstream/source mismatch: {target.name}")
        print(f"Verified {len(prior['files'])} pinned CC0 source samples")
        return

    packs = [
        ("Kenney", "https://kenney.nl/media/pages/assets/impact-sounds/87b4ddecda-1677589768/kenney_impact-sounds.zip", "https://kenney.nl/assets/impact-sounds", [
            "impactMetal_medium_002", "impactPlate_light_000", "impactPlate_light_002",
            "impactMining_000", "impactMining_002", "impactMining_004",
            "impactWood_medium_000", "impactWood_medium_002", "impactWood_medium_004",
            "impactGlass_light_000", "impactGlass_light_002", "impactGlass_heavy_000",
            "impactSoft_heavy_000",
        ]),
        ("Tinysized", "https://opengameart.org/sites/default/files/tinysized.zip", "https://opengameart.org/content/fantasy-sound-effects-tinysized-sfx", [
            "sword-clash-01", "sword-clash-03", "sword-clash-05",
            "tin-whistle-whoosh-01", "tube-plastic-whoosh-01", "tube-plastic-whoosh-02",
            "book-page-01", "book-page-02", "handcuffs-metal-lock-01", "handcuffs-metal-lock-02",
            "arrow-feathers-01", "quiver-leather-squeeze-01",
            "wood-twigs-break-01", "wood-twigs-break-02", "apple-cut-01", "water-pour-01",
            "compressed-air-spray-01", "metal-knife-scrape-01",
        ]),
    ]
    records = []
    for author, url, page, stems in packs:
        archive = CACHE / (author + ".zip")
        download(url, archive)
        with zipfile.ZipFile(archive) as source:
            for stem in stems:
                member = next(name for name in source.namelist() if Path(name).stem == stem)
                target = OUT / (author + "_" + Path(member).name)
                target.write_bytes(source.read(member))
                records.append({"file": target.name, "author": author, "license": "CC0-1.0", "page": page, "url": url, "archiveMember": member, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})

    commit_url = "https://api.github.com/repos/sgossner/VSCO-2-CE/commits/master"
    with urllib.request.urlopen(urllib.request.Request(commit_url, headers={"User-Agent": "RiftCrownAudioAuthoring/1.1"})) as reply:
        commit = json.load(reply)["sha"]
    paths = [
        "Strings/Violin Section/Spic/VlnEns_Spic_A3_v1_rr1.wav",
        "Strings/Violin Section/Spic/VlnEns_Spic_A3_v1_rr2.wav",
        "Strings/Violin Section/Spic/VlnEns_Spic_C4_v1_rr1.wav",
        "Strings/Violin Section/Spic/VlnEns_Spic_D3_v1_rr1.wav",
        "Strings/Violin Section/Spic/VlnEns_Spic_F#3_v1_rr1.wav",
        "Strings/Violin Section/susVib/VlnEns_susVib_A3_v1.wav",
        "Strings/Cello Section/susvib/susvib_D2_v1_1.wav",
        "Brass/F Horn/sus/MOHorn_sus_D2_v1_1.wav",
        "Brass/F Horn/sus/MOHorn_sus_A2_v1_1.wav",
        "Strings/Harp/KSHarp_D4_mf.wav", "Strings/Harp/KSHarp_A4_mf.wav",
        "Strings/Harp/KSHarp_C5_mf.wav", "Strings/Harp/KSHarp_F4_mf.wav",
        "Percussion/Glock/glock_medium_C5.wav",
        "Percussion/Timpani/Timpani3_Hit_v3_rr1_Sum.wav",
        "Percussion/Timpani/Timpani3_Hit_v1_rr1_Sum.wav",
        "Percussion/Snare2-HitSN_v3_rr1_Sum.wav",
    ]
    for path in paths:
        url = f"https://raw.githubusercontent.com/sgossner/VSCO-2-CE/{commit}/" + urllib.parse.quote(path)
        target = OUT / ("VSCO_" + Path(path).name)
        download(url, target)
        records.append({"file": target.name, "author": "Versilian Studios / Sam Gossner / Simon Dalzell", "license": "CC0-1.0", "page": "https://versilian-studios.com/vsco-community/", "url": url, "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
    download(f"https://raw.githubusercontent.com/sgossner/VSCO-2-CE/{commit}/LICENSE", OUT / "LICENSE-VSCO.txt")
    prior_path.write_text(json.dumps({"schemaVersion": 1, "license": "CC0-1.0", "files": records}, indent=2) + "\n", encoding="utf-8")
    print(f"Fetched {len(records)} CC0 palette samples")


if __name__ == "__main__":
    main()
