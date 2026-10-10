"""Audit real source gait contacts; run in Blender after the model forge.

This complements the UE integration test of imported foot bones. It measures
ground contact, lifted recovery, planted-foot drift and loop continuity rather
than merely checking that a named animation exists.
"""
from pathlib import Path
import bpy, json, hashlib, math

ROOT=Path(__file__).resolve().parents[1]
manifest=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/manifest['source']['file']))
scene=bpy.context.scene
report={'schema':1,'version':manifest['build'],'passed':False,'sourceBlend':manifest['source'],
        'checks':[],'cards':{},'errors':[],
        'scope':'Actual authored source bone contacts; native UnitMotion separately evaluates imported foot bones.'}
def pin(path):
    raw=path.read_bytes();return {'file':path.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
report['validatorScript']=pin(Path(__file__).resolve())
def check(name,passed,data=None):
    report['checks'].append({'name':name,'passed':bool(passed),'data':data})
    if not passed:report['errors'].append(name)
for name,model in manifest['characters'].items():
    if name=='tower_archer' or model['rigFamily'] not in ('humanoid','quadruped'):continue
    rig=bpy.data.objects['SKEL_'+name]
    expected_stride=.78 if name in ('mini_stampede','stampede') else .90 if model['rigFamily']=='quadruped' else 1.05
    check(name+' physical mesh is pinned',pin(ROOT/model['mesh']['file'])==model['mesh'])
    feet=['foot_l_contact','foot_r_contact'] if model['rigFamily']=='humanoid' else ['front_l_contact','front_r_contact','back_l_contact','back_r_contact']
    offsets=[0.,.5] if len(feet)==2 else [0.,.5,.5,0.]
    stance=.60 if len(feet)==2 else .50
    card_record={}
    for clip in model['animations']:
        if clip['name'] not in ('Locomotion','Charge'):continue
        stride=1.10 if clip['name']=='Charge' else expected_stride
        check(name+' '+clip['name']+' stride matches renderer cadence',abs(clip.get('strideMeters',0)-stride)<1e-9)
        check(name+' '+clip['name']+' actual FBX is pinned',pin(ROOT/clip['file'])=={key:clip[key] for key in ('file','bytes','sha256')})
        rig.animation_data.action=bpy.data.actions[clip['action']]
        samples=[];poses=[]
        for tick in range(101):
            phase=tick/100;frame=1+(clip['frames']-1)*phase
            scene.frame_set(int(frame),subframe=frame%1);bpy.context.view_layer.update()
            samples.append([list(rig.pose.bones[foot].matrix.translation) for foot in feet])
            if tick in (0,100):poses.append([tuple(x for row in bone.matrix for x in row) for bone in rig.pose.bones])
        loop_gap=max(abs(a-b) for first,last in zip(poses[0],poses[1]) for a,b in zip(first,last))
        check(name+' '+clip['name']+' closes without a pose jump',loop_gap<1e-5,loop_gap)
        foot_results={}
        for index,foot in enumerate(feet):
            points=[sample[index] for sample in samples]
            min_z=min(p[2] for p in points);max_z=max(p[2] for p in points)
            check(name+' '+clip['name']+' '+foot+' lifts above the ground',max_z-min_z>.055,{'minZ':min_z,'maxZ':max_z})
            check(name+' '+clip['name']+' '+foot+' stays above the ground',min_z>=-.025,min_z)
            # Adding the unit's forward -Y travel turns an in-place stance
            # into a physical ground-contact test. Skip exact contact changes.
            planted=[]
            for tick,point in enumerate(points):
                u=(tick/100+offsets[index])%1
                if .04<u<stance-.04:
                    planted.append((tick,point[1]-stride*tick/100,point[2]))
            runs=[];run=[]
            for sample in planted:
                if run and sample[0]!=run[-1][0]+1:runs.append(run);run=[]
                run.append(sample)
            if run:runs.append(run)
            drift=max(max(s[1] for s in run)-min(s[1] for s in run) for run in runs)
            flat=max(max(s[2] for s in run)-min(s[2] for s in run) for run in runs)
            check(name+' '+clip['name']+' '+foot+' remains planted through stance',drift<.025 and flat<.025,{'groundDriftMeters':drift,'heightVariationMeters':flat})
            foot_results[foot]={'groundDriftMeters':drift,'heightVariationMeters':flat,'minHeightMeters':min_z,'maxHeightMeters':max_z}
        card_record[clip['name']]={'samples':101,'strideMeters':stride,'loopGap':loop_gap,'feet':foot_results,'fbx':pin(ROOT/clip['file'])}
    card_record['mesh']=model['mesh'];report['cards'][name]=card_record
    rig.animation_data.action=None
report['passed']=not report['errors'];report['checkCount']=len(report['checks'])
path=ROOT/'Artifacts/QA/balance140-locomotion.json';path.parent.mkdir(parents=True,exist_ok=True)
path.write_text(json.dumps(report,indent=2),encoding='utf-8')
print('RIFT_LOCOMOTION_QA',len(report['cards']),'cards',report['checkCount'],'checks',len(report['errors']),'errors',flush=True)
for error in report['errors']:print('RIFT_LOCOMOTION_ERROR',error)
if report['errors']:raise RuntimeError('Locomotion contacts failed')
