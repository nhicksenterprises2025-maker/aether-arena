"""Measure current masters and compare the released v1.0.0 source masters.

Numeric QA and a listening reel, not a claim of human listening acceptance.
Short cues are padded to a 400 ms BS.1770 block, explicitly marked in the report.
"""
from pathlib import Path
import argparse
import hashlib
import io
import json
import math
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
if (ROOT/"Build/Tools/AudioPython").exists():
    sys.path.insert(0,str(ROOT/"Build/Tools/AudioPython"))
import numpy as np
import soundfile as sf
import pyloudnorm as loudness
from scipy import signal


def measure(blob):
    x,rate=sf.read(io.BytesIO(blob),always_2d=True)
    duration=len(x)/rate
    rms=float(np.sqrt(np.mean(x*x)))
    tail=x[-round(.03*rate):]
    mono=np.mean(x,axis=1)
    _,power=signal.welch(mono,fs=rate,nperseg=min(4096,len(x)))
    hz=np.linspace(0,rate/2,len(power))
    treble=float(np.sum(power[hz>4000])/max(np.sum(power),1e-20))
    midSide=float(np.sqrt(np.mean((x[:,0]-x[:,-1])**2)))/max(rms,1e-9) if x.shape[1]==2 else 0
    meter=loudness.Meter(rate)
    padded=np.pad(x,((0,max(0,round(.4*rate)-len(x))),(0,0)))
    lufs=float(meter.integrated_loudness(padded))
    peak4=float(np.max(np.abs(signal.resample_poly(x,4,1,axis=0))))
    return {"durationSeconds":duration,"channels":x.shape[1],"sampleRate":rate,"peak":float(np.max(np.abs(x))),
        "truePeak4xDbTP":20*math.log10(max(peak4,1e-9)),"rmsDbFS":20*math.log10(max(rms,1e-9)),
        "integratedLUFS":lufs if math.isfinite(lufs) else None,"loudnessPaddedTo400ms":duration<.4,
        "dcOffset":float(np.max(np.abs(np.mean(x,axis=0)))),"clippedSamples":int(np.sum(np.abs(x)>=.999)),
        "tail30msRmsDbFS":20*math.log10(max(float(np.sqrt(np.mean(tail*tail))),1e-9)),
        "endpointMagnitude":float(max(np.max(np.abs(x[0])),np.max(np.abs(x[-1])))),
        "loopBoundaryStep":float(np.max(np.abs(x[0]-x[-1]))),"energyAbove4kHzFraction":treble,
        "stereoDifferenceRelativeRMS":midSide}


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--baseline-ref",default="v1.0.0")
    args=parser.parse_args()
    output=ROOT/"Artifacts/QA/Audio/masters-1.1.0";output.mkdir(parents=True,exist_ok=True)
    manifest_path=ROOT/"Assets/Source/Audio/audio_manifest.json"
    manifest=json.loads(manifest_path.read_text(encoding="utf-8"))
    git=shutil.which("git") or "C:/Users/Noah/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/git/cmd/git.exe"
    baseline={}
    report={"schemaVersion":1,"manifestSha256":hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        "baselineRef":args.baseline_ref,"auditoryAcceptance":"Not assessed by a human; listening reel supplied for review.",
        "loudnessMethod":"pyloudnorm BS.1770-4, 400 ms blocks; sub-400 ms cues padded with silence", "sounds":{},"errors":[]}
    for name,entry in manifest["sounds"].items():
        blob=(ROOT/entry["file"]).read_bytes();actual=hashlib.sha256(blob).hexdigest()
        metrics=measure(blob);metrics["sha256"]=actual;metrics["role"]=entry["role"];metrics["loop"]=entry["loop"]
        if actual!=entry["sha256"]:report["errors"].append(name+":manifest hash")
        if metrics["clippedSamples"] or metrics["truePeak4xDbTP"]>=-1:report["errors"].append(name+":peak headroom")
        if metrics["dcOffset"]>.001:report["errors"].append(name+":DC")
        if metrics["rmsDbFS"]<-55:report["errors"].append(name+":silent")
        if not entry["loop"] and metrics["endpointMagnitude"]>.0001:report["errors"].append(name+":endpoint")
        if entry["loop"] and metrics["loopBoundaryStep"]>1/32768:report["errors"].append(name+":loop boundary")
        result=subprocess.run([git,"show",args.baseline_ref+":"+entry["file"]],cwd=ROOT,capture_output=True)
        if result.returncode==0:
            baseline[name]=result.stdout;metrics["baseline"]=measure(result.stdout)
            metrics["durationChangeSeconds"]=metrics["durationSeconds"]-metrics["baseline"]["durationSeconds"]
        report["sounds"][name]=metrics
    report["summary"]={"masters":len(report["sounds"]),"preservedCueIds":len(baseline),
        "variationTakes":len(report["sounds"])-len(baseline),"stereoContainers":sum(m["channels"]==2 for m in report["sounds"].values()),
        "audibleStereoMasters":sum(m["stereoDifferenceRelativeRMS"]>.03 for m in report["sounds"].values()),
        "maximumTruePeakDbTP":max(m["truePeak4xDbTP"] for m in report["sounds"].values()),
        "clippedSamples":sum(m["clippedSamples"] for m in report["sounds"].values()),
        "scoreLUFS":report["sounds"]["music_rift"]["integratedLUFS"]}
    # Alternate old/new cues with a quiet gap, then play the new score excerpt.
    selection=["ui_click","sword_hit","bow_release","heavy_slam","frost_attack","storm_bolt","meteor_impact","victory"]
    parts=[];markers=[];position=0
    for name in selection:
        for edition,blob in (("1.0.0",baseline[name]),("1.1.0",(ROOT/manifest["sounds"][name]["file"]).read_bytes())):
            x,rate=sf.read(io.BytesIO(blob),always_2d=True)
            if x.shape[1]==1:x=np.repeat(x,2,axis=1)
            # Keep original master levels; no normalization hides the comparison.
            parts.append(x*.75);markers.append({"cue":name,"edition":edition,"startSeconds":position})
            position+=len(x)/rate;parts.append(np.zeros((round(rate*.35),2)));position+=.35
    score,_=sf.read(ROOT/manifest["sounds"]["music_rift"]["file"],always_2d=True)
    parts.append(score[:round(12*44100)]*.75);markers.append({"cue":"music_rift","edition":"1.1.0","startSeconds":position})
    sf.write(output/"audio-comparison.wav",np.concatenate(parts),44100,subtype="PCM_16")
    (output/"audio-comparison-timeline.json").write_text(json.dumps(markers,indent=2)+"\n",encoding="utf-8")
    report["passed"]=not report["errors"]
    (output/"audio-master-verification.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"passed":report["passed"],"summary":report["summary"],"errors":report["errors"]},indent=2))
    if report["errors"]:raise SystemExit(1)


if __name__=="__main__":main()
