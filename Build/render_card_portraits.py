"""Render card portraits from the exact authored meshes/rigs exported to the game.

Run in Blender: --background --python Build/render_card_portraits.py.
No diffusion, image compositing, paint-over or substitute character is used.
"""
from pathlib import Path
import argparse
import bpy
import hashlib
import json
import math
import sys
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--cards', nargs='*')
opts = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
manifest_path = ROOT / 'Assets/asset_manifest.json'
manifest = json.loads(manifest_path.read_text())
bpy.ops.wm.open_mainfile(filepath=str(ROOT / manifest['source']['file']))
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 64
scene.cycles.use_denoising = True
scene.cycles.use_adaptive_sampling = True
scene.cycles.adaptive_threshold = .025
scene.cycles.sample_clamp_indirect = 4
scene.cycles.device = 'CPU'
try:
    devices = bpy.context.preferences.addons['cycles'].preferences
    devices.compute_device_type = 'OPTIX'
    devices.get_devices()
    if any(device.type == 'OPTIX' for device in devices.devices):
        for device in devices.devices:
            device.use = device.type == 'OPTIX'
        scene.cycles.device = 'GPU'
except (TypeError, RuntimeError):
    # Portrait content is identical on machines without an OptiX device.
    pass
scene.render.resolution_x = 768
scene.render.resolution_y = 960
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = False
scene.view_settings.view_transform = 'AgX'
scene.view_settings.look = 'AgX - Medium High Contrast'
scene.world.use_nodes = True
scene.world.node_tree.nodes['Background'].inputs[0].default_value = (.019, .035, .056, 1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value = .45

# Each portrait keeps the production material/mesh and gains deliberate
# photographic lighting. Close portraits put the face and held equipment in
# the small hand image; creatures and structures retain their full silhouette.
PORTRAIT_MOODS = {
    'ironclad': ((.027,.042,.054), (1,.88,.70), (.40,.68,.84)),
    'ember_archer': ((.055,.028,.025), (1,.83,.66), (.62,.40,.26)),
    'twin_blades': ((.039,.025,.058), (1,.86,.76), (.57,.38,.86)),
    'arc_mage': ((.024,.035,.069), (.88,.92,1), (.35,.56,1)),
    'boulderback': ((.034,.044,.040), (1,.88,.74), (.55,.66,.46)),
    'rambeast': ((.057,.038,.024), (1,.86,.65), (.72,.48,.27)),
    'sky_manta': ((.018,.049,.056), (.91,.97,1), (.25,.74,.78)),
    'vampire_bats': ((.043,.019,.041), (.95,.86,1), (.73,.27,.55)),
    'frost_fang': ((.033,.055,.074), (.94,.98,1), (.38,.69,.90)),
    'storm_raven': ((.020,.030,.059), (.92,.94,1), (.30,.49,.90)),
    'archer_tower': ((.040,.045,.035), (1,.90,.74), (.46,.66,.67)),
    'bullet_burst': ((.046,.034,.024), (1,.89,.67), (.50,.57,.66)),
    'nova_flask': ((.035,.024,.057), (.94,.88,1), (.58,.31,.94)),
    'meteor_shards': ((.052,.029,.022), (1,.83,.64), (.84,.34,.13)),
    'mini_stampede': ((.042,.033,.022), (1,.87,.69), (.60,.69,.43)),
    'stampede': ((.027,.037,.056), (1,.91,.73), (.38,.63,.88)),
}

studio = bpy.data.collections.new('CardPortraitStudio')
scene.collection.children.link(studio)
def own(obj):
    for col in list(obj.users_collection): col.objects.unlink(obj)
    studio.objects.link(obj)
    return obj

bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -.025))
floor = own(bpy.context.object)
floor.name = 'PortraitFloor'
mat = bpy.data.materials.new('PortraitSlate')
mat.use_nodes = True
shader = mat.node_tree.nodes['Principled BSDF']
shader.inputs['Base Color'].default_value = (.025, .049, .075, 1)
shader.inputs['Roughness'].default_value = .7
floor.data.materials.append(mat)
portrait_lights = {}
for name, loc, power, color, size in (
    ('Key', (3.5, -4.5, 5.5), 1100, (1, .87, .70), 3.2),
    ('Fill', (-3, -2, 3), 430, (.64, .74, .88), 4),
    ('Rim', (2, 3.5, 4.8), 1250, (.40, .79, 1), 2.4)):
    data = bpy.data.lights.new('Portrait' + name, 'AREA')
    data.energy, data.color, data.size = power, color, size
    obj = bpy.data.objects.new(data.name, data)
    studio.objects.link(obj)
    obj.location = loc
    obj.rotation_euler = (Vector((0, 0, 1)) - obj.location).to_track_quat('-Z', 'Y').to_euler()
    portrait_lights[name] = data
camera = bpy.data.objects.new('PortraitCamera', bpy.data.cameras.new('PortraitCamera'))
studio.objects.link(camera)
camera.data.type = 'ORTHO'
camera.data.lens = 65
scene.camera = camera

output = ROOT / 'Assets/Source/CardArt'
output.mkdir(parents=True, exist_ok=True)
report_path = output / 'model_portraits.json'
report = json.loads(report_path.read_text()) if report_path.exists() else {'schema': 1, 'cards': {}}
report.update({'method': 'Actual production Blender mesh/rig render, exported to the native game',
               'sourceBlend': manifest['source'], 'resolution': [768, 960],
               'rendering': {'engine': 'Cycles', 'samples': 64, 'denoising': True},
               'camera': 'Orthographic three-quarter; hero crop for humanoids, full creature/structure silhouette',
               'rendererScript': {'file': 'Build/render_card_portraits.py',
                                  'sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                                  'bytes': Path(__file__).stat().st_size},
               'aiGeneratedRasterArtwork': False})
report.pop('generatedArtwork',None)
selected = opts.cards or list(manifest['cards'])
for card in selected:
    binding = manifest['cards'][card]
    mood, key_color, rim_color = PORTRAIT_MOODS[card]
    shader.inputs['Base Color'].default_value = (*mood, 1)
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (*mood, 1)
    portrait_lights['Key'].color = key_color
    portrait_lights['Rim'].color = rim_color
    for col in bpy.data.collections: col.hide_render = col != studio
    temporary = []
    subjects = []
    sources = []
    def character(name, loc=(0, 0, 0), scale=1, action='Idle', phase=.16, rotation=0):
        original_rig = bpy.data.objects['SKEL_' + name]
        rig = original_rig.copy()
        studio.objects.link(rig)
        mesh = bpy.data.objects['SK_' + name].copy()
        studio.objects.link(mesh)
        mesh.parent = rig
        for mod in mesh.modifiers:
            if mod.type == 'ARMATURE': mod.object = rig
        rig.location, rig.scale = loc, (scale,) * 3
        rig.rotation_euler.z = rotation
        clip = next(a for a in manifest['characters'][name]['animations'] if a['name'] == action)
        rig.animation_data_create()
        rig.animation_data.action = bpy.data.actions[clip['action']]
        rig.hide_render = mesh.hide_render = False
        temporary.extend((mesh, rig))
        subjects.append(mesh)
        sources.append({'model': manifest['characters'][name]['mesh'], 'action': clip['name'], 'phase': phase})
        return 1 + round((clip['frames'] - 1) * phase)
    def static(name, loc=(0, 0, 0), scale=1, rotation=(0, 0, 0)):
        mesh = bpy.data.objects['SM_' + name].copy()
        studio.objects.link(mesh)
        mesh.hide_render = False
        mesh.location, mesh.scale, mesh.rotation_euler = loc, ((scale,) * 3 if isinstance(scale,(int,float)) else scale), rotation
        temporary.append(mesh)
        subjects.append(mesh)
        sources.append({'model': manifest['statics'][name]})
    frame = 1
    if card == 'archer_tower':
        # Match the deployed visual assembly: gameplay footprint stays 1.65 m,
        # with the presentation-only width/height and visible archer applied.
        plan_scale = 1.65 / max(manifest['statics']['archer_tower']['boundsMeters'][:2]) * 1.15
        static('archer_tower', scale=(plan_scale,plan_scale,1.22))
        frame = character('tower_archer', (0, 0, 1.94*1.22), .60, action='DrawRelease', phase=.37)
    elif card == 'twin_blades':
        frame = character(card, (-.87, .10, 0), .94, phase=.12, rotation=-.10)
        character(card, (.48, -.09, 0), 1, phase=.12, rotation=.10)
    elif card == 'vampire_bats':
        frame = character(card, (-.49, .28, .12), .56, action='WingCycle', phase=.16, rotation=-.15)
        character(card, (.50, .23, .22), .53, action='WingCycle', phase=.16, rotation=.15)
        character(card, (0, -.33, -.20), .67, action='WingCycle', phase=.16)
    elif card in ('mini_stampede','stampede'):
        # The herd card shows the same individual hog mesh as the live units;
        # a slightly staggered trio makes the multi-unit identity readable.
        frame=character(card, (-.65,.28,0),.82,phase=.16,rotation=-.14)
        character(card,(.59,.36,0),.79,phase=.16,rotation=.12)
        character(card,(0,-.30,0),1.,phase=.16)
    elif 'skeletal' in binding:
        name = binding['skeletal']
        action = 'WingCycle' if name in ('sky_manta', 'storm_raven') else 'Idle'
        frame = character(name, action=action, phase=.16)
    elif card == 'bullet_burst':
        for i in range(7):
            angle = (i - 3) * .18
            static('bullet_round', ((i - 3) * .075, .03 * abs(i - 3), .16 + .055 * abs(i - 3)), 1.8, (math.radians(65), 0, angle))
    elif card == 'meteor_shards':
        static('meteor_shard', (0, -.05, .35), 1.6, (.15, -.30, 0))
        static('meteor_shard', (-.33, .15, .71), .92, (.25, .40, -.4))
        static('meteor_shard', (.34, .09, .60), 1.05, (.15, -.25, .5))
    else: static(binding['static'])
    scene.frame_set(frame)
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    # Fit the posed silhouette itself. Projecting a rotated bounding box wastes
    # substantial width on empty corners, especially on broad flyer wings.
    points = []
    for subject in subjects:
        obj = subject.evaluated_get(depsgraph)
        evaluated_mesh = obj.to_mesh()
        points.extend(obj.matrix_world @ vertex.co for vertex in evaluated_mesh.vertices)
        obj.to_mesh_clear()
    lo = Vector(tuple(min(p[k] for p in points) for k in range(3)))
    hi = Vector(tuple(max(p[k] for p in points) for k in range(3)))
    hero_crop = card in ('ironclad', 'ember_archer', 'twin_blades', 'arc_mage')
    framed_points = [p for p in points if p.z >= lo.z + (hi.z-lo.z)*.25] if hero_crop else points
    frame_lo = Vector(tuple(min(p[k] for p in framed_points) for k in range(3)))
    frame_hi = Vector(tuple(max(p[k] for p in framed_points) for k in range(3)))
    target = (frame_lo + frame_hi) * .5
    camera.location = target + Vector((4.5, -8, 2.6 if hero_crop else 3.2))
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    inverse = camera.matrix_world.inverted()
    bpy.context.view_layer.update()
    inverse = camera.matrix_world.inverted()
    projected = [inverse @ p for p in framed_points]
    width = max(p.x for p in projected) - min(p.x for p in projected)
    height = max(p.y for p in projected) - min(p.y for p in projected)
    midpoint = Vector(((max(p.x for p in projected)+min(p.x for p in projected))*.5,
                       (max(p.y for p in projected)+min(p.y for p in projected))*.5,0))
    camera.location += camera.rotation_euler.to_matrix() @ midpoint
    fill = .93 if hero_crop else .90 if card in ('sky_manta','storm_raven','vampire_bats') else .87
    camera.data.ortho_scale = max(height / fill, width / (.8 * fill))
    path = output / (card + '.png')
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    info = {'file': str(path.relative_to(ROOT)).replace('\\', '/'),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'bytes': path.stat().st_size}
    manifest['illustrations'][card] = info
    report['cards'][card] = {**info, 'sources': sources, 'boundsMeters': [list(lo), list(hi)],
                           'poseFrame': frame, 'cameraOrthoScale': camera.data.ortho_scale,
                           'composition': 'Upper-body hero portrait' if hero_crop else 'Model-rendered herd trio' if card in ('mini_stampede','stampede') else 'Full silhouette',
                           'lighting': {'backdrop': mood, 'key': key_color, 'rim': rim_color}}
    for obj in temporary: bpy.data.objects.remove(obj, do_unlink=True)
    print('RIFT_CARD_PORTRAIT', card, info['sha256'], flush=True)
manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
print('RIFT_CARD_PORTRAITS_COMPLETE', len(selected), flush=True)
