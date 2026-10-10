"""Clean, production model construction for the 1.3.0 card roster.

Called by generate_assets.py inside Blender.  Equipment is built around the
actual rest-pose hand bones, and shares their weights; decorative passes do
not invent a second set of hands, eyes, shoulder plates or weapons.
"""
import math
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree


PALETTE_ADDITIONS = {
    'steel_shadow': (.095, .145, .19, .80, .36),
    'steel_edge': (.49, .62, .69, .83, .28),
    'gold_edge': (.82, .62, .29, .73, .30),
    'leather_edge': (.30, .15, .072, .0, .76),
    'skin_warm': (.79, .49, .32, .0, .75),
    'skin_shade': (.45, .215, .13, .0, .83),
    'hair_auburn': (.18, .060, .027, .0, .88),
    'cloth_ember_light': (.60, .135, .065, .0, .90),
    'cloth_purple_light': (.29, .12, .37, .0, .89),
    'cloth_blue_light': (.085, .30, .405, .0, .86),
    'fur_cream': (.46, .29, .16, .0, .93),
    'ice_fur_shade': (.29, .47, .53, .0, .89),
    'membrane_shade': (.18, .026, .082, .0, .85),
    'copper': (.61, .285, .105, .70, .35),
    'meteor': (.14, .16, .18, .08, .92),
    'meteor_edge': (.245, .245, .25, .10, .86),
    'hair_silver': (.57, .63, .64, .0, .89),
}


def remove(f, *names):
    for ob in list(f.parts):
        if any('_' + name in ob.name for name in names):
            f.parts.remove(ob)
            bpy.data.objects.remove(ob, do_unlink=True)


def loft(f, name, rings, key, bone=None, segments=32, smooth=True, front_flat=1.0):
    """Closed elliptical sections: (z, rx, ry, optional center x/y)."""
    verts, faces = [], []
    for ring in rings:
        z, rx, ry = ring[:3]
        cx, cy = ring[3:5] if len(ring) > 3 else (0, 0)
        for j in range(segments):
            angle = j * math.tau / segments
            x, y = math.sin(angle) * rx, math.cos(angle) * ry
            if y < 0:
                y = max(y, -ry * front_flat)
            verts.append((cx + x, cy + y, z))
    for row in range(len(rings) - 1):
        for j in range(segments):
            q = row * segments + j
            faces.append((q, row * segments + (j + 1) % segments,
                          (row + 1) * segments + (j + 1) % segments, q + segments))
    faces += [tuple(reversed(range(segments))),
              tuple((len(rings) - 1) * segments + j for j in range(segments))]
    # x=sin(a), y=cos(a) enumerates each section clockwise from above.
    # Reverse the closed surface so the atlas normals point out of the model.
    return f.mesh_data(name, verts, [tuple(reversed(face)) for face in faces], key, bone, smooth)


def plate(f, name, outline, key, bone=None, depth=.018, crown=.012):
    """A fitted plate with a beveled lip, rather than a box on a curved body."""
    points = [Vector(p) for p in outline]
    center = sum(points, Vector()) / len(points)
    verts = points + [center + (p - center) * .89 + Vector((0, -crown, 0)) for p in points]
    verts.append(center + Vector((0, -crown * 1.4, 0)))
    back = len(verts)
    verts += [p + Vector((0, depth, 0)) for p in points]
    n = len(points)
    faces = []
    for j in range(n):
        k = (j + 1) % n
        faces += [(j, k, n + k, n + j), (n + j, n + k, n * 2),
                  (j, back + j, back + k, k)]
    faces.append(tuple(back + j for j in reversed(range(n))))
    signed_volume=0.0
    for face in faces:
        a=Vector(verts[face[0]])
        for j in range(1,len(face)-1):
            signed_volume+=a.dot(Vector(verts[face[j]]).cross(Vector(verts[face[j+1]])))/6
    if signed_volume<0:faces=[tuple(reversed(face)) for face in faces]
    return f.mesh_data(name, verts, faces, key, bone, False)


def thin_cloth(f, name, verts, faces, key, bone, thickness=.010, flip=False):
    if flip:faces=[tuple(reversed(face)) for face in faces]
    ob = f.mesh_data(name, verts, faces, key, bone)
    mod = ob.modifiers.new('Tailored fabric thickness', 'SOLIDIFY')
    mod.thickness = thickness
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)
    return ob


def grip_center(f, side):
    bone = f.bones['hand_' + side]
    point = (Vector(bone['head']) + Vector(bone['tail'])) * .5
    # The back of the palm sits behind the handle; knuckles curl around its front.
    return Vector((point.x, -.162, point.z))


def bone_band(f,name,bone,t,radius,key,thickness=.008):
    a,b=Vector(f.bones[bone]['head']),Vector(f.bones[bone]['tail'])
    direction=(b-a).normalized()
    u=direction.cross(Vector((0,0,1)))
    if u.length<.01:u=direction.cross(Vector((0,1,0)))
    u.normalize();v=direction.cross(u).normalized()
    center=a.lerp(b,t)
    points=[center+(u*math.cos(j*math.tau/40)+v*math.sin(j*math.tau/40))*radius for j in range(41)]
    f.tube(name,points,thickness,key,bone,10)


def hand(f, side, k, armored=False, holding=True):
    sign = -1 if side == 'l' else 1
    bone = 'hand_' + side
    grip = grip_center(f, side)
    f.ellipsoid('fitted_glove_palm', grip + Vector((0, .041, 0)),
                (.067 * k, .050, .084 * k), 'leather', bone, 24)
    if holding:
        for digit in range(4):
            z = grip.z + (digit - 1.5) * .032 * k
            points = [(grip.x - sign * .049 * k, -.133, z),
                      (grip.x - sign * .038 * k, -.196, z),
                      (grip.x + sign * .018 * k, -.202, z),
                      (grip.x + sign * .039 * k, -.165, z)]
            f.tube('curled_glove_digit', points,
                   [.015 * k, .017 * k, .015 * k, .011 * k],
                   'steel' if armored else 'leather_edge', bone, 10)
        f.tube('gripping_thumb',
               [grip + Vector((sign * .05 * k, .035, .065 * k)),
                grip + Vector((sign * .055 * k, -.023, .05 * k)),
                grip + Vector((sign * .012 * k, -.043, .026 * k))],
               [.024 * k, .020 * k, .016 * k],
               'steel' if armored else 'leather', bone, 12)
    else:
        for digit in range(4):
            x = grip.x + (digit - 1.5) * .027 * k
            f.tube('relaxed_glove_digit', [(x, -.150, grip.z + .024 * k),
                                         (x, -.167, grip.z - .068 * k),
                                         (x, -.151, grip.z - .089 * k)],
                   [.016 * k, .015 * k, .011 * k], 'leather', bone, 10)
        f.tube('relaxed_thumb', [grip + Vector((sign * .056 * k, .015, .025 * k)),
                               grip + Vector((sign * .067 * k, -.020, -.033 * k))],
               [.024 * k, .015 * k], 'leather_edge', bone, 12)
    if armored:
        plate(f, 'fitted_gauntlet_plate',
              [(grip.x - .056 * k, -.084, grip.z - .029 * k),
               (grip.x + .056 * k, -.084, grip.z - .029 * k),
               (grip.x + .046 * k, -.081, grip.z + .073 * k),
               (grip.x - .046 * k, -.081, grip.z + .073 * k)],
              'steel_edge', bone, .018)
    return grip


def tailored_cape(f, k, cloth, short=False):
    verts, faces = [], []
    length = .47 if short else .77
    for row in range(11):
        t = row / 10
        width = (.235 + .055 * t) * k
        for col in range(17):
            u = col / 16 * 2 - 1
            verts.append((width * u, .225 + .085 * t + .019 * math.cos(u * math.pi * 3) * t,
                          (1.39 - length * t) * k + .035 * (1 - u * u) * t))
    for row in range(10):
        for col in range(16):
            i = row * 17 + col
            faces.append((i, i + 1, i + 18, i + 17))
    thin_cloth(f, 'tailored_back_cloak', verts, faces, cloth, 'cape')
    f.tube('cloak_hem', [verts[170 + j] for j in range(17)], .007 * k,
           'cloth_ember_light' if cloth == 'cloth_ember' else
           'cloth_purple_light' if cloth == 'cloth_purple' else 'cloth_blue_light', 'cape', 8)


def face(f, k, masked=False, mage=False):
    rings = [(z * k, x * k, y, 0, -.008) for z, x, y in
             [(1.53, .062, .085), (1.59, .115, .110), (1.68, .155, .150),
              (1.78, .172, .153), (1.88, .155, .141),
              (1.96, .108, .106), (2.005, .012, .045)]]
    loft(f, 'sculpted_face_planes', rings, 'black' if masked else 'skin_warm', 'head', 40, True, .93)
    for sign in (-1, 1):
        f.ellipsoid('ear_cartilage', (sign * .164 * k, -.002, 1.746 * k),
                    (.029 * k, .026, .057 * k), 'skin_warm', 'head', 20)
        f.ellipsoid('ear_inner', (sign * .177 * k, -.021, 1.751 * k),
                    (.010 * k, .011, .033 * k), 'skin_shade', 'head', 16)
        eye = Vector((sign * .068 * k, -.160, 1.781 * k))
        f.ellipsoid('sculpted_eye_socket', eye + Vector((0, .010, 0)),
                    (.037 * k, .012, .023 * k), 'black' if masked else 'skin_shade', 'head', 24)
        f.ellipsoid('single_eye_white', eye,
                    (.026 * k, .007, .014 * k), 'violet' if masked else 'ivory', 'head', 24, masked)
        f.ellipsoid('single_eye_iris', eye + Vector((sign * .002 * k, -.009, -.001 * k)),
                    (.010 * k, .004, .012 * k), 'violet' if masked else 'cyan' if mage else 'hair_auburn', 'head', 20, masked)
        f.ellipsoid('single_eye_pupil', eye + Vector((sign * .002 * k, -.014, -.001 * k)),
                    (.004 * k, .002, .008 * k), 'black', 'head', 16)
        f.tube('upper_eye_lid', [(sign * .038 * k, -.160, 1.788 * k),
                               (sign * .067 * k, -.171, 1.800 * k),
                               (sign * .099 * k, -.151, 1.790 * k)],
               [.007, .007, .004], 'black' if masked else 'skin_shade', 'head', 8)
        f.tube('single_expressive_brow',
               [(sign * .035 * k, -.151, 1.826 * k),
                (sign * .073 * k, -.156, 1.830 * k),
                (sign * .108 * k, -.136, 1.819 * k)],
               [.007 * k, .014 * k, .003 * k], 'black' if masked else 'hair_silver' if mage else 'hair_auburn', 'head', 10)
    nose = [(-.023 * k, -.146, 1.808 * k), (.023 * k, -.146, 1.808 * k),
            (-.032 * k, -.154, 1.699 * k), (.032 * k, -.154, 1.699 * k),
            (0, -.207, 1.716 * k), (0, -.166, 1.805 * k)]
    if not masked:
        f.mesh_data('single_nose_bridge', nose,
                    [(0, 2, 4, 5), (1, 5, 4, 3), (0, 5, 1), (2, 3, 4), (0, 1, 3, 2)],
                    'skin_warm', 'head')
    if masked:
        # The mask is one fitted cloth shell below the eyes; the face stays readable.
        plate(f, 'fitted_lower_face_mask',
              [(-.13 * k, -.143, 1.698 * k), (0, -.194, 1.719 * k),
               (.13 * k, -.143, 1.698 * k), (.102 * k, -.129, 1.605 * k),
               (0, -.143, 1.567 * k), (-.102 * k, -.129, 1.605 * k)],
              'cloth_purple', 'head', .009, .003)
        f.tube('mask_center_seam', [(0, -.201, 1.705 * k), (0, -.151, 1.58 * k)],
               .005 * k, 'cloth_purple_light', 'head', 6)
    else:
        f.tube('single_lip_line', [(-.042 * k, -.141, 1.648 * k),
                                  (0, -.151, 1.644 * k),
                                  (.042 * k, -.141, 1.650 * k)],
               [.0035 * k, .005 * k, .003 * k], 'skin_shade', 'head', 8)
        f.ellipsoid('chin_plane', (0, -.100, 1.604 * k),
                    (.071 * k, .026, .040 * k), 'skin_warm', 'head', 24)
        # Each exposed face has a single authored hairline inside the hood.
        hair = 'hair_silver' if mage else 'hair_auburn'
        hair_vertices=[]
        for row in range(2):
            for j in range(11):
                x=(j/10*2-1)*.126*k
                z=(1.932 if row==0 else 1.879-.014*(j%2))*k
                y=-.010-(.13 if row==0 else .158)*math.sqrt(max(.08,1-(x/(.15*k))**2))
                hair_vertices.append((x,y,z))
        thin_cloth(f,'single_connected_hair_fringe',hair_vertices,
                   [(j,j+1,j+12,j+11) for j in range(10)],hair,'head',.009,True)
        for j in (2,5,8):
            a=Vector(hair_vertices[j]);b=Vector(hair_vertices[j+11])
            f.tube('subtle_hair_strand',[a+Vector((0,-.004,0)),a.lerp(b,.45)+Vector((0,-.006,0)),b],
                   [.003,.004,.001],hair,'head',8)
        if mage:
            for sign in (-1, 1):
                f.tube('single_mage_mustache', [(sign * .009 * k, -.160, 1.676 * k),
                                               (sign * .045 * k, -.155, 1.67 * k),
                                               (sign * .068 * k, -.132, 1.647 * k)],
                       [.013 * k, .019 * k, .004 * k], 'hair_silver', 'head', 12)
            for j in range(5):
                x = (j - 2) * .024 * k
                f.tube('single_mage_beard_lock', [(x, -.121, 1.619 * k),
                                                 (x * .73, -.141, 1.566 * k),
                                                 (x * .30, -.143, (1.515 + .006 * abs(j - 2)) * k)],
                       [.022 * k, .021 * k, .002 * k], 'hair_silver', 'head', 12)


def fitted_hood(f, k, cloth, mage=False):
    verts, faces = [], []
    levels = [(1.55, .195, .145), (1.72, .230, .187),
              (1.93, .220, .184), (2.035, .145, .141), (2.065, .018, .050)]
    for row, (z, rx, ry) in enumerate(levels):
        for j in range(41):
            angle = -2.30 + j / 40 * 4.60
            fold = .006 * math.cos(angle * 5) * (1 - row / 5)
            verts.append(((rx + fold) * math.sin(angle) * k,
                          .026 + (ry + fold) * math.cos(angle), z * k))
    for row in range(4):
        for j in range(40):
            q = row * 41 + j
            faces.append((q, q + 1, q + 42, q + 41))
    thin_cloth(f, 'single_open_hood', verts, faces, cloth, 'head', .012, True)
    light = 'cloth_purple_light' if cloth == 'cloth_purple' else 'cloth_blue_light' if mage else 'cloth_ember_light'
    for edge in (0, 40):
        f.tube('hood_sewn_face_edge', [verts[row * 41 + edge] for row in range(5)],
               .008 * k, light, 'head', 8)
    # One scarf fold follows the upper chest and leaves space for the chin.
    f.tube('tailored_neck_cowl', [(-.215 * k, -.10, 1.49 * k),
                               (-.105 * k, -.202, 1.435 * k),
                               (.11 * k, -.195, 1.43 * k),
                               (.225 * k, -.09, 1.48 * k)],
           [.043 * k, .036 * k, .035 * k, .034 * k], cloth, 'chest', 16)
    if mage:
        # A single hat with a narrow brim; no second crown piercing a first hood.
        verts, faces = [], []
        for row, radius in enumerate((.185, .255, .285)):
            for j in range(49):
                a = j * math.tau / 48
                verts.append((math.cos(a) * radius * k, math.sin(a) * radius,
                              2.03 * k + .016 * math.sin(a) * row))
        for row in range(2):
            for j in range(48):
                q = row * 49 + j
                faces.append((q, q + 1, q + 50, q + 49))
        thin_cloth(f, 'single_mage_hat_brim', verts, faces, 'cloth_purple', 'head', .014, True)
        f.tube('single_mage_hat_crown', [(0, .01, 2.04 * k), (.014 * k, .02, 2.22 * k),
                                       (.067 * k, .05, 2.365 * k), (.108 * k, .07, 2.415 * k)],
               [(.181 * k, .166), (.115 * k, .109), (.043 * k, .042), (.002, .002)],
               'cloth_purple', 'head', 32)
        f.ring('hat_band', (0, .01, 2.07 * k), .182 * k, .012,
               'leather_edge', 'head')
        plate(f, 'hat_star_pin', [(-.026 * k, -.165, 2.06 * k), (0, -.174, 2.096 * k),
                                 (.026 * k, -.165, 2.06 * k), (0, -.173, 2.035 * k)],
              'gold_edge', 'head', .008, .005)


def fitted_sword(f, side, k, length, curved=False):
    bone, grip = 'hand_' + side, grip_center(f, side)
    sign = -1 if side == 'l' else 1
    guard_z = grip.z + .124 * k
    width = .059 * k if f.name == 'ironclad' else .044 * k
    verts, faces = [], []
    for j in range(9):
        t = j / 8
        x = grip.x + (sign * .07 * k * t * t if curved else 0)
        w = width * (1 - .48 * t) if j < 8 else .001
        y = grip.y
        z = guard_z + t * length
        verts += [(x - w, y, z), (x, y - .014, z), (x + w, y, z), (x, y + .012, z)]
    for j in range(8):
        for q in range(4):
            faces.append((j * 4 + q, j * 4 + (q + 1) % 4,
                          (j + 1) * 4 + (q + 1) % 4, (j + 1) * 4 + q))
    faces += [(3, 2, 1, 0), (32, 33, 34, 35)]
    f.mesh_data('single_forged_sword', verts, faces, 'silver', bone, False)
    f.tube('sword_handle', [grip + Vector((0, 0, -.073 * k)),
                           grip + Vector((0, 0, .114 * k))],
           [.031 * k, .029 * k], 'leather', bone, 16)
    for j in range(6):
        f.ring('sword_handle_wrap', grip + Vector((0, 0, (-.059 + j * .027) * k)),
               .032 * k, .0035 * k, 'leather_edge', bone)
    f.tube('single_sword_crossguard',
           [(grip.x - .12 * k, grip.y, guard_z - .010 * k),
            (grip.x - .045 * k, grip.y, guard_z + .006 * k),
            (grip.x + .045 * k, grip.y, guard_z + .006 * k),
            (grip.x + .12 * k, grip.y, guard_z - .010 * k)],
           [.017 * k, .023 * k, .023 * k, .017 * k], 'brass', bone, 12)
    f.ellipsoid('single_sword_pommel', grip + Vector((0, 0, -.091 * k)),
                (.039 * k, .032, .039 * k), 'gold_edge', bone, 20)
    if curved:
        f.tube('blade_inlaid_channel',
               [(grip.x, grip.y - .015, guard_z + .07),
                (grip.x + sign * .037 * k, grip.y - .015, guard_z + length * .72)],
               [.006 * k, .003 * k], 'violet', bone, 8, True)


def fitted_bow(f, k):
    grip = grip_center(f, 'l')
    # Bow limbs, grip and string share one center and one hand transform.
    # The bow plane is Y/Z, with its string behind the grip, along the arrow axis.
    points = []
    for j in range(25):
        t = j / 24 * 2 - 1
        points.append((grip.x, grip.y + .155 * t * t - .021 * math.sin(abs(t) * math.pi),
                       grip.z + t * .456 * k))
    f.tube('single_recurve_bow', points,
           [.016 + .011 * (1 - abs(j / 24 * 2 - 1)) for j in range(25)],
           'wood', 'hand_l', 16)
    f.tube('single_bow_string', [points[0], (grip.x, grip.y + .187, grip.z), points[-1]],
           .0034, 'ivory', 'hand_l', 8)
    f.tube('bow_handle_wrap', [(grip.x, grip.y, grip.z - .070 * k),
                              (grip.x, grip.y, grip.z + .070 * k)],
           .031 * k, 'leather_edge', 'hand_l', 16)
    for end in (-1, 1):
        f.ellipsoid('bow_limb_tip', (grip.x, grip.y + .15, grip.z + end * .444 * k),
                    (.019, .022, .040 * k), 'brass', 'hand_l', 20)
    # A sheathed quiver has one set of arrows. No permanently nocked arrow
    # passes through the chest in the idle pose; combat projectiles remain separate.
    f.tube('single_back_quiver', [(.213 * k, .303, .93 * k), (.247 * k, .314, 1.45 * k)],
           [(.070 * k, .061), (.081 * k, .066)], 'leather', 'chest', 24)
    f.ring('quiver_lip', (.247 * k, .314, 1.45 * k), .080 * k, .011,
           'leather_edge', 'chest')
    for j in range(4):
        x = (.201 + j * .029) * k
        z = (1.67 + (j % 2) * .037) * k
        f.tube('single_quiver_arrow', [(x, .315, 1.16 * k), (x, .317, z)],
               .006 * k, 'wood', 'chest', 10)
        feather = [(x - .009 * k, .305, z - .072 * k),
                   (x - .007 * k, .307, z - .019 * k),
                   (x + .003 * k, .307, z + .001 * k),
                   (x + .009 * k, .306, z - .05 * k),
                   (x + .001 * k, .305, z - .075 * k)]
        f.mesh_data('single_quiver_fletching', feather, [(0, 1, 2, 3, 4)], 'ivory', 'chest', False)
    f.tube('diagonal_quiver_strap', [(-.18 * k, -.178, 1.393 * k),
                                    (0, -.232, 1.195 * k),
                                    (.19 * k, -.158, 1.015 * k)],
           [.016 * k, .021 * k, .016 * k], 'leather_edge', 'chest', 10)


def fitted_shield(f, k):
    g = grip_center(f, 'l')
    outline = [(g.x - .22 * k, -.226, g.z + .46 * k),
               (g.x, -.286, g.z + .52 * k),
               (g.x + .22 * k, -.226, g.z + .46 * k),
               (g.x + .21 * k, -.231, g.z + .07 * k),
               (g.x, -.279, g.z - .22 * k),
               (g.x - .21 * k, -.231, g.z + .07 * k)]
    plate(f, 'single_kite_shield', outline, 'steel_shadow', 'hand_l', .037, .010)
    f.tube('shield_fitted_rim', outline + [outline[0]], .015 * k,
           'steel_edge', 'hand_l', 12)
    f.tube('shield_inner_handle', [(g.x, g.y, g.z - .055 * k),
                                  (g.x, g.y, g.z + .095 * k)],
           .031 * k, 'leather', 'hand_l', 16)
    crest = [(g.x - .082 * k, -.307, g.z + .30 * k),
             (g.x + .082 * k, -.307, g.z + .30 * k),
             (g.x + .078 * k, -.310, g.z + .19 * k),
             (g.x, -.313, g.z + .13 * k),
             (g.x - .078 * k, -.310, g.z + .19 * k)]
    plate(f, 'single_shield_crest', crest, 'brass', 'hand_l', .007, .002)
    f.rune((g.x, -.319, g.z + .22 * k), .052 * k, 'cyan', 'hand_l')
    for p in outline:
        f.rivet(Vector(p) + Vector((0, -.012, 0)), 'hand_l', 'brass', .013 * k)


def fitted_staff_and_book(f, k):
    g = grip_center(f, 'r')
    focus_z = 2.01 * k
    f.tube('single_staff_shaft', [(g.x, g.y, .19 * k), (g.x, g.y, 1.72 * k),
                                 (g.x, g.y, focus_z - .13 * k)],
           [.024 * k, .027 * k, .029 * k], 'wood', 'hand_r', 20)
    f.tube('staff_hand_wrap', [g + Vector((0, 0, -.095 * k)), g + Vector((0, 0, .096 * k))],
           .031 * k, 'leather_edge', 'hand_r', 16)
    for z in (.23 * k, 1.69 * k, focus_z - .13 * k):
        f.ring('staff_ferrule', (g.x, g.y, z), .032 * k, .008,
               'brass', 'hand_r')
    f.ring('single_staff_astrolabe', (g.x, g.y, focus_z), .122 * k, .013,
           'gold_edge', 'hand_r', 'Y')
    f.ring('single_staff_meridian', (g.x, g.y, focus_z), .101 * k, .008,
           'silver', 'hand_r', 'X')
    f.tube('single_staff_crystal', [(g.x, g.y, focus_z - .091 * k),
                                   (g.x, g.y, focus_z),
                                   (g.x, g.y, focus_z + .11 * k)],
           [.003, (.067 * k, .059), .002], 'cyan', 'hand_r', 6, True)
    # The left hand holds the lower spine of one book, clear of the forearm.
    b = grip_center(f, 'l')
    center = b + Vector((-.035 * k, -.027, .125 * k))
    f.panel('single_book_pages', center, (.170 * k, .072, .235 * k),
            'ivory', 'hand_l', bevel=.009)
    for dy in (-.048, .048):
        f.panel('single_book_cover', center + Vector((0, dy, 0)),
                (.194 * k, .016, .265 * k), 'cloth_purple', 'hand_l', bevel=.008)
    f.tube('book_spine', [center + Vector((.096 * k, 0, -.13 * k)),
                         center + Vector((.096 * k, 0, .13 * k))],
           [.028 * k, .028 * k], 'leather_edge', 'hand_l', 16)
    f.rune(center + Vector((0, -.058, 0)), .054 * k, 'gold_edge', 'hand_l')


def iron_helmet(f, k):
    loft(f, 'single_forged_closed_helmet',
         [(z * k, x * k, y, 0, -.007) for z, x, y in
          [(1.535, .106, .122), (1.61, .165, .165), (1.77, .203, .204),
           (1.92, .186, .187), (2.035, .116, .148), (2.075, .010, .082)]],
         'steel', 'head', 20, False, .96)
    plate(f, 'single_visor', [(-.15 * k, -.207, 1.813 * k),
                             (.15 * k, -.207, 1.813 * k),
                             (.145 * k, -.207, 1.758 * k),
                             (-.145 * k, -.207, 1.758 * k)], 'black', 'head', .006, .001)
    f.tube('visor_brow_edge', [(-.154 * k, -.213, 1.825 * k),
                              (0, -.225, 1.843 * k), (.154 * k, -.213, 1.825 * k)],
           [.009 * k, .013 * k, .009 * k], 'steel_edge', 'head', 12)
    f.tube('visor_eye_line', [(-.118 * k, -.216, 1.787 * k),
                             (.118 * k, -.216, 1.787 * k)], .0045,
           'cyan', 'head', 8, True)
    plate(f, 'single_helm_nasal_plate', [(-.026 * k, -.217, 1.768 * k),
                                       (.026 * k, -.217, 1.768 * k),
                                       (.020 * k, -.18, 1.600 * k),
                                       (0, -.18, 1.576 * k),
                                       (-.020 * k, -.18, 1.600 * k)],
          'brass', 'head', .009, .004)
    for sign in (-1, 1):
        for j in range(3):
            x = sign * (.058 + j * .026) * k
            f.panel('helm_breathing_slot', (x, -.175 + j * .006, 1.685 * k),
                    (.008 * k, .005, .035 * k), 'black', 'head', bevel=.002)
        f.rivet((sign * .161 * k, -.132, 1.92 * k), 'head', 'gold_edge', .013 * k)
    f.tube('single_plume_mount', [(0, .016, 2.055 * k), (0, .058, 2.138 * k)],
           [.025 * k, .019 * k], 'brass', 'head', 16)
    for j in range(5):
        f.tube('single_helmet_plume', [(0, .058, 2.13 * k),
                                     ((j - 2) * .012 * k, .14, 2.155 * k),
                                     ((j - 2) * .018 * k, .28, (2.08 - .016 * j) * k)],
               [.013 * k, .018 * k, .002],
               'cloth_blue_light' if j % 2 else 'cloth_blue', 'head', 12)


def humanoid_costume(f, k):
    armored, mage = f.name == 'ironclad', f.name == 'arc_mage'
    blades = f.name == 'twin_blades'
    cloth = 'cloth_blue' if armored else 'cloth_indigo' if mage else 'cloth_purple' if blades else 'cloth_ember'
    loft(f, 'single_tailored_torso', [(z * k, x * k, y) for z, x, y in
         [(.83, .208, .164), (.99, .232, .179), (1.17, .276, .191),
          (1.34, .292, .177), (1.46, .243, .140), (1.51, .088, .080)]],
         cloth, 'chest', 40, True)
    loft(f, 'single_waist_wrap', [(.745 * k, .225 * k, .161),
                                (.83 * k, .247 * k, .168), (.93 * k, .225 * k, .155)],
         'leather', 'pelvis', 32)
    f.tube('single_neck', [(0, 0, 1.48 * k), (0, 0, 1.64 * k)],
           [.068 * k, .086 * k], 'skin_warm', 'neck', 24)
    for sign, side in ((-1, 'l'), (1, 'r')):
        upper, fore = 'upperarm_' + side, 'forearm_' + side
        f.tube('tailored_sleeve', [(sign * .26 * k, 0, 1.42 * k),
                                 (sign * .365 * k, -.01, 1.29 * k),
                                 (sign * .43 * k, -.01, 1.16 * k)],
               [.111 * k, .098 * k, .071 * k], cloth, upper, 24)
        f.ellipsoid('single_elbow_joint', (sign * .43 * k, -.01, 1.16 * k),
                    (.078 * k, .080, .078 * k), 'leather', fore, 24)
        f.tube('single_forearm', [(sign * .43 * k, -.01, 1.16 * k),
                                (sign * .466 * k, -.06, 1.035 * k),
                                (sign * .48 * k, -.09, .935 * k)],
               [.071 * k, .079 * k, .058 * k], 'leather', fore, 24)
        hand(f, side, k, armored, holding=not(f.name in ('ember_archer', 'tower_archer') and side == 'r'))
        f.tube('single_trouser_leg', [(sign * .15 * k, 0, .845 * k),
                                     (sign * .16 * k, -.015, .635 * k),
                                     (sign * .16 * k, -.015, .49 * k)],
               [.115 * k, .097 * k, .070 * k], cloth, 'thigh_' + side, 24)
        f.ellipsoid('single_knee_joint', (sign * .16 * k, -.013, .49 * k),
                    (.079 * k, .078, .071 * k), 'leather', 'shin_' + side, 24)
        f.tube('single_boot_shaft', [(sign * .16 * k, -.015, .47 * k),
                                   (sign * .17 * k, 0, .275 * k),
                                   (sign * .17 * k, 0, .12 * k)],
               [.073 * k, .078 * k, .066 * k], 'leather', 'shin_' + side, 24)
        f.ellipsoid('single_boot', (sign * .17 * k, -.081, .098 * k),
                    (.094 * k, .164, .081 * k), 'steel_shadow' if armored else 'leather', 'foot_' + side, 28)
        f.panel('single_boot_sole', (sign * .17 * k, -.071, .037 * k),
                (.182 * k, .295, .038 * k), 'black', 'foot_' + side, bevel=.014)
        bone_band(f,'single_boot_top_seam','shin_'+side,.25,.077*k,
                  'brass' if armored else 'leather_edge',.005*k)
        if armored:
            f.ellipsoid('single_rounded_pauldron', (sign * .29 * k, -.011, 1.436 * k),
                        (.157 * k, .183, .103 * k), 'steel', upper, 32)
            f.tube('single_pauldron_lip', [(sign * .20 * k, -.160, 1.435 * k),
                                         (sign * .29 * k, -.190, 1.386 * k),
                                         (sign * .40 * k, -.118, 1.35 * k)],
                   .010 * k, 'steel_edge', upper, 12)
            plate(f, 'single_bracer', [(sign * .42 * k, -.085, 1.14 * k),
                                       (sign * .50 * k, -.09, 1.11 * k),
                                       (sign * .53 * k, -.125, .972 * k),
                                       (sign * .435 * k, -.137, .955 * k)], 'steel', fore, .025)
            plate(f, 'single_thigh_cuisse', [(sign * .075 * k, -.111, .835 * k),
                                            (sign * .23 * k, -.111, .835 * k),
                                            (sign * .226 * k, -.095, .559 * k),
                                            (sign * .11 * k, -.095, .542 * k)],
                  'steel', 'thigh_' + side, .020)
            f.ellipsoid('single_knee_couter', (sign * .16 * k, -.084, .49 * k),
                        (.089 * k, .046, .070 * k), 'steel_edge', 'shin_' + side, 24)
            plate(f, 'single_fitted_greave', [(sign * .102 * k, -.078, .407 * k),
                                             (sign * .232 * k, -.078, .407 * k),
                                             (sign * .215 * k, -.084, .152 * k),
                                             (sign * .127 * k, -.084, .15 * k)],
                  'steel', 'shin_' + side, .022)
        else:
            # Small fitted cloth shoulder caps, deliberately leaving shoulder joints clear.
            f.ellipsoid('single_soft_shoulder', (sign * .275 * k, .006, 1.431 * k),
                        (.128 * k, .150, .071 * k), cloth, upper, 28)
            f.tube('single_bracer_trim', [(sign * .417 * k, -.015, 1.125 * k),
                                         (sign * .44 * k, -.098, 1.115 * k),
                                         (sign * .492 * k, -.084, 1.09 * k)],
                   .008 * k, 'leather_edge', fore, 10)
        bone_band(f,'single_team_cuff',fore,.92,.068*k,'team',.009*k)
    # A closed belt conforms to the waist and remains on the pelvis in all animations.
    belt_x,belt_y=(.282,.220) if mage else (.253,.181)
    loft(f, 'single_waist_belt', [(.838 * k, belt_x * k, belt_y), (.905 * k, (belt_x-.007) * k, belt_y-.003)],
         'leather_edge', 'pelvis', 40)
    f.panel('single_belt_buckle', (0, -belt_y-.007, .87 * k), (.073 * k, .020, .067 * k),
            'brass', 'pelvis', bevel=.009)
    f.panel('single_buckle_inset', (0, -belt_y-.021, .87 * k), (.039 * k, .006, .032 * k),
            'leather', 'pelvis', bevel=.004)
    tailored_cape(f, k, cloth, blades)
    if armored:
        loft(f, 'single_tapered_cuirass', [(z * k, x * k, y) for z, x, y in
             [(.967, .209, .174), (1.105, .244, .205), (1.28, .292, .223),
              (1.397, .302, .198), (1.47, .23, .154)]],
             'steel', 'chest', 24, False, .98)
        f.tube('cuirass_neck_trim', [(-.211 * k, -.145, 1.445 * k),
                                   (0, -.172, 1.397 * k), (.211 * k, -.145, 1.445 * k)],
               .010 * k, 'brass', 'chest', 12)
        f.tube('single_cuirass_center_ridge', [(0, -.187, 1.435 * k),
                                              (0, -.237, 1.275 * k), (0, -.214, 1.10 * k)],
               [.008 * k, .014 * k, .007 * k], 'steel_edge', 'chest', 10)
        for sign in (-1, 1):
            plate(f, 'single_waist_tasset', [(sign * .037 * k, -.190, 1.015 * k),
                                            (sign * .205 * k, -.158, 1.015 * k),
                                            (sign * .221 * k, -.159, .864 * k),
                                            (sign * .06 * k, -.210, .842 * k)],
                  'steel', 'pelvis', .021)
        iron_helmet(f, k)
        fitted_sword(f, 'r', k, .72 * k)
        fitted_shield(f, k)
    else:
        face(f, k, blades, mage)
        fitted_hood(f, k, cloth, mage)
        if mage:
            # One cloth robe, open at the front, with a sculpted hem and real thickness.
            verts, faces = [], []
            # The skirt terminates inside the belt volume; its top edge cannot
            # poke through the tunic or form a jagged line above the buckle.
            for row, (z, radius) in enumerate(((.15, .326), (.34, .319), (.61, .277), (.79, .260), (.88, .257))):
                for j in range(41):
                    angle = -.91 * math.pi + j / 40 * 1.82 * math.pi
                    fold = .009 * math.cos(angle * 7)
                    verts.append(((radius + fold) * math.sin(angle) * k,
                                  (radius * .64 + .035 + fold) * math.cos(angle),
                                  z * k + .017 * math.cos(angle * 3) * (1 - row / 5)))
            for row in range(4):
                for j in range(40):
                    q = row * 41 + j
                    faces.append((q, q + 1, q + 42, q + 41))
            thin_cloth(f, 'single_split_mage_robe', verts, faces, 'cloth_indigo', 'pelvis', .012, True)
            for edge in (0, 40):
                f.tube('single_robe_front_trim', [verts[row * 41 + edge] for row in range(5)],
                       .007 * k, 'brass', 'pelvis', 10)
            fitted_staff_and_book(f, k)
            f.rune((0, -.206, 1.215 * k), .081 * k, 'cyan', 'chest')
        elif blades:
            plate(f, 'single_assassin_chest_guard', [(-.192 * k, -.179, 1.345 * k),
                                                    (.192 * k, -.179, 1.345 * k),
                                                    (.16 * k, -.21, 1.073 * k),
                                                    (0, -.224, 1.028 * k),
                                                    (-.16 * k, -.21, 1.073 * k)],
                  'leather', 'chest', .017, .006)
            f.rune((0, -.239, 1.20 * k), .070 * k, 'violet', 'chest')
            fitted_sword(f, 'l', k, .58 * k, True)
            fitted_sword(f, 'r', k, .58 * k, True)
        else:
            loft(f, 'single_fitted_archer_vest', [(1.00 * k, .240 * k, .196),
                                                (1.14 * k, .274 * k, .206),
                                                (1.31 * k, .306 * k, .221),
                                                (1.44 * k, .287 * k, .20)],
                 'leather', 'chest', 40)
            for sign in (-1, 1):
                f.tube('single_vest_sewn_edge', [(sign * .198 * k, -.135, 1.354 * k),
                                                (sign * .247 * k, -.131, 1.235 * k),
                                                (sign * .204 * k, -.125, 1.04 * k)],
                       .005 * k, 'leather_edge', 'chest', 8)
            for z in (1.11, 1.23):
                f.rivet((.024 * k, -.220, z * k), 'chest', 'gold_edge', .010 * k)
            fitted_bow(f, k)
    # A tiny cloth tab provides team tint without adding another armor block.
    bone_band(f,'single_team_sleeve_band','upperarm_l',.60,.10*k,'team',.009*k)


def clean_creature_details(f):
    # Hogs are fully authored by their own compact builder. The legacy final
    # quadruped branch is Frost Fang's species pass, not a generic equipment
    # pass; applying it here creates floating frost armor on short hog rigs.
    if f.name in ('mini_stampede','stampede'):return
    if f.family == 'quadruped':
        remove(f, 'team_collar_band')
        if f.name == 'boulderback':
            remove(f, 'chiseled_shell_plate', 'rift_crystal', 'rift_vein', 'eye_brow', 'eye', 'stone_tusk')
            f.ellipsoid('single_carapace_underlay', (0, .14, 1.405), (.596, .724, .147),
                        'stone_dark', 'spine', 40)
            # Deliberate stone courses are separated, not a stack of intersecting shards.
            for row in range(3):
                y = -.32 + row * .40
                for col in range(3):
                    x = (col - 1) * .337
                    z = 1.51 + .063 * (1 - abs(col - 1)) - .04 * abs(row - 1)
                    variance=.014*math.sin(row*4.73+col*1.83)
                    rings = [(z - .067, .177+variance, .219-variance, x, y),
                             (z, .180+variance, .213-variance, x+.012*math.sin(row+col), y),
                             (z + .047, .140, .172, x, y+.018*math.cos(row+col)),
                             (z + .077+variance, .008, .010, x-.04*math.sin(col+row*2), y)]
                    loft(f, 'fitted_carapace_stone', rings,
                         'limestone' if (row + col) % 2 else 'stone', 'spine', 7, False)
            for x, y, height in ((-.295, .00, .44), (.00, .28, .53), (.295, .57, .36)):
                f.tube('single_carapace_crystal', [(x, y, 1.58),
                                                  (x + .017, y + .025, 1.58 + height * .68),
                                                  (x + .01, y + .049, 1.58 + height)],
                       [.090, .068, .002], 'violet', 'spine', 6, True)
            for sign in (-1, 1):
                f.ellipsoid('single_stone_eye_socket', (sign * .222, -1.087, 1.286),
                            (.063, .027, .041), 'stone_dark', 'head', 20)
                f.ellipsoid('single_stone_eye', (sign * .222, -1.112, 1.286),
                            (.025, .012, .020), 'violet', 'head', 20, True)
                f.tube('single_stone_brow', [(sign * .16, -1.090, 1.345),
                                            (sign * .23, -1.084, 1.35),
                                            (sign * .29, -1.06, 1.324)],
                       [.020, .024, .009], 'limestone', 'head', 12)
                f.tube('single_stone_tusk', [(sign * .222, -1.105, 1.034),
                                            (sign * .282, -1.205, .965),
                                            (sign * .303, -1.291, 1.09)],
                       [.058, .040, .002], 'limestone', 'jaw', 12)
            f.tube('single_stone_jaw_line', [(-.12, -1.291, 1.029),
                                            (0, -1.328, 1.016), (.12, -1.291, 1.029)],
                   .008, 'stone_dark', 'jaw', 10)
        elif f.name == 'rambeast':
            remove(f, 'sculpted_curled_horn', 'rivet', 'saddle_trim', 'saddle_armor',
                   'forehead_armor', 'harness', 'eye_brow', 'eye', 'fur_lock', 'ear', 'muzzle', 'nose')
            f.ellipsoid('single_ram_muzzle',(0,-1.133,1.075),(.185,.228,.154),'fur','jaw',32)
            f.ellipsoid('single_ram_nose',(0,-1.337,1.105),(.080,.033,.043),'black','jaw',24)
            f.tube('single_ram_mouth_line',[(-.084,-1.322,1.031),(0,-1.353,1.020),(.084,-1.322,1.031)],
                   .007,'fur_dark','jaw',10)
            for sign in (-1, 1):
                points, radii = [], []
                # Horn coils beside each temple in Y/Z, clear of both eyes and snout.
                for j in range(33):
                    t = j / 32
                    a = -.58 + t * math.pi * 1.76
                    radius = .275 * (1 - .43 * t)
                    points.append((sign * (.32 + .13 * math.sin(t * math.pi)),
                                   -.765 + math.cos(a) * radius, 1.427 + math.sin(a) * radius))
                    radii.append(.076 * (1 - .84 * t))
                f.tube('single_temple_horn', points, radii, 'horn', 'head', 20)
                # Restrained facial fur and a second material under the mane.
                for j in range(6):
                    z = 1.08 + j * .046
                    f.tube('single_ram_cheek_lock', [(sign * .265, -.941, z+.026),
                                                    (sign * .342, -.969, z - .044),
                                                    (sign * .389, -.918, z - .095)],
                           [.036, .028, .002], 'fur_cream' if j % 3 == 0 else 'fur', 'head', 10)
                f.ellipsoid('single_ram_eye_socket', (sign * .227, -1.092, 1.30),
                            (.055, .022, .037), 'fur_dark', 'head', 20)
                f.ellipsoid('single_ram_eye', (sign * .227, -1.114, 1.30),
                            (.025, .010, .018), 'ember', 'head', 20, True)
                f.tube('single_ram_brow', [(sign * .16, -1.07, 1.34),
                                          (sign * .28, -1.063, 1.336)],
                       [.017, .009], 'fur_cream', 'head', 12)
            f.ellipsoid('single_leather_saddle', (0, .21, 1.407), (.323, .397, .067),
                        'leather', 'spine', 32)
            for sign in (-1, 1):
                f.tube('single_saddle_edge', [(sign * .26, -.105, 1.407),
                                             (sign * .339, .17, 1.39),
                                             (sign * .265, .50, 1.407)],
                       .014, 'leather_edge', 'spine', 12)
            fitted_ram_harness(f)
            plate(f, 'single_ram_forehead_guard', [(-.132, -1.015, 1.385),
                                                   (0, -1.065, 1.429),
                                                   (.132, -1.015, 1.385),
                                                   (0, -1.141, 1.329)],
                  'steel', 'head', .017, .029)
            f.rune((0, -1.101, 1.382), .027, 'ember', 'head')
        else:
            remove(f, 'fur_lock', 'facial_fur', 'subtle_face_marking', 'claw',
                   'ice_spine', 'frost_collar', 'harness', 'articulated_frost_armor', 'frost_armor_edge',
                   'frost_brow_armor','rift_rune')
            # One dorsal crystal cluster and two fitted shoulder plates leave the face clear.
            for x, y, h in ((-.13, -.06, .32), (.10, .25, .44), (-.10, .53, .28)):
                base=anatomy_height(f,x,y,1.28)-.026
                f.tube('single_frost_dorsal_crystal', [(x, y, base),
                                                      (x * 1.07, y + .03, base + h * .72),
                                                      (x * 1.10, y + .058, base + h)],
                       [.074, .050, .002], 'ice', 'spine', 6)
            for sign in (-1, 1):
                f.ellipsoid('single_frost_shoulder_cap',(sign*.361,-.235,1.22),
                            (.124,.224,.113),'ice_steel','spine',32)
                for j in range(4):
                    f.tube('single_feline_cheek_lock', [(sign * .24, -.891, 1.35 - j * .068),
                                                       (sign * .355, -.94, 1.303 - j * .068),
                                                       (sign * .386, -.914, 1.275 - j * .068)],
                           [.023, .027, .002], 'ice_fur', 'head', 10)
                for j in range(3):
                    f.tube('single_feline_fur_stripe', [(sign * (.28 + j * .026), -.63, 1.255),
                                                       (sign * (.40 + j * .024), -.58, 1.08)],
                           [.010, .002], 'ice_fur_shade', 'neck', 8)
            # Remove the original saber teeth before authoring one clear pair.
            remove(f, 'saber_fang')
            for sign in (-1, 1):
                f.tube('single_fitted_saber_fang', [(sign * .155, -1.197, 1.06),
                                                   (sign * .169, -1.26, .917),
                                                   (sign * .171, -1.269, .77)],
                       [.040, .029, .002], 'ivory', 'jaw', 16)
        # One small team-color collar stays on the same neck transform.
        f.tube('single_team_creature_collar', [(-.293, -.64, 1.064), (0, -.782, 1.047),
                                              (.293, -.64, 1.064)],
               .019, 'team', 'neck', 12)
    elif f.family == 'flyer':
        remove(f, 'team_chest_mount', 'team_dorsal_mount', 'electric_wing_conduit',
               'cyan_fin_edge', 'wing_identity_edge')
        if f.name == 'sky_manta':
            remove(f, 'dorsal_rift_mark', 'eye_socket', 'glowing_eye', 'sculpted_head')
            f.ellipsoid('single_streamlined_manta_head', (0, -.455, 1.082),
                        (.218, .234, .131), 'teal', 'head', 40)
            f.ellipsoid('single_manta_underbelly', (0, -.18, 1.006),
                        (.285, .379, .061), 'teal_light', 'body', 32)
            for sign in (-1, 1):
                f.ellipsoid('single_manta_eye_socket', (sign * .145, -.620, 1.129),
                            (.042, .028, .029), 'stone_dark', 'head', 24)
                f.ellipsoid('single_manta_eye', (sign * .151, -.641, 1.13),
                            (.020, .011, .016), 'cyan', 'head', 20, True)
                f.tube('single_manta_cephalic_fin', [(sign * .15, -.54, 1.05),
                                                    (sign * .237, -.648, 1.052),
                                                    (sign * .263, -.707, 1.114)],
                       [.039, .020, .002], 'teal_light', 'head', 16)
                f.tube('single_manta_fin_leading_edge', [(sign * .275, -.43, 1.055),
                                                        (sign * .72, -.293, 1.172),
                                                        (sign * 1.105, -.109, 1.14),
                                                        (sign * 1.55, .207, 1.014)],
                       [.007, .007, .006, .002], 'cyan', 'wing_' + ('l' if sign < 0 else 'r'), 10, True)
            f.tube('single_manta_dorsal_keel', [(0, -.218, 1.18), (0, .055, 1.232),
                                              (0, .32, 1.177)],
                   [.024, .043, .006], 'teal_light', 'body', 20)
            f.ellipsoid('single_manta_team_mark',(0,-.045,1.211),(.062,.082,.009),
                        'team','body',24)
        elif f.name == 'vampire_bats':
            remove(f, 'pointed_ear', 'eye_socket', 'glowing_eye', 'bat_fur_lock', 'vampire_core')
            for sign in (-1, 1):
                points = [(sign * .093, -.40, 1.359), (sign * .14, -.383, 1.55),
                          (sign * .202, -.404, 1.64), (sign * .181, -.477, 1.383)]
                f.mesh_data('single_sculpted_bat_ear', points,
                            [(0, 1, 2), (0, 2, 3), (1, 3, 2), (0, 3, 1)], 'plum', 'head')
                f.mesh_data('single_bat_inner_ear', [(sign * .127, -.420, 1.40),
                                                    (sign * .188, -.428, 1.60),
                                                    (sign * .17, -.470, 1.403)], [(0, 1, 2)],
                            'membrane', 'head')
                f.ellipsoid('single_bat_eye_socket', (sign * .107, -.631, 1.273),
                            (.048, .024, .039), 'black', 'head', 24)
                f.ellipsoid('single_bat_eye', (sign * .109, -.651, 1.276),
                            (.024, .010, .022), 'violet', 'head', 20, True)
                for j in range(3):
                    f.tube('single_bat_neck_fur', [(sign * .10, -.205, 1.14 - j * .04),
                                                  (sign * .18, -.14, 1.08 - j * .04),
                                                  (sign * .163, -.091, 1.025 - j * .04)],
                           [.020, .027, .002], 'plum', 'body', 10)
            f.ellipsoid('single_bat_muzzle', (0, -.645, 1.203), (.067, .044, .041),
                        'membrane_shade', 'jaw', 24)
            f.ellipsoid('single_bat_nose', (0, -.691, 1.232), (.027, .023, .022),
                        'black', 'jaw', 20)
            f.ring('single_bat_team_mark', (0, -.224, 1.007), .042, .007, 'team', 'body', 'Y')
        else:
            remove(f, 'eye_socket', 'glowing_eye', 'sculpted_beak', 'storm_core', 'chest_core_mount')
            # Tapered avian beak and feather brows replace the spherical cartoon snout.
            f.tube('single_raven_upper_beak', [(0, -.62, 1.255), (0, -.78, 1.244),
                                              (0, -.88, 1.183)],
                   [(.082, .032), (.051, .025), (.001, .001)], 'steel_shadow', 'head', 14)
            f.tube('single_raven_lower_beak', [(0, -.625, 1.209), (0, -.783, 1.197),
                                              (0, -.862, 1.181)],
                   [(.059, .015), (.038, .012), (.001, .001)], 'steel_edge', 'jaw', 14)
            for sign in (-1, 1):
                f.ellipsoid('single_raven_eye_socket', (sign * .116, -.619, 1.30),
                            (.044, .019, .033), 'black', 'head', 24)
                f.ellipsoid('single_raven_eye', (sign * .119, -.638, 1.302),
                            (.022, .009, .020), 'cyan', 'head', 20, True)
                f.tube('single_raven_brow_feather', [(sign * .07, -.592, 1.35),
                                                    (sign * .125, -.606, 1.347),
                                                    (sign * .16, -.563, 1.327)],
                       [.009, .017, .002], 'raven_blue', 'head', 10)
                f.tube('single_raven_lightning_inlay', [(sign * .30, -.021, 1.17),
                                                       (sign * .59, .006, 1.165),
                                                       (sign * .79, -.018, 1.158),
                                                       (sign * 1.23, .066, 1.125)],
                       [.007, .006, .005, .002], 'cyan', 'wing_' + ('l' if sign < 0 else 'r'), 10, True)
            f.ellipsoid('single_storm_chest_crystal', (0, -.403, 1.093),
                        (.052, .023, .064), 'cyan', 'body', 24, True)
            f.ring('single_storm_crystal_mount', (0, -.402, 1.093), .076, .009,
                   'team', 'body', 'Y')
        unify_flyer_body(f)
        if f.name=='storm_raven':raven_plumage(f)
        continuous_wing_weights(f)


def anatomy_height(f,x,y,fallback):
    anatomy=next((ob for ob in f.parts if ob.name.startswith(f.name+'_continuous_anatomy')),None)
    if anatomy is None:return fallback
    origin=anatomy.matrix_world.inverted()@Vector((x,y,3))
    direction=anatomy.matrix_world.to_3x3().inverted()@Vector((0,0,-1))
    hit,point,normal,index=anatomy.ray_cast(origin,direction)
    return (anatomy.matrix_world@point).z if hit else fallback


def fitted_ram_harness(f):
    """One saddle band follows the actual sculpt and its skin deformation."""
    anatomy=next(ob for ob in f.parts if ob.name.startswith(f.name+'_continuous_anatomy'))
    inverse=anatomy.matrix_world.inverted()
    center=Vector((0,.13,.98))
    points=[]
    for step in range(49):
        angle=step/48*math.tau
        direction=Vector((math.sin(angle),0,math.cos(angle)))
        local_direction=inverse.to_3x3()@direction
        hit,point,normal,index=anatomy.ray_cast(inverse@center,local_direction)
        if not hit:raise ValueError('Rambeast saddle band missed its sculpt surface')
        world_normal=(anatomy.matrix_world.to_3x3()@normal).normalized()
        points.append(anatomy.matrix_world@point+world_normal*.030)
    band=f.tube('single_fitted_saddle_band',points,.013,'leather_edge',sides=12)
    anatomy.data.calc_loop_triangles()
    triangles=list(anatomy.data.loop_triangles)
    tree=BVHTree.FromPolygons([v.co for v in anatomy.data.vertices],
                             [t.vertices for t in triangles],all_triangles=True)
    groups={g.index:band.vertex_groups.new(name=g.name) for g in anatomy.vertex_groups}
    for vertex in band.data.vertices:
        point,normal,index,distance=tree.find_nearest(inverse@(band.matrix_world@vertex.co))
        tri=triangles[index]
        a,b,c=(anatomy.data.vertices[i] for i in tri.vertices)
        v0,v1,v2=b.co-a.co,c.co-a.co,point-a.co
        d00,d01,d11,d20,d21=v0.dot(v0),v0.dot(v1),v1.dot(v1),v2.dot(v0),v2.dot(v1)
        divisor=max(1e-12,d00*d11-d01*d01)
        wb=(d11*d20-d01*d21)/divisor;wc=(d00*d21-d01*d20)/divisor
        bary=[max(0,1-wb-wc),max(0,wb),max(0,wc)]
        total=sum(bary)
        weights={}
        for source,factor in zip((a,b,c),bary):
            for group in source.groups:
                weights[group.group]=weights.get(group.group,0)+group.weight*factor/total
        for group,weight in weights.items():
            if weight>1e-6:groups[group].add([vertex.index],weight,'REPLACE')


def unify_flyer_body(f):
    names=('organic_body','chest','sculpted_head','single_streamlined_manta_head')
    members=[ob for ob in f.parts if any(ob.name.startswith(f.name+'_'+n) for n in names)]
    if len(members)<2:return
    bpy.ops.object.select_all(action='DESELECT')
    for ob in members:
        ob.select_set(True)
        f.parts.remove(ob)
    bpy.context.view_layer.objects.active=members[0]
    bpy.ops.object.join()
    anatomy=bpy.context.object
    bpy.context.scene.cursor.location=(0,0,0)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    mod=anatomy.modifiers.new('Continuous flight anatomy','REMESH')
    mod.mode='VOXEL';mod.voxel_size=.019;mod.use_smooth_shade=True
    bpy.ops.object.modifier_apply(modifier=mod.name)
    smooth=anatomy.modifiers.new('Flight surface polish','SMOOTH')
    smooth.factor=.55;smooth.iterations=4
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    eligible={name:f.bones[name] for name in ('body','neck','head','jaw','tail')}
    for group in list(anatomy.vertex_groups):anatomy.vertex_groups.remove(group)
    for name in eligible:anatomy.vertex_groups.new(name=name)
    for vertex in anatomy.data.vertices:
        distances=[]
        for name,bone in eligible.items():
            a,b=Vector(bone['head']),Vector(bone['tail'])
            direction=b-a
            t=max(0,min(1,(vertex.co-a).dot(direction)/max(.0001,direction.length_squared)))
            distances.append(((vertex.co-a-direction*t).length,name))
        candidates=sorted(distances)[:3]
        weights=[1/(.023+d)**4 for d,name in candidates]
        total=sum(weights)
        for (_,name),weight in zip(candidates,weights):
            anatomy.vertex_groups[name].add([vertex.index],weight/total,'REPLACE')
    for uv in list(anatomy.data.uv_layers):anatomy.data.uv_layers.remove(uv)
    anatomy.name='continuous_flight_anatomy'
    f.add(anatomy,'raven' if f.name=='storm_raven' else 'teal' if f.name=='sky_manta' else 'plum')


def raven_plumage(f):
    # Old small locks sat inside the body; this single layer follows the visible hull.
    for ob in list(f.parts):
        if 'sculpted_feather' in ob.name and ob.vertex_groups.get('body'):
            f.parts.remove(ob)
            bpy.data.objects.remove(ob,do_unlink=True)
    for row in range(3):
        z=1.285-row*.068
        radius=math.sqrt(max(.01,1-((z-1.06)/.25)**2))
        for j in range(12):
            a=j*math.tau/12+(row%2)*math.pi/12
            # Keep the forward throat open for the chest crystal and head motion.
            if math.cos(a)<-.62:continue
            start=Vector((math.sin(a)*.317*radius,math.cos(a)*.50*radius+.02,z))
            end=start+Vector((math.sin(a)*.049,.082,-.083))
            normal=Vector((math.sin(a),math.cos(a),.22)).normalized()
            side=(end-start).cross(normal).normalized()
            middle=start.lerp(end,.42)+normal*.014
            verts=[start-side*.029,middle-side*.038,end,middle+side*.038,start+side*.029,
                   middle+normal*.012]
            faces=[(0,1,5),(1,2,5),(2,3,5),(3,4,5),(4,0,5),(4,3,2,1,0)]
            f.mesh_data('single_raven_body_feather',verts,faces,
                        'raven_blue' if (j+row)%3 else 'raven','body')


def continuous_wing_weights(f):
    """Membranes, finger spars and feathers use the same span weight gradient."""
    for ob in f.parts:
        if not any(group.name.startswith('wing_') for group in ob.vertex_groups):
            continue
        for group in list(ob.vertex_groups):
            ob.vertex_groups.remove(group)
        for side in ('l', 'r'):
            for suffix in ('', '_outer', '_tip'):
                ob.vertex_groups.new(name='wing_' + side + suffix)
        for vertex in ob.data.vertices:
            p = ob.matrix_world @ vertex.co
            side = 'l' if p.x < 0 else 'r'
            x = abs(p.x)
            if x < .50:
                weights = (1, 0, 0)
            elif x < .99:
                t = (x - .50) / .49
                weights = (1 - t, t, 0)
            else:
                t = min(1, (x - .99) / .43)
                weights = (0, 1 - t, t)
            for suffix, weight in zip(('', '_outer', '_tip'), weights):
                if weight > 0:
                    ob.vertex_groups['wing_' + side + suffix].add([vertex.index], weight, 'REPLACE')


def nova_flask(f):
    profile = [(z, r, r) for z, r in [(.16, .145), (.195, .216), (.29, .28),
                                    (.46, .325), (.61, .308), (.76, .246),
                                    (.87, .137), (.935, .109), (1.045, .109)]]
    loft(f, 'single_crystal_flask_body', profile, 'glass', segments=48)
    # Cage centerlines sit just outside the authored hull at every height.
    for j in range(5):
        a = j * math.tau / 5
        points = [(math.cos(a) * (r + .013), math.sin(a) * (r + .013), z)
                  for z, r in ((.20, .222), (.29, .286), (.46, .33), (.61, .313), (.76, .252))]
        f.tube('single_flask_cage_rail', points, .010, 'brass', sides=12)
    for z, r in ((.201, .227), (.761, .258), (1.038, .119)):
        f.ring('single_flask_cage_band', (0, 0, z), r, .016, 'gold_edge')
    loft(f, 'single_flask_foot', [(.145, .152, .152), (.158, .181, .181), (.18, .18, .18)],
         'brass', segments=32)
    loft(f, 'single_flask_stopple', [(1.04, .093, .093), (1.102, .10, .10),
                                    (1.126, .092, .092)], 'wood', segments=32)
    f.ellipsoid('single_trapped_nova', (0, 0, .49), (.165, .165, .185),
                'violet', detail=32, glow=True)
    f.ring('single_flask_neck_seal', (0, 0, .977), .115, .006, 'violet', glow=True)
    plate(f, 'single_flask_crest', [(-.05, -.34, .515), (0, -.345, .575),
                                   (.05, -.34, .515), (0, -.345, .448)],
          'brass', depth=.009, crown=.003)
    f.rune((0, -.356, .515), .029, 'violet')


def meteor_shard(f):
    # Asymmetric fracture sections retain the projectile's original length and axis.
    verts, faces = [], []
    sections = [(-.30, .008, -.03, .018), (-.19, .093, -.02, .006),
                (.012, .181, .022, -.004), (.20, .134, -.028, .021),
                (.375, .068, -.024, .01), (.54, .003, .021, .015)]
    for row, (z, radius, cx, cy) in enumerate(sections):
        for j in range(7):
            a = j * math.tau / 7 + .08
            r = radius * (1 + .10 * math.sin(j * 2.37 + row * .81))
            verts.append((cx + math.cos(a) * r, cy + math.sin(a) * r, z + .014 * math.sin(j * 1.7) * radius))
    for row in range(5):
        for j in range(7):
            a, b, c, d = row * 7 + j, row * 7 + (j + 1) % 7, (row + 1) * 7 + (j + 1) % 7, (row + 1) * 7 + j
            faces += [(a, b, c), (a, c, d)]
    faces += [tuple(reversed(range(7))), tuple(35 + j for j in range(7))]
    mesh = f.mesh_data('single_fractured_meteor', verts, faces, 'meteor', smooth=False)
    # Surface crack points are interpolated from real facet vertices, with tiny outward clearance.
    for facet in (3, 5):
        points = []
        for row in range(1, 5):
            a, b = Vector(verts[row * 7 + facet]), Vector(verts[row * 7 + (facet + 1) % 7])
            p = a.lerp(b, .40 + .10 * math.sin(row * 2))
            outward = Vector((p.x, p.y, 0)).normalized() * .003
            points.append(p + outward)
        f.tube('single_meteor_surface_fissure', points, [.004, .007, .005, .002],
               'ember', sides=7, glow=True)
    return mesh


def bullet_round(f):
    loft(f, 'single_brass_cartridge', [(-.10, .027, .027), (-.091, .030, .030),
                                      (-.080, .025, .025), (.016, .025, .025),
                                      (.027, .022, .022)], 'brass', segments=24)
    loft(f, 'single_copper_ogive', [(.020, .022, .022), (.061, .020, .020),
                                  (.103, .013, .013), (.141, .001, .001)],
         'copper', segments=24)
    f.ring('single_cartridge_extractor_rim', (0, 0, -.095), .030, .0034, 'steel_edge')
    f.ring('single_cartridge_seal', (0, 0, .019), .024, .0018, 'leather_edge')
    f.ellipsoid('single_cartridge_primer', (0, 0, -.104), (.009, .009, .0014), 'copper', detail=16)


def archer_tower(f):
    # Solid inner barrel closes old gaps; masonry courses remain separate, chamfered stones.
    loft(f, 'single_tower_mortar_barrel', [(.22, .759, .759), (1.82, .759, .759)],
         'stone_dark', segments=48)
    for row in range(5):
        for j in range(14):
            a = j * math.tau / 14 + (row % 2) * math.pi / 14
            f.panel('single_hewn_tower_stone', (math.cos(a) * .774, math.sin(a) * .774, .40 + row * .288),
                    (.343, .178, .265), 'limestone' if (j + row) % 4 else 'stone',
                    rotation=(0, 0, a + math.pi / 2), bevel=.020)
    loft(f, 'single_archer_tower_foundation', [(.07, .984, .984), (.125, 1.015, 1.015),
                                            (.223, 1.01, 1.01), (.29, .908, .908)],
         'stone_dark', segments=48, smooth=False)
    for z in (.29, 1.82):
        f.ring('single_tower_dressed_course', (0, 0, z), .862, .040, 'limestone')
    loft(f, 'single_archer_tower_platform', [(1.81, 1.035, 1.035), (1.922, 1.045, 1.045)],
         'wood', segments=48)
    f.ring('single_archer_platform_lip', (0, 0, 1.933), 1.045, .021, 'brass')
    for j in range(8):
        a = j * math.pi / 4
        f.panel('single_archer_merlon', (math.cos(a) * .921, math.sin(a) * .921, 2.128),
                (.286, .187, .368), 'stone_dark', rotation=(0, 0, a + math.pi / 2), bevel=.024)
        f.panel('single_archer_merlon_cap', (math.cos(a) * .921, math.sin(a) * .921, 2.319),
                (.305, .204, .035), 'limestone', rotation=(0, 0, a + math.pi / 2), bevel=.010)
    plate(f, 'single_archer_crest_plaque', [(-.15, -.867, 1.28), (.15, -.867, 1.28),
                                         (.145, -.88, 1.01), (0, -.881, .923), (-.145, -.88, 1.01)],
          'steel_shadow', depth=.023, crown=.006)
    f.rune((0, -.9, 1.105), .078, 'cyan')
    f.panel('single_archer_team_banner', (0, .877, 1.20), (.265, .024, .66), 'team', bevel=.014)


def model_design_metadata(f):
    return {'revision': '1.4.0', 'equipmentFit': 'rest-pose hand center; same-bone equipment and glove' if f.family == 'humanoid' else None,
            'detailPasses': 1, 'wingWeights': 'continuous shared span gradient' if f.family == 'flyer' else None,
            'locomotion': 'Model-space two-bone IK with planted stance, lifted recovery and flat soles' if f.family in ('humanoid','quadruped') else None,
            'hogStyle': 'Fitted crown, blue saddlecloth and shoulder plates' if f.name=='stampede' else 'Wild bristles, cloven hooves and plain team collar' if f.name=='mini_stampede' else None}


def fitted_attack_socket(f, k):
    if f.name in ('ember_archer', 'tower_archer'):
        # Keep the released arrow anchored to the nock hand and preserve the
        # existing Skeleton hierarchy when these meshes are reimported.
        point, parent = grip_center(f, 'r') + Vector((0, -.034, .016*k)), 'hand_r'
    elif f.name == 'arc_mage':
        grip = grip_center(f, 'r')
        point, parent = Vector((grip.x, grip.y - .08, 2.01 * k)), 'hand_r'
    else:
        point, parent = grip_center(f, 'r') + Vector((0, -.015, .61 * k)), 'hand_r'
    f.bones['attack_origin'] = {'head': list(point), 'tail': list(point + Vector((0, 0, .045))), 'parent': parent}
    f.socket_data('attack_origin', point, parent)
