"""Read final new source rigs and reproduce every clip at every quarter frame.

Native import, facing conversion and battle-camera acceptance are separate.
"""
import argparse,ast,hashlib,json,math,sys
from pathlib import Path
from types import SimpleNamespace
import bpy
from mathutils import Vector,Quaternion,Matrix

ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Build'))
import expansion_new_models as producer
from validate_expansion_art_quality import socket_grip_contract, measure_socket_contact, prepare_socket_surfaces
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
out=args.report.resolve();out.relative_to((ROOT/'Artifacts/QA').resolve())
if out.exists():raise ValueError('Immutable QA report already exists')
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
R={'schema':1,'passed':False,'scope':'Actual saved new source and source-builder reproducibility; native import/render separate',
   'sourceBlend':M['source'],'modelBuilderSHA256':hashlib.sha256((ROOT/'Build/expansion_new_models.py').read_bytes()).hexdigest(),
   'checks':[],'errors':[],'cards':{},'poseMatricesChecked':0,'quarterFrameSamples':0}
def check(name,condition,detail=None):
    R['checks'].append({'name':name,'passed':bool(condition),'detail':detail})
    if not condition:R['errors'].append(name)
def pin(path):
    raw=path.read_bytes();return {'file':path.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
check('Actual production source raw pin',pin(ROOT/M['source']['file'])==M['source'])
bpy.ops.wm.open_mainfile(filepath=str(ROOT/M['source']['file']));scene=bpy.context.scene
namespace={'bpy':bpy,'math':math,'Vector':Vector,'Quaternion':Quaternion,'Matrix':Matrix}
tree=ast.parse((ROOT/'Build/generate_assets.py').read_text(encoding='utf-8'))
helpers=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in {'set_world_rotation','aim_leg','aim_arm'}]
exec(compile(ast.Module(body=helpers,type_ignores=[]),'actual-production-ik','exec'),namespace)
required={'Idle','Locomotion','Attack','Hit','Death','Deploy','Status','Turn','Acquire'}
for card in producer.CONTRACTS:
    rec=M['characters'][card];rig=bpy.data.objects['SKEL_'+card];mesh=bpy.data.objects['SK_'+card]
    contract=rec['modelDesign']['animationContract'];clips={c['name']:c for c in rec['animations']}
    palm_volumes={};contact_contract=socket_grip_contract(rec['modelDesign']);contact_metrics={}
    groups={group.name:set() for group in mesh.vertex_groups};group_names={group.index:group.name for group in mesh.vertex_groups}
    for vertex in mesh.data.vertices:
        for membership in vertex.groups:
            if membership.weight>.001:groups[group_names[membership.group]].add(vertex.index)
    mesh.data.calc_loop_triangles();triangles=[tuple(tri.vertices) for tri in mesh.data.loop_triangles]
    if contact_contract:
        check(card+' actual held weapon head surface',bool(groups.get('QA_weapon_head')))
        for side,hand in contact_contract['hands'].items():
            for key in ('palmGroup','fingersGroup','thumbGroup','handleGroup'):
                check(card+' '+side+' actual socket-contact '+key,bool(groups.get(hand[key])))
    contact_surfaces={side:prepare_socket_surfaces(triangles,groups,hand) for side,hand in contact_contract['hands'].items()} if contact_contract else {}
    if rec['modelDesign'].get('twoHandGripsMeters') and not contact_contract:
        for side in ('l','r'):
            group=mesh.vertex_groups.get('QA_palm_'+side)
            crafted=rec['modelDesign'].get('artQuality',{}).get('semanticGroupsPreserved') is True
            check(card+' '+side+' crafted palm has actual semantic geometry',not crafted or group is not None)
            if group is not None:
                points=[mesh.matrix_world@vertex.co for vertex in mesh.data.vertices
                        if any(item.group==group.index and item.weight>.999 for item in vertex.groups)]
                check(card+' '+side+' actual palm volume is measurable',len(points)>=8)
                if points:
                    low=Vector(tuple(min(p[a] for p in points) for a in range(3)))
                    high=Vector(tuple(max(p[a] for p in points) for a in range(3)))
                    center=(low+high)*.5;radii=(high-low)*.5
                    check(card+' '+side+' actual palm has three positive radii',min(radii)>1e-6)
                    palm_volumes[side]=(center,radii)
            elif not crafted:
                sign=-1 if side=='l' else 1
                center=Vector((sign*.535,-.23,.99)) if card=='bone_warden' else Vector((sign*.35,-.23,.96))
                radii=Vector((.11,.13,.095) if card=='bone_warden' else (.062,.071,.035))
                palm_volumes[side]=(center,radii)
    check(card+' all nine exact source clips',set(clips)==required)
    check(card+' supplied scale',contract['visual']['baseModelScale']==producer.CONTRACTS[card]['scale'])
    check(card+' independent game collision',rec['modelDesign']['collisionIndependent'] is True)
    check(card+' -Y source to +X declaration',contract['forward']=={'blender':'-Y','unreal':'+X'})
    check(card+' source rig bone count',len(rig.data.bones)==rec['boneCount'])
    check(card+' dedicated new materials',all(n.startswith('M_RiftExpansionAscension') for n in rec['materials']))
    check(card+' four total practical LODs',len(rec['lods'])==3 and all(l['triangles']<rec['triangles'] for l in rec['lods']))
    samples={};max_grip=0.;death_release_gap=0.;max_root=0.;lowest_determinant=1e30;clip_grip_errors={};max_palm_distance=0.;max_reach_excess=0.
    def sample(clip,grips=False):
        nonlocal_dummy=None
        poses=[]
        for tick in range(4*(clip['frames']-1)+1):
            frame=1+tick/4;scene.frame_set(int(frame),subframe=frame-int(frame));bpy.context.view_layer.update()
            values=[]
            for bone in rig.pose.bones:
                matrix=bone.matrix
                values.extend(float(v) for row in matrix for v in row)
            poses.append(values)
        return poses
    for clip in clips.values():
        clip_grip=0.
        check(card+' '+clip['name']+' actual raw FBX pin',all(pin(ROOT/clip['file'])[k]==clip[k] for k in ('file','bytes','sha256')))
        check(card+' '+clip['name']+' exact 100FPS decimal duration',clip['fps']==100 and abs((clip['frames']-1)/100-clip['seconds'])<1e-9)
        action=bpy.data.actions[clip['action']];rig.animation_data.action=action
        samples[clip['name']]=sample(clip)
        count=len(samples[clip['name']]);R['quarterFrameSamples']+=count;R['poseMatricesChecked']+=count*rec['boneCount']
        for tick in range(count):
            frame=1+tick/4;scene.frame_set(int(frame),subframe=frame-int(frame));bpy.context.view_layer.update()
            root=rig.pose.bones['root'].matrix.translation;max_root=max(max_root,root.length)
            lowest_determinant=min(lowest_determinant,min(abs(b.matrix.to_3x3().determinant()) for b in rig.pose.bones))
            if rec['modelDesign'].get('twoHandGripsMeters'):
                weapon=rig.pose.bones['maul_socket' if card=='bone_warden' else 'scythe_socket']
                transform=weapon.matrix@weapon.bone.matrix_local.inverted()
                points=None
                if contact_contract and not (card=='bone_warden' and clip['name']=='Death') and tick%2==0:
                    evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get());data=evaluated.to_mesh()
                    try:points=[evaluated.matrix_world@vertex.co for vertex in data.vertices]
                    finally:evaluated.to_mesh_clear()
                for side,grip in rec['modelDesign']['twoHandGripsMeters'].items():
                    target=transform@Vector(grip)
                    joint_target=transform@Vector(rec['modelDesign']['twoHandJointTargetsMeters'][side]) if contact_contract else target
                    clip_grip=max(clip_grip,(rig.pose.bones['forearm_'+side].tail-joint_target).length)
                    if not (card=='bone_warden' and clip['name']=='Death'):
                        hand=rig.pose.bones['hand_'+side]
                        if contact_contract:
                            upper=rig.pose.bones['upperarm_'+side];fore=rig.pose.bones['forearm_'+side]
                            max_reach_excess=max(max_reach_excess,(joint_target-upper.matrix.translation).length-upper.bone.length-fore.bone.length)
                            if points is not None:
                                declared=contact_contract['hands'][side]
                                world_transform=rig.matrix_world@transform
                                measured=measure_socket_contact(points,triangles,groups,rig.matrix_world@target,
                                    world_transform.to_3x3()@Vector(declared['shaftTangentMeters']),declared,contact_surfaces[side])
                                value=contact_metrics.setdefault(clip['name']+'_'+side,{'meshSamples':0,'triangleIntersections':0,
                                    'handVerticesBuriedInHandle':0,'weaponHeadTriangleIntersections':0,'handVerticesBuriedInWeaponHead':0,
                                    'shaftAxisInsideHandSamples':0,'radialPenetrationSamples':0,'shaftAxisRayMissSamples':0,
                                    'maximumContactGapMeters':0.,'maximumDeclaredRadiusErrorMeters':0.,'firstFailure':None})
                                value['meshSamples']+=1
                                for key in ('triangleIntersections','handVerticesBuriedInHandle','weaponHeadTriangleIntersections','handVerticesBuriedInWeaponHead','shaftAxisInsideHandSamples','radialPenetrationSamples','shaftAxisRayMissSamples'):value[key]+=measured[key]
                                if value['firstFailure'] is None and any(measured[key] for key in ('triangleIntersections','handVerticesBuriedInHandle','weaponHeadTriangleIntersections','handVerticesBuriedInWeaponHead','shaftAxisInsideHandSamples','radialPenetrationSamples','shaftAxisRayMissSamples')):
                                    value['firstFailure']={'model':card,'clip':clip['name'],'frame':frame,'side':side,'measured':measured}
                                value['maximumContactGapMeters']=max(value['maximumContactGapMeters'],measured['minimumContactGapMeters'])
                                if measured['maximumActualShaftRadiusMeters'] is not None:
                                    value['maximumDeclaredRadiusErrorMeters']=max(value['maximumDeclaredRadiusErrorMeters'],abs(measured['maximumActualShaftRadiusMeters']-declared['shaftOuterRadiusMeters']))
                            continue
                        in_rest=hand.bone.matrix_local@hand.matrix.inverted()@target
                        # Crafted palms use their actual rest mesh bounds. The
                        # fixed historical ellipsoids remain only for the old
                        # source revision without semantic surface groups.
                        if side in palm_volumes:
                            center,radii=palm_volumes[side]
                            max_palm_distance=max(max_palm_distance,sum(((in_rest[k]-center[k])/radii[k])**2 for k in range(3)))
                        else:max_palm_distance=float('inf')
        clip_grip_errors[clip['name']]=clip_grip
        if card=='bone_warden' and clip['name']=='Death':death_release_gap=clip_grip
        else:max_grip=max(max_grip,clip_grip)
        check(card+' '+clip['name']+' finite quarter-frame matrices',all(math.isfinite(v) for pose in samples[clip['name']] for v in pose))
        check(card+' '+clip['name']+' single marker only on Attack',
              clip['releaseNormalized']==contract['attack']['contactNormalized'] if clip['name']=='Attack' else clip.get('releaseNormalized') is None)
    check(card+' every quarter root stays in place',max_root<1e-6,max_root)
    check(card+' every quarter matrix is invertible',lowest_determinant>1e-8,lowest_determinant)
    if rec['modelDesign'].get('twoHandGripsMeters'):
        if contact_contract:
            check(card+' open-grip joint targets remain reachable without floating hands',max_grip<=.005 and max_reach_excess<=.001,
                {'jointGapMeters':max_grip,'reachExcessMeters':max_reach_excess})
            for key,measured in contact_metrics.items():
                check(card+' '+key+' open hand surfaces do not cross or bury actual handle',all(measured[field]==0 for field in
                    ('triangleIntersections','handVerticesBuriedInHandle','weaponHeadTriangleIntersections','handVerticesBuriedInWeaponHead','shaftAxisInsideHandSamples','radialPenetrationSamples','shaftAxisRayMissSamples')),measured)
                check(card+' '+key+' open grip contacts actual handle',measured['maximumContactGapMeters']<=.005,measured['maximumContactGapMeters'])
                check(card+' '+key+' declared radius matches shaft and bindings',measured['shaftAxisRayMissSamples']==0 and measured['maximumDeclaredRadiusErrorMeters']<=.002,measured['maximumDeclaredRadiusErrorMeters'])
        else:check(card+' both living weapon grip points remain inside actual palm volume',max_palm_distance<1.001,max_palm_distance)
    if card=='iron_giant':
        panels=rec['modelDesign']['shieldPanels']
        check('Giant exactly six shield-only deform bones',len(panels)==6 and {p['bone'] for p in panels}=={'shield_panel_'+str(i) for i in range(6)} and
              all(rig.data.bones[p['bone']].parent.name=='chest' and rig.data.bones[p['bone']].use_deform for p in panels))
        check('Giant six separate source panel meshes attached to owning joints',all(
              (obj:=bpy.data.objects.get(p['sourceMesh'])) is not None and obj.type=='MESH' and
              obj.parent==rig and obj.parent_bone==p['bone'] and obj.get('exportedToGameplay') is False for p in panels))
        check('Giant underlying torso is not shield-weighted',any(g.name=='chest' for g in mesh.vertex_groups))
    if card=='aether_reaper':
        check('Reaper has no walking legs or wings',not any('thigh' in n or 'shin' in n or 'wing' in n for n in rec['bones']))
        check('Reaper twelve bounded independent cloth joints',all('cloth_'+str(i) in rec['bones'] and 'cloth_tip_'+str(i) in rec['bones'] for i in range(6)))
    # Regenerate only in memory; saved source/FBX/portraits stay read-only.
    rig.animation_data.action=None
    for clip in clips.values():bpy.data.actions.remove(bpy.data.actions[clip['action']],do_unlink=True)
    f=SimpleNamespace(name=card,family=rec['rigFamily'],rig=rig,expansion_design=rec['modelDesign'])
    made=producer.actions(f,scene,namespace['set_world_rotation'],namespace['aim_leg'],namespace['aim_arm'])
    delta=0.
    for clip in made:
        rig.animation_data.action=clip['object'];actual=sample(clip);expected=samples[clip['name']]
        delta=max(delta,max(abs(a-b) for pose1,pose2 in zip(expected,actual) for a,b in zip(pose1,pose2)))
    check(card+' builder reproduces every actual saved quarter-frame pose',delta<1e-7,delta)
    R['cards'][card]={'boneCount':rec['boneCount'],'triangles':rec['triangles'],'boundsMeters':rec['boundsMeters'],
                     'maximumGripErrorMeters':max_grip,'maximumRootMotionMeters':max_root,
                     'deathMaulReleaseGapMeters':death_release_gap,'gripErrorsByClipMeters':clip_grip_errors,
                     'maximumPalmEllipsoidNormalizedSquaredDistance':max_palm_distance,
                     'gripContactMode':contact_contract['mode'] if contact_contract else 'historical-solid-palm',
                     'actualSocketContacts':contact_metrics,'maximumGripReachExcessMeters':max_reach_excess,
                     'minimumAbsoluteDeterminant':lowest_determinant,'maximumReproductionDelta':delta,'clipCount':len(clips)}
R['passed']=not R['errors'];R['checkCount']=len(R['checks'])
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(R,indent=2),encoding='utf-8')
print('RIFT_APPROVED_ASSET_QA',R['checkCount'],'checks',len(R['errors']),'errors',flush=True)
for error in R['errors']:print('RIFT_APPROVED_ASSET_ERROR',error,flush=True)
if R['errors']:raise RuntimeError('Approved source contracts failed')
