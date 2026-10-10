"""Run in Blender after generate_assets.py. Audits authored source AND exported FBX files."""
from pathlib import Path
import bpy, json, math, hashlib
from io_scene_fbx import parse_fbx
ROOT=Path(__file__).resolve().parents[1]
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text())
def source_pin(path):
    raw=path.read_bytes()
    return {'file':path.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
report={'schema':1,'version':M.get('build','').removeprefix('UE-'),
        'provenance':{'assetManifest':source_pin(ROOT/'Assets/asset_manifest.json'),
                      'sourceBlend':source_pin(ROOT/M['source']['file']),
                      'validatorScript':source_pin(Path(__file__).resolve())},
        'checks':[],'characters':{},'statics':{},'errors':[],
        'unrealImportScale':1,'unrealImportUnitConversion':True,'visualReview':'Actual rendered contact sheets reviewed separately; numerical checks do not establish production art quality.'}
def check(name,ok,detail=''):
    report['checks'].append({'name':name,'passed':bool(ok),'detail':detail})
    if not ok:report['errors'].append(name+': '+str(detail))
def parse(file):
    path=ROOT/file;tree,version=parse_fbx.parse(str(path));return tree
def direct(tree,name):return next((x for x in tree.elems if x.id==name),None)
def count(tree,name):
    return int(tree.id==name)+sum(count(x,name) for x in tree.elems)
def unit(file):
    tree=parse(file);settings=direct(tree,b'GlobalSettings');props=direct(settings,b'Properties70')
    return next(x.props[-1] for x in props.elems if x.props[0]==b'UnitScaleFactor')
def verified_file(entry,label):
    path=ROOT/entry['file'];check(label+' exists',path.exists())
    if path.exists():check(label+' SHA256',hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256'])

verified_file(M['source'],'Production scene')
bpy.ops.wm.open_mainfile(filepath=str(ROOT/M['source']['file']))
required={'Idle','Locomotion','Attack','Hit','Death','Deploy','Status','Turn','Acquire'}
for name,record in M['characters'].items():
    mesh=bpy.data.objects.get('SK_'+name);rig=bpy.data.objects.get('SKEL_'+name)
    check(name+' source mesh',mesh is not None and mesh.type=='MESH')
    check(name+' source skeleton',rig is not None and rig.type=='ARMATURE')
    if not mesh or not rig:continue
    dims=list(mesh.dimensions);vertices=len(mesh.data.vertices)
    check(name+' dimension manifest',max(abs(a-b) for a,b in zip(dims,record['boundsMeters']))<1e-5,dims)
    check(name+' finite vertices',all(all(math.isfinite(x) for x in v.co) for v in mesh.data.vertices))
    check(name+' UV layer',bool(mesh.data.uv_layers))
    check(name+' finite UVs',all(all(math.isfinite(x) and -.0001<=x<=1.0001 for x in uv.uv) for uv in mesh.data.uv_layers.active.data))
    check(name+' consolidated material slots',1<=len(mesh.data.materials)<=3,len(mesh.data.materials))
    check(name+' armature modifier',any(mod.type=='ARMATURE' and mod.object==rig for mod in mesh.modifiers))
    deform={b.name for b in rig.data.bones if b.use_deform}
    groups={g.index:g.name for g in mesh.vertex_groups}
    unweighted=[v.index for v in mesh.data.vertices if not any(g.weight>0 and groups.get(g.group) in deform for g in v.groups)]
    check(name+' every vertex skinned',not unweighted,{'vertices':vertices,'unweighted':len(unweighted)})
    check(name+' declared bones present',set(record['bones'])==set(rig.data.bones.keys()))
    check(name+' required sockets',{'attack_origin','hp_anchor','status_anchor','impact_origin'}<=set(record['sockets']))
    check(name+' required actions',required<=set(x['name'] for x in record['animations']))
    verified_file(record['mesh'],name+' mesh FBX')
    scale=unit(record['mesh']['file']);check(name+' FBX explicit units',scale==100.0,scale)
    root=parse(record['mesh']['file']);objects=direct(root,b'Objects')
    skins=[e for e in objects.elems if e.id==b'Deformer' and len(e.props)>2 and e.props[2]==b'Skin']
    check(name+' FBX skin exported',len(skins)==1,len(skins))
    lodtri=[]
    for lod in record['lods']:
        verified_file(lod,name+' LOD'+str(lod['level']));lodtri.append(lod['triangles'])
        check(name+' LOD'+str(lod['level'])+' smaller',0<lod['triangles']<record['triangles'])
    check(name+' three ordered LODs',len(lodtri)==3 and lodtri==sorted(lodtri,reverse=True),lodtri)
    action_stats=[]
    for clip in record['animations']:
        verified_file(clip,name+' '+clip['name']+' FBX')
        tree=parse(clip['file']);objects=direct(tree,b'Objects')
        stacks=[e for e in objects.elems if e.id==b'AnimationStack']
        curves=[e for e in objects.elems if e.id==b'AnimationCurve']
        check(name+' '+clip['name']+' one FBX animation stack',len(stacks)==1,len(stacks))
        check(name+' '+clip['name']+' bone tracks',len(curves)>=len(record['bones'])*3,len(curves))
        action=bpy.data.actions.get(clip['action']);check(name+' '+clip['name']+' source action',action is not None)
        if action:
            rig.animation_data.action=action;scene=bpy.context.scene;matrices=[]
            for frame in (1,max(2,clip['frames']//2)):
                scene.frame_set(frame);bpy.context.view_layer.update()
                matrices.append([tuple(x for row in b.matrix for x in row) for b in rig.pose.bones])
            changed=sum(any(abs(a-b)>1e-6 for a,b in zip(x,y)) for x,y in zip(matrices[0],matrices[1]))
            check(name+' '+clip['name']+' animated pose',changed>0,changed)
            action_stats.append({'action':clip['name'],'movingBones':changed,'fbxCurves':len(curves),'durationSeconds':clip['seconds']})
    rig.animation_data.action=None;bpy.context.scene.frame_set(1)
    report['characters'][name]={'vertices':vertices,'triangles':record['triangles'],'boneCount':len(rig.data.bones),
                               'materialSlots':len(mesh.data.materials),'boundsMeters':dims,'actions':action_stats,'lodTriangles':lodtri}
for name,record in M['statics'].items():
    verified_file(record,name+' static FBX');check(name+' static FBX units',unit(record['file'])==100)
    check(name+' static materials',1<=len(record['materials'])<=3,len(record['materials']))
    for lod in record['lods']:verified_file(lod,name+' static LOD'+str(lod['level']))
    report['statics'][name]={'triangles':record['triangles'],'materialSlots':len(record['materials']),'boundsMeters':record['boundsMeters']}
for channel in ('BaseColor','Normal','ORM','TeamMask'):
    p=ROOT/'Assets/Source/Textures'/('T_RiftAtlas_'+channel+'.png');check(channel+' texture source',p.exists())
check('All troop identities',set(('ironclad','ember_archer','twin_blades','boulderback','arc_mage','rambeast','sky_manta','vampire_bats','frost_fang','storm_raven','mini_stampede','stampede'))<=set(M['characters']))
check('All crown/building/spell identities',set(('tower_guard','tower_core','archer_tower','nova_flask','meteor_shard','bullet_round'))<=set(M['statics']))
# Empirical FBX roundtrip in Blender is independent of source manifests and confirms unit handling.
expected=M['characters']['ironclad']['boundsMeters']
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(ROOT/M['characters']['ironclad']['mesh']['file']),use_anim=False)
mesh=next(o for o in bpy.context.scene.objects if o.type=='MESH');actual=list(mesh.dimensions)
check('Ironclad FBX unit roundtrip',max(abs(a-b) for a,b in zip(expected,actual))<.002,{'sourceMeters':expected,'importMeters':actual,'fbxUnitScaleFactor':100})
for name,pin in report['provenance'].items():
    check(name+' unchanged through validation',source_pin(ROOT/pin['file'])==pin)
report['passed']=not report['errors'];report['checkCount']=len(report['checks'])
(ROOT/'Assets/asset_qa_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('RIFT_ASSET_QA',report['checkCount'],'checks;',len(report['errors']),'errors')
for err in report['errors']:print('RIFT_ASSET_ERROR',err)
if report['errors']:raise RuntimeError('Asset QA failed')
