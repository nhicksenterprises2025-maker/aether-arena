"""Verify deployment coverage, recorded-source pins and actual PCM headroom.

Creates a complete, unnormalized audition reel. Numeric checks do not assert
human listening acceptance or prove the packaged mixer; use Test-UnrealAudio.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

from verify_audio_masters import measure, sf, np

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--baseline-ref",default="v1.7.1")
    args=parser.parse_args()
    manifest=json.loads((ROOT/"Assets/Source/Audio/audio_manifest.json").read_text())
    cards=json.loads((ROOT/"Assets/Data/card_manifest.json").read_text())["cards"]
    baseline=json.loads(subprocess.run(["git","show",args.baseline_ref+":Assets/Source/Audio/audio_manifest.json"],cwd=ROOT,check=True,capture_output=True).stdout)
    palette=json.loads((ROOT/"Assets/Source/Audio/Palette/sources.json").read_text())
    pins={entry["file"]:entry for entry in palette["files"]}
    errors=[];measurements={};parts=[];timeline=[];position=0
    for name,entry in baseline["sounds"].items():
        actual=hashlib.sha256((ROOT/entry["file"]).read_bytes()).hexdigest()
        if actual!=entry["sha256"] or manifest["sounds"].get(name)!=entry:
            errors.append("Released master/metadata changed: "+name)
    expected=set()
    for card in cards:
        card_id=card["id"];profile=manifest.get("spawnCueProfiles",{}).get(card_id,{})
        names=["spawn_"+card_id+suffix for suffix in ("","_v2","_v3")];expected.update(names)
        if profile.get("takes")!=names:errors.append("Take roster differs: "+card_id)
        for name in names:
            entry=manifest["sounds"].get(name)
            if not entry:errors.append("Missing signature: "+name);continue
            blob=(ROOT/entry["file"]).read_bytes();metrics=measure(blob);metrics["sha256"]=hashlib.sha256(blob).hexdigest()
            measurements[name]=metrics
            if metrics["sha256"]!=entry["sha256"]:errors.append(name+": master hash")
            if entry.get("cardId")!=card_id or entry["role"]!="deployment" or entry["loop"]:errors.append(name+": wrong role/card/loop")
            if not .15<metrics["durationSeconds"]<1.6:errors.append(name+": duration")
            if metrics["clippedSamples"] or metrics["truePeak4xDbTP"]>=-3:errors.append(name+": headroom")
            if metrics["dcOffset"]>.001 or metrics["endpointMagnitude"]>.0001:errors.append(name+": DC/endpoint")
            if not -32<metrics["rmsDbFS"]<-19:errors.append(name+": level")
            if metrics["sampleRate"]!=44100 or metrics["channels"]!=1:errors.append(name+": format")
            if not entry["sources"]:errors.append(name+": missing recorded sources")
            for source_name in entry["sources"]:
                pin=pins.get(source_name);source_path=ROOT/"Assets/Source/Audio/Palette"/source_name
                if not pin or pin["license"]!="CC0-1.0" or hashlib.sha256(source_path.read_bytes()).hexdigest()!=pin["sha256"]:
                    errors.append(name+": unverified recorded source "+source_name)
            x,rate=sf.read(ROOT/entry["file"],always_2d=True)
            timeline.append({"card":card_id,"take":name,"startSeconds":position,"durationSeconds":len(x)/rate})
            parts.extend([x,np.zeros((round(rate*.3),1))]);position+=len(x)/rate+.3
    actual={name for name in manifest["sounds"] if name.startswith("spawn_")}
    if actual!=expected:errors.append("Extra/missing deployment master")
    hashes=[metrics["sha256"] for metrics in measurements.values()]
    if len(set(hashes))!=len(hashes):errors.append("Two takes have identical PCM files")
    output=ROOT/"Artifacts/QA/RosterAudio";output.mkdir(parents=True,exist_ok=True)
    sf.write(output/"all-63-deployment-takes.wav",np.concatenate(parts),44100,subtype="PCM_16")
    report={"schema":1,"passed":not errors,"baselineRef":args.baseline_ref,"preservedMasters":len(baseline["sounds"]),
            "playableCards":len(cards),"deploymentTakes":len(measurements),"uniqueFiles":len(set(hashes)),"measurements":measurements,
            "listeningAcceptance":"Not assessed by automated measurements; all-63-deployment-takes.wav supplies every take at mastered level.",
            "runtimeAcceptance":"Requires current packaged Test-UnrealAudio result; source metrics do not prove runtime playback.","errors":errors}
    (output/"verification.json").write_text(json.dumps(report,indent=2)+"\n")
    (output/"audition-timeline.json").write_text(json.dumps(timeline,indent=2)+"\n")
    print(json.dumps({key:value for key,value in report.items() if key not in ("measurements","listeningAcceptance","runtimeAcceptance")},indent=2))
    if errors:raise SystemExit(1)


if __name__=="__main__":main()
