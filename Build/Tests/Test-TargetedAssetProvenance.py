"""Reject actual current source, model, artwork and historical binding mutations."""
from pathlib import Path
import argparse, copy, hashlib, json, sys
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Build'))
import portrait_source_revision as revision
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args();out=args.output.resolve();out.relative_to(ROOT/'Artifacts/QA')
if out.exists():raise ValueError('Fixture evidence must use a new output file')
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text())
P=json.loads((ROOT/'Assets/Source/CardArt/model_portraits.json').read_text())
checks=[]
def reject(name,mutation=None,corrupt=None):
    candidate=copy.deepcopy(M);original=revision.raw_pin
    if mutation:mutation(candidate)
    if corrupt:
        def changed(root,path):
            value=original(root,path)
            if value['file']==corrupt:value['sha256']='0'*64
            return value
        revision.raw_pin=changed
    try:
        revision.validate_revision(ROOT,candidate,P['sourceBlend'])
        checks.append({'name':name,'passed':False,'error':'Mutation unexpectedly accepted'})
    except (ValueError,KeyError,TypeError) as error:
        checks.append({'name':name,'passed':True,'observedRejection':str(error)})
    finally:revision.raw_pin=original
accepted=revision.validate_revision(ROOT,M,P['sourceBlend'])
checks.append({'name':'Actual unmodified current four-stage chain accepted','passed':True})
reject('Additional Warden geometry scope rejected',lambda m:m['sourceRevisions'][3]['changedCharacters'].append('bone_warden'))
reject('Changed historical third-stage source rejected',lambda m:m['sourceRevisions'][2]['toSource'].update(sha256='0'*64))
reject('Incorrect current Source hash rejected',lambda m:m['source'].update(sha256='0'*64))
reject('Incorrect current Iron FBX pin rejected',lambda m:m['characters']['iron_giant']['mesh'].update(sha256='0'*64))
reject('Changed Iron scale rejected',lambda m:m['characters']['iron_giant']['modelDesign'].update(baseModelScale=1.1))
reject('Missing Iron LOD rejected',lambda m:m['characters']['iron_giant']['lods'].pop())
reject('Changed actual clip metadata rejected',lambda m:m['characters']['iron_giant']['animations'][0].update(seconds=.01))
reject('Changed non-Iron model record rejected',lambda m:m['characters']['bone_warden']['modelDesign'].update(baseModelScale=.1))
reject('Relabelled non-Iron picture rejected',lambda m:m['illustrations']['aether_reaper'].update(sourceSceneSHA256=m['source']['sha256']))
reject('Incorrect Iron illustration binding rejected',lambda m:m['illustrations']['iron_giant'].update(sha256='0'*64))
reject('Changed current Source bytes rejected',corrupt=M['source']['file'])
reject('Changed preserved b7 Source bytes rejected',corrupt=M['sourceRevisions'][3]['beforeSourceSnapshot']['file'])
reject('Changed non-Iron Warden export bytes rejected',corrupt=M['characters']['bone_warden']['mesh']['file'])
reject('Changed old Reaper card pixels rejected',corrupt=M['illustrations']['aether_reaper']['file'])
reject('Changed current Iron card pixels rejected',corrupt=M['illustrations']['iron_giant']['file'])
reject('Changed historical acceptance rejected',corrupt=M['sourceRevisions'][3]['historicalAcceptance']['file'])
reject('Changed independent saved-scene evidence rejected',corrupt=M['sourceRevisions'][3]['savedSceneComparison']['file'])
reject('Changed current model metadata snapshot rejected',corrupt=M['sourceRevisions'][3]['currentModelRecord']['file'])
reject('Changed actual generation code snapshot rejected',corrupt=M['sourceRevisions'][3]['generationProducerUsedSnapshot']['file'])
reject('Changed equivalent producer code version rejected',corrupt=M['sourceRevisions'][3]['recordedGenerationProducerSnapshot']['file'])
R={'schema':1,'passed':all(c['passed'] for c in checks),'scope':'Actual maintained validator, read-only dictionary and raw-read corruption controls; no Source file mutations',
   'checkCount':len(checks),'checks':checks,'acceptedScope':accepted,
   'manifest':revision.raw_pin(ROOT,'Assets/asset_manifest.json'),
   'validator':revision.raw_pin(ROOT,'Build/portrait_source_revision.py'),
   'fixture':revision.raw_pin(ROOT,Path(__file__))}
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(R,indent=2))
print('RIFT_TARGETED_PROVENANCE_FIXTURES',len(checks),'checks',sum(not c['passed'] for c in checks),'errors')
if not R['passed']:raise RuntimeError('Targeted provenance mutation controls failed')
