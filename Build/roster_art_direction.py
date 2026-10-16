"""Roster-wide authored form and secondary-motion pass; no gameplay transforms."""
import math
import bpy
from mathutils import Vector


def _bend(f, name, amount, phase):
    bone = f.rig.pose.bones.get(name)
    if bone:
        bone.rotation_euler.x += amount * math.sin(phase)


def secondary_motion(f, clip, t):
    """Small owning-joint follow-through, evaluated before each baked pose key."""
    phase = t * math.tau
    walking = clip in ('Locomotion', 'Charge', 'WingCycle')
    attack = clip in ('Attack', 'DrawRelease', 'Cast', 'AlternateStrike', 'HeavyImpact', 'FrostAttack')
    envelope = math.sin(math.pi * t) if attack else 1.0
    if clip == 'Death':
        envelope = max(0., 1. - t * 1.4)
    if 'cape_tip' in f.rig.pose.bones:
        bone = f.rig.pose.bones['cape_tip']
        bone.rotation_euler = tuple(value * envelope for value in (
            (.10 if walking else .025) + .065 * math.sin(phase - .8),
            .025 * math.sin(phase - 1.1), .035 * math.sin(phase - .5)))
    if 'crest_tip' in f.rig.pose.bones:
        _bend(f, 'crest_tip', .09 if walking else .028, phase - .75)
    if 'mane_tip' in f.rig.pose.bones:
        _bend(f, 'mane_tip', .11 if walking else .025, phase - .6)
    for index, side in enumerate(('l', 'r')):
        bone = f.rig.pose.bones.get('ear_' + side)
        if bone and clip != 'Death':
            bone.rotation_euler.z += (.065 if walking else .025) * math.sin(phase + index * 2.1)
    if f.name == 'bone_warden':
        # Captives breathe and look independently inside their original cage.
        for index in range(4):
            _bend(f, 'captive_head_' + str(index), .035 * envelope, phase + index * 1.7)
    elif f.name == 'iron_giant' and clip in ('Idle', 'Locomotion'):
        # Different shoulder/forearm lag adds weight while preserving foot IK.
        _bend(f, 'forearm_l', .018, phase - .8)
        _bend(f, 'forearm_r', -.018, phase - .8)
    if f.name in ('ironclad','ember_archer','tower_archer','twin_blades','arc_mage'):
        arrow=f.rig.pose.bones.get('nocked_arrow')
        if arrow:
            # Keep transforms invertible during visibility changes. The
            # separate combat projectile begins at the existing release event.
            visibility=1. if attack and .08<t<.48 else .001
            arrow.scale=(visibility,)*3
        if clip=='Locomotion':
            arm_swing=.13 if f.name=='twin_blades' else .085 if f.name=='ironclad' else .055
            _bend(f,'upperarm_l',arm_swing,phase)
            _bend(f,'upperarm_r',-arm_swing,phase)
            _bend(f,'forearm_r',.025,phase-.5)
        elif clip=='Idle':
            _bend(f,'chest',.008 if f.name=='ironclad' else .004,phase)
            if f.name=='ember_archer':
                head=f.rig.pose.bones['head']
                head.rotation_euler.z += .020*math.sin(phase-.3)


def _rescale_part(obj, scale):
    # Change forms around their own authored centers, never rig/socket scale.
    center = sum((vertex.co for vertex in obj.data.vertices), Vector()) / len(obj.data.vertices)
    for vertex in obj.data.vertices:
        delta = vertex.co - center
        vertex.co = center + Vector(tuple(delta[axis] * scale[axis] for axis in range(3)))
    obj.data.update()


def _cloth_tip(f):
    if 'cape' not in f.bones:
        return
    cape = f.bones['cape']
    base = Vector(cape['head']).lerp(Vector(cape['tail']), .55)
    f.bone('cape_tip', base, Vector(cape['tail']), 'cape')
    for part in f.parts:
        if not any(word in part.name for word in ('tailored_back_cloak', 'cloak_hem')):
            continue
        parent = part.vertex_groups.get('cape')
        tip = part.vertex_groups.new(name='cape_tip')
        height = cape['head'][2] - cape['tail'][2]
        for vertex in part.data.vertices:
            z = (part.matrix_world @ vertex.co).z
            weight = max(0., min(1., (cape['head'][2] - z) / height))
            weight = weight * weight * (3 - 2 * weight)
            tip.add([vertex.index], weight, 'REPLACE')
            if parent:
                parent.add([vertex.index], 1 - weight, 'REPLACE')


def _humanoid(f):
    k = f.bones['head']['head'][2] / 1.6
    replaced = ('single_helmet_plume',) if f.name=='ironclad' else ('single_lip_line',) if f.name in ('ember_archer','tower_archer') else ()
    for part in list(f.parts):
        if any(word in part.name for word in replaced):
            f.parts.remove(part)
            bpy.data.objects.remove(part,do_unlink=True)
    # Enlarge the complete face/hood assembly together, keeping facial layers
    # registered. This is authored silhouette only; actor/model scales stay set
    # by the approved card data and hand/weapon sockets remain unchanged.
    pivot=Vector((0,0,1.72*k))
    for part in f.parts:
        head=part.vertex_groups.get('head')
        if not head:
            continue
        inverse=part.matrix_world.inverted()
        for vertex in part.data.vertices:
            if not any(item.group==head.index and item.weight>.999 for item in vertex.groups):
                continue
            delta=part.matrix_world@vertex.co-pivot
            vertex.co=inverse@(pivot+Vector((delta.x*1.10,delta.y*1.035,delta.z*1.025)))
        part.data.update()
    for part in f.parts:
        name = part.name
        if 'single_eye_white' in name or 'single_eye_iris' in name:
            _rescale_part(part, (1.10, 1., 1.13))
        if 'single_expressive_brow' in name:
            _rescale_part(part, (1.06, 1., 1.08))
    _cloth_tip(f)
    if f.name == 'ironclad':
        # Broad quiet armor faces with one crest; no extra intersecting plates.
        for side in (-1, 1):
            f.tube('forged_cuirass_edge_' + str(side),
                   [(side * .10*k, -.224, 1.135*k), (side*.215*k, -.204, 1.31*k),
                    (side*.19*k, -.159, 1.415*k)], [.007, .009, .005], 'steel_edge', 'chest', 10)
        f.bone('crest_tip', (0, .058, 2.13*k), (0, .16, 2.28*k), 'head')
        f.tube('sculpted_crest_plume', [(0,.058,2.13*k),(0,.080,2.19*k),
               (0,.21,2.22*k),(0,.29,2.14*k)],
               [(.018,.027),(.043,.042),(.035,.026),(.001,.001)], 'cloth_ember', 'crest_tip', 16)
    elif f.name in ('ember_archer', 'tower_archer'):
        for part in list(f.parts):
            if part.vertex_groups.get('hand_r') and any(word in part.name for word in ('relaxed_glove_digit','relaxed_thumb')):
                f.parts.remove(part)
                bpy.data.objects.remove(part,do_unlink=True)
        right=f.bones['hand_r']
        grip=Vector(((right['head'][0]+right['tail'][0])*.5,-.162,
                     (right['head'][2]+right['tail'][2])*.5))
        f.tube('draw_index_finger',[grip+Vector((-.026,.027,.025)),
               grip+Vector((-.028,-.020,.012)),grip+Vector((-.012,-.035,.002))],
               [.014,.014,.009],'leather_edge','hand_r',10)
        f.tube('draw_pinching_thumb',[grip+Vector((.043,.035,.020)),
               grip+Vector((.031,-.023,.013)),grip+Vector((.010,-.035,.001))],
               [.020,.017,.010],'leather','hand_r',12)
        for digit in range(3):
            dx=(digit-1)*.024
            f.tube('draw_folded_finger',[grip+Vector((dx,.018,-.012)),
                   grip+Vector((dx,-.018,-.044)),grip+Vector((dx,-.008,-.058))],
                   [.013,.014,.008],'leather','hand_r',10)
        string=next(part for part in f.parts if 'single_bow_string' in part.name)
        nock=sum((string.matrix_world@vertex.co for vertex in list(string.data.vertices)[8:16]),Vector())/8
        f.bone('bow_nock',nock,nock+Vector((0,.05,0)),'hand_l')
        f.bone('nocked_arrow',nock,nock+Vector((0,-.05,0)),'bow_nock')
        left=string.vertex_groups['hand_l']
        middle=string.vertex_groups.new(name='bow_nock')
        # Three tube sections: endpoints remain attached to bow limbs while
        # the center follows the draw hand. No whole-string translation.
        for vertex in string.data.vertices:
            weight=1. if 8<=vertex.index<16 else 0.
            left.add([vertex.index],1.-weight,'REPLACE')
            middle.add([vertex.index],weight,'REPLACE')
        f.tube('drawn_arrow_shaft',[nock,nock+Vector((0,-.55,0))],.0065,
               'wood','nocked_arrow',10)
        f.tube('drawn_arrow_broadhead',[nock+Vector((0,-.53,0)),nock+Vector((0,-.59,0))],
               [.018,.001],'silver','nocked_arrow',4)
        f.bone('crest_tip', (0,.13,1.88*k), (0,.24,1.67*k), 'head')
        f.tube('swept_auburn_ponytail', [(0,.13,1.89*k),(.025,.215,1.83*k),
               (.035,.24,1.73*k),(.012,.27,1.65*k)],
               [.047,.055,.039,.002], 'hair_auburn', 'crest_tip', 20)
        f.tube('confident_smile', [(-.039*k,-.145,1.65*k),(0,-.155,1.647*k),
               (.044*k,-.143,1.66*k)], [.003,.004,.002], 'skin_shade', 'head', 10)
    elif f.name == 'twin_blades':
        # One asymmetrical stitched mask makes the face readable at battle scale.
        f.tube('mask_cheek_seam', [(-.105*k,-.151,1.68*k),(-.075*k,-.179,1.65*k),
               (-.028*k,-.175,1.62*k)], .0035, 'cloth_purple_light', 'head', 8)
        for side in (-1, 1):
            f.tube('fitted_boot_edge', [(side*.17*k-.060, -.13,.10*k),
                   (side*.17*k,-.17,.10*k),(side*.17*k+.060,-.13,.10*k)],
                   .007, 'leather_edge', 'foot_'+('l' if side<0 else 'r'), 10)
    elif f.name == 'arc_mage':
        for side in (-1, 1):
            f.tube('robe_embroidered_lightning', [(side*.12*k,-.171,.64*k),
                   (side*.15*k,-.163,.57*k),(side*.11*k,-.181,.52*k),
                   (side*.14*k,-.185,.45*k)], .004, 'cloth_blue_light', 'pelvis', 8)


def _beasts(f):
    if f.name in ('mini_stampede', 'stampede'):
        # Mini herd remains bare/scrappy; royal herd gets a coherent saddle cap.
        if f.name == 'mini_stampede':
            for side in (-1, 1):
                f.tube('scrappy_cheek_whisker', [(side*.15,-.49,.36),(side*.18,-.52,.365),
                       (side*.19,-.53,.37)], .0022, 'fur_cream', 'head', 6)
        else:
            for part in f.parts:
                if 'pupil_' in part.name:
                    _rescale_part(part,(1.,1.14,1.12))
        return
    if f.name == 'boulderback':
        for side in (-1, 1):
            f.tube('carved_stone_cheek', [(side*.31,-1.05,1.17),(side*.37,-.98,1.13),
                   (side*.40,-.90,1.08)], [.009,.014,.005], 'stone_dark', 'head', 8)
        return
    if f.name == 'frost_fang':
        # Layered mane follows neck, tips follow a small lag joint.
        f.bone('mane_tip', (0,-.51,1.27), (0,-.34,1.38), 'neck')
        for side in (-1, 1):
            for j in range(5):
                f.tube('sculpted_frost_mane', [(side*(.22+j*.025),-.59+j*.055,1.31-j*.025),
                       (side*(.30+j*.024),-.52+j*.055,1.36-j*.025),
                       (side*(.35+j*.022),-.40+j*.055,1.27-j*.025)],
                       [.032,.037,.001], 'ice_fur', 'mane_tip', 10)
        return
    if f.name == 'rambeast':
        f.bone('mane_tip', (0,-.45,1.33), (0,-.22,1.40), 'neck')
        for j in range(7):
            x=(j-3)*.065
            f.tube('curled_ram_forelock', [(x,-.59,1.40),(x*.95,-.63,1.46),
                   (x*.86,-.62,1.50),(x*.80,-.58,1.49)],
                   [.026,.032,.024,.003], 'fur_cream', 'head', 12)


def _flyers(f):
    if f.name == 'sky_manta':
        # Three gill cuts per side under a single continuous organic body.
        for side in (-1, 1):
            for j in range(3):
                f.tube('manta_gill_slit', [(side*(.18+j*.025),-.37+j*.065,.987),
                       (side*(.22+j*.025),-.34+j*.065,.992)], .004, 'teal', 'body', 8)
    elif f.name == 'vampire_bats':
        for side in (-1, 1):
            f.tube('sculpted_bat_whisker_fold', [(side*.040,-.684,1.207),
                   (side*.074,-.666,1.185),(side*.087,-.634,1.19)],
                   [.008,.011,.003], 'membrane', 'jaw', 10)
    elif f.name == 'storm_raven':
        for side in (-1, 1):
            for j in range(3):
                f.tube('swept_storm_crown_feather', [(side*.046,-.44+j*.025,1.38),
                       (side*.064,-.40+j*.03,1.47+j*.012),
                       (side*.046,-.32+j*.03,1.46+j*.012)],
                       [.015,.021,.001], 'raven_blue', 'head', 8)


def _expansion(f):
    if f.name == 'aether_reaper':
        # Silver cowl stitching stays on the existing head joint; no face/shaft edits.
        for side in (-1,1):
            f.tube('cowl_fitted_seam', [(side*.11,.066,1.69),(side*.14,.106,1.58),
                   (side*.12,.122,1.47)], .004, 'cloth_purple', 'head', 10)
    elif f.name == 'bone_warden':
        for side in (-1,1):
            f.tube('sculpted_bone_temple_ridge', [(side*.15,-.055,1.94),
                   (side*.17,-.10,1.88),(side*.15,-.13,1.84)],
                   [.012,.016,.004], 'ivory', 'head', 10)
    elif f.name == 'bone_warden_skeleton':
        for side in (-1,1):
            f.tube('small_scapula_ridge', [(side*.07,.042,.62),
                   (side*.11,.054,.66),(side*.13,.038,.62)],
                   [.008,.012,.005], 'ivory', 'chest', 10)
    elif f.name == 'iron_giant':
        # A coherent rear exhaust cap and broad shoulder relief replace tiny clutter.
        for side in (-1,1):
            f.tube('arched_rear_exhaust', [(side*.13,.254,.91),(side*.13,.292,1.01),
                   (side*.13,.314,1.075)], [.028,.030,.023], 'steel_shadow', 'chest', 16)
            f.ring('rolled_exhaust_lip', (side*.13,.314,1.075), .024,.005,'copper','chest')
    elif f.name == 'pulverizer':
        for j in range(4):
            a=j*math.pi/2
            # Four fitted luminous inserts announce the compression chamber.
            f.tube('crystal_chamber_inset', [(.43*math.cos(a),.43*math.sin(a),.335),
                   (.48*math.cos(a),.48*math.sin(a),.335)], .006, 'violet', 'root', 8, True)


def polish_geometry(f, skinned):
    if skinned:
        if f.name in ('ironclad','ember_archer','tower_archer','twin_blades','arc_mage'):
            _humanoid(f)
        elif f.family == 'quadruped':
            _beasts(f)
        elif f.family == 'flyer':
            _flyers(f)
        else:
            _expansion(f)
    elif f.name == 'nova_flask':
        f.ring('wax_seal_neck', (0,0,1.065), .102,.007,'cloth_purple')
    elif f.name == 'bullet_round':
        for z in (-.075,-.065):
            f.ring('machined_cartridge_channel', (0,0,z), .025,.001,'steel_shadow')
    elif f.name == 'bridge':
        for side in (-1,1):
            for y in (-1.8,1.8):
                f.tube('carved_bridge_brace', [(side*1.70,y,-.32),
                       (side*1.85,y-.22 if y>0 else y+.22,.12)], [.065,.052], 'brass', sides=12)
            f.tube('bridge_rail_light_edge', [(side*2.02,-2.05,.865),
                   (side*2.02,2.05,.865)], .015,'brass',sides=10)
    elif f.name in ('tower_core','tower_guard'):
        size=1.14 if f.name=='tower_core' else 1.
        for side in (-1,1):
            f.tube('front_archivolt', [(side*.28,-1.015*size,.93),
                   (side*.28,-1.015*size,1.45),(side*.14,-1.015*size,1.60),
                   (0,-1.015*size,1.64)], .026,'brass',sides=12)
        for y in (-.72,.72):
            for x in (-.72,.72):
                f.panel('seated_corner_cap_relief', (x*size,y*size,.344),
                        (.28,.28,.025),'stone',bevel=.025)
    elif f.name == 'archer_tower':
        for j in range(8):
            a=j*math.pi/4
            f.tube('archer_merlon_inlaid_edge', [(math.cos(a)*1.025,math.sin(a)*1.025,2.025),
                   (math.cos(a)*1.025,math.sin(a)*1.025,2.22)], .007,'brass',sides=10)
    elif f.name == 'banner':
        points=[(j/16*.91,.045*math.sin(j/16*math.pi*2.2+.3),1.58) for j in range(17)]
        f.tube('tailored_banner_hem',points,.008,'brass',sides=8)
    elif f.name == 'shadow_surge_wisp':
        f.tube('wispy_shadow_tendril', [(.025,.02,.06),(-.015,.065,.12),
               (.015,.09,.19),(.065,.085,.22)], [.015,.017,.010,.001],
               'violet',sides=10,glow=True)
    # Real chamfered edges catch the key light. Preserve anatomical smooth
    # surfaces and the semantic vertex groups used by grip/clearance validation.
    for part in f.parts:
        if any(word in part.name for word in ('cuirass','helmet','carapace_stone','tasset','masonry')):
            bevel=part.modifiers.new('Forged silhouette bevel','BEVEL')
            bevel.width=.004 if skinned else .006
            bevel.segments=2
            bevel.limit_method='ANGLE'
            bevel.angle_limit=.5
            bpy.context.view_layer.objects.active=part
            bpy.ops.object.modifier_apply(modifier=bevel.name)
    if hasattr(f,'expansion_design'):
        f.expansion_design['rosterArtRevision']='crafted-roster-2'
