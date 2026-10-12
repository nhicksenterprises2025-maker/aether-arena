"""Read actual Blender rigs and focused saved scenes for the selected contracts."""
from pathlib import Path
import hashlib
import json
import math
import ast
import struct
import bpy

ROOT=Path(__file__).resolve().parents[1]
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
REPORT={'schema':1,'passed':False,'scope':'Actual selected Blender source, FBX provenance and editable art scenes; native import/game-camera review is separate',
        'sourceBlend':M['source'],'checks':[],'errors':[],'cards':{}}
def check(name,condition,detail=None):
    REPORT['checks'].append({'name':name,'passed':bool(condition),'detail':detail})
    if not condition:REPORT['errors'].append(name)
def pin(path):
    raw=path.read_bytes();return {'file':path.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
def curves(action):
    if hasattr(action,'fcurves'):return list(action.fcurves)
    return [curve for layer in action.layers for strip in layer.strips for bag in strip.channelbags for curve in bag.fcurves]

def action_keys(action):
    return {(curve.data_path,curve.array_index):{
        'extrapolation':curve.extrapolation,
        'keys':[{'co':tuple(point.co),'left':tuple(point.handle_left),'right':tuple(point.handle_right),
                 'interpolation':point.interpolation,'leftType':point.handle_left_type,
                 'rightType':point.handle_right_type} for point in curve.keyframe_points]}
        for curve in curves(action)}

def authored_portrait_rate(root,record):
    def explicit_rate(script,filename,target,scope=None):
        if not script or script.get('file')!=filename or pin(root/filename)!=script:
            raise ValueError('Portrait studio stage does not match its exact recorded source: '+filename)
        tree=ast.parse((root/filename).read_text(encoding='utf-8'))
        if scope:
            functions=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name==scope]
            if len(functions)!=1:raise ValueError('Portrait stage has no unique authored function')
            tree=functions[0]
        rates=[]
        for node in ast.walk(tree):
            if isinstance(node,ast.Assign) and any(ast.unparse(item)==target for item in node.targets):
                if not isinstance(node.value,ast.Constant) or type(node.value.value) is not int:
                    raise ValueError('Portrait studio frame rate is not one explicit authored integer')
                rates.append(node.value.value)
        if len(rates)!=1 or rates[0]<=0:
            raise ValueError('Portrait studio frame rate is missing or ambiguous')
        return rates[0]
    renderer_rate=None
    if record.get('rendererScript'):
        renderer_rate=explicit_rate(record['rendererScript'],'Build/render_expansion_portraits.py',
                                    'scene.render.fps','render_selected')
    # The recorded startup-scene stage runs after rendering and owns the saved
    # studio clock. Validate both actual producers; source clips stay100FPS.
    if record.get('sceneFinalizer'):
        return explicit_rate(record['sceneFinalizer'],'Build/finalize_expansion_art_scenes.py',
                             'target.render.fps')
    if renderer_rate is None:raise ValueError('No exact recorded portrait studio rate stage')
    return renderer_rate

def staged_reference_matches(reference,actual,clip,subject):
    if clip.get('fps')!=100 or subject.get('evaluatedAtFrame')!=1 or \
       subject.get('poseSourceFrame')!=1+subject.get('phaseOffsetFrames',0) or \
       subject.get('phaseOffsetFrames')!=round((clip['frames']-1)*subject['phase']) or set(reference)!=set(actual):
        return False
    for name,expected in reference.items():
        observed=actual[name]
        if observed['extrapolation']!=expected['extrapolation'] or len(observed['keys'])!=len(expected['keys']):return False
        for before,after in zip(expected['keys'],observed['keys']):
            if any(before[key]!=after[key] for key in ('interpolation','leftType','rightType')):return False
            for key in ('co','left','right'):
                for axis in (0,1):
                    value=before[key][axis]-(subject['phaseOffsetFrames'] if axis==0 else 0)
                    # Blender stores key/handle vectors in float32. The renderer
                    # performs this exact frame shift; no timing tolerance is used.
                    expected_value=struct.unpack('f',struct.pack('f',value))[0]
                    if after[key][axis]!=expected_value:return False
    return True

def portrait_studio_matches(root,record,scene):
    return scene.render.fps==authored_portrait_rate(root,record) and scene.render.fps_base==1

bpy.ops.wm.open_mainfile(filepath=str(ROOT/M['source']['file']))
source_actions={}
for card in ('mini_stampede','stampede','pulverizer'):
    record=M['characters'][card];rig=bpy.data.objects['SKEL_'+card];mesh=bpy.data.objects['SK_'+card]
    contract=record['modelDesign']['animationContract'];attack=contract['attack'];locomotion=contract['locomotion']
    check(card+' dedicated expansion surface',all(name.startswith('M_RiftExpansion') for name in record['materials']))
    check(card+' source proxy is editable and excluded from gameplay FBX',
          (proxy:=bpy.data.objects.get('PROXY_'+card)) is not None and proxy.hide_render and proxy.get('exportedToGameplay') is False)
    check(card+' normalized forward declaration',contract['forward']=={'blender':'-Y','unreal':'+X'})
    check(card+' supplied model scale',contract['visual']['baseModelScale']=={'mini_stampede':1.30,'stampede':.85,'pulverizer':.95}[card])
    clips={clip['name']:clip for clip in record['animations']}
    check(card+' retains all nine100FPS source clips',set(clips)=={'Idle','Locomotion','Attack','Hit','Death','Deploy','Status','Turn','Acquire'})
    source_actions[card]={name:{'clip':clip,'keys':action_keys(bpy.data.actions[clip['action']])} for name,clip in clips.items()}
    check(card+' exact attack envelope source duration',abs(clips['Attack']['seconds']-attack['anticipationSeconds']-attack['recoverySeconds'])<1e-9)
    check(card+' single authoritative visual contact marker',clips['Attack']['releaseNormalized']==attack['contactNormalized'] and
          all(clip.get('releaseNormalized') is None for name,clip in clips.items() if name!='Attack'))
    check(card+' exact death envelope source duration',abs(clips['Death']['seconds']-contract['death']['cleanupSeconds'])<1e-9)
    check(card+' dense exact decimal timing',all(clip.get('fps')==100 for clip in clips.values()))
    pose_checks={}
    for name,clip in clips.items():
        rig.animation_data.action=bpy.data.actions[clip['action']]
        for fraction in (0.,.25,.50,.75,1.):
            bpy.context.scene.frame_set(1+round((clip['frames']-1)*fraction));bpy.context.view_layer.update()
            root=rig.pose.bones['root'].matrix.translation
            check(card+' '+name+' fixed authority-independent root '+str(fraction),root.length<1e-6)
            check(card+' '+name+' finite evaluated joints '+str(fraction),all(math.isfinite(v) for bone in rig.pose.bones for row in bone.matrix for v in row))
    if card!='pulverizer':
        check(card+' independently jointed ears',{'ear_l','ear_r'}.issubset(rig.data.bones.keys()))
        check(card+' exact reference gallop cycle',abs(clips['Locomotion']['seconds']-locomotion['referenceCycleSeconds'])<1e-9)
        check(card+' source stride gives approved reference cadence',abs(locomotion['strideMeters']*contract['visual']['baseModelScale']/locomotion['referenceSpeedTilesPerSecond']-locomotion['referenceCycleSeconds'])<1e-9)
    else:
        check(card+' four compressing struts',all('piston_'+str(i) in rig.data.bones for i in range(4)))
        check(card+' independently jointed top ring','compression_ring' in rig.data.bones)
        check(card+' two bounded death fragment joints',all('ring_fragment_'+str(s) in rig.data.bones for s in (-1,1)))
        check(card+' low presentation height',1.20<=record['boundsMeters'][2]<=1.31,record['boundsMeters'])
        clip=clips['Attack'];rig.animation_data.action=bpy.data.actions[clip['action']]
        bpy.context.scene.frame_set(1+round((clip['frames']-1)*attack['contactNormalized']));bpy.context.view_layer.update()
        group=mesh.vertex_groups['hammer'].index
        vertices=[v.index for v in mesh.data.vertices if any(g.group==group and g.weight>.999 for g in v.groups)]
        evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get());data=evaluated.to_mesh()
        contact=min((evaluated.matrix_world@data.vertices[i].co).z for i in vertices);evaluated.to_mesh_clear()
        check(card+' actual annular striker contacts recessed plate',abs(contact-.337)<.001,contact)
        for living in ('Idle','Attack','Deploy'):
            rig.animation_data.action=bpy.data.actions[clips[living]['action']];bpy.context.scene.frame_set(1);bpy.context.view_layer.update()
            check(card+' living '+living+' hides death fragments',all(max(rig.pose.bones['ring_fragment_'+str(s)].scale)<.002 for s in (-1,1)))
    REPORT['cards'][card]={'boundsMeters':record['boundsMeters'],'boneCount':record['boneCount'],
                           'triangles':record['triangles'],'contract':contract}

portraits=json.loads((ROOT/'Assets/Source/CardArt/model_portraits.json').read_text(encoding='utf-8'))
for card,count in (('mini_stampede',5),('stampede',15),('pulverizer',1)):
    record=portraits['cards'][card]
    scene_path=ROOT/record['editableScene']['file']
    check(card+' editable portrait scene raw pin',pin(scene_path)==record['editableScene'])
    bpy.ops.wm.open_mainfile(filepath=str(scene_path));scene=bpy.context.scene
    # A library-written scene may open alongside Blender's factory scene.
    scene=next(s for s in bpy.data.scenes if s.name=='Portrait_'+card)
    check(card+' opens directly into editable production scene',bpy.context.scene==scene and len(bpy.data.scenes)==1)
    studio_rate=authored_portrait_rate(ROOT,record)
    check(card+' editable scene uses its actual pinned renderer studio rate',portrait_studio_matches(ROOT,record,scene),
          {'studioFPS':scene.render.fps,'expectedStudioFPS':studio_rate,'sourceReferenceFPS':100})
    bpy.context.window.scene=scene
    meshes=[obj for obj in scene.objects if obj.type=='MESH' and obj.name.startswith('Portrait_'+card+'_Model_')]
    rigs=[obj for obj in scene.objects if obj.type=='ARMATURE' and obj.name.startswith('Portrait_'+card+'_Rig_')]
    check(card+' saved scene exact actual model count',len(meshes)==count,len(meshes))
    check(card+' saved scene exact actual skeleton count',len(rigs)==count,len(rigs))
    check(card+' saved scene true master dimensions',(scene.render.resolution_x,scene.render.resolution_y)==(1024,1280))
    check(card+' saved scene perspective camera',scene.camera is not None and scene.camera.data.type=='PERSP')
    check(card+' saved scene pins final exported model',scene.get('modelSourceSHA256')==M['characters'][card]['mesh']['sha256'])
    for subject in record['sources']:
        rig=next(obj for obj in rigs if obj.name.endswith('_'+subject['instance']))
        action=rig.animation_data.action
        reference=source_actions[card][subject['action']]
        check(card+' '+subject['instance']+' exact100FPS source key and handle phase mapping',
              staged_reference_matches(reference['keys'],action_keys(action),reference['clip'],subject),
              {'sourceFPS':reference['clip']['fps'],'studioFPS':scene.render.fps,
               'offsetFrames':subject['phaseOffsetFrames'],'sourceFrame':subject['poseSourceFrame'],
               'evaluatedAtFrame':subject['evaluatedAtFrame']})
        frames=[point.co.x for curve in curves(action) for point in curve.keyframe_points]
        check(card+' '+subject['instance']+' actual phase-shifted action',
              bool(frames) and abs(min(frames)-(1-subject['phaseOffsetFrames']))<1e-6 and
              action.name==subject['stagedAction'])
        check(card+' '+subject['instance']+' uniform supplied anatomy scale',
              all(abs(v-subject['scale'])<1e-6 for v in rig.scale))
REPORT['passed']=not REPORT['errors'];REPORT['checkCount']=len(REPORT['checks'])
out=ROOT/'Artifacts/QA/ExpansionAssets/selected-contracts-qa.json'
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
print('RIFT_EXPANSION_CONTRACT_QA',REPORT['checkCount'],'checks',len(REPORT['errors']),'errors')
for error in REPORT['errors']:print('RIFT_EXPANSION_CONTRACT_ERROR',error)
if REPORT['errors']:raise RuntimeError('Selected expansion contracts failed')
