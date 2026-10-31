"""Render and measure the exact final Dart Gun mesh and baked pose actions."""
from pathlib import Path
import bpy, json, math, hashlib
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
manifest=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/manifest['source']['file']))
scene=bpy.context.scene
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
try:
    prefs=bpy.context.preferences.addons['cycles'].preferences
    prefs.compute_device_type='OPTIX';prefs.get_devices()
    for device in prefs.devices:device.use=device.type=='OPTIX'
    scene.cycles.device='GPU'
except (TypeError,RuntimeError):pass
scene.render.resolution_x=960;scene.render.resolution_y=960;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.025,.04,.03,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.35
for col in bpy.data.collections:col.hide_render=col.name!='dart_gun'
studio=bpy.data.collections.new('DartGunReview');scene.collection.children.link(studio)
for name,loc,energy,color,size in [('Key',(3,-4,5),1000,(1,.91,.8),4),('Fill',(-3,-3,2.6),450,(.65,.8,1),3),('Rim',(1,2,3),1100,(.75,.9,.65),3)]:
    data=bpy.data.lights.new(name,'AREA');data.energy=energy;data.color=color;data.size=size
    obj=bpy.data.objects.new(name,data);studio.objects.link(obj);obj.location=loc
    obj.rotation_euler=(Vector((0,0,1))-obj.location).to_track_quat('-Z','Y').to_euler()
camera=bpy.data.objects.new('DartReviewCamera',bpy.data.cameras.new('DartReviewCamera'))
studio.objects.link(camera);camera.data.type='ORTHO';scene.camera=camera
rig=bpy.data.objects['SKEL_dart_gun'];mesh=bpy.data.objects['SK_dart_gun']
rig.hide_render=False;mesh.hide_render=False
output=ROOT/'Artifacts/QA/DartGun190/Art';output.mkdir(parents=True,exist_ok=True)
views=[('full', 'Idle', 1, (0,-.13,1.), (3.5,-7,2.7),2.2),
       ('face','Idle',1,(0,-.04,1.74),(1.7,-7,1.2),.62),
       ('grip','Attack',14,(.14,-.40,1.26),(4,-7,2.3),.95),
       ('walk','Locomotion',22,(0,-.12,1.),(3.5,-7,2.5),2.2),
       ('fire','Attack',16,(0,-.15,1.22),(3.5,-7,2.0),1.6)]
records=[]
for name,clip,frame,target,offset,size in views:
    rig.animation_data_create();rig.animation_data.action=bpy.data.actions['AN_dart_gun_'+clip]
    scene.frame_set(frame);bpy.context.view_layer.update()
    target=Vector(target);camera.location=target+Vector(offset)
    camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=size
    dest=output/(name+'.png');scene.render.filepath=str(dest);bpy.ops.render.render(write_still=True)
    records.append({'file':dest.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
                    'clip':clip,'frame':frame})
# Every pose matrix must remain finite and invertible at integer and half frames.
matrices=0;bad=[]
for clip in manifest['characters']['dart_gun']['animations']:
    rig.animation_data.action=bpy.data.actions[clip['action']]
    for step in range(2*(clip['frames']-1)+1):
        frame=1+step*.5;scene.frame_set(int(frame),subframe=frame%1)
        bpy.context.view_layer.update()
        for bone in rig.pose.bones:
            matrices+=1
            if not all(math.isfinite(v) for row in bone.matrix for v in row) or abs(bone.matrix.determinant())<1e-8:
                bad.append([clip['name'],frame,bone.name])
proof={'schema':1,'passed':not bad,'source':manifest['source'],'model':manifest['characters']['dart_gun'],
       'renderedViews':records,'poseMatricesChecked':matrices,'invalidMatrices':bad,
       'visualInspection':'Separate direct image review required; matrix checks do not establish art quality.'}
(output/'model-review.json').write_text(json.dumps(proof,indent=2),encoding='utf-8')
if bad:raise RuntimeError('Invalid baked pose matrices')
print('DART_GUN_REVIEW_COMPLETE',matrices,flush=True)
