"""Master three deployment signatures per playable card, retaining released audio.

Uses pinned CC0 performances and recorded foley, never OS text-to-speech.
Run fetch_audio_sources.py to restore the credited source recordings first.
"""
import hashlib
import json

import generate_audio as a


PROFILES = {
    "dart_gun": "Measured veteran acknowledgements, worn leather and a dry weapon latch",
    "ironclad": "Confident human acknowledgements, fitted armor and shield latch",
    "ember_archer": "Performed fire calls, leather bow grip and restrained ember snap",
    "twin_blades": "Three mischievous creature battlecries and paired blade flicks",
    "boulderback": "Low stone creature calls, gravel weight and settling rock",
    "arc_mage": "Short voiced incantations with a rising rune chord",
    "rambeast": "Compact throaty challenge, horn scrape and hoof stamp",
    "sky_manta": "Breathy gliding creature call and soft harmonic fin shimmer",
    "vampire_bats": "Three recorded bat screeches, staggered wing foley",
    "frost_fang": "Three recorded canine growls with a short cold exhalation",
    "storm_raven": "Recorded crow caw with charged feather crackle",
    "meteor_shards": "Fracturing stone, descending flame and a short celestial warning",
    "archer_tower": "Different wooden deployment latches, bow string and watch bell",
    "bullet_burst": "Three mechanical chambering signatures with a brisk air cut",
    "nova_flask": "Glass stopper, liquid-free crystal spin and a bright alchemical chime",
    "mini_stampede": "Three short scrappy nasal creature grunts and light hoof scrabble",
    "stampede": "Lower war-hog rally grunts, heavier hoofs and a brass collar response",
    "pulverizer": "Three locking mechanisms, compression thump and violet resonance",
    "bone_warden": "Lower performed battlecries, dry ribcage rattle and hammer settling",
    "aether_reaper": "Breathy voiced shadow calls, a scythe whisper and dark resonance",
    "iron_giant": "Deep human acknowledgements through a short metal cavity and shield lock",
    "shadow_surge": "Three brief reversing cloth shadows and low Aether chords",
}


def voice(dst, name, pitch=1, at=.03, gain=.8, limit=1.0, airy=False):
    x = a.source(name).mean(axis=1)
    if pitch != 1:
        x = a.pitched(x, pitch)
    x = a.filtered(a.filtered(x, 100, "highpass"), 6200)
    x = a.fade(x[:round(limit*a.RATE)], .008, .055)
    if airy:
        # Retain the performer's articulation; a quiet breath follows its envelope.
        envelope = a.dsp.sosfilt(a.dsp.butter(1, 35, fs=a.RATE, output="sos"), abs(x))
        rng = a.np.random.default_rng(1800+len(x))
        breath = a.filtered(rng.standard_normal(len(x)), 3300)
        x = x*.64 + breath*envelope*.7
    a.put(dst, x, at, gain)


def recipe(card, v):
    x = a.frames(1.42)
    rate = [1, .96, 1.04][v]
    wood = f"Kenney_impactWood_medium_00{v*2}"
    metal = f"Tinysized_sword-clash-0{v*2+1}"
    mine = f"Kenney_impactMining_00{v*2}"
    cry = f"Fvcalderan_battlecry{v}"
    ack = ["yes0", "yes1", "greet0"][v]
    if card == "dart_gun":
        voice(x,"BrandonSong_"+ack,.94,limit=1.10,airy=True)
        a.layer(x,"Tinysized_quiver-leather-squeeze-01",.18,at=.04,limit=.22)
        a.layer(x,"Tinysized_handcuffs-metal-lock-01",.22,at=.14,pitch=.96,limit=.18)
    elif card == "ironclad":
        voice(x,"BrandonSong_"+ack,.89,limit=.95)
        a.layer(x,metal,.20,limit=.22,pitch=.85)
        a.layer(x,"Tinysized_handcuffs-metal-lock-01",.17,at=.07,limit=.20)
    elif card == "ember_archer":
        voice(x,"Cicifyre_Type2_"+["fire","burn","hellstorm"][v],1.03,limit=1.08)
        a.layer(x,"Tinysized_quiver-leather-squeeze-01",.14,limit=.20)
        a.layer(x,"Tinysized_wood-twigs-break-0"+str(1+v%2),.09,at=.13,limit=.13)
    elif card == "twin_blades":
        voice(x,cry,.86,limit=.85)
        a.layer(x,metal,.12,at=.02,limit=.11,pitch=1.4)
        a.layer(x,"Tinysized_metal-knife-scrape-01",.13,at=.15,limit=.13,pitch=1.3)
    elif card == "boulderback":
        voice(x,f"Ogrebane_monster-{v+1}",.67,limit=.96,gain=.65)
        a.layer(x,mine,.25,pitch=.65,limit=.42)
        a.put(x,a.body(.32,87,40),at=.08,gain=.12)
    elif card == "arc_mage":
        voice(x,"BrandonSong_attackbig"+str(v),.94,limit=.77)
        a.put(x,a.glint(.65,[[294,440,880],[330,494,988],[349,523,1046]][v]),at=.06,gain=1.1)
        a.layer(x,"Tinysized_compressed-air-spray-01",.05,limit=.13)
    elif card == "rambeast":
        voice(x,f"Ogrebane_monster-{v+10}",.82,limit=.65)
        a.layer(x,wood,.26,pitch=.72,limit=.20)
        a.layer(x,"Tinysized_metal-knife-scrape-01",.06,at=.08,limit=.22,pitch=.64)
    elif card == "sky_manta":
        voice(x,f"Ogrebane_monster-{v+14}",1.7,limit=.58,gain=.35,airy=True)
        a.layer(x,"Tinysized_book-page-0"+str(1+v%2),.13,pitch=.72,limit=.34)
        a.put(x,a.glint(.72,[[261,523,784],[294,587,880],[330,659,988]][v]),at=.07,gain=.6)
    elif card == "vampire_bats":
        voice(x,f"AntumDeluge_bat_{v+1:02d}",.78,limit=.65,gain=.5)
        a.layer(x,"Tinysized_book-page-01",.15,at=.02,limit=.19,pitch=1.6)
        a.layer(x,"Tinysized_book-page-02",.09,at=.14,limit=.15,pitch=1.8)
    elif card == "frost_fang":
        voice(x,"Qubodup_dog_"+["growl","grumble","snarl"][v],.94,limit=.85,gain=.55)
        a.put(x,a.air(.35,1830+v,800,4400,decay=9),at=.08,gain=.022)
        a.layer(x,"Kenney_impactGlass_light_000",.07,at=.12,limit=.15,pitch=1.4)
    elif card == "storm_raven":
        # Three distinct caw extracts, rather than three renamed identical files.
        raw = a.source("Zeroisnotnull_crow_caw").mean(axis=1)
        start = round([.0,.31,.61][v]*a.RATE)
        a.put(x,a.fade(a.pitched(raw[start:start+round(.65*a.RATE)],.83),.012,.055),.025,.55)
        a.put(x,a.air(.17,1850+v,1200,5100),at=.08,gain=.026)
        a.put(x,a.glint(.38,[147,294,589+v*5]),at=.13,gain=.30)
    elif card == "meteor_shards":
        a.layer(x,mine,.4,pitch=.84,limit=.35)
        a.layer(x,"Tinysized_wood-twigs-break-0"+str(1+v%2),.18,at=.07,limit=.25,pitch=.75)
        a.put(x,a.air(.44,1860+v,160,2800,swell=True),at=.05,gain=.08)
        a.put(x,a.glint(.55,[196,294,392+v*16]),at=.04,gain=.5)
    elif card == "archer_tower":
        a.layer(x,wood,.48,pitch=.81,limit=.27)
        a.layer(x,"Tinysized_quiver-leather-squeeze-01",.26,at=.07,limit=.22,pitch=rate)
        a.layer(x,"Tinysized_handcuffs-metal-lock-0"+str(1+v%2),.22,at=.17,limit=.18)
        a.put(x,a.glint(.45,[[784,1568],[880,1760],[698,1397]][v]),at=.19,gain=.45)
    elif card == "bullet_burst":
        a.layer(x,"Tinysized_handcuffs-metal-lock-0"+str(1+v%2),.6,pitch=1.08*rate,limit=.22)
        a.layer(x,metal,.12,at=.08,limit=.10,pitch=1.25)
        a.layer(x,"Tinysized_compressed-air-spray-01",.13,at=.11,limit=.14,pitch=1.2)
        a.put(x,a.body(.18,150+v*12,100),at=.04,gain=.09)
    elif card == "nova_flask":
        a.layer(x,"Kenney_impactGlass_light_00"+str(v%2*2),.4,pitch=1.1*rate,limit=.24)
        a.layer(x,"Tinysized_quiver-leather-squeeze-01",.09,at=.02,limit=.15,pitch=1.4)
        a.put(x,a.glint(.62,[[523,1046,1568],[587,1174,1761],[659,1318,1978]][v]),at=.09,gain=.9)
    elif card in ("mini_stampede","stampede"):
        royal = card=="stampede"
        voice(x,f"Ogrebane_monster-{v+7}",1.26 if royal else 1.7,limit=.55,gain=.7)
        a.layer(x,wood,.24 if royal else .17,at=.05,pitch=.83 if royal else 1.23,limit=.19)
        a.layer(x,wood,.15,at=.16+v*.025,pitch=.91 if royal else 1.4,limit=.13)
        if royal: a.layer(x,metal,.075,at=.10,pitch=1.08,limit=.18)
    elif card == "pulverizer":
        a.layer(x,"Tinysized_handcuffs-metal-lock-0"+str(1+v%2),.45,pitch=.68,limit=.28)
        a.layer(x,"Kenney_impactPlate_light_000",.22,at=.10,pitch=.62*rate,limit=.3)
        a.put(x,a.body(.38,72+v*7,39),at=.07,gain=.24)
        a.put(x,a.glint(.55,[146,220+v*4,440]),at=.12,gain=.7)
    elif card == "bone_warden":
        voice(x,"BrandonSong_attackbig"+str(v+3),.69,limit=.85,gain=.65)
        a.layer(x,"Tinysized_wood-twigs-break-0"+str(1+v%2),.3,pitch=1.3,limit=.22)
        a.layer(x,mine,.18,at=.13,pitch=.8,limit=.25)
    elif card == "aether_reaper":
        voice(x,"BrandonSong_attack"+str(v),.80,limit=.82,gain=.7,airy=True)
        a.layer(x,"Tinysized_tube-plastic-whoosh-02",.12,pitch=.76,limit=.26)
        a.put(x,a.glint(.6,[[110,165,330],[123,185,370],[104,156,311]][v]),at=.08,gain=.6)
    elif card == "iron_giant":
        voice(x,"BrandonSong_"+ack,.61,limit=1.08,gain=.72)
        a.layer(x,"Kenney_impactPlate_light_002",.25,limit=.31,pitch=.58)
        a.layer(x,"Tinysized_handcuffs-metal-lock-02",.15,at=.13,limit=.25,pitch=.72*rate)
        a.put(x,a.body(.3,80,38),at=.07,gain=.14)
    elif card == "shadow_surge":
        cloth = a.source("Tinysized_tube-plastic-whoosh-0"+str(1+v%2)).mean(axis=1)
        a.put(x,a.fade(a.pitched(cloth[::-1],.73*rate),.05,.08),gain=.25)
        a.put(x,a.glint(.65,[[98,147,294],[110,165,330],[123,185,370]][v]),at=.08,gain=.7)
    else:
        raise ValueError(card)
    # Trim unused tail, keeping a diffuse release without drawn-out chatter.
    energy = a.np.flatnonzero(a.np.abs(x) > .0003)
    if not len(energy):
        raise RuntimeError("Silent deployment signature: "+card)
    x = x[:min(len(x),int(energy[-1]) + round(.10*a.RATE))]
    return a.room(x,.08 if card not in ("aether_reaper","arc_mage") else .16)


def main():
    manifest_path = a.OUT/"audio_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    original = {name: entry["sha256"] for name,entry in manifest["sounds"].items() if not name.startswith("spawn_")}
    cards = json.loads((a.ROOT/"Assets/Data/card_manifest.json").read_text(encoding="utf-8"))["cards"]
    if {card["id"] for card in cards} != set(PROFILES):
        raise RuntimeError("Deployment audio roster differs from playable cards")
    catalog = {}
    for card,identity in PROFILES.items():
        takes = []
        for v in range(3):
            name = "spawn_"+card+("" if v==0 else f"_v{v+1}")
            a.USED_SOURCES.clear()
            a.source.cache_clear()
            x = recipe(card,v)
            a.write(name,x,peak=.60,rms_db=-23,role="deployment",sources=sorted(a.USED_SOURCES))
            a.MANIFEST["sounds"][name]["productionSource"] = "Build/generate_roster_audio.py"
            a.MANIFEST["sounds"][name]["cardId"] = card
            takes.append(name)
        catalog[card] = {"identity":identity,"takes":takes,"trigger":"one card_play per playId; never entity_spawn"}
    manifest["sounds"].update(a.MANIFEST["sounds"])
    manifest["spawnCueProfiles"] = catalog
    manifest["design"] += "; three recorded deployment signatures per playable card" if "recorded deployment" not in manifest["design"] else ""
    for name,pin in original.items():
        if hashlib.sha256((a.ROOT/manifest["sounds"][name]["file"]).read_bytes()).hexdigest()!=pin:
            raise RuntimeError("Released master changed: "+name)
    manifest_path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8",newline="\n")
    destination=a.ROOT/"Artifacts/QA/RosterAudio";destination.mkdir(parents=True,exist_ok=True)
    report={"schema":1,"existingMastersUnchanged":len(original),"newMasters":len(a.MANIFEST["sounds"]),"roster":catalog,
            "listeningAcceptance":"Not asserted by numeric authoring; audition reel supplied by verify_roster_audio.py"}
    (destination/"production.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({"existingMastersUnchanged":len(original),"newMasters":len(a.MANIFEST["sounds"]),"cards":len(catalog)}))


if __name__=="__main__":
    main()
