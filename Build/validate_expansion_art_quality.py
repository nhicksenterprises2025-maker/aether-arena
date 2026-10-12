"""Measure revised saved meshes and poses, and render honest animation review.

Run in Blender after selected generation. Reports are immutable QA artifacts;
the source blend, FBX, atlas and portraits are never repaired or saved here.
Geometric gates are distinct from human aesthetic and native-camera acceptance.
"""
import argparse
import array
import ast
import hashlib
import json
import math
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
CARDS = ('mini_stampede', 'stampede', 'pulverizer', 'bone_warden',
         'bone_warden_skeleton', 'aether_reaper', 'iron_giant')
CLIPS = ('Idle', 'Locomotion', 'Attack', 'Hit', 'Death', 'Deploy', 'Status', 'Turn', 'Acquire')
VIEWS = {'front': (0, -1, .12), 'left_threequarter': (-.75, -1, .35),
         'right_threequarter': (.75, -1, .35), 'battle_elevation': (.28, -1, 1.25)}
THRESHOLDS = {'livingFloorPenetrationMeters': .020, 'deathFloorPenetrationMeters': .040,
              'gripToHandMeters': .005, 'landmarkSurfaceDistanceMeters': .015,
              'eyeRayEndpointToleranceMeters': .006,
              'stanceHeightBandMeters': .008, 'stanceVerticalSpeedMetersPerSecond': .10,
              'stanceSlipReferenceFraction': .12, 'stanceSlipMetersPerSecondAllowance': .025,
              'semanticSeparateSurfaceGapMeters': .004}


def socket_grip_contract(design):
    """The new open grip has separate joint and shaft attachment anchors.

    Old source revisions retain the exact solid-palm volume rule. A declared
    new contract cannot silently fall back to that rule if it is malformed.
    """
    contract = design.get('gripContactContract')
    if contract is None:
        return None
    if contract.get('schema') != 1 or contract.get('mode') != 'socket-contact':
        raise ValueError('Unsupported authored socket-contact contract')
    if (set(contract.get('hands', {})) != {'l', 'r'} or set(design.get('twoHandJointTargetsMeters', {})) != {'l', 'r'}
            or set(design.get('twoHandGripsMeters', {})) != {'l', 'r'}):
        raise ValueError('Socket-contact contract requires both authored hand joint targets')
    for side, hand in contract['hands'].items():
        for mapping in ('twoHandGripsMeters', 'twoHandJointTargetsMeters'):
            point = design[mapping][side]
            if len(point) != 3 or not all(math.isfinite(v) for v in point):
                raise ValueError('Socket-contact anchor and joint targets must be finite 3D points')
        if hand.get('handBone') != 'hand_' + side:
            raise ValueError('Socket-contact owning hand is inconsistent')
        for key in ('palmGroup', 'fingersGroup', 'thumbGroup', 'handleGroup'):
            if not isinstance(hand.get(key), str) or not hand[key]:
                raise ValueError('Socket-contact surface group is absent: ' + key)
        tangent = hand.get('shaftTangentMeters', [])
        if len(tangent) != 3 or not all(math.isfinite(v) for v in tangent) or abs(sum(v*v for v in tangent)-1) > 1e-5:
            raise ValueError('Socket-contact tangent must be a finite unit vector')
        if any(not math.isfinite(hand.get(key, math.nan)) or hand[key] <= 0
               for key in ('shaftOuterRadiusMeters', 'cavityHalfLengthMeters')):
            raise ValueError('Socket-contact radius and axial coverage must be positive')
    return contract


def prepare_socket_surfaces(triangles, groups, contract):
    """Cache semantic topology only; each pose still supplies actual vertices."""
    names = [contract[key] for key in ('palmGroup', 'fingersGroup', 'thumbGroup', 'handleGroup')] + ['QA_weapon_head']
    return {name: [tri for tri in triangles if all(i in groups[name] for i in tri)] for name in names}


def measure_socket_contact(points, triangles, groups, anchor, tangent, contract, prepared=None):
    """Actual evaluated surfaces: contact is allowed, interpenetration is not.

    Closed palm/finger/thumb components are measured separately for containment;
    the full handle includes its bindings. Axis and radial samples also detect
    a buried long handle whose source vertices happen to be far from the hand.
    """
    from mathutils import Vector
    from mathutils.bvhtree import BVHTree
    import numpy as np
    prepared = prepared or prepare_socket_surfaces(triangles, groups, contract)
    surface_data = {}
    component_cache = prepared.setdefault('_closedComponentTopology', {})
    def components(group):
        if group not in component_cache:
            # Joining authored primitives preserves their separate closed
            # volumes. Classify their union, rather than XOR parity across a
            # shaft and overlapping leather bindings or wrist anatomy.
            parents = {}
            def root(vertex):
                parents.setdefault(vertex, vertex)
                while parents[vertex] != vertex:
                    parents[vertex] = parents[parents[vertex]]
                    vertex = parents[vertex]
                return vertex
            for tri in prepared[group]:
                for vertex in tri[1:]:
                    parents[root(vertex)] = root(tri[0])
            parts = {}
            for tri in prepared[group]:
                parts.setdefault(root(tri[0]), []).append(tri)
            component_cache[group] = list(parts.values())
        return component_cache[group]
    def ray_data(selected):
        coords = np.asarray([[points[i] for i in tri] for tri in selected], dtype=np.float64)
        return {'coords': coords, 'edge1': coords[:, 1] - coords[:, 0],
                'edge2': coords[:, 2] - coords[:, 0],
                'low': coords.min(axis=(0, 1)), 'high': coords.max(axis=(0, 1))}
    def tree(group):
        selected = prepared[group]
        if not selected:
            raise ValueError('Socket-contact semantic group contains no triangles')
        vertices = sorted({i for tri in selected for i in tri})
        remap = {old: new for new, old in enumerate(vertices)}
        surface = BVHTree.FromPolygons([Vector(points[i]) for i in vertices],
            [tuple(remap[i] for i in tri) for tri in selected], all_triangles=True, epsilon=1e-7)
        surface_data[id(surface)] = [ray_data(part) for part in components(group)]
        return surface
    def ray_terms(data, direction):
        h = np.cross(direction, data['edge2'])
        determinant = np.einsum('ij,ij->i', data['edge1'], h)
        valid = np.abs(determinant) > 1e-14; reciprocal = np.zeros_like(determinant)
        reciprocal[valid] = 1 / determinant[valid]
        return h, valid, reciprocal
    def exact_distances(data, origin, direction, cached_terms=None):
        # BVH ray_cast may double-hit a triangle or accept an adjacent expanded
        # edge. Float64 barycentric intersections use the SAME saved geometry.
        coords = data['coords']; edge1 = data['edge1']; edge2 = data['edge2']
        direction = np.asarray(direction, dtype=np.float64)
        h, valid, reciprocal = cached_terms if cached_terms is not None else ray_terms(data, direction)
        delta = np.asarray(origin, dtype=np.float64) - coords[:, 0]
        u = reciprocal * np.einsum('ij,ij->i', delta, h); q = np.cross(delta, edge1)
        v = reciprocal * np.einsum('j,ij->i', direction, q)
        distance = reciprocal * np.einsum('ij,ij->i', edge2, q)
        selected = valid & (u >= -1e-9) & (v >= -1e-9) & (u + v <= 1 + 1e-9) & (distance >= 0)
        return distance[selected]
    def inside(surface, position):
        position = np.asarray(position, dtype=np.float64)
        for data in surface_data[id(surface)]:
            if np.any(position < data['low']) or np.any(position > data['high']):
                continue
            if '_containmentRays' not in data:
                data['_containmentRays'] = []
                for direction in ((.871, .314, .377), (-.223, .917, .331), (.419, -.537, .732)):
                    direction = np.asarray(direction, dtype=np.float64); direction /= np.linalg.norm(direction)
                    data['_containmentRays'].append((direction, ray_terms(data, direction)))
            votes = []
            for direction, terms in data['_containmentRays']:
                hits = np.sort(exact_distances(data, position, direction, terms))
                # Two adjacent triangles at one surface crossing are one hit.
                # This 10nm numerical deduplication is below the unchanged
                # 20micrometre contact precision; no physical gap is widened.
                count = int(len(hits) and (1 + np.count_nonzero(np.diff(hits) > 1e-8)))
                votes.append(count % 2 == 1)
            if sum(votes) >= 2:
                return True
        return False
    hand_groups = [groups[contract[key]] for key in ('palmGroup', 'fingersGroup', 'thumbGroup')]
    handle_indices = groups[contract['handleGroup']]
    surfaces = [tree(contract[key]) for key in ('palmGroup', 'fingersGroup', 'thumbGroup')]
    handle = tree(contract['handleGroup'])
    weapon_head = tree('QA_weapon_head')
    handle_ray_data = ray_data(prepared[contract['handleGroup']])
    def exact_handle_distances(origin, direction):
        # BVH ray_cast can miss shared triangle seams at Blender's float32
        # coordinates. Intersect the SAME evaluated triangles in float64; the
        # tiny dimensionless barycentric allowance only retains shared edges.
        return exact_distances(handle_ray_data, origin, direction)
    intersections = sum(len(surface.overlap(handle)) for surface in surfaces)
    head_intersections = sum(len(surface.overlap(weapon_head)) for surface in surfaces)
    hand_indices = set().union(*hand_groups)
    # A hand entirely inside a thick shaft can have no crossing triangles.
    buried_vertices = sum(inside(handle, Vector(points[i])) and
        handle.find_nearest(Vector(points[i]))[3] > 2e-5 for i in hand_indices)
    head_buried_vertices = sum(inside(weapon_head, Vector(points[i])) and
        weapon_head.find_nearest(Vector(points[i]))[3] > 2e-5 for i in hand_indices)
    contact_gap = min(handle.find_nearest(Vector(points[i]))[3] for i in hand_indices)
    contact_gap = min(contact_gap, min(surface.find_nearest(Vector(points[i]))[3]
        for surface in surfaces for i in handle_indices))
    anchor = Vector(anchor); tangent = Vector(tangent).normalized()
    basis = tangent.cross(Vector((0, 1, 0)))
    if basis.length < .01:
        basis = tangent.cross(Vector((1, 0, 0)))
    basis.normalize(); other = tangent.cross(basis).normalized()
    axis_inside = 0; radial_penetrations = 0; ray_misses = 0; first_ray_miss = None; radial_radii = []; minimum_clearance = math.inf
    for station in range(9):
        origin = anchor + tangent * contract['cavityHalfLengthMeters'] * (station / 4 - 1)
        axis_inside += sum(inside(surface, origin) for surface in surfaces)
        for sample in range(12):
            angle = sample * math.tau / 12
            direction = basis * math.cos(angle) + other * math.sin(angle)
            distances = exact_handle_distances(origin, direction)
            local = distances[distances <= contract['shaftOuterRadiusMeters'] + .01]
            if not len(local):
                ray_misses += 1
                if first_ray_miss is None:
                    first_ray_miss = {'station': station, 'radialSample': sample,
                                      'originMeters': list(origin), 'direction': list(direction)}
                continue
            # Outer leather bindings belong to the actual grip surface too.
            outer_radius = float(local.max())
            radial_radii.append(outer_radius)
            for surface in surfaces:
                # Use the same exact saved triangles on both sides of the
                # cavity. BVH can report a zero-distance pseudo-hit for a hand
                # triangle actually beyond the shaft (retained Death19.5 case).
                nearest = math.inf
                for data in surface_data[id(surface)]:
                    hits = exact_distances(data, origin, direction)
                    if len(hits):
                        nearest = min(nearest, float(hits.min()))
                if math.isfinite(nearest):
                    gap = nearest - outer_radius
                    minimum_clearance = min(minimum_clearance, gap)
                    radial_penetrations += int(gap < -2e-5)
    return {'triangleIntersections': intersections, 'handVerticesBuriedInHandle': buried_vertices,
            'weaponHeadTriangleIntersections': head_intersections, 'handVerticesBuriedInWeaponHead': head_buried_vertices,
            'shaftAxisInsideHandSamples': axis_inside, 'radialPenetrationSamples': radial_penetrations,
            'shaftAxisRayMissSamples': ray_misses,
            'firstShaftAxisRayMiss': first_ray_miss,
            'minimumContactGapMeters': contact_gap, 'minimumRadialClearanceMeters': minimum_clearance if math.isfinite(minimum_clearance) else None,
            'minimumActualShaftRadiusMeters': min(radial_radii, default=None), 'maximumActualShaftRadiusMeters': max(radial_radii, default=None),
            'declaredShaftOuterRadiusMeters': contract['shaftOuterRadiusMeters'], 'axisStations': 9, 'radialDirections': 12}


def pin(path):
    path = Path(path).resolve()
    raw = path.read_bytes()
    return {'file': path.relative_to(ROOT).as_posix(), 'bytes': len(raw),
            'sha256': hashlib.sha256(raw).hexdigest()}


def main():
    import bpy
    import numpy as np
    from mathutils import Vector, Quaternion, Matrix
    from mathutils.bvhtree import BVHTree
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--models', nargs='+', choices=CARDS, default=list(CARDS))
    parser.add_argument('--preview-scene',type=Path,help='Isolated saved candidate with serialized modelDesignContract; no production FBX claims')
    parser.add_argument('--render-review', type=Path, help='New isolated folder for actual poses and motion PNG frames')
    parser.add_argument('--video-clips', nargs='*', choices=CLIPS, default=['Locomotion', 'Attack', 'Death'])
    parser.add_argument('--video-fps', type=int, choices=(12, 24), default=24)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
    output = args.report.resolve()
    output.relative_to((ROOT / 'Artifacts/QA').resolve())
    if output.exists():
        raise ValueError('Immutable art-quality report already exists')
    if args.render_review:
        args.render_review = args.render_review.resolve()
        args.render_review.relative_to((ROOT / 'Artifacts/QA').resolve())
        if args.render_review.exists() and any(args.render_review.iterdir()):
            raise ValueError('Review folder must be new; old previews cannot be relabelled')
    manifest = json.loads((ROOT / 'Assets/asset_manifest.json').read_text(encoding='utf-8'))
    source = args.preview_scene.resolve() if args.preview_scene else ROOT / manifest['source']['file']
    if args.preview_scene:source.relative_to((ROOT/'Artifacts/QA').resolve())
    report = {'schema': 1, 'passed': False, 'fixture': False,
              'scope': 'Actual evaluated saved production meshes/poses and strict builder reproduction; human art and native-camera acceptance are separate',
              'sourceBlend': pin(source), 'manifest': pin(ROOT / 'Assets/asset_manifest.json'),
              'verifier': pin(Path(__file__)), 'thresholds': THRESHOLDS,
              'preview': bool(args.preview_scene), 'productionExportPinsChecked': not bool(args.preview_scene),
              'sampling': {'poseMatrices': 'Every integer and quarter frame, inclusive endpoints',
                           'evaluatedMeshes': 'Every integer and half frame, inclusive endpoints',
                           'occlusionViews': VIEWS, 'occlusionPhases': [0, .25, .5, .75, 1]},
              'checks': [], 'errors': [], 'cards': {}, 'renders': [],
              'optionalReview': {'requested': bool(args.render_review), 'completed': False},
              'reviewRequired': 'Human playback/normal-camera review; these gates do not rate artistic quality'}
    def check(name, condition, detail=None):
        report['checks'].append({'name': name, 'passed': bool(condition), 'detail': detail})
        if not condition:
            report['errors'].append(name)
            print('RIFT_ART_QUALITY_FAILURE', name, repr(detail), flush=True)
    if not args.preview_scene:
        check('Saved source is the exact current manifest source', report['sourceBlend'] == manifest['source'])
    bpy.ops.wm.open_mainfile(filepath=str(source))
    scene = bpy.context.scene
    if args.preview_scene:
        actual_records={}
        for name in args.models:
            mesh=bpy.data.objects['SK_'+name];rig=bpy.data.objects['SKEL_'+name]
            if not mesh.get('modelDesignContract'):
                raise ValueError('Preview must retain its actual serialized modelDesignContract: '+name)
            design=json.loads(mesh['modelDesignContract'])
            clips=[]
            for clip_name in CLIPS:
                action=bpy.data.actions.get('AN_'+name+'_'+clip_name)
                if action is None:raise ValueError('Actual preview clip is missing: '+name+' '+clip_name)
                start,end=action.frame_range
                if abs(start-1)>1e-6 or abs(end-round(end))>1e-6:
                    raise ValueError('Saved authored preview frame bounds are invalid: '+action.name)
                clips.append({'name':clip_name,'action':action.name,'frames':round(end),'fps':100,
                    'seconds':(end-start)/100,'releaseNormalized':design['animationContract']['attack']['contactNormalized'] if clip_name=='Attack' else None})
            sockets={bone.name:{'bone':bone.name,'parent':bone.parent.name,'positionMeters':list(bone.head_local)}
                     for bone in rig.data.bones if bone.name.endswith('_contact')}
            actual_records[name]={'id':name,'modelDesign':design,'animations':clips,'sockets':sockets,
                'triangles':sum(len(poly.vertices)-2 for poly in mesh.data.polygons),'boundsMeters':list(mesh.dimensions),
                'geometryReferenceAction':rig.animation_data.action.name if rig.animation_data and rig.animation_data.action else None,
                'geometryReferenceFrame':scene.frame_current+scene.frame_subframe,
                'rigFamily':'quadruped' if name in ('mini_stampede','stampede') else 'mechanism' if name=='pulverizer' else 'specter' if name=='aether_reaper' else 'humanoid'}
        manifest={'source':pin(source),'characters':actual_records,'preview':True}
    sys.path.insert(0, str(ROOT / 'Build'))
    import expansion_new_models as new_producer
    import expansion_asset_models as old_producer
    report['producerPins'] = {name: pin(ROOT / ('Build/' + name + '.py'))
                              for name in ('generate_assets', 'expansion_new_models', 'expansion_asset_models')}
    namespace = {'bpy': bpy, 'math': math, 'Vector': Vector, 'Quaternion': Quaternion, 'Matrix': Matrix}
    tree = ast.parse((ROOT / 'Build/generate_assets.py').read_text(encoding='utf-8'))
    helpers = [node for node in tree.body if isinstance(node, ast.FunctionDef) and
               node.name in {'set_world_rotation', 'aim_leg', 'aim_arm'}]
    exec(compile(ast.Module(body=helpers, type_ignores=[]), 'actual-production-ik', 'exec'), namespace)

    def set_frame(frame):
        scene.frame_set(int(frame), subframe=frame - int(frame))
        bpy.context.view_layer.update()

    def posed_point(rig, bone, point):
        joint = rig.pose.bones[bone]
        return rig.matrix_world @ joint.matrix @ joint.bone.matrix_local.inverted() @ Vector(point)

    def group_indices(mesh):
        result = {group.name: set() for group in mesh.vertex_groups}
        names = {group.index: group.name for group in mesh.vertex_groups}
        for vertex in mesh.data.vertices:
            for membership in vertex.groups:
                if membership.weight > .001:
                    result[names[membership.group]].add(vertex.index)
        return result

    def make_bvh(points, triangles, indices=None):
        selected = triangles if indices is None else [tri for tri in triangles if all(i in indices for i in tri)]
        if not selected:
            return None
        vertices=sorted({i for triangle in selected for i in triangle})
        remap={old:new for new,old in enumerate(vertices)}
        compact=[tuple(remap[i] for i in triangle) for triangle in selected]
        return BVHTree.FromPolygons([Vector(points[i]) for i in vertices], compact, all_triangles=True, epsilon=1e-7)

    def surface_gap(points, triangles, a, b):
        av = set().union(*a); bv = set().union(*b)
        atree = make_bvh(points, triangles, av); btree = make_bvh(points, triangles, bv)
        if atree is None or btree is None:
            raise ValueError('A declared separate surface has no actual triangles')
        distance = min((btree.find_nearest(Vector(points[i]))[3] for i in av), default=math.inf)
        distance = min(distance, min((atree.find_nearest(Vector(points[i]))[3] for i in bv), default=math.inf))
        intersections = len(atree.overlap(btree))
        return distance, intersections

    def inside(tree, point):
        # Closed semantic palms only. Majority of three oblique rays avoids a
        # shared-edge ambiguity and does not enlarge the actual palm surface.
        counts = []
        for direction in ((.871, .314, .377), (-.223, .917, .331), (.419, -.537, .732)):
            direction = Vector(direction).normalized(); origin = Vector(point)
            count = 0
            for unused in range(32):
                hit = tree.ray_cast(origin, direction)
                if hit[0] is None:
                    break
                count += 1; origin = hit[0] + direction * 2e-6
            counts.append(count % 2 == 1)
        return sum(counts) >= 2 or tree.find_nearest(Vector(point))[3] < 2e-5

    for name in args.models:
        record = manifest['characters'][name]
        rig = bpy.data.objects['SKEL_' + name]
        mesh = bpy.data.objects['SK_' + name]
        design = record['modelDesign']; quality = design.get('artQuality', {})
        contact_contract = socket_grip_contract(design)
        clips = {clip['name']: clip for clip in record['animations']}
        check(name + ' retains exact nine clips', set(clips) == set(CLIPS))
        check(name + ' has source geometry authoring semantics', quality.get('schema') == 1)
        groups = group_indices(mesh)
        mesh.data.calc_loop_triangles()
        triangles = [tuple(triangle.vertices) for triangle in mesh.data.loop_triangles]
        # A preceding model's audit advances the shared scene clock. Compare
        # each preview at its actual saved action/frame, or production at the
        # unanimated rest state used by export_character's bounds metadata.
        if args.preview_scene:
            rig.animation_data.action = bpy.data.actions.get(record['geometryReferenceAction']) if record['geometryReferenceAction'] else None
            set_frame(record['geometryReferenceFrame'])
        else:
            rig.animation_data.action = None
            for bone in rig.pose.bones:
                bone.rotation_euler=(0,0,0);bone.location=(0,0,0);bone.scale=(1,1,1)
            set_frame(1)
        actual_bounds = list(mesh.dimensions)
        check(name + ' declared geometry matches actual saved mesh', len(triangles) == record['triangles'] and
              max(abs(a - b) for a, b in zip(actual_bounds, record['boundsMeters'])) <= 1e-5)
        if not args.preview_scene:
            for exported in [record['mesh']] + record.get('lods', []):
                check(name + ' actual mesh export pin ' + exported['file'], all(pin(ROOT / exported['file'])[key] == exported[key]
                                                                             for key in ('file', 'bytes', 'sha256')))
        vertex_count = len(mesh.data.vertices)
        foot_sockets = {key: value for key, value in record['sockets'].items() if key.endswith('_contact')}
        grips = []
        if design.get('twoHandGripsMeters'):
            weapon = 'maul_socket' if name == 'bone_warden' else 'scythe_socket'
            grips = [{'hand': side, 'weaponBone': weapon, 'positionMeters': point}
                     for side, point in design['twoHandGripsMeters'].items()]
        elif design.get('oneHandGripMeters'):
            grips = [design['oneHandGripMeters']]
        face_landmarks = quality.get('faceLandmarks', [])
        regions = quality.get('regions', {})
        if name in ('mini_stampede', 'stampede'):
            face_landmarks = [{'name': 'pupil_' + side, 'bone': 'head',
                              'positionMeters': quality['landmarksMeters']['pupil_' + side],
                              'group': regions['pupil_' + side]['vertexGroup']} for side in ('l', 'r')]
        for landmark in face_landmarks:
            check(name + ' actual face group ' + landmark['name'], bool(groups.get(landmark['group'])))
        for grip in grips:
            check(name + ' actual closed palm group ' + grip['hand'], bool(groups.get('QA_palm_' + grip['hand'])))
            if contact_contract:
                check(name + ' actual held weapon head surface', bool(groups.get('QA_weapon_head')))
                for key in ('palmGroup', 'fingersGroup', 'thumbGroup', 'handleGroup'):
                    declared = contact_contract['hands'][grip['hand']][key]
                    check(name + ' actual socket-contact group ' + declared, bool(groups.get(declared)))
        for key, region in regions.items():
            check(name + ' semantic surface ' + key, bool(groups.get(region['vertexGroup'])))
        contact_surfaces = {side: prepare_socket_surfaces(triangles, groups, declared)
                            for side, declared in contact_contract['hands'].items()} if contact_contract else {}
        captives = design.get('captiveSkeletons') if name == 'bone_warden' else None
        captive_faces = {}; captive_bones = []
        if captives:
            check(name + ' four interior skeletons are visual bodies', captives.get('count') == 4 and captives.get('gameplayEntities') is False)
            check(name + ' interior cage surface exists', bool(groups.get('QA_rib_cage')))
            check(name + ' four separate interior body groups exist', len(captives.get('surfaceGroups', [])) == 4 and
                  len(set(captives['surfaceGroups'])) == 4 and all(groups.get(group) for group in captives['surfaceGroups']))
            captive_bones = captives.get('rootBones', []) + captives.get('headBones', []) + captives.get('armBones', [])
            check(name + ' each interior skeleton has independent body head and arm joints', len(captive_bones) == 12 and
                  len(set(captive_bones)) == 12 and all(bone in rig.pose.bones for bone in captive_bones))
            for group in ['QA_rib_cage'] + captives['surfaceGroups']:
                captive_faces[group] = [triangle for triangle in triangles if all(i in groups[group] for i in triangle)]
        sampled = {}; clip_reports = {}; card_max_grip = 0.; max_delta = 0.
        for clip_name in CLIPS:
            clip = clips[clip_name]
            if not args.preview_scene:
                check(name + ' ' + clip_name + ' actual FBX pin', all(pin(ROOT / clip['file'])[key] == clip[key]
                                                                       for key in ('file', 'bytes', 'sha256')))
            rig.animation_data.action = bpy.data.actions[clip['action']]
            frames = 4 * (clip['frames'] - 1) + 1
            poses = []; metrics = {'quarterSamples': frames, 'meshSamples': 0,
                    'maximumRootMotionMeters': 0., 'minimumAbsoluteDeterminant': math.inf,
                    'minimumMeshZ': math.inf, 'minimumScale': math.inf,
                    'maximumGripToHandMeters': 0., 'outsidePalmSamples': 0,
                    'maximumReachExcessMeters': 0., 'occlusion': [], 'separateSurfaces': {},
                    'groundContacts': {}, 'nonfiniteMatrices': 0, 'nonfiniteMeshSamples': 0,
                    'socketContacts': {}, 'captiveSurfaces': {}, 'captiveMotions': {}}
            captive_initial = {}
            feet = {key: [] for key in foot_sockets}
            contact_set = {round(phase * (frames - 1)) for phase in (0, .25, .5, .75, 1)}
            marker_tick = round((clip.get('releaseNormalized') or 0.) * (frames - 1))
            contact_set.add(marker_tick)
            for tick in range(frames):
                frame = 1 + tick / 4
                set_frame(frame)
                values = array.array('d')
                for bone in rig.pose.bones:
                    flat = [float(value) for row in bone.matrix for value in row]
                    values.extend(flat)
                    metrics['nonfiniteMatrices'] += int(not all(math.isfinite(value) for value in flat))
                    metrics['minimumAbsoluteDeterminant'] = min(metrics['minimumAbsoluteDeterminant'], abs(bone.matrix.to_3x3().determinant()))
                    metrics['minimumScale'] = min(metrics['minimumScale'], *(abs(value) for value in bone.scale))
                poses.append(values)
                metrics['maximumRootMotionMeters'] = max(metrics['maximumRootMotionMeters'], rig.pose.bones['root'].matrix.translation.length)
                if captives and clip_name != 'Death':
                    for bone_name in captive_bones:
                        basis = [float(value) for row in rig.pose.bones[bone_name].matrix_basis for value in row]
                        initial = captive_initial.setdefault(bone_name, basis)
                        value = metrics['captiveMotions'].setdefault(bone_name, {'maximumIndependentLocalBasisDelta': 0., 'loopEndpointLocalBasisDelta': None})
                        delta = max(abs(a-b) for a, b in zip(basis, initial))
                        value['maximumIndependentLocalBasisDelta'] = max(value['maximumIndependentLocalBasisDelta'], delta)
                        if tick == frames - 1:
                            value['loopEndpointLocalBasisDelta'] = delta
                if tick % 2 and tick not in contact_set:
                    continue
                evaluated = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
                data = evaluated.to_mesh()
                try:
                    if len(data.vertices) != vertex_count:
                        raise ValueError('Evaluated mesh topology changed; source semantic indices are no longer valid')
                    flat = np.empty(vertex_count * 3, dtype=np.float64)
                    data.vertices.foreach_get('co', flat)
                    points = flat.reshape((-1, 3))
                    transform = np.array(evaluated.matrix_world)
                    points = points @ transform[:3, :3].T + transform[:3, 3]
                finally:
                    evaluated.to_mesh_clear()
                metrics['meshSamples'] += 1
                metrics['nonfiniteMeshSamples'] += int(not np.isfinite(points).all())
                metrics['minimumMeshZ'] = min(metrics['minimumMeshZ'], float(points[:, 2].min()))
                if captives and clip_name != 'Death':
                    cage = make_bvh(points, captive_faces['QA_rib_cage'])
                    cage_indices = groups['QA_rib_cage']; cage_points = points[sorted(cage_indices)]
                    low = cage_points.min(axis=0); high = cage_points.max(axis=0)
                    for group in captives['surfaceGroups']:
                        body = make_bvh(points, captive_faces[group]); body_points = points[sorted(groups[group])]
                        intersections = len(body.overlap(cage))
                        excess = max(float(np.max(low - body_points.min(axis=0))), float(np.max(body_points.max(axis=0) - high)), 0.)
                        value = metrics['captiveSurfaces'].setdefault(group, {'halfFrameSamples': 0, 'triangleIntersections': 0,
                            'maximumTriangleIntersections': 0, 'firstIntersection': None, 'samplesOutsideCageAABB': 0,
                            'largestCageAABBBoundsExcessMeters': 0., 'clearanceSamples': 0, 'minimumSampledSurfaceGapMeters': math.inf})
                        value['halfFrameSamples'] += 1; value['triangleIntersections'] += intersections
                        value['maximumTriangleIntersections'] = max(value['maximumTriangleIntersections'], intersections)
                        if intersections and value['firstIntersection'] is None:
                            value['firstIntersection'] = {'model': name, 'clip': clip_name, 'frame': frame, 'trianglePairs': intersections}
                        value['samplesOutsideCageAABB'] += int(excess > 1e-5)
                        value['largestCageAABBBoundsExcessMeters'] = max(value['largestCageAABBBoundsExcessMeters'], excess)
                        if tick in contact_set:
                            gap = min(cage.find_nearest(Vector(points[i]))[3] for i in groups[group])
                            gap = min(gap, min(body.find_nearest(Vector(points[i]))[3] for i in cage_indices))
                            value['clearanceSamples'] += 1
                            value['minimumSampledSurfaceGapMeters'] = min(value['minimumSampledSurfaceGapMeters'], gap)
                phase = tick / (frames - 1)
                for key, socket in foot_sockets.items():
                    position = rig.matrix_world @ rig.pose.bones[socket['bone']].matrix.translation
                    foot_indices = groups.get(socket['parent'], set())
                    minimum = min((points[i][2] for i in foot_indices), default=position.z)
                    feet[key].append({'phase': phase, 'seconds': phase * clip['seconds'],
                                      'point': list(position), 'soleMinimumZ': float(minimum)})
                if clip_name != 'Death' or (contact_contract and name != 'bone_warden'):
                    for grip in grips:
                        side = grip['hand']; target = posed_point(rig, grip['weaponBone'], grip['positionMeters'])
                        joint_target = posed_point(rig, grip['weaponBone'], design['twoHandJointTargetsMeters'][side]) if contact_contract else target
                        hand = rig.matrix_world @ rig.pose.bones['hand_' + side].matrix.translation
                        error = (joint_target - hand).length
                        metrics['maximumGripToHandMeters'] = max(metrics['maximumGripToHandMeters'], error)
                        if contact_contract:
                            declared = contact_contract['hands'][side]
                            transform = rig.matrix_world @ rig.pose.bones[grip['weaponBone']].matrix @ rig.pose.bones[grip['weaponBone']].bone.matrix_local.inverted()
                            tangent = transform.to_3x3() @ Vector(declared['shaftTangentMeters'])
                            measured = measure_socket_contact(points, triangles, groups, target, tangent, declared, contact_surfaces[side])
                            value = metrics['socketContacts'].setdefault(side, {'meshSamples': 0, 'triangleIntersections': 0,
                                'handVerticesBuriedInHandle': 0, 'weaponHeadTriangleIntersections': 0, 'handVerticesBuriedInWeaponHead': 0,
                                'shaftAxisInsideHandSamples': 0, 'radialPenetrationSamples': 0, 'shaftAxisRayMissSamples': 0,
                                'maximumContactGapMeters': 0., 'minimumRadialClearanceMeters': math.inf,
                                'minimumActualShaftRadiusMeters': math.inf, 'maximumActualShaftRadiusMeters': 0.,
                                'maximumDeclaredRadiusErrorMeters': 0., 'firstFailure': None})
                            value['meshSamples'] += 1
                            for key in ('triangleIntersections', 'handVerticesBuriedInHandle', 'weaponHeadTriangleIntersections',
                                        'handVerticesBuriedInWeaponHead', 'shaftAxisInsideHandSamples', 'radialPenetrationSamples', 'shaftAxisRayMissSamples'):
                                value[key] += measured[key]
                            if value['firstFailure'] is None and any(measured[key] for key in ('triangleIntersections',
                                    'handVerticesBuriedInHandle', 'weaponHeadTriangleIntersections', 'handVerticesBuriedInWeaponHead',
                                    'shaftAxisInsideHandSamples', 'radialPenetrationSamples', 'shaftAxisRayMissSamples')):
                                value['firstFailure'] = {'model': name, 'clip': clip_name, 'frame': frame, 'side': side, 'measured': measured}
                            value['maximumContactGapMeters'] = max(value['maximumContactGapMeters'], measured['minimumContactGapMeters'])
                            if measured['minimumRadialClearanceMeters'] is not None:
                                value['minimumRadialClearanceMeters'] = min(value['minimumRadialClearanceMeters'], measured['minimumRadialClearanceMeters'])
                            if measured['maximumActualShaftRadiusMeters'] is not None:
                                value['minimumActualShaftRadiusMeters'] = min(value['minimumActualShaftRadiusMeters'], measured['minimumActualShaftRadiusMeters'])
                                value['maximumActualShaftRadiusMeters'] = max(value['maximumActualShaftRadiusMeters'], measured['maximumActualShaftRadiusMeters'])
                                value['maximumDeclaredRadiusErrorMeters'] = max(value['maximumDeclaredRadiusErrorMeters'],
                                    abs(measured['maximumActualShaftRadiusMeters'] - declared['shaftOuterRadiusMeters']))
                        else:
                            palm = make_bvh(points, triangles, groups.get('QA_palm_' + side, set()))
                            metrics['outsidePalmSamples'] += int(palm is None or not inside(palm, target))
                        upper = rig.pose.bones['upperarm_' + side]; fore = rig.pose.bones['forearm_' + side]
                        shoulder = rig.matrix_world @ upper.matrix.translation
                        excess = (joint_target - shoulder).length - (upper.bone.length + fore.bone.length)
                        metrics['maximumReachExcessMeters'] = max(metrics['maximumReachExcessMeters'], excess)
                if tick in contact_set and face_landmarks:
                    all_tree = make_bvh(points, triangles)
                    landmark_samples = []
                    for landmark in face_landmarks:
                        destination = posed_point(rig, landmark['bone'], landmark['positionMeters'])
                        eye_tree = make_bvh(points, triangles, groups[landmark['group']])
                        surface = eye_tree.find_nearest(destination)
                        visible = {}
                        for view, direction in VIEWS.items():
                            origin = destination + Vector(direction).normalized() * max(3., float(max(record['boundsMeters'])) * 3)
                            ray = surface[0] - origin
                            hit = all_tree.ray_cast(origin, ray.normalized(), ray.length + .01)
                            visible[view] = hit[0] is not None and (hit[0] - surface[0]).length <= THRESHOLDS['eyeRayEndpointToleranceMeters']
                        landmark_samples.append({'name': landmark['name'], 'distanceToActualEyeSurfaceMeters': surface[3], 'visible': visible})
                    metrics['occlusion'].append({'phase': phase, 'landmarks': landmark_samples})
                if clip_name != 'Death' and tick in contact_set:
                    def separate(key, gap, intersections, required):
                        previous=metrics['separateSurfaces'].setdefault(key,{'minimumGapMeters':math.inf,
                            'triangleIntersections':0,'requiredGapMeters':required})
                        previous['minimumGapMeters']=min(previous['minimumGapMeters'],gap)
                        previous['triangleIntersections']+=intersections
                    for declaration in quality.get('clearances', []):
                        if clip_name not in declaration['clips']:
                            continue
                        if any(abs(phase - ignored) < 1e-6 for ignored in declaration.get('ignorePhases', [])):
                            continue
                        gap, intersections = surface_gap(points, triangles,
                            [groups[key] for key in declaration['groupsA']], [groups[key] for key in declaration['groupsB']])
                        old = metrics['separateSurfaces'].setdefault(declaration['name'],
                            {'minimumGapMeters': math.inf, 'triangleIntersections': 0, 'requiredGapMeters': declaration['minimumMeters']})
                        old['minimumGapMeters'] = min(old['minimumGapMeters'], gap)
                        old['triangleIntersections'] += intersections
                    if name in ('mini_stampede','stampede'):
                        guard=groups[regions['snout_guard']['vertexGroup']]
                        pupils=[groups[regions['pupil_'+side]['vertexGroup']] for side in ('l','r')]
                        gap,intersections=surface_gap(points,triangles,[guard],pupils)
                        separate('snout_guard_clear_of_eyes',gap,intersections,.085)
                        for leg in ('front','back'):
                            for side in ('l','r'):
                                a=groups[regions['hoof_'+leg+'_'+side+'_-1']['vertexGroup']]
                                b=groups[regions['hoof_'+leg+'_'+side+'_1']['vertexGroup']]
                                gap,intersections=surface_gap(points,triangles,[a],[b])
                                separate('hoof_cleft_'+leg+'_'+side,gap,intersections,.004)
                        guard_tree=make_bvh(points,triangles,guard)
                        jaw=rig.pose.bones['jaw']
                        forward=(rig.matrix_world.to_3x3()@jaw.matrix.to_3x3()@jaw.bone.matrix_local.inverted().to_3x3()@Vector((0,-1,0))).normalized()
                        for side,sign in (('l',-1),('r',1)):
                            well=posed_point(rig,'jaw',(sign*.053,-.641,.300))
                            origin=well+forward*2
                            hit=guard_tree.ray_cast(origin,-forward,2.001)
                            metrics.setdefault('nostrilApertureBlockedSamples',0)
                            metrics['nostrilApertureBlockedSamples']+=int(hit[0] is not None)
                    if name=='aether_reaper':
                        # Opposite gores must remain distinct; neighboring cloth
                        # seams and lining layers intentionally meet and are not
                        # subjected to a generic intersection rejection.
                        for pair in ((0,3),(1,4),(2,5)):
                            a=groups['QA_cloak']&groups['cloth_'+str(pair[0])]
                            b=groups['QA_cloak']&groups['cloth_'+str(pair[1])]
                            gap,intersections=surface_gap(points,triangles,[a],[b])
                            separate('opposite_cloak_gores_'+str(pair),gap,intersections,.005)
                    if name == 'iron_giant':
                        for panel in range(6):
                            gap, intersections = surface_gap(points, triangles,
                                [groups['QA_shield_panel_' + str(panel)]], [groups['QA_giant_body']])
                            old = metrics['separateSurfaces'].setdefault('shield_body_' + str(panel),
                                {'minimumGapMeters': math.inf, 'triangleIntersections': 0, 'requiredGapMeters': THRESHOLDS['semanticSeparateSurfaceGapMeters']})
                            old['minimumGapMeters'] = min(old['minimumGapMeters'], gap); old['triangleIntersections'] += intersections
                    if name == 'pulverizer':
                        def region_points(key): return points[sorted(groups[regions[key]['vertexGroup']])]
                        head_bottom = float(region_points('compression_head')[:, 2].min())
                        cage_top = float(region_points('crystal_cage_top')[:, 2].max())
                        gap = head_bottom - cage_top
                        old = metrics['separateSurfaces'].setdefault('compression_head_above_crystal_cage',
                            {'minimumGapMeters': math.inf, 'triangleIntersections': 0, 'requiredGapMeters': .002})
                        old['minimumGapMeters'] = min(old['minimumGapMeters'], gap)
                        for piston in range(4):
                            collar = region_points('guide_collar_' + str(piston)); rod = region_points('sliding_rod_' + str(piston))
                            center = (collar[:, :2].min(axis=0) + collar[:, :2].max(axis=0)) * .5
                            radial_gap = float(np.linalg.norm(collar[:, :2] - center, axis=1).min() - np.linalg.norm(rod[:, :2] - center, axis=1).max())
                            old = metrics['separateSurfaces'].setdefault('rod_inside_guide_' + str(piston),
                                {'minimumGapMeters': math.inf, 'triangleIntersections': 0, 'requiredGapMeters': .005})
                            old['minimumGapMeters'] = min(old['minimumGapMeters'], radial_gap)
                        if clip_name == 'Attack' and tick == marker_tick:
                            striker = region_points('annular_ground_striker')
                            metrics['strikerContactPlaneZ'] = float(striker[:, 2].min())
                        crystal=region_points('suspended_crystal')
                        aperture=region_points('annular_ground_striker')
                        radial_gap=float(np.linalg.norm(aperture[:,:2],axis=1).min()-np.linalg.norm(crystal[:,:2],axis=1).max())
                        separate('crystal_inside_striker_aperture',radial_gap,0,.029)
                        if clip_name in ('Idle','Attack'):
                            for segment in range(12):
                                a=groups[regions['compression_rim_segment_'+str(segment)]['vertexGroup']]
                                b=groups[regions['compression_rim_segment_'+str((segment+1)%12)]['vertexGroup']]
                                gap,intersections=surface_gap(points,triangles,[a],[b])
                                separate('compression_rim_gap_'+str(segment),gap,intersections,.005)
            sampled[clip_name] = poses
            floor_allowance = THRESHOLDS['deathFloorPenetrationMeters'] if clip_name == 'Death' else THRESHOLDS['livingFloorPenetrationMeters']
            check(name + ' ' + clip_name + ' finite invertible quarter-frame poses', metrics['nonfiniteMatrices'] == 0 and
                  metrics['minimumAbsoluteDeterminant'] > 1e-15 and metrics['minimumScale'] >= 1e-6, metrics['minimumAbsoluteDeterminant'])
            check(name + ' ' + clip_name + ' root stays authority independent', metrics['maximumRootMotionMeters'] < 1e-6, metrics['maximumRootMotionMeters'])
            check(name + ' ' + clip_name + ' actual evaluated mesh stays finite', metrics['nonfiniteMeshSamples'] == 0)
            check(name + ' ' + clip_name + ' avoids deep ground clipping', metrics['minimumMeshZ'] >= -floor_allowance, metrics['minimumMeshZ'])
            if grips and (clip_name != 'Death' or (contact_contract and name != 'bone_warden')):
                if contact_contract:
                    for side, measured in metrics['socketContacts'].items():
                        if not math.isfinite(measured['minimumRadialClearanceMeters']):
                            measured['minimumRadialClearanceMeters'] = None
                        if not math.isfinite(measured['minimumActualShaftRadiusMeters']):
                            measured['minimumActualShaftRadiusMeters'] = None
                        check(name + ' ' + clip_name + ' open hand surfaces do not cross or bury actual handle ' + side,
                            all(measured[key] == 0 for key in ('triangleIntersections', 'handVerticesBuriedInHandle',
                                'weaponHeadTriangleIntersections', 'handVerticesBuriedInWeaponHead',
                                'shaftAxisInsideHandSamples', 'radialPenetrationSamples', 'shaftAxisRayMissSamples')), measured)
                        check(name + ' ' + clip_name + ' open grip stays in contact with actual handle ' + side,
                            measured['maximumContactGapMeters'] <= THRESHOLDS['gripToHandMeters'], measured['maximumContactGapMeters'])
                        check(name + ' ' + clip_name + ' declared grip radius matches actual shaft and bindings ' + side,
                            measured['shaftAxisRayMissSamples'] == 0 and measured['maximumDeclaredRadiusErrorMeters'] <= .002, measured['maximumDeclaredRadiusErrorMeters'])
                else:
                    check(name + ' ' + clip_name + ' living grips remain inside actual closed palm surfaces', metrics['outsidePalmSamples'] == 0, metrics['outsidePalmSamples'])
                check(name + ' ' + clip_name + ' weapon grips stay reachable without floating hands',
                      metrics['maximumGripToHandMeters'] <= THRESHOLDS['gripToHandMeters'] and metrics['maximumReachExcessMeters'] <= .001,
                      {'handGap': metrics['maximumGripToHandMeters'], 'reachExcess': metrics['maximumReachExcessMeters']})
                card_max_grip = max(card_max_grip, metrics['maximumGripToHandMeters'])
            for group, measured in metrics['captiveSurfaces'].items():
                check(name + ' ' + clip_name + ' interior body avoids crossing actual rib surfaces ' + group,
                      measured['triangleIntersections'] == 0, measured)
                check(name + ' ' + clip_name + ' interior body has measured clearance from ribs ' + group,
                      measured['minimumSampledSurfaceGapMeters'] >= THRESHOLDS['semanticSeparateSurfaceGapMeters'], measured)
                check(name + ' ' + clip_name + ' interior body remains within rib-cage axis-aligned bounds ' + group,
                      measured['samplesOutsideCageAABB'] == 0, measured)
            if captives and clip_name in ('Idle', 'Locomotion', 'Status'):
                check(name + ' ' + clip_name + ' interior bodies heads and arms each have their own living motion',
                      all(value['maximumIndependentLocalBasisDelta'] > 1e-6 for value in metrics['captiveMotions'].values()), metrics['captiveMotions'])
                check(name + ' ' + clip_name + ' interior motions close their loop without a jump',
                      all(value['loopEndpointLocalBasisDelta'] <= 1e-7 for value in metrics['captiveMotions'].values()), metrics['captiveMotions'])
            for key, measured in metrics['separateSurfaces'].items():
                check(name + ' ' + clip_name + ' separate surface ' + key,
                      measured['minimumGapMeters'] >= measured['requiredGapMeters'] - 1e-5 and measured['triangleIntersections'] == 0, measured)
            if name == 'pulverizer' and clip_name == 'Attack':
                check(name + ' actual striker meets impact plate at contact', abs(metrics.get('strikerContactPlaneZ', -999) - .337) <= .002,
                      metrics.get('strikerContactPlaneZ'))
            if name in ('mini_stampede','stampede') and clip_name!='Death':
                check(name+' '+clip_name+' both snout-guard nostril apertures remain open',
                      metrics.get('nostrilApertureBlockedSamples',1)==0,metrics.get('nostrilApertureBlockedSamples'))
            if face_landmarks:
                check(name + ' ' + clip_name + ' declared eyes coincide with actual posed surfaces', all(
                    eye['distanceToActualEyeSurfaceMeters'] <= THRESHOLDS['landmarkSurfaceDistanceMeters']
                    for sample in metrics['occlusion'] for eye in sample['landmarks']))
                if clip_name in ('Idle', 'Status', 'Acquire'):
                    check(name + ' ' + clip_name + ' identity face is not fully occluded from both threequarter views', all(
                        any(eye['visible']['left_threequarter'] for eye in sample['landmarks']) and
                        any(eye['visible']['right_threequarter'] for eye in sample['landmarks'])
                        for sample in metrics['occlusion']), metrics['occlusion'])
            if clip_name == 'Locomotion' and foot_sockets:
                stride = design['animationContract']['locomotion']['strideMeters']
                reference_speed = stride / clip['seconds']
                for key, track in feet.items():
                    low = min(item['point'][2] for item in track)
                    slips = []; stance = 0
                    for a, b in zip(track, track[1:]):
                        delta = b['seconds'] - a['seconds']
                        if delta <= 0 or max(a['point'][2], b['point'][2]) > low + THRESHOLDS['stanceHeightBandMeters'] or \
                           abs(b['point'][2] - a['point'][2]) / delta > THRESHOLDS['stanceVerticalSpeedMetersPerSecond']:
                            continue
                        travel_a = Vector(a['point']); travel_b = Vector(b['point'])
                        travel_a.y -= stride * a['phase']; travel_b.y -= stride * b['phase']
                        slips.append(Vector((travel_b.x - travel_a.x, travel_b.y - travel_a.y)).length / delta)
                        stance += 1
                    observed = {'stanceIntervals': stance, 'referenceSpeedSourceMetersPerSecond': reference_speed,
                                'medianStanceSlipMetersPerSecond': float(np.median(slips)) if slips else None,
                                'maximumStanceSlipMetersPerSecond': max(slips, default=None),
                                'minimumContactZ': low, 'maximumContactZ': max(item['point'][2] for item in track),
                                'minimumSoleZ': min(item['soleMinimumZ'] for item in track)}
                    metrics['groundContacts'][key] = observed
                    allowance = reference_speed * THRESHOLDS['stanceSlipReferenceFraction'] + THRESHOLDS['stanceSlipMetersPerSecondAllowance']
                    check(name + ' locomotion has measurable planted stance ' + key, stance >= 2 and low <= .025, observed)
                    check(name + ' locomotion planted stance avoids visible skating ' + key,
                          bool(slips) and float(np.median(slips)) <= allowance, {'measured': observed, 'allowedSlipMetersPerSecond': allowance})
            clip_reports[clip_name] = metrics
        # Reauthor actions in this process only and compare all stored poses.
        # This does not write the blend or any production artifact.
        rig.animation_data.action = None
        for clip in clips.values():
            bpy.data.actions.remove(bpy.data.actions[clip['action']], do_unlink=True)
        f = SimpleNamespace(name=name, family=record['rigFamily'], rig=rig, expansion_design=design)
        if name in new_producer.CONTRACTS:
            made = new_producer.actions(f, scene, namespace['set_world_rotation'], namespace['aim_leg'], namespace['aim_arm'])
        else:
            made = old_producer.actions(f, scene, namespace['set_world_rotation'], namespace['aim_leg'])
        for clip in made:
            rig.animation_data.action = clip['object']
            maximum = 0.
            expected = sampled[clip['name']]
            check(name + ' regenerated ' + clip['name'] + ' retains exact sample count', 4 * (clip['frames'] - 1) + 1 == len(expected))
            for tick, old_pose in enumerate(expected):
                set_frame(1 + tick / 4)
                actual = [float(value) for bone in rig.pose.bones for row in bone.matrix for value in row]
                maximum = max(maximum, max(abs(a - b) for a, b in zip(actual, old_pose)))
            check(name + ' ' + clip['name'] + ' current builder reproduces every saved quarter-frame pose', maximum <= 1e-7, maximum)
            max_delta = max(max_delta, maximum)
        rig.animation_data.action = None
        report['cards'][name] = {'clipCount': len(clips), 'maximumReproductionDelta': max_delta,
                                'maximumLivingGripGapMeters': card_max_grip, 'sourceTriangles': record['triangles'],
                                'sourceBoundsMeters': record['boundsMeters'], 'measuredTriangles': len(triangles),
                                'measuredBoundsMeters': actual_bounds, 'clips': clip_reports}
        print('RIFT_ART_QUALITY_CARD', name, len(report['errors']), 'errors', flush=True)
    # Finish and preserve all mathematical evidence before optional rendering.
    # A renderer failure must never destroy the evaluated geometry/pose report.
    check('Source blend remained byte identical during read-only audit', pin(source) == report['sourceBlend'])
    for key, record in report['producerPins'].items():
        check('Producer stayed unchanged during art audit ' + key, pin(ROOT / record['file']) == record)
    report['passed'] = not report['errors']
    report['checkCount'] = len(report['checks'])
    output.parent.mkdir(parents=True, exist_ok=True)
    math_output = output.with_name(output.stem + '.geometry.json')
    if math_output.exists():
        raise ValueError('Immutable mathematical report already exists')
    math_output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    report['mathematicalEvidence'] = pin(math_output)
    if args.render_review:
        # Reload the saved source so all rendered actions are the original saved
        # actions, rather than the reproduction actions created above in memory.
        try:
            bpy.ops.wm.open_mainfile(filepath=str(source))
            render_review(bpy, Vector, manifest, args, report)
            report['optionalReview']['completed'] = True
        except Exception as error:
            import traceback
            report['optionalReview']['exception'] = traceback.format_exc()
            check('Actual optional review rendering completed', False, repr(error))
    # Recheck all inputs after optional renders as well.
    check('Source blend remained byte identical during read-only audit', pin(source) == report['sourceBlend'])
    for key, record in report['producerPins'].items():
        check('Producer stayed unchanged during art audit ' + key, pin(ROOT / record['file']) == record)
    report['passed'] = not report['errors']
    report['checkCount'] = len(report['checks'])
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('RIFT_ART_QUALITY', report['checkCount'], 'checks', len(report['errors']), 'errors', flush=True)
    if report['errors']:
        raise RuntimeError('Art geometry/pose gates failed; inspect exact frames and actual sheets: ' + ', '.join(report['errors']))


def render_review(bpy, Vector, manifest, args, report):
    """Actual source render frames plus a simple HTML contact/playback index.

    PNG sequences remain available when a Blender build lacks a video encoder.
    No synthetic motion or image paint-over is used.
    """
    import html
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 384; scene.render.resolution_y = 384
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.film_transparent = False
    scene.render.fps = args.video_fps
    stage = bpy.data.collections.get('RenderStage')
    if stage is None:
        # Isolated candidate scenes can omit the production studio. This QA
        # studio exists only in memory; neither source nor candidate is saved.
        stage = bpy.data.collections.new('ArtQualityReviewStudio')
        scene.collection.children.link(stage)
        camera_data = bpy.data.cameras.new('ArtQualityReviewCamera')
        camera = bpy.data.objects.new('ArtQualityReviewCamera', camera_data)
        stage.objects.link(camera); scene.camera = camera
        for label, location, energy, size in (
                ('key', (3, -4, 6), 650, 5), ('fill', (-4, -2, 3), 350, 4),
                ('rim', (1, 4, 5), 550, 3)):
            light_data = bpy.data.lights.new('ArtQualityReview_' + label, 'AREA')
            light_data.energy = energy; light_data.shape = 'DISK'; light_data.size = size
            light = bpy.data.objects.new(light_data.name, light_data); stage.objects.link(light)
            light.location = location
            light.rotation_euler = (Vector((0, 0, .6)) - light.location).to_track_quat('-Z', 'Y').to_euler()
        if scene.world is None:
            scene.world = bpy.data.worlds.new('ArtQualityReviewWorld')
        scene.world.use_nodes = True
        scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.15, .18, .21, 1)
        scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = .5
    else:
        camera = scene.camera
    if camera is None:
        raise ValueError('Review studio does not contain an actual camera')
    folder = args.render_review
    folder.mkdir(parents=True, exist_ok=True)
    rows = []
    for name in args.models:
        record = manifest['characters'][name]
        rig = bpy.data.objects['SKEL_' + name]
        for collection in bpy.data.collections:
            if collection is not None:
                collection.hide_render = collection != stage and collection.name != name
        for obj in bpy.data.collections[name].all_objects:
            if obj is not None:
                obj.hide_render = obj.name not in ('SK_' + name, 'SKEL_' + name)
        height = max(.7, record['boundsMeters'][2]); width = max(record['boundsMeters'][:2])
        target = Vector((0, 0, height * .48))
        camera.data.type = 'ORTHO'; camera.data.ortho_scale = max(height * 1.55, width * 1.65)
        for clip_name in CLIPS:
            clip = next(clip for clip in record['animations'] if clip['name'] == clip_name)
            rig.animation_data.action = bpy.data.actions[clip['action']]
            phases = sorted(set([0, .25, .5, .75, 1] + ([clip['releaseNormalized']] if clip_name == 'Attack' else [])))
            previews = []
            for view, direction in (('front', (0, -6, 2)), ('threequarter', (4, -6, 3))):
                camera.location = target + Vector(direction)
                camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
                for phase in phases:
                    scene.frame_set(1 + int(round((clip['frames'] - 1) * phase)))
                    bpy.context.view_layer.update()
                    path = folder / 'poses' / (name + '_' + clip_name + '_' + view + '_' + f'{phase:.5f}' + '.png')
                    path.parent.mkdir(parents=True, exist_ok=True)
                    scene.render.filepath = str(path)
                    bpy.ops.render.render(write_still=True)
                    item = {'model': name, 'clip': clip_name, 'phase': phase, 'view': view, **pin(path)}
                    report['renders'].append(item); previews.append(item)
            rows.append((name, clip_name, previews))
            if clip_name in args.video_clips:
                camera.location = target + Vector((4, -6, 3))
                camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
                count = max(2, int(round(clip['seconds'] * args.video_fps)) + 1)
                frames = []
                for tick in range(count):
                    phase = tick / (count - 1)
                    frame = 1 + (clip['frames'] - 1) * phase
                    scene.frame_set(int(frame), subframe=frame - int(frame)); bpy.context.view_layer.update()
                    path = folder / 'motion' / (name + '_' + clip_name) / f'{tick:04}.png'
                    path.parent.mkdir(parents=True, exist_ok=True)
                    scene.render.filepath = str(path); bpy.ops.render.render(write_still=True)
                    frames.append(pin(path))
                report.setdefault('animationSequences', []).append({'model': name, 'clip': clip_name,
                    'fps': args.video_fps, 'sourceDurationSeconds': clip['seconds'], 'frameCount': count,
                    'frames': frames, 'source': manifest['source'],
                    'note': 'Exact saved source poses resampled for playback; no runtime FPS measurement'})
    body = ['<!doctype html><meta charset="utf-8"><title>Saved model and animation review</title>',
            '<style>body{font:15px Georgia,serif;background:#ecebe6;color:#222;margin:24px}section{margin:28px 0}figure{display:inline-block;margin:4px}img{width:192px;height:192px}figcaption{font:12px system-ui}h1,h2{font-family:system-ui}</style>',
            '<h1>Saved model and animation review</h1><p>Actual revised source poses. Review face/weapon separation, grip, armor gaps, foot contact, attack contact and death cleanup. Geometric checks do not establish aesthetic acceptance.</p>']
    for name, clip, items in rows:
        body.append('<section><h2>' + html.escape(name + ' / ' + clip) + '</h2>')
        for item in items:
            relative = (ROOT / item['file']).relative_to(folder).as_posix()
            label = item['view'] + ' · ' + f'{item["phase"]:.3f}'
            body.append('<figure><img src="' + html.escape(relative) + '"><figcaption>' + html.escape(label) + '</figcaption></figure>')
        body.append('</section>')
    (folder / 'index.html').write_text('\n'.join(body), encoding='utf-8')
    report['reviewIndex'] = pin(folder / 'index.html')


def compose_review():
    """Compose readable sheets and animated previews from the actual PNGs.

    Run using ordinary Python with Pillow after Blender's immutable report.
    These are source animation previews, never a runtime FPS measurement.
    """
    from PIL import Image, ImageDraw, ImageFont
    import html
    parser = argparse.ArgumentParser(description=compose_review.__doc__)
    parser.add_argument('--compose-review', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.compose_review.resolve(); source.relative_to(ROOT / 'Artifacts/QA')
    output = args.output.resolve(); output.relative_to(ROOT / 'Artifacts/QA')
    if output.exists():
        raise ValueError('Composed review output must be a fresh directory')
    report = json.loads(source.read_text(encoding='utf-8'))
    output.mkdir(parents=True)
    font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 16)
    large = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 22)
    groups = {}
    def checked_image(record):
        path = (ROOT / record['file']).resolve(); path.relative_to(ROOT / 'Artifacts/QA')
        if any(pin(path)[key] != record[key] for key in ('bytes', 'sha256')):
            raise ValueError('Actual rendered frame changed: ' + record['file'])
        return Image.open(path).convert('RGB')
    for item in report.get('renders', []):
        groups.setdefault((item['model'], item['clip']), []).append(item)
    evidence = {'schema': 1, 'scope': 'Actual saved source poses, composed sheets and animated WebP previews; no native or runtime FPS claim',
                'sourceReport': pin(source), 'sourceBlend': report['sourceBlend'], 'verifier': pin(Path(__file__)),
                'contactSheets': [], 'animations': []}
    page = ['<!doctype html><meta charset="utf-8"><title>Actual source motion review</title>',
            '<style>body{font:16px system-ui;margin:24px;background:#e9e8e3;color:#222}img.sheet{width:100%;max-width:1536px}img.motion{width:384px}section{margin:28px 0}</style>',
            '<h1>Actual source motion review</h1><p>Rendered saved mesh and rig poses. Animation speed follows each authored source duration. Geometry gates and human visual acceptance remain separate.</p>']
    for (model, clip), items in sorted(groups.items()):
        views = sorted({item['view'] for item in items}); phases = sorted({item['phase'] for item in items})
        tile = 256; sheet = Image.new('RGB', (tile * len(phases), 56 + (tile + 28) * len(views)), '#e9e8e3')
        draw = ImageDraw.Draw(sheet); draw.text((12, 14), model + ' / ' + clip, font=large, fill='#222')
        for item in items:
            x = phases.index(item['phase']) * tile; y = 56 + views.index(item['view']) * (tile + 28)
            image = checked_image(item); image.thumbnail((tile, tile))
            sheet.paste(image, (x + (tile - image.width) // 2, y))
            draw.text((x + 8, y + tile + 3), item['view'] + '  ' + f'{item["phase"]:.3f}', font=font, fill='#222')
        path = output / (model + '_' + clip + '_sheet.png'); sheet.save(path)
        evidence['contactSheets'].append({'model': model, 'clip': clip, **pin(path)})
        page.append('<section><h2>' + html.escape(model + ' / ' + clip) + '</h2><img class="sheet" src="' + path.name + '"></section>')
    for sequence in report.get('animationSequences', []):
        frames = [checked_image(item) for item in sequence['frames']]
        if len(frames) < 2:
            raise ValueError('Actual motion sequence needs at least two sampled frames')
        # Endpoint is a pose sample, not an extra temporal interval. Holding the
        # final frame for 1 ms preserves the authored duration without a full
        # duplicate frame at a loop boundary.
        milliseconds = max(2, round(sequence['sourceDurationSeconds'] * 1000))
        ends = [round(i * (milliseconds - 1) / (len(frames) - 1)) for i in range(len(frames))]
        durations = [ends[i + 1] - ends[i] for i in range(len(frames) - 1)] + [1]
        path = output / (sequence['model'] + '_' + sequence['clip'] + '.webp')
        frames[0].save(path, save_all=True, append_images=frames[1:], duration=durations, loop=0, lossless=True)
        evidence['animations'].append({'model': sequence['model'], 'clip': sequence['clip'],
            'sourceDurationSeconds': sequence['sourceDurationSeconds'], 'encodedDurationMilliseconds': sum(durations),
            'frameCount': len(frames), 'sourceFrames': sequence['frames'], **pin(path)})
        page.append('<section><h2>' + html.escape(sequence['model'] + ' / ' + sequence['clip']) + '</h2><img class="motion" src="' + path.name + '"></section>')
    if not groups:
        raise ValueError('The actual quality report contains no rendered poses')
    (output / 'index.html').write_text('\n'.join(page), encoding='utf-8')
    evidence['index'] = pin(output / 'index.html')
    (output / 'composition.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
    print(json.dumps({'contactSheets': len(evidence['contactSheets']), 'animations': len(evidence['animations']), 'report': pin(output / 'composition.json')}))


if __name__ == '__main__':
    compose_review() if '--compose-review' in sys.argv else main()
