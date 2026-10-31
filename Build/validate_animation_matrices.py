"""Audit every authored clip sample and actual FBX for unsafe bone transforms.

Run in Blender with --report <repo QA JSON> and optional --failed-fbx <snapshot>.
Checks every integer and quarter frame, including first and final samples.
"""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy
from io_scene_fbx import parse_fbx

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--report', type=Path, required=True)
parser.add_argument('--failed-fbx', type=Path)
args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
report_path = args.report.resolve()
if not report_path.is_relative_to((ROOT / 'Artifacts/QA').resolve()):
    raise ValueError('Animation safety report must remain in repository QA artifacts')
manifest = json.loads((ROOT / 'Assets/asset_manifest.json').read_text(encoding='utf-8'))
report = {'schema': 1, 'passed': False,
          'scope': 'Actual saved production scene and every exported character clip; no native import claim',
          'sampling': 'All integer and quarter frames, inclusive endpoints',
          'sourceBlendSHA256': hashlib.sha256((ROOT / manifest['source']['file']).read_bytes()).hexdigest(),
          'checks': [], 'errors': [], 'clips': [], 'poseMatricesChecked': 0, 'sourceSamples': 0}

def check(name, condition, detail=None):
    report['checks'].append({'name': name, 'passed': bool(condition), 'detail': detail})
    if not condition:
        report['errors'].append(name)

def walk(element):
    yield element
    for child in element.elems:
        yield from walk(child)

def inspect_fbx(path):
    tree, version = parse_fbx.parse(str(path))
    elements = list(walk(tree))
    nonfinite = []
    numeric_values = 0
    for element in elements:
        for prop in element.props:
            values = prop if isinstance(prop, array.array) else [prop] if isinstance(prop, float) else []
            for value in values:
                numeric_values += 1
                if isinstance(value, (float, int)) and not math.isfinite(value):
                    if len(nonfinite) < 20:
                        nonfinite.append(element.id.decode(errors='replace'))
    connections = [element for element in elements if element.id == b'C' and len(element.props) >= 4]
    scale_nodes = {element.props[1] for element in connections
                   if element.props[0] == b'OP' and element.props[3] == b'Lcl Scaling'}
    scale_curve_ids = {element.props[1] for element in connections if element.props[2] in scale_nodes}
    curves = [element for element in elements if element.id == b'AnimationCurve' and element.props[0] in scale_curve_ids]
    scale_values = [value for curve in curves for child in curve.elems if child.id == b'KeyValueFloat'
                    for value in child.props[0]]
    # Initial transform properties must also be invertible, independently of curves.
    initial_scales = [value for element in elements if element.id == b'P' and element.props[0] == b'Lcl Scaling'
                      for value in element.props[-3:]]
    unsafe_scales = [value for value in scale_values + initial_scales if not math.isfinite(value) or abs(value) < 1e-6]
    return {'file': path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'version': version,
            'numericValuesChecked': numeric_values, 'nonfiniteElements': nonfinite,
            'scaleCurveCount': len(curves), 'unsafeScaleValues': len(unsafe_scales),
            'minimumAbsoluteScale': min(map(abs, scale_values + initial_scales), default=None)}

if args.failed_fbx:
    report['regressionSnapshot'] = inspect_fbx(args.failed_fbx.resolve())
    previous = report['regressionSnapshot']
    check('Failed original FBX reproduces nonfinite or singular scaling',
          bool(previous['nonfiniteElements']) or previous['unsafeScaleValues'] > 0, previous)

bpy.ops.wm.open_mainfile(filepath=str(ROOT / manifest['source']['file']))
scene = bpy.context.scene
for card_id, card in manifest['characters'].items():
    rig = bpy.data.objects['SKEL_' + card_id]
    for clip in card['animations']:
        action = bpy.data.actions[clip['action']]
        rig.animation_data.action = action
        if hasattr(action, 'slots') and action.slots:
            rig.animation_data.action_slot = action.slots[0]
        for bone in rig.pose.bones:
            bone.rotation_mode = 'XYZ'
        unsafe = []
        minimum_scale = 1e30
        minimum_determinant = 1e30
        matrix_count = 0
        samples = 4 * (clip['frames'] - 1) + 1
        first_pose = None
        maximum_pose_change = 0.
        for sample in range(samples):
            frame = 1 + sample / 4
            integer = int(frame)
            scene.frame_set(integer, subframe=frame - integer)
            bpy.context.view_layer.update()
            pose = [value for bone in rig.pose.bones for row in bone.matrix for value in row]
            if first_pose is None:
                first_pose = pose
            else:
                maximum_pose_change = max(maximum_pose_change, max(abs(a-b) for a,b in zip(first_pose, pose)))
            for bone in rig.pose.bones:
                matrix_count += 1
                minimum_scale = min(minimum_scale, *(abs(value) for value in bone.scale))
                determinant = bone.matrix.to_3x3().determinant()
                minimum_determinant = min(minimum_determinant, abs(determinant))
                finite = all(math.isfinite(value) for row in bone.matrix for value in row)
                invertible = finite and math.isfinite(determinant) and abs(determinant) > 1e-15
                if invertible:
                    try:
                        inverse = bone.matrix.inverted()
                        invertible = all(math.isfinite(value) for row in inverse for value in row)
                    except ValueError:
                        invertible = False
                if not invertible or any(abs(value) < 1e-6 for value in bone.scale):
                    if len(unsafe) < 20:
                        unsafe.append({'bone': bone.name, 'frame': frame,
                                       'scale': list(bone.scale), 'determinant': determinant})
        check(card_id + ' ' + clip['name'] + ' all sampled bone matrices finite and invertible', not unsafe, unsafe)
        if clip['name'] == 'Attack' or (clip['name'] == 'Locomotion' and card_id != 'pulverizer'):
            check(card_id + ' ' + clip['name'] + ' evaluates actual animated motion', maximum_pose_change > .001, maximum_pose_change)
        fbx = inspect_fbx(ROOT / clip['file'])
        check(card_id + ' ' + clip['name'] + ' FBX numeric arrays finite', not fbx['nonfiniteElements'], fbx['nonfiniteElements'])
        check(card_id + ' ' + clip['name'] + ' FBX initial and animation scales nonzero',
              fbx['scaleCurveCount'] >= len(card['bones']) * 3 and fbx['unsafeScaleValues'] == 0, fbx)
        check(card_id + ' ' + clip['name'] + ' FBX matches current manifest pin', fbx['sha256'] == clip['sha256'])
        report['clips'].append({'card': card_id, 'clip': clip['name'], 'samples': samples,
                                'maximumPoseChange': maximum_pose_change,
                                'poseMatrices': matrix_count, 'minimumSourceScale': minimum_scale,
                                'minimumMatrixDeterminant': minimum_determinant, 'fbx': fbx})
        report['poseMatricesChecked'] += matrix_count
        report['sourceSamples'] += samples
    rig.animation_data.action = None
    print('RIFT_ANIMATION_MATRIX_QA_CARD', card_id, flush=True)
report['passed'] = not report['errors']
report['checkCount'] = len(report['checks'])
report_path.parent.mkdir(parents=True, exist_ok=True)
report_path.write_text(json.dumps(report, indent=2), encoding='utf-8')
print('RIFT_ANIMATION_MATRIX_QA', report['checkCount'], 'checks', len(report['errors']), 'errors',
      report['poseMatricesChecked'], 'pose matrices', flush=True)
if report['errors']:
    raise RuntimeError('Unsafe authored animation matrices: ' + ', '.join(report['errors']))
