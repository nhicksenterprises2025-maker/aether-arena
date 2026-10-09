"""Render Rift's recorded foley and original orchestral arrangement.

Authoring dependencies: numpy, scipy, soundfile (requirements-audio.txt).
Run fetch_audio_sources.py once to restore the pinned, credited CC0 palette.
The game loads cooked SoundWaves; Python is never needed to play.
"""
from pathlib import Path
from functools import lru_cache
import hashlib
import json
import math
import sys

ROOT = Path(__file__).resolve().parents[1]
LOCAL_DEPS = ROOT / "Build/Tools/AudioPython"
if LOCAL_DEPS.exists():
    sys.path.insert(0, str(LOCAL_DEPS))
import numpy as np
import soundfile as sf
from scipy import signal as dsp

OUT = ROOT / "Assets/Source/Audio"
PALETTE = OUT / "Palette"
RATE = 44100
TAU = math.tau
MANIFEST = {"schemaVersion": 2, "originalSource": "Build/generate_audio.py", "sampleRate": RATE,
            "palette": "Assets/Source/Audio/Palette/sources.json", "design": "Recorded material layers; short transients; three combat variations; original 16-bar orchestral score", "sounds": {}}
USED_SOURCES=set()


def frames(seconds, stereo=False):
    return np.zeros((round(seconds * RATE), 2) if stereo else round(seconds * RATE), dtype=np.float64)


def fade(x, attack=.003, release=.06):
    x = x.copy()
    a, r = min(len(x), round(attack*RATE)), min(len(x), round(release*RATE))
    if a:
        envelope = np.sin(np.linspace(0, math.pi/2, a))**2
        x[:a] *= envelope[:, None] if x.ndim == 2 else envelope
    if r:
        envelope = np.cos(np.linspace(0, math.pi/2, r))**2
        x[-r:] *= envelope[:, None] if x.ndim == 2 else envelope
    return x


def filtered(x, hz, mode="lowpass", order=2, cyclic=False):
    coefficients=dsp.butter(order, hz, btype=mode, fs=RATE, output="sos")
    if cyclic:
        warm=min(len(x),RATE)
        return dsp.sosfilt(coefficients,np.concatenate((x[-warm:],x)),axis=0)[warm:]
    return dsp.sosfilt(coefficients,x,axis=0)


@lru_cache(maxsize=128)
def source(name):
    path = next(PALETTE.glob(name + ".*"), None)
    if path is None:
        raise FileNotFoundError(f"Missing CC0 palette source {name}; run Build/fetch_audio_sources.py")
    USED_SOURCES.add(path.name)
    x, rate = sf.read(path, dtype="float64", always_2d=True)
    if rate != RATE:
        x = dsp.resample_poly(x, RATE, rate, axis=0)
    x = filtered(x, 38, "highpass")
    energy = np.max(np.abs(x), axis=1)
    audible = np.flatnonzero(energy > max(.0001, np.max(energy)*.012))
    if len(audible):
        x = x[max(0, audible[0]-round(.008*RATE)):min(len(x), audible[-1]+round(.07*RATE))]
    x *= .6 / max(.001, np.max(np.abs(x)))
    return fade(x, .002, .035)


def pitched(x, ratio):
    # Polyphase resampling avoids high-frequency images on lowered foley.
    denominator = 10000
    numerator = max(1, round(denominator / ratio))
    return dsp.resample_poly(x, numerator, denominator, axis=0)


def put(dst, x, at=0, gain=1, pan=0, cyclic=False):
    if dst.ndim == 1 and x.ndim == 2:
        x = np.mean(x, axis=1)
    if dst.ndim == 2:
        if x.ndim == 1:
            x = np.column_stack((x, x))
        x = x * np.array([math.sqrt(1-max(0,pan)), math.sqrt(1+min(0,pan))])
    start = round(at * RATE)
    if cyclic:
        for begin in range(0, len(x), len(dst)):
            chunk = x[begin:begin+len(dst)]
            positions = (start + begin + np.arange(len(chunk))) % len(dst)
            np.add.at(dst, positions, chunk*gain)
    else:
        if start < 0:
            x, start = x[-start:], 0
        n = min(len(x), len(dst)-start)
        if n > 0:
            dst[start:start+n] += x[:n]*gain


def layer(dst, name, gain=1, at=0, pitch=1, limit=None, pan=0):
    x = pitched(source(name), pitch) if pitch != 1 else source(name)
    if limit is not None:
        x = fade(x[:round(limit*RATE)], .002, min(.06, limit/4))
    put(dst, x, at, gain, pan)


def air(seconds, seed, low=180, high=3500, decay=8, swell=False):
    rng = np.random.default_rng(seed)
    x = filtered(rng.standard_normal(round(seconds*RATE)), low, "highpass")
    x = filtered(x, high)
    t = np.linspace(0, 1, len(x))
    env = np.sin(math.pi*t)**1.6 if swell else np.exp(-decay*t)
    return fade(x*env, .006, .05)


def body(seconds, hz=90, end=42, strength=1):
    t = np.arange(round(seconds*RATE))/RATE
    f = end+(hz-end)*np.exp(-t*18)
    x = np.sin(TAU*np.cumsum(f)/RATE) * np.exp(-t*9) * strength
    return fade(x, .002, .04)


def glint(seconds, frequencies, seed=11):
    t = np.arange(round(seconds*RATE))/RATE
    rng = np.random.default_rng(seed)
    x = np.zeros(len(t))
    for i, hz in enumerate(frequencies):
        x += np.sin(TAU*hz*t+rng.uniform(-.12,.12))*np.exp(-t*(7+i*2))/(i+1)
    return fade(x*.045, .004, .08)


def room(x, wet=.14, loop=False):
    # A short diffuse tail, avoiding conspicuous rhythmic combat echoes.
    result = x.copy()
    for index, (delay, amount) in enumerate(((.023,.34),(.041,.28),(.071,.19),(.109,.13),(.157,.09),(.227,.055))):
        tail = filtered(x, max(1600, 6500-index*680), cyclic=loop)
        if x.ndim == 2 and index % 2:
            tail = tail[:, ::-1]
        put(result, tail, delay, amount*wet, cyclic=loop)
    return result


def write(name, x, peak=.62, rms_db=-23, loop=False, role="combat", sources=None):
    x = filtered(x, 35, "highpass",cyclic=loop)
    x -= np.mean(x, axis=0)
    if not loop:
        x = fade(x, .002, .055)
    rms = math.sqrt(float(np.mean(x*x)))
    level = 10**(rms_db/20)
    x *= min(level/max(rms,1e-6), peak/max(float(np.max(np.abs(x))),1e-6))
    # Preserve transient crest and >3 dB headroom before the bounded mix.
    if loop:
        n = round(.003*RATE)
        difference = x[0]-x[-1]
        x[-n:] += np.linspace(0,1,n).reshape((n,1) if x.ndim==2 else (n,))*difference
    path = OUT / (name + ".wav")
    sf.write(path, x, RATE, subtype="PCM_16")
    decoded,_ = sf.read(path, always_2d=True)
    MANIFEST["sounds"][name] = {"file": path.relative_to(ROOT).as_posix(), "duration": len(x)/RATE,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "loop": loop, "channels": decoded.shape[1],
        "peak": float(np.max(np.abs(decoded))), "rmsDbFS": 20*math.log10(max(1e-8,float(np.sqrt(np.mean(decoded*decoded))))),
        "dcOffset": float(np.max(np.abs(np.mean(decoded,axis=0)))), "role": role,
        "sources": sources or ["Recorded CC0 palette and original synthesis; see Palette/sources.json"]}


def foley_cues():
    # Different recorded takes, retaining the original base name compatibility.
    for variation in range(3):
        suffix = "" if variation==0 else f"_v{variation+1}"
        take = [0,2,4][variation]
        metal = [1,3,5][variation]
        rate = [1,.965,1.035][variation]
        def render(name, duration, recipe, rms=-23, peak=.66):
            x=frames(duration); recipe(x); write(name+suffix, room(x,.13), peak,rms)
        render("sword_hit",.46,lambda x:(layer(x,f"Tinysized_sword-clash-0{metal}",.62,pitch=.87*rate,limit=.3),layer(x,f"Kenney_impactWood_medium_00{take}",.58,limit=.14),put(x,body(.22,112,63),gain=.15)), -22)
        render("shield_hit",.52,lambda x:(layer(x,f"Kenney_impactPlate_light_00{[0,2,0][variation]}",.56,pitch=.88*rate,limit=.32),layer(x,f"Tinysized_sword-clash-0{metal}",.4,limit=.24),put(x,body(.22,123,72),gain=.19)), -23)
        render("tower_hit",.65,lambda x:(layer(x,f"Kenney_impactMining_00{take}",.9,pitch=.8*rate,limit=.46),layer(x,f"Kenney_impactWood_medium_00{take}",.36,pitch=.8,limit=.3),put(x,body(.34,94,46),gain=.3)), -21)
        render("building_hit",.5,lambda x:(layer(x,f"Kenney_impactWood_medium_00{take}",.95,pitch=.81*rate,limit=.35),layer(x,"Tinysized_wood-twigs-break-01",.18,at=.025,limit=.23),put(x,body(.28,98,55),gain=.19)), -22)
        render("arrow_hit",.34,lambda x:(layer(x,f"Kenney_impactWood_medium_00{take}",.72,pitch=1.32*rate,limit=.14),layer(x,"Tinysized_arrow-feathers-01",.14,limit=.19),put(x,body(.2,176,91),gain=.1)), -24)
        render("sword_attack",.31,lambda x:(layer(x,"Tinysized_tube-plastic-whoosh-0"+str(variation%2+1),.7,pitch=1.18*rate,limit=.27),layer(x,"Tinysized_metal-knife-scrape-01",.08,limit=.09)), -25,.46)
        render("dual_attack",.41,lambda x:(layer(x,"Tinysized_tin-whistle-whoosh-01",.53,pitch=1.28*rate,limit=.17),layer(x,"Tinysized_tube-plastic-whoosh-02",.6,at=.085,pitch=1.4,limit=.19)), -25,.48)
        render("bow_release",.36,lambda x:(layer(x,"Tinysized_quiver-leather-squeeze-01",.27,limit=.16),layer(x,"Tinysized_wood-twigs-break-02",.68,at=.016,pitch=1.23*rate,limit=.12),layer(x,"Tinysized_arrow-feathers-01",.35,at=.035,pitch=1.55,limit=.21)), -23)
        render("heavy_slam",.86,lambda x:(layer(x,f"Kenney_impactMining_00{take}",.9,pitch=.67*rate,limit=.56),layer(x,"Kenney_impactSoft_heavy_000",.75,pitch=.72,limit=.3),put(x,body(.48,82,39),gain=.42),put(x,air(.6,33+variation,60,1600),at=.03,gain=.075)), -20,.7)
        render("bat_bite",.23,lambda x:(layer(x,"Tinysized_apple-cut-01",.6,pitch=1.4*rate,limit=.14),layer(x,"Tinysized_book-page-01",.15,pitch=1.6,limit=.12)), -28,.38)
    for name,duration,recipe,rms in [
        ("arrow_flight",.3,lambda x:layer(x,"Tinysized_arrow-feathers-01",.65,pitch=1.5,limit=.25),-27),
        ("charge",.65,lambda x:(layer(x,"Tinysized_tube-plastic-whoosh-02",.85,pitch=.64,limit=.5),put(x,body(.4,73,43),at=.08,gain=.3)),-23),
        ("wing_air",.65,lambda x:(layer(x,"Tinysized_book-page-01",.35,pitch=.75,limit=.28),layer(x,"Tinysized_book-page-02",.28,at=.2,pitch=.78,limit=.29)),-29),
    ]:
        x=frames(duration);recipe(x);write(name,room(x,.1),.55,rms)


def magic_cues():
    def spell(name, duration, recipe, rms=-23):
        x=frames(duration,stereo=True);recipe(x);write(name,room(x,.3),.7,rms)
    spell("arc_cast",.48,lambda x:(layer(x,"Tinysized_tube-plastic-whoosh-01",.48,pitch=1.2,limit=.25),put(x,glint(.4,[587,1174,1735]),gain=.9),put(x,air(.3,71,700,4400,swell=True),gain=.065)),-25)
    spell("arc_impact",.56,lambda x:(layer(x,"Kenney_impactGlass_light_000",.37,pitch=.78,limit=.24),put(x,body(.3,162,78),gain=.34),put(x,air(.38,72,360,4700),gain=.1),put(x,glint(.46,[587,1183,2382]),at=.02,gain=.7)))
    spell("manta_cast",.6,lambda x:(layer(x,"Tinysized_tube-plastic-whoosh-02",.48,pitch=.87,limit=.36),put(x,glint(.5,[392,785,1561]),gain=.65)),-26)
    spell("manta_impact",.68,lambda x:(put(x,body(.34,146,54),gain=.31),layer(x,"Kenney_impactSoft_heavy_000",.68,limit=.2),put(x,air(.44,81,130,3300),gain=.09),put(x,glint(.6,[392,791,1537]),gain=.65)))
    spell("frost_attack",.58,lambda x:(layer(x,"Tinysized_compressed-air-spray-01",.17,pitch=.93,limit=.4),layer(x,"Kenney_impactGlass_light_002",.32,at=.055,pitch=1.1,limit=.24),put(x,air(.46,95,900,5400,swell=True),gain=.065)),-25)
    spell("frost_slow",.32,lambda x:(layer(x,"Kenney_impactGlass_light_000",.28,pitch=1.45,limit=.2),put(x,glint(.27,[1174,2379,3503]),gain=.42)),-30)
    spell("storm_bolt",.52,lambda x:(layer(x,"Kenney_impactPlate_light_002",.18,pitch=1.4,limit=.16),put(x,air(.19,103,400,5800),gain=.26),put(x,body(.31,124,49),gain=.38)),-22)
    spell("storm_pulse",.85,lambda x:(put(x,body(.5,106,34),gain=.6),put(x,air(.4,107,180,3700),gain=.24),layer(x,"Kenney_impactMetal_medium_002",.22,pitch=.64,limit=.48),put(x,glint(.6,[293,591,884]),gain=.5)),-21)
    spell("stun",.28,lambda x:(put(x,air(.2,109,450,4900),gain=.18),layer(x,"Tinysized_handcuffs-metal-lock-01",.22,pitch=1.13,limit=.14)),-28)
    spell("nova_impact",1.1,lambda x:(layer(x,"Kenney_impactGlass_heavy_000",.5,pitch=.7,limit=.42),put(x,body(.65,84,37),gain=.64),put(x,air(.65,113,80,2800),gain=.28),put(x,glint(.9,[294,443,887]),at=.05,gain=.65)),-20)
    spell("deploy",.44,lambda x:(layer(x,"Tinysized_book-page-01",.3,pitch=1.15,limit=.18),layer(x,"Kenney_impactSoft_heavy_000",.54,at=.055,pitch=1.2,limit=.21),put(x,glint(.35,[440,881]),at=.055,gain=.36)),-25)
    spell("meteor_tick",.48,lambda x:(layer(x,"Tinysized_wood-twigs-break-02",.4,pitch=.58,limit=.26),put(x,body(.28,72,42),gain=.32),put(x,air(.36,149,70,1350),gain=.09)),-27)
    spell("meteor_impact",1.75,lambda x:(layer(x,"Tinysized_tube-plastic-whoosh-01",.4,pitch=.61,limit=.23),layer(x,"Kenney_impactMining_002",.9,at=.08,pitch=.54,limit=.7),layer(x,"Tinysized_wood-twigs-break-01",.55,at=.17,pitch=.59,limit=.68),put(x,body(.85,94,28),at=.08,gain=.88),put(x,air(1.35,139,55,2600),at=.08,gain=.31)),-19)
    x=frames(.64)
    for i in range(7):
        layer(x,"Tinysized_handcuffs-metal-lock-02",.33,at=i*.045,pitch=1.2+(i%3)*.08,limit=.08)
        put(x,body(.12,165,81),at=i*.045,gain=.18)
        put(x,air(.08,127+i,900,4800),at=i*.045,gain=.12)
    write("bullet_burst",room(x,.12),.64,-22)
    x=frames(2.05,True)
    layer(x,"Kenney_impactMining_004",.9,pitch=.55,limit=.85)
    put(x,body(1.3,72,27),gain=.88)
    for i in range(9):
        layer(x,"Tinysized_wood-twigs-break-0"+str(i%2+1),.24/(1+i*.18),at=.1+i*.115,pitch=.63+i*.025,limit=.27,pan=(-.6 if i%2 else .6))
    put(x,air(1.7,59,55,1900),gain=.18)
    write("tower_destroy",room(x,.35),.73,-19)


def ui_cues():
    for name,duration,recipe,rms in [
        ("ui_click",.15,lambda x:(layer(x,"Kenney_impactWood_medium_000",.35,pitch=1.85,limit=.08),layer(x,"Tinysized_handcuffs-metal-lock-01",.17,pitch=1.35,limit=.08)),-27),
        ("ui_hover",.11,lambda x:layer(x,"Tinysized_book-page-01",.3,pitch=1.4,limit=.09),-34),
        ("ui_error",.25,lambda x:(layer(x,"Kenney_impactWood_medium_002",.47,pitch=.83,limit=.1),layer(x,"Kenney_impactWood_medium_002",.25,at=.085,pitch=.74,limit=.11)),-26),
        ("ui_save",.62,lambda x:(layer(x,"Tinysized_book-page-02",.16,pitch=1.25,limit=.13),put(x,instrument("harp",74,.4),at=.035,gain=.27),put(x,instrument("harp",81,.4),at=.14,gain=.22)),-27),
    ]:
        x=frames(duration,True);recipe(x);write(name,room(x,.12),.5,rms,role="interface")


INSTRUMENTS = {
    "spic": [(69,"VlnEns_Spic_A3_v1_rr1"),(69,"VlnEns_Spic_A3_v1_rr2"),(72,"VlnEns_Spic_C4_v1_rr1"),(62,"VlnEns_Spic_D3_v1_rr1"),(66,"VlnEns_Spic_F#3_v1_rr1")],
    "violin": [(69,"VlnEns_susVib_A3_v1")],
    "cello": [(50,"susvib_D2_v1_1")],
    "horn": [(50,"MOHorn_sus_D2_v1_1"),(57,"MOHorn_sus_A2_v1_1")],
    "harp": [(74,"KSHarp_D4_mf"),(81,"KSHarp_A4_mf"),(84,"KSHarp_C5_mf"),(77,"KSHarp_F4_mf")],
    "glock": [(84,"glock_medium_C5")],
}


@lru_cache(maxsize=512)
def instrument(kind, note, seconds, variation=0):
    choices=INSTRUMENTS[kind]
    distance=min(abs(root-note) for root,_ in choices)
    candidates=[(root,name) for root,name in choices if abs(root-note)==distance]
    root,name=candidates[variation%len(candidates)]
    x=pitched(source("VSCO_"+name),2**((note-root)/12))
    duration=round(seconds*RATE)
    if len(x)>duration:
        x=fade(x[:duration], .012 if kind in ("horn","violin","cello") else .002, min(.18,seconds*.18))
    return x


def orchestra():
    beat=60/108
    seconds=16*4*beat
    x=frames(seconds,True)
    chords=[(50,53,57),(46,50,53),(48,53,57),(48,52,55),
            (50,53,57),(46,50,53),(48,52,55),(45,49,52),
            (50,53,57),(46,50,53),(48,53,57),(48,52,55),
            (46,50,53),(48,52,55),(45,49,52),(45,49,52)]
    melody=[[(74,0,1.4),(77,1.5,.4),(76,2,1.7)],[(74,0,1.5),(72,2,1.5)],
            [(69,0,1),(72,1,1),(74,2,1.6)],[(76,0,2.4),(72,3,.8)],
            [(74,0,1),(77,1,1),(81,2,1.6)],[(79,0,1.5),(77,2,1.6)],
            [(76,0,1),(74,1,1),(72,2,1.5)],[(73,0,3.4)]]
    for bar,chord in enumerate(chords):
        at=bar*4*beat
        strength=.86 if bar<8 else 1.0
        for i,note in enumerate(chord):
            put(x,instrument("violin",note+12,round(beat*3.8,3)),at+.025*i,.13*strength,pan=-.36+i*.16,cyclic=True)
        put(x,instrument("cello",chord[0],round(beat*3.85,3)),at,.23*strength,pan=.32,cyclic=True)
        for step,index in enumerate((0,2,1,2,0,2,1,2)):
            note=chord[index]+12
            offset=(.003 if step%2 else -.004)
            put(x,instrument("spic",note,.33,step%2),at+step*beat/2+offset,(.09 if step%2 else .13)*strength,pan=-.4,cyclic=True)
            if step%2==0:
                put(x,instrument("harp",note+12,.9),at+step*beat/2+.013,.095,pan=.43,cyclic=True)
        for note,start,duration in melody[bar%8]:
            put(x,instrument("horn",note-12,round(duration*beat,3)),at+start*beat+.016,.29*strength,pan=-.12,cyclic=True)
        for pulse in (0,2.5):
            put(x,source("VSCO_Timpani3_Hit_v1_rr1_Sum"),at+pulse*beat,.16 if pulse else .22,pan=.13,cyclic=True)
        if bar>=8:
            put(x,source("VSCO_Snare2-HitSN_v3_rr1_Sum"),at+3*beat,.09,pan=.28,cyclic=True)
        if bar in (0,4,8,12):
            put(x,instrument("glock",86,1.3),at+beat,.085,pan=.35,cyclic=True)
    x=room(x,.48,True)
    write("music_rift",x,.58,-22,True,"music",["Original Rift 16-bar composition; recorded VSCO 2 CE chamber orchestra (CC0)"])

    def fanfare(name, notes, duration=2.4):
        y=frames(duration,True)
        for i,note in enumerate(notes):
            start=i*.18
            put(y,instrument("horn",note,.85 if i<len(notes)-1 else 1.3),start,.48,pan=-.14)
            put(y,instrument("spic",note+12,.4),start,.18,pan=-.3)
            put(y,instrument("harp",note+24,1),start,.15,pan=.4)
        put(y,source("VSCO_Timpani3_Hit_v3_rr1_Sum"),.015,.27)
        if name=="victory":
            for note in (62,66,69):
                put(y,instrument("violin",note,1.2),.67,.16,pan=-.25)
        write(name,room(y,.45),.7,-22,role="announcement")
    fanfare("victory",[50,57,62,66],2.5)
    fanfare("defeat",[53,50,45],2.4)
    fanfare("overtime",[50,57,62],2.3)
    fanfare("tiebreaker",[45,49,52],2.3)
    fanfare("aether_two",[62,69],1.65)
    fanfare("aether_three",[62,69,74],1.9)
    fanfare("core_awaken",[38,45,50],2.5)


def ambience():
    x=frames(12,True)
    water=filtered(source("Tinysized_water-pour-01"),4800)
    for i in range(9):
        put(x,water,i*1.37,.35,pan=(-.55 if i%2 else .55),cyclic=True)
    write("river_ambience",room(x,.22,True),.28,-31,True,"ambience")


def main():
    if not (PALETTE/"sources.json").exists():
        raise RuntimeError("Run Build/fetch_audio_sources.py to restore the CC0 palette")
    for entry in json.loads((PALETTE/"sources.json").read_text(encoding="utf-8"))["files"]:
        if hashlib.sha256((PALETTE/entry["file"]).read_bytes()).hexdigest()!=entry["sha256"]:
            raise RuntimeError("Source palette checksum mismatch: "+entry["file"])
    foley_cues();magic_cues();orchestra();ui_cues();ambience()
    MANIFEST["usedSourceFiles"]=sorted(USED_SOURCES)
    (OUT/"audio_manifest.json").write_text(json.dumps(MANIFEST,indent=2)+"\n",encoding="utf-8")
    print(f"Rendered {len(MANIFEST['sounds'])} mastered sounds ({sum(s['channels']==2 for s in MANIFEST['sounds'].values())} stereo) with pinned CC0 sources")


if __name__=="__main__":
    main()
