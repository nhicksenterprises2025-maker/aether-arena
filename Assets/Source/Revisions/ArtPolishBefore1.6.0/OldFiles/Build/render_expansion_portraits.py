"""Render selected final production rigs into editable portrait scenes and masters."""
from pathlib import Path
import hashlib
import json
import math
import bpy
from mathutils import Vector


def render_selected(root, selected):
    manifest_path=root/'Assets/asset_manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    bpy.ops.wm.open_mainfile(filepath=str(root/manifest['source']['file']))
    output=root/'Assets/Source/CardArt'
    renders=root/'Assets/Renders/Expansion'
    for folder in (output/'Masters',output/'Scenes',renders/'Small',renders/'Identity'):
        folder.mkdir(parents=True,exist_ok=True)
    report_path=output/'model_portraits.json'
    report=json.loads(report_path.read_text(encoding='utf-8'))
    report['sourceBlend']=manifest['source']
    # Runtime remains the existing supported size; individual records retain
    # their original authored source scene instead of forging global provenance.
    report.update({'resolution':[768,960],'masterResolution':[1024,1280],
                   'method':'Actual final production rig render with editable staged scene',
                   'aiGeneratedRasterArtwork':False})
    renderer=root/'Build/render_card_portraits.py'
    report['rendererScript']=pin(root,renderer)
    expansion_report={'schema':1,'sourceBlend':manifest['source'],
                      'renderer':pin(root,Path(__file__)),'cards':{}}
    for card in selected:
        scene=bpy.data.scenes.new('Portrait_'+card)
        scene.render.engine='CYCLES';scene.cycles.samples=80
        scene.cycles.use_denoising=True;scene.cycles.use_adaptive_sampling=True
        scene.cycles.adaptive_threshold=.025;scene.cycles.sample_clamp_indirect=4
        scene.cycles.device='CPU'
        try:
            devices=bpy.context.preferences.addons['cycles'].preferences
            devices.compute_device_type='OPTIX';devices.get_devices()
            if any(d.type=='OPTIX' for d in devices.devices):
                for device in devices.devices:device.use=device.type=='OPTIX'
                scene.cycles.device='GPU'
        except (TypeError,RuntimeError):pass
        scene.render.resolution_x=1024;scene.render.resolution_y=1280
        scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
        scene.render.film_transparent=False;scene.render.fps=60
        scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
        world=bpy.data.worlds.new('PortraitWorld_'+card);scene.world=world;world.use_nodes=True
        world.node_tree.nodes['Background'].inputs[0].default_value=(.022,.047,.055,1)
        world.node_tree.nodes['Background'].inputs[1].default_value=.38
        studio=bpy.data.collections.new('PortraitStudio_'+card);scene.collection.children.link(studio)
        bpy.context.window.scene=scene
        sources=[];subjects=[]
        def character(loc=(0,0,0),scale=1,action='Locomotion',phase=.18,rotation=0,tag=''):
            rig=bpy.data.objects['SKEL_'+card].copy();rig.data=rig.data.copy()
            rig.name='Portrait_'+card+'_Rig_'+tag;studio.objects.link(rig)
            mesh=bpy.data.objects['SK_'+card].copy();mesh.data=mesh.data.copy()
            mesh.name='Portrait_'+card+'_Model_'+tag;studio.objects.link(mesh)
            mesh.parent=rig;mesh.matrix_parent_inverse.identity()
            for modifier in mesh.modifiers:
                if modifier.type=='ARMATURE':modifier.object=rig
            rig.location=loc;rig.scale=(scale,)*3;rig.rotation_euler.z=rotation
            clip=next(c for c in manifest['characters'][card]['animations'] if c['name']==action)
            rig.animation_data_create();rig.animation_data.action=bpy.data.actions[clip['action']]
            # Separate deterministic NLA action phase per instance is baked into
            # a copied action, not a cosmetic gameplay spawn or timing offset.
            copied=rig.animation_data.action.copy();copied.name=rig.name+'_PortraitPose'
            rig.animation_data.action=copied
            shift=round((clip['frames']-1)*phase)
            if hasattr(copied,'fcurves'):curves=copied.fcurves
            else:curves=[curve for layer in copied.layers for strip in layer.strips
                         for bag in strip.channelbags for curve in bag.fcurves]
            for curve in curves:
                for point in curve.keyframe_points:point.co.x-=shift;point.handle_left.x-=shift;point.handle_right.x-=shift
            rig.hide_render=mesh.hide_render=False
            sources.append({'model':manifest['characters'][card]['mesh'],'action':action,'phase':phase,
                            'instance':tag,'scale':scale,'positionMeters':list(loc),
                            'phaseOffsetFrames':shift,'poseSourceFrame':1+shift,'evaluatedAtFrame':1,
                            'stagedAction':copied.name})
            subjects.append(mesh)
            return rig,mesh
        if card=='mini_stampede':
            for i,(x,y) in enumerate(((0,-1.14),(-.58,.04),(.58,.04),(-.64,1.24),(.64,1.24))):
                character((x,y,0),1.30,phase=.09+i*.065,rotation=-.08,tag=str(i+1))
        elif card=='stampede':
            for row in range(3):
                for col in range(5):
                    i=row*5+col
                    character(((col-2)*.50+(row-1)*.09,(row-1)*1.34,0),.85,
                              phase=.09+(i%5)*.047,rotation=-.05,tag=str(i+1))
        else:
            marker=manifest['characters'][card]['modelDesign']['animationContract']['attack']['contactNormalized']
            character(scale=.95,action='Attack',phase=marker,tag='impact')
        # Quiet environment context frames the final species without obscuring count.
        def material(name,color,metal=0,rough=.85,emission=0):
            mat=bpy.data.materials.new(name);mat.use_nodes=True
            shader=mat.node_tree.nodes.get('Principled BSDF')
            shader.inputs['Base Color'].default_value=(*color,1)
            shader.inputs['Metallic'].default_value=metal;shader.inputs['Roughness'].default_value=rough
            if emission:shader.inputs['Emission Color'].default_value=(*color,1);shader.inputs['Emission Strength'].default_value=emission
            return mat
        slate=material('PortraitSlate_'+card,(.045,.09,.105) if card=='mini_stampede' else (.075,.072,.09))
        bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.02))
        floor=bpy.context.object;floor.name='SlateGround_'+card
        for collection in list(floor.users_collection):collection.objects.unlink(floor)
        studio.objects.link(floor);floor.data.materials.append(slate)
        if card!='pulverizer':
            cliffmat=material('TealCliffs_'+card,(.035,.15,.17))
            for i,(x,y,z,sx,sy,sz) in enumerate(((-2.7,2.8,.5,.6,.8,.9),(2.8,3.6,.7,.7,1.,1.3),(-2.8,4.8,.8,.5,.6,1.6))):
                vertices=[]
                for row,(height,width,shift) in enumerate(((0,1.,0),(.52,.84,.04),(1.,.63,-.055))):
                    for j in range(7):
                        a=j*math.tau/7+.18*row
                        radius=width*(1.+.09*math.sin(j*2.7+i))
                        vertices.append((x+sx*radius*math.cos(a)+shift,y+sy*radius*math.sin(a),sz*height))
                faces=[tuple(reversed(range(7))),tuple(14+j for j in range(7))]
                for row in range(2):
                    for j in range(7):faces.append((row*7+j,row*7+(j+1)%7,(row+1)*7+(j+1)%7,(row+1)*7+j))
                data=bpy.data.meshes.new('TealCliffRock_'+str(i));data.from_pydata(vertices,[],faces);data.update()
                cliff=bpy.data.objects.new('DistantCliff_'+str(i),data);studio.objects.link(cliff);data.materials.append(cliffmat)
                bevel=cliff.modifiers.new('HandCutCliffEdges','BEVEL');bevel.width=.025;bevel.segments=2
        else:
            violet=material('PortraitPressureRing',(.40,.055,.80),rough=.35,emission=2)
            bpy.ops.mesh.primitive_torus_add(major_radius=1.05,minor_radius=.018,major_segments=96,minor_segments=8,location=(0,0,.008))
            ring=bpy.context.object;ring.name='VioletPressureFront'
            for collection in list(ring.users_collection):collection.objects.unlink(ring)
            studio.objects.link(ring);ring.data.materials.append(violet)
            crackmat=material('CrackedStoneGrooves',(.012,.019,.022))
            for j in range(7):
                a=j*math.tau/7+.16
                points=[Vector((math.cos(a)*r,math.sin(a)*r,.001)) for r in (.64,.92,1.12,1.36)]
                points[2]+=Vector((.04,-.025,0))
                curve=bpy.data.curves.new('StoneCrack_'+str(j),'CURVE');curve.dimensions='3D';curve.bevel_depth=.006;curve.bevel_resolution=1
                line=curve.splines.new('POLY');line.points.add(len(points)-1)
                for point,vec in zip(line.points,points):point.co=(*vec,1)
                obj=bpy.data.objects.new(curve.name,curve);studio.objects.link(obj);obj.data.materials.append(crackmat)
        for name,loc,power,color,size in (
            ('WarmKey',(3.6,-4.5,6.2),1300,(1,.80,.61),3.4),
            ('CoolFill',(-3.6,-1.5,4.1),500,(.53,.72,.85),4.0),
            ('TealRim',(-1.5,4,5.8),1550,(.35,.72,.80) if card!='pulverizer' else (.65,.35,.95),2.5)):
            light=bpy.data.lights.new(card+'_'+name,'AREA');light.energy=power;light.color=color;light.size=size
            obj=bpy.data.objects.new(light.name,light);studio.objects.link(obj);obj.location=loc
            obj.rotation_euler=(Vector((0,0,.5))-obj.location).to_track_quat('-Z','Y').to_euler()
        camera=bpy.data.objects.new('PortraitCamera_'+card,bpy.data.cameras.new('PortraitCamera_'+card))
        studio.objects.link(camera);scene.camera=camera
        camera.data.type='PERSP';camera.data.lens=58
        scene.frame_set(1);bpy.context.view_layer.update()
        points=[]
        depsgraph=bpy.context.evaluated_depsgraph_get()
        for mesh in subjects:
            evaluated=mesh.evaluated_get(depsgraph);data=evaluated.to_mesh()
            points.extend(evaluated.matrix_world@v.co for v in data.vertices);evaluated.to_mesh_clear()
        low=Vector(tuple(min(p[k] for p in points) for k in range(3)))
        high=Vector(tuple(max(p[k] for p in points) for k in range(3)))
        target=(low+high)*.5
        target.z+=.08
        direction=Vector((2.8,-7.8,6.0 if card=='stampede' else 3.4 if card=='pulverizer' else 4.8)).normalized()
        # Fit evaluated silhouettes in perspective with room for the game's badge
        # and name strip. Iterate actual projected vertices, never guessed bounds.
        from bpy_extras.object_utils import world_to_camera_view
        distance=5.
        for step in range(60):
            camera.location=target+direction*distance
            camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
            bpy.context.view_layer.update()
            projected=[world_to_camera_view(scene,camera,p) for p in points]
            if min(p.x for p in projected)>.065 and max(p.x for p in projected)<.935 and min(p.y for p in projected)>.14 and max(p.y for p in projected)<.84:break
            distance*=1.035
        camera['badgeSafeZone']='Top-left 20%; primary face is centered/right'
        camera['nameSafeZone']='Bottom 14%'
        scene['cardId']=card;scene['subjectCount']=len(subjects)
        scene['modelSourceSHA256']=manifest['characters'][card]['mesh']['sha256']
        scene['productionSourceSHA256']=manifest['source']['sha256']
        scene['designForward']='Blender -Y; imported Unreal +X'
        master=output/'Masters'/(card+'.png')
        scene.render.filepath=str(master);bpy.ops.render.render(write_still=True,scene=scene.name)
        # Exact, editable render scene with final rigs, actions, camera and lights.
        scene_path=output/'Scenes'/(card+'_Portrait.blend')
        bpy.data.libraries.write(str(scene_path),{scene},fake_user=True,compress=True)
        runtime=output/(card+'.png');small=renders/'Small'/(card+'_96x120.png')
        image=bpy.data.images.load(str(master),check_existing=False)
        image.scale(768,960);image.filepath_raw=str(runtime);image.file_format='PNG';image.save()
        image.scale(96,120);image.filepath_raw=str(small);image.save()
        bpy.data.images.remove(image)
        info=pin(root,runtime);info['sourceSceneSHA256']=manifest['source']['sha256']
        manifest['illustrations'][card]=info
        composition='Five boarlets in a diagonal charge' if card=='mini_stampede' else 'Fifteen boarlets, five across and three deep' if card=='stampede' else 'Four-brace compression machine at its authoritative contact pose'
        record={**info,'sources':list(sources),'boundsMeters':[list(low),list(high)],
                'poseFrame':1,'cameraProjection':'Perspective','cameraDistance':distance,'composition':composition,
                'subjectCount':len(subjects),'master':pin(root,master),'editableScene':pin(root,scene_path),
                'smallReview':pin(root,small),'runtimeResolution':[768,960],'masterResolution':[1024,1280],
                'lighting':{'key':'warm copper','rim':'cool teal/violet'},
                'safeZones':{'topLeftBadge':.20,'bottomName':.14}}
        report['cards'][card]=record;expansion_report['cards'][card]=record
        render_identity_views(root,card,manifest,scene,studio,character)
        print('RIFT_EXPANSION_PORTRAIT',card,record['subjectCount'],master,flush=True)
    manifest_path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    report_path.write_text(json.dumps(report,indent=2),encoding='utf-8')
    (renders/'portrait-production.json').write_text(json.dumps(expansion_report,indent=2),encoding='utf-8')
    print('RIFT_EXPANSION_PORTRAITS_COMPLETE',len(selected),flush=True)


def pin(root,path):
    raw=path.read_bytes()
    return {'file':path.relative_to(root).as_posix(),'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw)}


def render_identity_views(root,card,manifest,scene,studio,character):
    # One source mesh per view, detached from portrait-only depth arrangement.
    old_objects=list(studio.objects)
    for obj in old_objects:
        if obj.name.startswith(('Portrait_','DistantCliff_','StoneCrack_','VioletPressureFront')) and obj.type in ('MESH','CURVE'):
            obj.hide_render=True
    rig,mesh=character(action='Idle',phase=0,tag='identity')
    scene.frame_set(1);bpy.context.view_layer.update()
    camera=scene.camera;camera.data.type='ORTHO'
    bounds=manifest['characters'][card]['boundsMeters'];height=bounds[2]
    camera.data.ortho_scale=max(height*1.45,max(bounds[:2])*1.4)
    target=Vector((0,0,height*.47))
    scene.render.resolution_x=512;scene.render.resolution_y=512
    output=root/'Assets/Renders/Expansion/Identity'
    views=[]
    for label,offset in (('front',(0,-6,1.5)),('rear',(0,6,1.5)),('side',(6,0,1.5)),('threequarter',(4,-6,3.5))):
        camera.location=target+Vector(offset);camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        path=output/(card+'_'+label+'.png');scene.render.filepath=str(path)
        bpy.ops.render.render(write_still=True,scene=scene.name);views.append({'view':label,**pin(root,path)})
    # Flat silhouette is intentionally a separate inspection pass, not new art.
    silhouette=bpy.data.materials.new('Silhouette_'+card);silhouette.use_nodes=True
    shader=silhouette.node_tree.nodes['Principled BSDF'];shader.inputs['Base Color'].default_value=(.006,.012,.018,1)
    shader.inputs['Roughness'].default_value=1
    old=list(mesh.data.materials);mesh.data.materials.clear();mesh.data.materials.append(silhouette)
    path=output/(card+'_silhouette.png');scene.render.filepath=str(path)
    bpy.ops.render.render(write_still=True,scene=scene.name);views.append({'view':'silhouette',**pin(root,path)})
    mesh.data.materials.clear()
    for mat in old:mesh.data.materials.append(mat)
    manifest['characters'][card]['identityViews']=views
