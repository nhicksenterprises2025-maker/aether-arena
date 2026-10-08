"""Original procedural sound design and composed Rift score, no sampled media.

Synthesizes modal metal/stone, bow mechanics, airflow, granular spell energy,
layered status cues and an original 8-bar score. Reproducible 16-bit PCM masters.
"""
from pathlib import Path
import array
import hashlib
import json
import math
import random
import wave

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "Assets/Source/Audio"
OUT.mkdir(parents=True, exist_ok=True)
RATE = 44100
TAU = math.tau

def signal(seconds):
    return array.array("f", [0.0]) * int(RATE * seconds)

def add_tone(samples, start, seconds, hz, gain, decay=3.0, pitch_end=None, shape="sine"):
    begin = int(start * RATE)
    count = min(int(seconds * RATE), len(samples) - begin)
    phase = 0
    for j in range(max(0, count)):
        t = j / RATE
        q = t / seconds
        frequency = hz if pitch_end is None else hz * (pitch_end / hz) ** q
        phase += TAU * frequency / RATE
        value = math.sin(phase)
        if shape == "brass":
            value = (value + .3 * math.sin(phase * 2) + .12 * math.sin(phase * 3)) / 1.42
        if shape == "string":
            value = (value + .2 * math.sin(phase * 3) + .08 * math.sin(phase * 5)) / 1.28
        attack = min(1.0, t / .008)
        tail = min(1.0, max(0.0, (seconds-t) / .025))
        samples[begin+j] += value * gain * attack * tail * math.exp(-decay*q)

def noise(samples, start, seconds, gain, seed, cutoff=.17, decay=5.0, swell=False):
    rng = random.Random(seed)
    begin = int(start * RATE)
    previous = 0
    count = min(int(seconds * RATE), len(samples) - begin)
    for j in range(max(0, count)):
        q = j / max(1, count)
        raw = rng.uniform(-1, 1)
        previous += cutoff * (raw-previous)
        envelope = math.sin(math.pi*q)**1.2 if swell else math.exp(-decay*q) * min(1,j/80)
        samples[begin+j] += previous * gain * envelope

def echo(samples, delays=(.031,.067,.113), wet=.18):
    original = samples[:]
    for index, seconds in enumerate(delays):
        offset = int(seconds * RATE)
        level = wet/(index+1)
        for j in range(offset, len(samples)):
            samples[j] += original[j-offset]*level

MANIFEST = {"schemaVersion": 1, "originalSource": "Build/generate_audio.py", "sampleRate": RATE, "sounds": {}}

def write(name, samples, volume=.72, loop=False):
    peak = max((abs(s) for s in samples), default=1)
    scale = min(1.0, volume / max(peak,.001))
    pcm = array.array("h", (int(max(-.999,min(.999,s*scale))*32767) for s in samples))
    path = OUT / (name + ".wav")
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(pcm.tobytes())
    MANIFEST["sounds"][name] = {"file": path.relative_to(ROOT).as_posix(), "duration": len(samples)/RATE, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "loop": loop, "peak": min(peak,volume), "channels": 1}

def metal(name, seed, heavy=False):
    samples = signal(1.15 if heavy else .7)
    base = 95 if heavy else 360
    for ratio, gain in [(1,.48),(2.71,.19),(4.13,.14),(5.87,.07),(8.39,.035)]:
        add_tone(samples,0,.9 if heavy else .6,base*ratio,gain,5 if heavy else 7)
    noise(samples,0,.18,.5,seed,.5,9)
    echo(samples)
    write(name,samples)

def whoosh(name, seed, bass=0):
    samples = signal(.7)
    noise(samples,0,.45,.8,seed,.27,3,True)
    if bass:
        add_tone(samples,.08,.5,bass,.3,3,pitch_end=bass*.55)
    echo(samples,wet=.08)
    write(name,samples,.62)

def magic(name, color, seed, seconds=1.4):
    samples = signal(seconds)
    chord = {"arc":(293.66,440,587.33),"frost":(659.25,987.77,1318.5),"storm":(82.41,123.47,329.63),"nova":(146.83,220,349.23),"manta":(392,587.33,783.99)}[color]
    for i,hz in enumerate(chord):
        add_tone(samples,i*.015,seconds-i*.015,hz,.33/(i+1),4,pitch_end=hz*.92,shape="string")
    noise(samples,0,min(.6,seconds),.65,seed,.35 if color=="storm" else .07,6)
    echo(samples,delays=(.057,.113,.191),wet=.24)
    write(name,samples,.65)

metal("sword_hit",11)
metal("tower_hit",13,True)
metal("shield_hit",17)
metal("building_hit",19,True)
whoosh("sword_attack",23,145)
whoosh("dual_attack",29,220)
whoosh("arrow_flight",31)
whoosh("charge",37,85)
whoosh("wing_air",41)

samples=signal(.8)
noise(samples,0,.035,.75,43,.7,8)
add_tone(samples,.01,.55,187,.34,6,pitch_end=140,shape="string")
add_tone(samples,.005,.21,732,.09,8)
write("bow_release",samples)

samples=signal(.6)
noise(samples,0,.14,.65,47,.5,9)
add_tone(samples,0,.35,245,.24,8,pitch_end=115)
write("arrow_hit",samples)

samples=signal(.85)
noise(samples,0,.4,.8,53,.08,6)
add_tone(samples,0,.6,92,.55,5,pitch_end=54)
add_tone(samples,.04,.3,210,.2,8)
write("heavy_slam",samples)

samples=signal(2.4)
noise(samples,0,2,.9,59,.07,4)
add_tone(samples,0,1.6,70,.6,3,pitch_end=32)
for i in range(11):
    at=.05+i*.12
    noise(samples,at,.16,.35,61+i,.6,8)
    add_tone(samples,at,.2,130+i*41,.07,9)
echo(samples,wet=.28)
write("tower_destroy",samples)

for name,color,seed in [("arc_cast","arc",73),("arc_impact","arc",79),("manta_cast","manta",83),("manta_impact","manta",89),("frost_attack","frost",97),("frost_slow","frost",101),("storm_bolt","storm",103),("storm_pulse","storm",107),("stun","storm",109),("nova_impact","nova",113)]:
    magic(name,color,seed)

samples=signal(.65)
for i in range(7):
    at=i*.045
    noise(samples,at,.07,.5,127+i,.7,9)
    add_tone(samples,at,.1,230-i*8,.19,7,pitch_end=75)
write("bullet_burst",samples)

samples=signal(2)
noise(samples,0,.65,.7,137,.04,4,True)
noise(samples,.43,1.1,.85,139,.13,5)
add_tone(samples,.43,1.3,110,.65,4,pitch_end=40)
for i in range(5):
    add_tone(samples,.5+i*.05,.5,230+i*52,.12,8)
echo(samples,delays=(.09,.19,.32),wet=.22)
write("meteor_impact",samples)

samples=signal(.8)
noise(samples,0,.5,.7,149,.07,5)
add_tone(samples,0,.5,95,.27,6,pitch_end=65)
write("meteor_tick",samples)

samples=signal(.5)
noise(samples,0,.07,.45,151,.7,8)
add_tone(samples,0,.3,490,.18,7,pitch_end=160)
write("bat_bite",samples)

magic("deploy","arc",157,1.1)
magic("core_awaken","storm",163,2.6)
for name,frequencies in [("ui_click",(660,880)),("ui_hover",(440,660)),("ui_error",(220,207)),("ui_save",(523.25,659.25,783.99)),("aether_two",(293.66,440,587.33)),("aether_three",(293.66,440,587.33,880)),("overtime",(146.83,220,293.66)),("tiebreaker",(110,146.83,220)),("victory",(293.66,369.99,440,587.33)),("defeat",(146.83,174.61,220,293.66))]:
    duration=.24 if name.startswith("ui") else 2.5
    samples=signal(duration)
    for i,hz in enumerate(frequencies):
        start=i*(.025 if name.startswith("ui") else .11)
        add_tone(samples,start,duration-start,hz,.24,4,shape="brass" if not name.startswith("ui") else "sine")
    echo(samples,wet=.12)
    write(name,samples,.48 if name.startswith("ui") else .68)

# A composed D-minor/Aeolian score. Eight bars at 96 BPM, changing chord roots,
# bowed-pad overtones, a restrained plucked ostinato and sparse bell melody.
seconds=40
samples=signal(seconds)
beat=.625
roots=[146.83,130.81,116.54,130.81,146.83,174.61,130.81,110]
for bar,root in enumerate(roots*2):
    start=bar*4*beat
    for ratio,gain in [(1,.085),(1.5,.047),(2,.025)]:
        add_tone(samples,start,2.6,root*ratio,gain,1.3,shape="string")
    pattern=[1,1.5,2,1.5,1.25,1.5,2,1.5]
    for note,ratio in enumerate(pattern):
        add_tone(samples,start+note*beat/2,.65,root*ratio*2,.043,7,shape="string")
    if bar%2==0:
        add_tone(samples,start+.625,1.7,root*4,.035,4)
        add_tone(samples,start+1.875,1.7,root*3,.025,4)
    noise(samples,start,2.45,.025,211+bar,.003,1.5,True)
echo(samples,delays=(.113,.223,.379),wet=.22)
# Seam envelope reaches zero at both ends so the exact mastered loop is click-free.
for index in range(len(samples)):
    t=index/RATE
    samples[index]*=min(1,t/.3,max(0,(seconds-t)/.3))
write("music_rift",samples,.45,True)

samples=signal(8)
noise(samples,0,8,.23,307,.005,.1,True)
add_tone(samples,0,8,55,.025,.1)
write("river_ambience",samples,.24,True)

(OUT/"audio_manifest.json").write_text(json.dumps(MANIFEST,indent=2),encoding="utf-8")
print(f"Authored {len(MANIFEST['sounds'])} original PCM sounds into {OUT}")
