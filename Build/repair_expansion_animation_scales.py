"""Reauthor Pulverizer Death without changing any mesh or portrait export.

Run with Blender --background --python this-file -- --report-dir <repo QA path>.
The same corrected action builder is used by normal full asset generation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import bpy

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Build'))
import expansion_asset_models

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--report-dir', type=Path, required=True)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
report_dir = args.report_dir.resolve()
if not report_dir.is_relative_to((ROOT / 'Artifacts/QA').resolve()):
    raise ValueError('Repair evidence must remain inside repository QA artifacts')
report_dir.mkdir(parents=True, exist_ok=False)

def pin(path):
    raw = path.read_bytes()
    return {'file': path.relative_to(ROOT).as_posix(), 'bytes': len(raw),
            'sha256': hashlib.sha256(raw).hexdigest()}

manifest_path = ROOT / 'Assets/asset_manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
record = manifest['characters']['pulverizer']
death_record = next(clip for clip in record['animations'] if clip['name'] == 'Death')
source_path = ROOT / manifest['source']['file']
death_path = ROOT / death_record['file']
before_pins = {path.relative_to(ROOT).as_posix(): pin(path)
               for path in (ROOT / 'Assets').rglob('*') if path.is_file()}
for path in (source_path, death_path, manifest_path):
    destination = report_dir / 'before' / path.relative_to(ROOT)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(path, destination)
(report_dir / 'before-pins.json').write_text(json.dumps(before_pins, indent=2), encoding='utf-8')

bpy.ops.wm.open_mainfile(filepath=str(source_path))
scene = bpy.context.scene
rig = bpy.data.objects['SKEL_pulverizer']

def geometry_signature():
    payload = {}
    for obj in bpy.data.objects:
        if obj.type == 'MESH':
            payload[obj.name] = {
                'vertices': [list(vertex.co) for vertex in obj.data.vertices],
                'faces': [list(poly.vertices) for poly in obj.data.polygons],
                'uv': [[list(item.uv) for item in layer.data] for layer in obj.data.uv_layers],
                'weights': [[(item.group, item.weight) for item in vertex.groups]
                            for vertex in obj.data.vertices],
                'materials': [material.name if material else None for material in obj.data.materials]}
        elif obj.type == 'ARMATURE':
            payload[obj.name] = {'restBones': {
                bone.name: {'parent': bone.parent.name if bone.parent else None,
                            'matrix': [list(row) for row in bone.matrix_local]}
                for bone in obj.data.bones}}
    return hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(',', ':')).encode()).hexdigest()

geometry_before = geometry_signature()
old_action = bpy.data.actions[death_record['action']]
bpy.data.actions.remove(old_action, do_unlink=True)
authored = expansion_asset_models.actions(
    SimpleNamespace(name='pulverizer', family='mechanism', rig=rig),
    scene, None, None, only={'Death'})
clip = authored[0]
if clip['action'] != death_record['action']:
    raise RuntimeError('Repair did not retain the canonical action name')
rig.animation_data.action = clip['object']
scene.render.fps = clip['fps']
scene.frame_start = 1
scene.frame_end = clip['frames']
bpy.ops.object.select_all(action='DESELECT')
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
# Match the ordinary production exporter while keeping all geometry exports fixed.
with tempfile.TemporaryDirectory(prefix='RiftDeathRepair-') as staging:
    staged = Path(staging) / death_path.name
    bpy.ops.export_scene.fbx(
        filepath=str(staged), use_selection=True, object_types={'ARMATURE', 'MESH'},
        global_scale=1, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
        axis_forward='-Y', axis_up='Z', use_mesh_modifiers=True, add_leaf_bones=False,
        primary_bone_axis='Y', secondary_bone_axis='X', use_armature_deform_only=False,
        bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False,
        bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
        bake_anim_step=1, bake_anim_simplify_factor=0, path_mode='ABSOLUTE', mesh_smooth_type='FACE')
    os.replace(staged, death_path)
rig.animation_data.action = None
scene.frame_set(1)
scene.render.fps = 30
for bone in rig.pose.bones:
    bone.rotation_euler = (0, 0, 0)
    bone.location = (0, 0, 0)
    bone.scale = (1, 1, 1)
bpy.context.view_layer.update()
geometry_after = geometry_signature()
if geometry_before != geometry_after:
    raise RuntimeError('Authored mesh geometry or rest bones changed during animation repair')
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(source_path))
death_record.update({key: value for key, value in clip.items() if key != 'object'})
death_record.update(pin(death_path))
manifest['source'].update(pin(source_path))
manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')

allowed = {path.relative_to(ROOT).as_posix() for path in (source_path, death_path, manifest_path)}
unchanged = []
unexpected = []
for filename, old in before_pins.items():
    if filename in allowed:
        continue
    current = pin(ROOT / filename)
    (unchanged if old == current else unexpected).append(filename)
report = {'schema': 1, 'passed': not unexpected,
          'cause': 'Zero-scale death debris made first-frame FBX bind transforms singular.',
          'fix': 'Minimum .001 debris scale with dense linear scale keys; same production action builder.',
          'unchangedAssetFiles': len(unchanged), 'unexpectedChanges': unexpected,
          'geometrySignatureBefore': geometry_before, 'geometrySignatureAfter': geometry_after,
          'changedFiles': sorted(allowed), 'sourceBlend': manifest['source'], 'deathClip': death_record,
          'failedSourceSnapshot': str(report_dir / 'before'),
          'limitations': 'Native UE reimport is separate; numerical matrix safety is checked next.'}
(report_dir / 'repair.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('RIFT_DEATH_SOURCE_REPAIRED', len(unchanged), 'unchanged asset files', flush=True)
if unexpected:
    raise RuntimeError('Unexpected asset changes: ' + ', '.join(unexpected))
