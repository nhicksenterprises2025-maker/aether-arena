"""Focused read-only exported Iron Giant/LOD/shield inspection in Blender."""
from pathlib import Path
import argparse, hashlib, json, math, sys
import bpy
from mathutils import Vector
from io_scene_fbx import parse_fbx

ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
out=args.output.resolve();out.relative_to(ROOT/'Artifacts/QA')
if out.exists():raise ValueError('Focused export review must use a fresh output folder')
out.mkdir(parents=True)
def pin(path):
    p=Path(path);raw=p.read_bytes()
    return {'file':p.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text())
g=M['characters']['iron_giant'];source=ROOT/M['source']['file']
R={'schema':1,'scope':'Actual Iron Giant Source and exported four skeletal FBX LODs; imported shield-bone visibility and pose renders, no native gameplay claim',
   'source':pin(source),'manifest':pin(ROOT/'Assets/asset_manifest.json'),'verifier':pin(Path(__file__)),
   'checks':[],'errors':[],'exports':[],'renders':[]}
def check(name,ok,detail=None):
    R['checks'].append({'name':name,'passed':bool(ok),'detail':detail})
    if not ok:R['errors'].append(name)
def direct(node,name):return next((n for n in node.elems if n.id==name),None)
check('Source raw bytes equal manifest',R['source']==M['source'])
bpy.ops.wm.open_mainfile(filepath=str(source))
scene=bpy.context.scene;mesh=bpy.data.objects['SK_iron_giant'];rig=bpy.data.objects['SKEL_iron_giant']
deform={b.name for b in rig.data.bones if b.use_deform}
groups={gr.index:gr.name for gr in mesh.vertex_groups}
check('Exactly 34 authored bones and six chest-owned shield bones',len(rig.data.bones)==34 and all(
      rig.data.bones['shield_panel_'+str(i)].parent.name=='chest' for i in range(6)))
check('All Source vertices have finite positive owning deform weights',all(
      any(groups.get(w.group) in deform and math.isfinite(w.weight) and w.weight>0 for w in v.groups) for v in mesh.data.vertices))
preserved=json.loads((ROOT/'Assets/Source/Revisions/DesignUpdate1.7.0/iron-giant-replacement-visor-clearance.json').read_text())
check('Source studio retains exact pre-replacement timing',scene.render.fps==preserved['preservedSceneTiming']['fps'],scene.render.fps)
check('All nine actual clip exports declare 100 FPS',all(c['fps']==100 for c in g['animations']))
check('Nine clips remain in place with single attack contact and 0.8 second death',
      len(g['animations'])==9 and g['modelDesign']['rootMotion'] is False and
      g['modelDesign']['animationContract']['death']['cleanupSeconds']==.8)
for i in range(6):
    bn='shield_panel_'+str(i);group=mesh.vertex_groups['QA_'+bn].index
    indices=[v.index for v in mesh.data.vertices if any(w.group==group and w.weight>0 for w in v.groups)]
    check('Entire detailed shell exclusively follows '+bn,bool(indices) and all(
          {groups[w.group] for w in mesh.data.vertices[j].groups if groups[w.group] in deform and w.weight>0}=={bn} for j in indices))
    editable=bpy.data.objects['SOURCE_iron_giant_'+bn]
    check('Editable panel stays attached to '+bn,editable.parent==rig and editable.parent_bone==bn and editable.hide_render)
for clip in g['animations']:
    p=ROOT/clip['file'];tree,version=parse_fbx.parse(str(p));objects=direct(tree,b'Objects')
    check(clip['name']+' exported bytes are current',pin(p)=={k:clip[k] for k in ('file','bytes','sha256')})
    check(clip['name']+' has one real animation stack and full bone curves',
          sum(n.id==b'AnimationStack' for n in objects.elems)==1 and
          sum(n.id==b'AnimationCurve' for n in objects.elems)>=34*3)

# A private in-memory studio renders imported meshes. The accepted Source is
# never saved here and the imported LODs are the actual exported FBX payloads.
for col in bpy.data.collections:col.hide_render=True
studio=bpy.data.collections.new('IronExportReview');scene.collection.children.link(studio)
camdata=bpy.data.cameras.new('IronExportCamera');camera=bpy.data.objects.new(camdata.name,camdata)
studio.objects.link(camera);scene.camera=camera;camdata.type='ORTHO';camdata.ortho_scale=2.2
for label,pos,energy,size in [('key',(3,-4,6),650,5),('fill',(-4,-2,3),350,4),('rim',(1,4,5),550,3)]:
    data=bpy.data.lights.new('IronExport_'+label,'AREA');data.energy=energy;data.shape='DISK';data.size=size
    ob=bpy.data.objects.new(data.name,data);studio.objects.link(ob);ob.location=pos
    ob.rotation_euler=(Vector((0,0,.65))-ob.location).to_track_quat('-Z','Y').to_euler()
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=640;scene.render.resolution_y=640
scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.render.film_transparent=False
scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.095,.12,.15,1)
scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.35
target=Vector((0,0,.70))
def render(label,direction):
    camera.location=target+Vector(direction)
    camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    p=out/(label+'.png');scene.render.filepath=str(p);bpy.ops.render.render(write_still=True)
    R['renders'].append({'label':label,**pin(p)})
for level,entry in enumerate([g['mesh']]+g['lods']):
    p=ROOT/entry['file'];tree,version=parse_fbx.parse(str(p));objects=direct(tree,b'Objects')
    geometry=next(n for n in objects.elems if n.id==b'Geometry' and n.props[-1]==b'Mesh')
    verts=list(direct(geometry,b'Vertices').props[0]);indices=list(direct(geometry,b'PolygonVertexIndex').props[0])
    count=len(verts)//3;covered=set();invalid=[]
    clusters=[n for n in objects.elems if n.id==b'Deformer' and n.props[-1]==b'Cluster']
    for cl in clusters:
        ci=direct(cl,b'Indexes');cw=direct(cl,b'Weights')
        if ci is None or cw is None:continue
        for index,weight in zip(ci.props[0],cw.props[0]):
            if not math.isfinite(weight) or weight<0 or index<0 or index>=count:invalid.append((index,weight))
            if weight>0:covered.add(index)
    settings=direct(direct(tree,b'GlobalSettings'),b'Properties70')
    units=next(n.props[-1] for n in settings.elems if n.props[0]==b'UnitScaleFactor')
    triangles=0;face=0
    for index in indices:
        face+=1
        if index<0:triangles+=face-2;face=0
    check('LOD'+str(level)+' exact current exported pin',pin(p)=={k:entry[k] for k in ('file','bytes','sha256')})
    check('LOD'+str(level)+' actual explicit meter units',units==100)
    check('LOD'+str(level)+' every actual exported vertex is positively skinned',covered==set(range(count)) and not invalid,
          {'vertices':count,'covered':len(covered),'invalid':len(invalid)})
    expected=g['triangles'] if level==0 else entry['triangles']
    check('LOD'+str(level)+' actual polygon triangle total',triangles==expected,triangles)
    check('LOD'+str(level)+' finite actual vertices',all(math.isfinite(v) for v in verts))
    old=set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(p),use_anim=False,use_image_search=False)
    made=set(bpy.data.objects)-old
    impmesh=next(ob for ob in made if ob.type=='MESH');imprig=next(ob for ob in made if ob.type=='ARMATURE')
    for ob in made:
        for col in list(ob.users_collection):col.objects.unlink(ob)
        studio.objects.link(ob);ob.hide_render=False
    check('LOD'+str(level)+' empirical FBX size stays in meters',max(abs(a-b) for a,b in zip(impmesh.dimensions,g['boundsMeters']))<.02,list(impmesh.dimensions))
    before_bounds=list(impmesh.dimensions)
    if level in (0,2):
        for view,offset in [('front',(0,-6,1.5)),('rear',(0,6,1.5)),('left',(-5,-6,3)),('right',(5,-6,3))]:
            render('LOD'+str(level)+'-intact-'+view,offset)
        for i in range(6):imprig.pose.bones['shield_panel_'+str(i)].scale=(.000001,)*3
        bpy.context.view_layer.update()
        render('LOD'+str(level)+'-shield-broken-front',(0,-6,1.5))
        render('LOD'+str(level)+'-shield-broken-rear',(0,6,1.5))
        check('LOD'+str(level)+' body persists after all shield bones shrink',len(impmesh.data.vertices)>0 and
              max(impmesh.dimensions)>1.0 and all(imprig.pose.bones['shield_panel_'+str(i)].scale.x<.000002 for i in range(6)))
    R['exports'].append({'level':level,'vertices':count,'triangles':triangles,'skinClusters':len(clusters),**pin(p)})
    for ob in made:bpy.data.objects.remove(ob,do_unlink=True)
check('Source unchanged during focused exported review',pin(source)==R['source'])
check('Manifest unchanged during focused exported review',pin(ROOT/'Assets/asset_manifest.json')==R['manifest'])
R['passed']=not R['errors'];R['checkCount']=len(R['checks'])
(out/'report.json').write_text(json.dumps(R,indent=2))
print('RIFT_IRON_EXPORT_QA',R['checkCount'],'checks',len(R['errors']),'errors',flush=True)
if R['errors']:raise RuntimeError(', '.join(R['errors']))
