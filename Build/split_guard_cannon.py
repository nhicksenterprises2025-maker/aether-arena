"""Separate the existing guard cannon without rebuilding other production assets.

Blender: --background --python-exit-code 2 --python Build/split_guard_cannon.py
The full asset forge imports split_guard_mesh so regeneration uses the same split.
All seven cannon islands retain their original vertices, topology, UVs and materials.
"""
from pathlib import Path
import hashlib
import json
import os
import struct
import tempfile
import time

import bpy

ROOT = Path(__file__).resolve().parents[1]
CANNON_PARTS = (
    ('cradle', (0., .05, 2.55), (.84, .80, .46)),
    ('barrel', (0., -.425, 2.63), (.40, 1.09, .40)),
    ('band_rear', (0., -.22, 2.63), (.408, .05, .408)),
    ('band_middle', (0., -.71, 2.63), (.408, .05, .408)),
    ('band_front', (0., -.94, 2.63), (.408, .05, .408)),
    ('muzzle', (0., -.985, 2.63), (.226, .024, .226)),
    ('sight', (0., -.69, 2.82), (.065, .09, .06)),
)


def geometry_faces(mesh):
    """Canonical oriented face hashes include coordinates, UVs and materials."""
    result = []
    uv = mesh.data.uv_layers.active
    for polygon in mesh.data.polygons:
        corners = []
        for loop_index in polygon.loop_indices:
            loop = mesh.data.loops[loop_index]
            co = mesh.data.vertices[loop.vertex_index].co
            tex = uv.data[loop_index].uv if uv else (0., 0.)
            corners.append(struct.pack('<5f', *co, *tex))
        # Blender may renumber the first corner during separation; preserve
        # winding while canonicalizing only that cyclic starting corner.
        variants = [b''.join(corners[i:] + corners[:i]) for i in range(len(corners))]
        slot = mesh.data.materials[polygon.material_index]
        material = slot.name if slot else '<none>'
        result.append(hashlib.sha256(material.encode() + bytes([polygon.use_smooth]) + min(variants)).hexdigest())
    return sorted(result)


def geometry_hash(mesh):
    return hashlib.sha256(''.join(geometry_faces(mesh)).encode()).hexdigest()


def connected_islands(mesh):
    adjacent = [[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacent[a].append(b)
        adjacent[b].append(a)
    unseen = set(range(len(mesh.vertices)))
    islands = []
    while unseen:
        start = unseen.pop()
        stack, island = [start], {start}
        while stack:
            for neighbor in adjacent[stack.pop()]:
                if neighbor in unseen:
                    unseen.remove(neighbor)
                    island.add(neighbor)
                    stack.append(neighbor)
        islands.append(island)
    return islands


def split_guard_mesh(guard):
    """Shared forge/patch split; returns an unchanged authored cannon object."""
    if guard.type != 'MESH' or guard.name != 'SM_tower_guard':
        raise ValueError('Guard split requires the original SM_tower_guard mesh')
    if bpy.data.objects.get('SM_guard_cannon'):
        raise ValueError('Guard cannon already exists; refusing to split twice')
    bpy.context.view_layer.update()
    original_matrix = guard.matrix_world.copy()
    original_faces = geometry_faces(guard)
    islands = connected_islands(guard.data)
    bounds = []
    for island in islands:
        # The first masonry object determines the joined object's local rotation.
        # Match original authored coordinates in source-world space, so both
        # the existing saved blend and a fresh Forge.finish use identical rules.
        points = [guard.matrix_world @ guard.data.vertices[i].co for i in island]
        low = [min(point[axis] for point in points) for axis in range(3)]
        high = [max(point[axis] for point in points) for axis in range(3)]
        bounds.append(([(a+b)*.5 for a, b in zip(low, high)], [b-a for a, b in zip(low, high)]))
    selected, matches = set(), []
    for name, center, extent in CANNON_PARTS:
        found = [i for i, (actual_center, actual_extent) in enumerate(bounds)
                 if max(abs(a-b) for a, b in zip(actual_center, center)) < .012
                 and max(abs(a-b) for a, b in zip(actual_extent, extent)) < .024]
        if len(found) != 1:
            raise RuntimeError(f'Expected exactly one original cannon {name}, found {len(found)}')
        selected.update(islands[found[0]])
        matches.append({'name': name, 'vertices': len(islands[found[0]])})
    bpy.ops.object.select_all(action='DESELECT')
    guard.hide_set(False)
    guard.select_set(True)
    bpy.context.view_layer.objects.active = guard
    for vertex in guard.data.vertices:
        vertex.select = vertex.index in selected
    for edge in guard.data.edges:
        edge.select = all(i in selected for i in edge.vertices)
    for polygon in guard.data.polygons:
        polygon.select = all(i in selected for i in polygon.vertices)
    before_objects = set(bpy.data.objects)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.separate(type='SELECTED')
    bpy.ops.object.mode_set(mode='OBJECT')
    created = set(bpy.data.objects) - before_objects
    if len(created) != 1:
        raise RuntimeError('Guard split did not produce exactly one cannon object')
    cannon = created.pop()
    cannon.name = 'SM_guard_cannon'
    collection = bpy.data.collections.new('guard_cannon')
    bpy.context.scene.collection.children.link(collection)
    for current in list(cannon.users_collection):
        current.objects.unlink(cannon)
    collection.objects.link(cannon)
    bpy.context.view_layer.update()
    if sorted(geometry_faces(guard) + geometry_faces(cannon)) != original_faces:
        raise RuntimeError('Guard split altered original geometry, UVs, winding or materials')
    for obj in (guard, cannon):
        if max(abs(obj.matrix_world[row][col]-original_matrix[row][col]) for row in range(4) for col in range(4)) > 1e-7:
            raise RuntimeError('Guard split altered original authored object transform')
    cannon['rift_original_part_count'] = len(matches)
    cannon['rift_original_parts'] = json.dumps(matches)
    return cannon


def guard_socket_metadata(statics):
    statics['tower_guard']['sockets'] = {'hp_anchor': {'positionMeters': [0, 0, 3.05]}}
    statics['tower_guard']['aimAssembly'] = {'mesh': 'guard_cannon', 'stationaryArchitecture': True}
    statics['guard_cannon']['sockets'] = {'muzzle': {'positionMeters': [0, -.99, 2.63]}}
    statics['guard_cannon']['pivotMeters'] = [0, 0, 0]
    statics['guard_cannon']['originalPartCount'] = 7


def file_info(path):
    return {'file': path.relative_to(ROOT).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'bytes': path.stat().st_size}


def atomic_replace(staged, destination):
    if not destination.resolve().is_relative_to(ROOT.resolve()):
        raise ValueError('Targeted guard export escaped the repository')
    for attempt in range(12):
        try:
            os.replace(staged, destination)
            return
        except OSError:
            if attempt == 11:
                raise
            time.sleep(.25)


def export_static(obj, work):
    name = obj.name.removeprefix('SM_')
    destination = ROOT / 'Assets/Export/Environment' / (obj.name + '.fbx')

    def fbx(current, filename):
        bpy.ops.object.select_all(action='DESELECT')
        current.select_set(True)
        bpy.context.view_layer.objects.active = current
        staged = work / filename
        bpy.ops.export_scene.fbx(filepath=str(staged), use_selection=True, object_types={'MESH'},
                                global_scale=1, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                                axis_forward='-Y', axis_up='Z', use_mesh_modifiers=True,
                                add_leaf_bones=False, bake_anim=False, path_mode='ABSOLUTE', mesh_smooth_type='FACE')
        return staged

    def triangles(current):
        return sum(len(p.vertices)-2 for p in current.data.polygons)

    outputs = [(fbx(obj, destination.name), destination)]
    record = {'name': name, 'triangles': triangles(obj), 'boundsMeters': list(obj.dimensions),
              'materials': [m.name for m in obj.data.materials], 'lods': []}
    for level, ratio in ((1, .5), (2, .22)):
        lod = obj.copy()
        lod.data = obj.data.copy()
        obj.users_collection[0].objects.link(lod)
        lod.name = obj.name + '_LOD' + str(level)
        bpy.context.view_layer.objects.active = lod
        modifier = lod.modifiers.new('Authored static LOD reduction', 'DECIMATE')
        modifier.ratio = ratio
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        lod_path = destination.parent / (lod.name + '.fbx')
        outputs.append((fbx(lod, lod_path.name), lod_path))
        record['lods'].append({'level': level, 'triangles': triangles(lod)})
        lod_data=lod.data
        bpy.data.objects.remove(lod, do_unlink=True)
        bpy.data.meshes.remove(lod_data)
    return record, outputs


def run_targeted_export():
    manifest_path = ROOT / 'Assets/asset_manifest.json'
    manifest = json.loads(manifest_path.read_text())
    source_path = ROOT / manifest['source']['file']
    bpy.ops.wm.open_mainfile(filepath=str(source_path))
    bpy.context.preferences.filepaths.save_version = 0
    guard = bpy.data.objects.get('SM_tower_guard')
    if not guard:
        raise RuntimeError('Original production guard mesh is missing')
    # Refuse a repeated targeted invocation rather than separating the same
    # geometry twice or rewriting an already accepted source snapshot.
    if bpy.data.objects.get('SM_guard_cannon'):
        raise RuntimeError('Guard source is already split; no export performed')
    preserved_meshes = {obj.name: geometry_hash(obj) for obj in bpy.data.objects
                        if obj.type == 'MESH' and obj != guard}
    preserved_exports = {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in (ROOT / 'Assets/Export').rglob('*.fbx')
                         if not path.name.startswith(('SM_tower_guard', 'SM_guard_cannon'))}
    original_geometry = geometry_hash(guard)
    cannon = split_guard_mesh(guard)
    with tempfile.TemporaryDirectory(prefix='RiftGuard-Split-') as temporary:
        work = Path(temporary)
        records, outputs = {}, []
        for obj in (guard, cannon):
            record, files = export_static(obj, work)
            records[record['name']] = record
            outputs.extend(files)
        for name, expected in preserved_meshes.items():
            if geometry_hash(bpy.data.objects[name]) != expected:
                raise RuntimeError('Targeted export altered unrelated production mesh: ' + name)
        for relative, expected in preserved_exports.items():
            if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != expected:
                raise RuntimeError('Targeted export altered unrelated FBX: ' + relative)
        staged_blend = work / source_path.name
        bpy.ops.wm.save_as_mainfile(filepath=str(staged_blend), relative_remap=False)
        for staged, destination in outputs:
            atomic_replace(staged, destination)
        for name, record in records.items():
            primary = ROOT / 'Assets/Export/Environment' / ('SM_' + name + '.fbx')
            record.update(file_info(primary))
            for lod in record['lods']:
                lod.update(file_info(primary.parent / ('SM_' + name + '_LOD' + str(lod['level']) + '.fbx')))
            manifest['statics'][name] = record
        guard_socket_metadata(manifest['statics'])
        atomic_replace(staged_blend, source_path)
        manifest['source'] = file_info(source_path)
        staged_manifest = work / 'asset_manifest.json'
        staged_manifest.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        atomic_replace(staged_manifest, manifest_path)
    report = {'schema': 1, 'passed': True, 'method': 'Exact seven-island split of the existing authored guard mesh; no asset regeneration',
              'originalGeometrySha256': original_geometry, 'source': manifest['source'],
              'originalParts': json.loads(cannon['rift_original_parts']), 'geometryAndUVsPreserved': True,
              'unrelatedSourceMeshesUnchanged': len(preserved_meshes), 'unrelatedExportsUnchanged': len(preserved_exports),
              'preservedExportHashes': preserved_exports, 'outputs': records,
              'characterCount': len(manifest['characters']), 'staticCount': len(manifest['statics'])}
    (ROOT / 'Assets/guard_cannon_split_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('RIFT_GUARD_SPLIT_COMPLETE', report['characterCount'], report['staticCount'], '7 original parts;', len(preserved_exports), 'unrelated FBX unchanged', flush=True)


if __name__ == '__main__':
    run_targeted_export()
