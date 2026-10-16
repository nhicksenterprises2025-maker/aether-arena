"""Render the four approved cards and Skeleton identity from final source rigs.

No raster generation, copied asset, fake spell creature or old portrait overwrite.
Blender --background --python this-file -- --cards bone_warden ...
"""
from pathlib import Path
import argparse, hashlib, json, math, sys
import bpy
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Build'))
from asset_file_io import write_json
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cards',nargs='+',default=['bone_warden','aether_reaper','iron_giant','shadow_surge','bone_warden_skeleton'])
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
allowed={'bone_warden','aether_reaper','iron_giant','shadow_surge','bone_warden_skeleton'}
if set(args.cards)-allowed:raise ValueError('Unapproved portrait scope')
manifest_path=ROOT/'Assets/asset_manifest.json';M=json.loads(manifest_path.read_text(encoding='utf-8'))
portrait_path=ROOT/'Assets/Source/CardArt/model_portraits.json';P=json.loads(portrait_path.read_text(encoding='utf-8'))
R={'schema':1,'sourceBlend':M['source'],'method':'Final production meshes, authored poses, perspective studio scenes','cards':{}}

def pin(path):
    raw=path.read_bytes();return {'file':path.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}

R['renderer']=pin(Path(__file__))
for folder in ('Assets/Source/CardArt/Masters','Assets/Source/CardArt/Scenes','Assets/Source/CardArt/Children','Assets/Renders/Expansion/Small','Assets/Renders/Expansion/Identity'):
    (ROOT/folder).mkdir(parents=True,exist_ok=True)

def curves(action):
    return list(action.fcurves) if hasattr(action,'fcurves') else [c for l in action.layers for s in l.strips for b in s.channelbags for c in b.fcurves]

for card in args.cards:
    # Reopen for each card, preventing studio copies from contaminating source.
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/M['source']['file']))
    scene=bpy.data.scenes.new('Portrait_'+card);bpy.context.window.scene=scene
    studio=bpy.data.collections.new('PortraitStudio_'+card);scene.collection.children.link(studio)
    scene.render.engine='CYCLES';scene.cycles.samples=64;scene.cycles.use_denoising=True
    scene.cycles.use_adaptive_sampling=True;scene.cycles.adaptive_threshold=.025
    scene.cycles.sample_clamp_indirect=4;scene.cycles.device='CPU'
    try:
        pref=bpy.context.preferences.addons['cycles'].preferences
        pref.compute_device_type='OPTIX';pref.get_devices()
        if any(d.type=='OPTIX' for d in pref.devices):
            for d in pref.devices:d.use=d.type=='OPTIX'
            scene.cycles.device='GPU'
    except (TypeError,RuntimeError):pass
    scene.render.resolution_x=1024;scene.render.resolution_y=1280;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.fps=100
    scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
    world=bpy.data.worlds.new('PortraitWorld_'+card);scene.world=world;world.use_nodes=True
    mood={'bone_warden':(.022,.043,.035),'aether_reaper':(.029,.020,.061),
          'iron_giant':(.041,.037,.033),'shadow_surge':(.025,.016,.054),
          'bone_warden_skeleton':(.022,.043,.035)}[card]
    world.node_tree.nodes['Background'].inputs[0].default_value=(*mood,1)
    world.node_tree.nodes['Background'].inputs[1].default_value=.28
    subjects=[];sources=[]
    def material(name,color,metal=0,rough=.75,emission=0):
        mat=bpy.data.materials.new(name);mat.use_nodes=True;p=mat.node_tree.nodes['Principled BSDF']
        p.inputs['Base Color'].default_value=(*color,1);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal
        if emission:p.inputs['Emission Color'].default_value=(*color,1);p.inputs['Emission Strength'].default_value=emission
        return mat
    def own(obj):
        for col in list(obj.users_collection):col.objects.unlink(obj)
        studio.objects.link(obj);return obj
    def character(model,loc=(0,0,0),scale=1,action='Idle',phase=.16,rotation=0,tag='hero',cutaway=False,veil=False):
        rig=bpy.data.objects['SKEL_'+model].copy();rig.data=rig.data.copy();rig.name='Portrait_'+card+'_Rig_'+tag
        studio.objects.link(rig)
        mesh=bpy.data.objects['SK_'+model].copy();mesh.data=mesh.data.copy();mesh.name='Portrait_'+card+'_Model_'+tag
        studio.objects.link(mesh);mesh.parent=rig;mesh.matrix_parent_inverse.identity()
        for modifier in mesh.modifiers:
            if modifier.type=='ARMATURE':modifier.object=rig
        rig.location=loc;rig.scale=(scale,)*3;rig.rotation_euler.z=rotation
        clip=next(c for c in M['characters'][model]['animations'] if c['name']==action)
        copied=bpy.data.actions[clip['action']].copy();copied.name=rig.name+'_PortraitPose'
        shift=round((clip['frames']-1)*phase)
        for curve in curves(copied):
            for key in curve.keyframe_points:key.co.x-=shift;key.handle_left.x-=shift;key.handle_right.x-=shift
        rig.animation_data_create();rig.animation_data.action=copied
        if cutaway:
            # Source-only illustration cutaway: exactly one shielded Giant.
            for curve in curves(copied):
                if curve.data_path=='pose.bones["shield_panel_0"].scale':
                    for key in curve.keyframe_points:key.co.y=.001;key.handle_left.y=.001;key.handle_right.y=.001
        if veil:
            subdued=material('IllustratedVioletVeil_'+tag,(.055,.04,.115),rough=.67)
            mesh.data.materials.clear();mesh.data.materials.append(subdued)
        rig.hide_render=mesh.hide_render=False
        sources.append({'model':M['characters'][model]['mesh'],'action':action,'phase':phase,
                        'instance':tag,'scale':scale,'positionMeters':list(loc),'phaseOffsetFrames':shift,
                        'poseSourceFrame':shift+1,'evaluatedAtFrame':1,'stagedAction':copied.name,
                        'illustrationOnlyShieldCutaway':cutaway,'illustrationOnlyAffectedAlly':veil})
        subjects.append(mesh);return rig,mesh
    def static(model,loc=(0,0,0),scale=1,rotation=0,tag=''):
        mesh=bpy.data.objects['SM_'+model].copy();mesh.data=mesh.data.copy();mesh.name='Portrait_'+card+'_FX_'+tag
        studio.objects.link(mesh);mesh.location=loc;mesh.scale=(scale,)*3;mesh.rotation_euler.z=rotation;mesh.hide_render=False
        sources.append({'model':M['statics'][model],'illustrationOnlyEffect':True,'instance':tag})
        subjects.append(mesh);return mesh
    if card=='bone_warden':
        marker=M['characters'][card]['modelDesign']['animationContract']['attack']['contactNormalized']
        character(card,(0,-.32,0),1.5,action='Attack',phase=max(0,marker-.16),rotation=-.13)
        for i,(x,y,z) in enumerate(((-1.04,.48,0),(.91,.48,0),(-1.32,1.12,.025),(1.18,1.12,.025))):
            character('bone_warden_skeleton',(x,y,z),.65,action='Deploy',phase=.32,rotation=(i-1.5)*.10,tag='summon_'+str(i+1))
        composition='One imposing Warden; four subordinate Skeleton death-summon silhouettes behind the green ribs'
    elif card=='aether_reaper':
        character(card,scale=1.10,action='Attack',phase=.22/.60-.13)
        composition='One airborne hooded Reaper; attached silver crescent scythe toward lower right; quiet violet studio'
    elif card=='iron_giant':
        character(card,scale=1.,cutaway=True,action='Locomotion',phase=.15,rotation=-.08)
        composition='One broad automaton, intact cool steel shell with one small cutaway showing warm underlying body'
    elif card=='bone_warden_skeleton':
        character(card,scale=.65,action='Attack',phase=.12/.32)
        composition='Subordinate Skeleton identity; one chipped sword, ivory bones and bronze cuff; not a deck card'
    else:
        character('ironclad',(-.58,.02,0),.73,rotation=-.10,tag='ally_1',veil=True)
        character('twin_blades',(.59,.17,0),.80,rotation=.13,tag='ally_2',veil=True)
        character('bone_warden_skeleton',(0,.68,0),.92,tag='ally_3',veil=True)
        static('shadow_surge_ribbon',scale=1.10,tag='circular_spell_motif')
        for i in range(3):
            a=i*math.tau/3+.30
            static('shadow_surge_wisp',(math.cos(a)*.83,math.sin(a)*.83,.09),1.55,rotation=a+math.pi,tag='wisp_'+str(i+1))
        composition='Spell illustration: three affected ally silhouettes in a readable low violet circular veil; no spawned creature'
    slate=material('PortraitSlate_'+card,mood,rough=.83)
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.028));floor=own(bpy.context.object);floor.name='QuietSlateGround_'+card;floor.data.materials.append(slate)
    # Quiet colored studio floor matches the roster; no placeholder scenery.
    if card=='bone_warden':
        green=material('FourSoulWisps',(.08,.54,.13),emission=1.2)
        for i,(x,y) in enumerate(((-1.04,.48),(.91,.48),(-1.32,1.12),(1.18,1.12))):
            curve=bpy.data.curves.new('RibToSummonWisp_'+str(i),'CURVE');curve.dimensions='3D';curve.bevel_depth=.014;curve.bevel_resolution=2
            spline=curve.splines.new('BEZIER');spline.bezier_points.add(2)
            for point,co in zip(spline.bezier_points,((0,-.1,1.7),(x*.55,y*.55,1.25),(x,y,.60))):
                point.co=co;point.handle_left_type=point.handle_right_type='AUTO'
            obj=bpy.data.objects.new(curve.name,curve);studio.objects.link(obj);obj.data.materials.append(green)
    for name,loc,power,color,size in (
        ('Key',(3.5,-4.5,6.2),1250,(1,.86,.70),3.5),
        ('Fill',(-3,-1.5,3.2),530,(.57,.70,.89),4.),
        ('Rim',(-1.2,3.7,4.8),1450,(.31,.74,.44) if card=='bone_warden' else (.39,.68,1) if card=='aether_reaper' else (.62,.38,.90),2.4)):
        data=bpy.data.lights.new(card+'_'+name,'AREA');data.energy=power;data.color=color;data.size=size
        obj=bpy.data.objects.new(data.name,data);studio.objects.link(obj);obj.location=loc
        obj.rotation_euler=(Vector((0,0,1.))-obj.location).to_track_quat('-Z','Y').to_euler()
    camera=bpy.data.objects.new('PortraitCamera_'+card,bpy.data.cameras.new('PortraitCamera_'+card));studio.objects.link(camera);scene.camera=camera
    camera.data.type='PERSP';camera.data.lens=58
    scene.frame_set(1);bpy.context.view_layer.update();points=[];graph=bpy.context.evaluated_depsgraph_get()
    for mesh in subjects:
        evaluated=mesh.evaluated_get(graph);data=evaluated.to_mesh();points.extend(evaluated.matrix_world@v.co for v in data.vertices);evaluated.to_mesh_clear()
    low=Vector(tuple(min(p[k] for p in points) for k in range(3)));high=Vector(tuple(max(p[k] for p in points) for k in range(3)))
    target=(low+high)*.5;direction=Vector((3.1,-8,3.1 if card!='shadow_surge' else 5.1)).normalized();distance=max(.8,(high-low).length*.80)
    for _ in range(80):
        camera.location=target+direction*distance;camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();bpy.context.view_layer.update()
        projected=[world_to_camera_view(scene,camera,p) for p in points]
        if min(p.x for p in projected)>.065 and max(p.x for p in projected)<.935 and min(p.y for p in projected)>.14 and max(p.y for p in projected)<.83:break
        distance*=1.035
    scene['cardId']=card;scene['subjectCount']=len(subjects);scene['productionSourceSHA256']=M['source']['sha256']
    scene['designForward']='Blender -Y; imported Unreal +X';scene['spellIllustration']=card=='shadow_surge'
    if card in M['characters']:scene['modelSourceSHA256']=M['characters'][card]['mesh']['sha256']
    master=ROOT/'Assets/Source/CardArt/Masters'/(card+'.png');scene.render.filepath=str(master)
    bpy.ops.render.render(write_still=True,scene=scene.name)
    scene_path=ROOT/'Assets/Source/CardArt/Scenes'/(card+'_Portrait.blend')
    # Save an ordinary startup-ready file without orphan factory scenes.
    for other in list(bpy.data.scenes):
        if other!=scene:bpy.data.scenes.remove(other)
    # New editable scenes travel with their actual authored atlas pixels.
    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(scene_path),compress=True)
    runtime=ROOT/('Assets/Source/CardArt/Children' if card=='bone_warden_skeleton' else 'Assets/Source/CardArt')/(card+'.png')
    small=ROOT/'Assets/Renders/Expansion/Small'/(card+'_96x120.png')
    image=bpy.data.images.load(str(master),check_existing=False);image.scale(768,960);image.filepath_raw=str(runtime);image.save()
    image.scale(96,120);image.filepath_raw=str(small);image.save();bpy.data.images.remove(image)
    record={**pin(runtime),'sourceSceneSHA256':M['source']['sha256'],'sources':json.loads(json.dumps(sources)),
            'boundsMeters':[list(low),list(high)],'poseFrame':1,'cameraProjection':'Perspective','cameraDistance':distance,
            'composition':composition,'subjectCount':len(subjects),'master':pin(master),'editableScene':pin(scene_path),
            'smallReview':pin(small),'runtimeResolution':[768,960],'masterResolution':[1024,1280],
            'safeZones':{'topLeftBadge':.20,'bottomName':.14},'rendererScript':R['renderer']}
    if card!='bone_warden_skeleton':
        M['illustrations'][card]={key:record[key] for key in ('file','bytes','sha256','sourceSceneSHA256')}
        P['cards'][card]=record
    R['cards'][card]=record
    # Individual actual-model identity views; no portrait summons or staging.
    for obj in list(studio.objects):
        if obj.type in ('MESH','CURVE'):obj.hide_render=True
    if card!='shadow_surge':
        rig,mesh=character(card,action='Idle',phase=0,tag='identity')
        identity_meshes=[mesh];bounds=M['characters'][card]['boundsMeters'];height=bounds[2]
        target=Vector((0,0,height*.48));ortho=max(height*1.42,max(bounds[:2])*1.42)
    else:
        # Spell identity contains only the real source ring and three wisps.
        identity_meshes=[static('shadow_surge_ribbon',tag='identity_ring')]
        for i in range(3):
            angle=i*math.tau/3+.30
            identity_meshes.append(static('shadow_surge_wisp',(math.cos(angle)*.76,math.sin(angle)*.76,.09),
                                         1.25,rotation=angle+math.pi,tag='identity_wisp_'+str(i)))
        target=Vector((0,0,.10));ortho=2.7
    scene.frame_set(1);bpy.context.view_layer.update();camera.data.type='ORTHO';camera.data.ortho_scale=ortho
    scene.render.resolution_x=512;scene.render.resolution_y=512;scene.cycles.samples=32;views=[]
    for label,offset in (('front',(0,-6,1.5)),('rear',(0,6,1.5)),('side',(6,0,1.5)),('threequarter',(4,-6,3.5))):
        camera.location=target+Vector(offset);camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        path=ROOT/'Assets/Renders/Expansion/Identity'/(card+'_'+label+'.png');scene.render.filepath=str(path)
        bpy.ops.render.render(write_still=True,scene=scene.name);views.append({'view':label,**pin(path)})
    silhouette=material('Silhouette',(.006,.009,.012))
    for mesh in identity_meshes:mesh.data.materials.clear();mesh.data.materials.append(silhouette)
    path=ROOT/'Assets/Renders/Expansion/Identity'/(card+'_silhouette.png');scene.render.filepath=str(path)
    bpy.ops.render.render(write_still=True,scene=scene.name);views.append({'view':'silhouette',**pin(path)})
    if card!='shadow_surge':M['characters'][card]['identityViews']=views
    else:
        # No skeletal/creature record is introduced for this spell.
        M['characters'].pop(card,None);M['cards'][card]['identityViews']=views
    print('RIFT_APPROVED_CARD_ART',card,len(subjects),flush=True)
write_json(manifest_path,M)
write_json(portrait_path,P)
write_json(ROOT/'Assets/Renders/Expansion/approved-portrait-production.json',R)
print('RIFT_APPROVED_CARD_ART_COMPLETE',len(args.cards),flush=True)
