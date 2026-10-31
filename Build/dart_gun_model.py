"""Dart Gun: an elderly veteran, with fitted hands and a self-contained firing rig."""
import math
import bpy
from mathutils import Vector
from upgrade_card_models import loft

CHARACTERS = {'dart_gun'}
STATICS = {'dart_projectile'}
PALETTE = {
    'skin': (.265, .125, .073, 0, .76),
    'skin_warm': (.34, .18, .105, 0, .78),
    'skin_shade': (.205, .092, .052, 0, .82),
    'cloth_blue': (.16, .195, .085, 0, .91),
    'cloth_blue_light': (.255, .285, .14, 0, .9),
    'cloth_indigo': (.09, .12, .065, 0, .92),
    'hair_silver': (.43, .46, .42, 0, .88),
    'leather': (.12, .065, .037, 0, .84),
    'leather_edge': (.27, .16, .085, 0, .8),
    'steel': (.24, .29, .28, .68, .36),
    'steel_shadow': (.065, .085, .08, .65, .43),
}


def build(Forge, humanoid_rig):
    f = Forge('dart_gun')
    humanoid_rig(f, 1.83)
    # The rifle-ready rest pose is the rig's actual bind pose. Both hands are
    # built around their own grip, rather than placed over a complete weapon.
    for side, shoulder, elbow, wrist, fingers in (
        ('r', (.265, 0, 1.34), (.405, -.03, 1.12), (.235, -.29, 1.235), (.235, -.345, 1.20)),
        ('l', (-.265, 0, 1.34), (-.36, -.18, 1.12), (.19, -.565, 1.245), (.245, -.60, 1.255))):
        f.bone('upperarm_'+side, shoulder, elbow, 'chest')
        f.bone('forearm_'+side, elbow, wrist, 'upperarm_'+side)
        f.bone('hand_'+side, wrist, fingers, 'forearm_'+side)
    f.bone('coat_hem', (0, .10, .94), (0, .10, .75), 'pelvis')
    f.socket('attack_origin', (.235, -.945, 1.315), 'hand_r')
    f.socket('hp_anchor', (0, -.025, 2.10), 'head')
    # Silhouette: narrow aged hips, relaxed sloping shoulders, roomy field coat.
    coat=loft(f, 'tailored_field_jacket', [(.74,.285,.18),(.79,.285,.18),(.86,.27,.17),(.92,.24,.16),
        (.99,.25,.165),(1.09,.263,.17),(1.17,.275,.17),(1.33,.285,.15),
        (1.41,.235,.13),(1.45,.135,.105)],'cloth_blue','chest',40)
    chest=coat.vertex_groups['chest'];pelvis=coat.vertex_groups.new(name='pelvis')
    for v in coat.data.vertices:
        weight=max(0.,min(1.,(v.co.z-.92)/.21));weight=weight*weight*(3-2*weight)
        chest.add([v.index],weight,'REPLACE');pelvis.add([v.index],1-weight,'REPLACE')
    f.panel('jacket_front_placket', (0,-.173,1.09), (.035,.017,.51), 'cloth_blue_light', 'chest', bevel=.005)
    for z in (.88,1.,1.12,1.24,1.34): f.rivet((.006,-.190,z),'chest','brass',.012)
    f.tube('jacket_hem_stitch',[(-.23,-.07,.77),(-.19,-.14,.77),(0,-.174,.77),(.19,-.14,.77),(.23,-.07,.77)],.004,'cloth_blue_light','coat_hem',8)
    for side, sign in (('l',-1),('r',1)):
        upper=f.bones['upperarm_'+side]; fore=f.bones['forearm_'+side]
        a,b,c=Vector(upper['head']),Vector(upper['tail']),Vector(fore['tail'])
        f.ellipsoid('rounded_shoulder_joint_'+side,a,(.132,.12,.14),'cloth_blue','upperarm_'+side,28)
        f.tube('sloping_sleeve_'+side,[a,a.lerp(b,.3),a.lerp(b,.75),b],[.125,.125,.093,.087],'cloth_blue','upperarm_'+side,24)
        f.ellipsoid('elbow_fold_'+side,b,(.09,.088,.085),'cloth_blue','forearm_'+side,20)
        f.tube('forearm_sleeve_'+side,[b,b.lerp(c,.45),b.lerp(c,.86)],[.088,.083,.062],'cloth_blue','forearm_'+side,24)
        f.tube('cuff_'+side,[b.lerp(c,.84),b.lerp(c,.92)],[.068,.065],'cloth_blue_light','forearm_'+side,20)
        # Pockets, reinforced edges, epaulettes and tiny repeated stitching.
        f.panel('breast_pocket_'+side,(sign*.15,-.171,1.235),(.142,.027,.13),'cloth_blue_light','chest',bevel=.012)
        f.panel('pocket_flap_'+side,(sign*.15,-.193,1.288),(.153,.014,.039),'cloth_blue','chest',bevel=.005)
        f.rivet((sign*.15,-.205,1.282),'chest','brass',.009)
        f.panel('cargo_pocket_'+side,(sign*.16,-.176,.85),(.145,.035,.12),'cloth_blue','coat_hem',bevel=.012)
        f.tube('collar_fold_'+side,[(sign*.10,-.086,1.45),(sign*.16,-.145,1.385),(sign*.045,-.185,1.31)],
               [(.036,.024),(.042,.025),(.014,.01)],'cloth_blue_light','chest',12)
        f.panel('shoulder_epaulette_'+side,(sign*.24,-.015,1.415),(.11,.08,.024),'cloth_blue_light','chest',bevel=.005)
        for i in range(7):
            f.tube('pocket_stitch_'+side,[(sign*.15-.06+i*.018,-.192,1.18),(sign*.15-.053+i*.018,-.192,1.18)],.0025,'horn','chest',6)
        # Legs remain separate all the way to the crotch; boots have soles/toes.
        th=f.bones['thigh_'+side]; sh=f.bones['shin_'+side]
        f.tube('trouser_thigh_'+side,[th['head'],Vector(th['head']).lerp(Vector(th['tail']),.5),th['tail']],[.108,.099,.077],'cloth_indigo','thigh_'+side,24)
        f.ellipsoid('covered_knee_'+side,sh['head'],(.081,.079,.081),'cloth_indigo','shin_'+side,24)
        f.tube('trouser_calf_'+side,[sh['head'],Vector(sh['head']).lerp(Vector(sh['tail']),.58),sh['tail']],[.078,.069,.056],'cloth_indigo','shin_'+side,24)
        x=sign*.159
        f.ellipsoid('boot_shaped_toe_'+side,(x,-.065,.105),(.083,.163,.095),'leather','foot_'+side,28)
        f.panel('boot_sole_'+side,(x,-.061,.037),(.169,.323,.036),'black','foot_'+side,bevel=.025)
        f.tube('boot_gaiter_'+side,[(x,0,.12),(x,0,.25),(x,0,.31)],[.078,.079,.072],'leather_edge','shin_'+side,24)
        for z in (.17,.205,.24,.275):
            f.tube('boot_lace_'+side,[(x-.04,-.074,z),(x+.04,-.074,z+.017)],.004,'horn','shin_'+side,6)
        for i in range(4): f.panel('boot_tread_'+side,(x,-.17+i*.071,.014),(.148,.025,.014),'black','foot_'+side,bevel=.003)
    # Belt, buckle and side ammunition case sit clear of the hands and rifle.
    f.tube('fitted_belt',[(.25*math.cos(i*math.tau/48),.176*math.sin(i*math.tau/48),.94) for i in range(49)],.017,'leather','pelvis',12)
    f.panel('belt_buckle',(0,-.175,.94),(.067,.025,.049),'brass','pelvis',bevel=.006)
    f.panel('dart_pouch',(-.265,.055,1.0),(.09,.14,.19),'leather','pelvis',bevel=.018)
    f.panel('pouch_flap',(-.313,.052,1.065),(.018,.135,.05),'leather_edge','pelvis',bevel=.006)
    # Service ribbons and a single medal: clean visual hierarchy, no oversized insignia.
    for i,key in enumerate(('cloth_ember','ivory','cloth_blue_light')):
        f.panel('service_ribbon',( -.15+(i-1)*.037,-.202,1.345),(.032,.012,.021),key,'chest',bevel=.002)
    f.tube('medal_ribbon',[(-.15,-.209,1.26),(-.15,-.213,1.22)],.012,'cloth_ember','chest',8)
    f.ellipsoid('bronze_service_medal',(-.15,-.219,1.204),(.018,.006,.021),'brass','chest',20)
    # Aged face: a shaped continuous head surface, sagging jowls and restrained details.
    f.tube('neck',[(0,-.008,1.42),(0,-.035,1.56)],[.079,.081],'skin','neck',28)
    loft(f,'aged_head',[(1.49,.072,.083,0,-.035),(1.53,.132,.122,0,-.035),
        (1.61,.17,.143,0,-.03),(1.70,.18,.152,0,-.022),(1.80,.175,.146,0,-.012),
        (1.88,.145,.124,0,0),(1.925,.081,.080,0,0),(1.937,.015,.018,0,0)],'skin','head',48)
    for sign in (-1,1):
        f.ellipsoid('ear',(sign*.18,-.015,1.71),(.039,.027,.071),'skin_warm','head',24)
        f.ellipsoid('ear_inner',(sign*.199,-.036,1.714),(.018,.009,.040),'skin_shade','head',20)
        f.ellipsoid('cheek_plane',(sign*.105,-.122,1.66),(.052,.012,.041),'skin_warm','head',28)
        f.ellipsoid('jowl',(sign*.094,-.111,1.565),(.048,.014,.031),'skin','head',24)
        f.ellipsoid('eye_socket',(sign*.075,-.164,1.747),(.055,.014,.028),'skin_shade','head',24)
        f.ellipsoid('eye',(sign*.075,-.177,1.752),(.037,.009,.018),'ivory','head',24)
        f.ellipsoid('iris',(sign*.075,-.186,1.751),(.013,.004,.014),'fur_dark','head',20)
        f.ellipsoid('pupil',(sign*.075,-.189,1.752),(.006,.002,.009),'black','head',16)
        f.ellipsoid('eye_catchlight',(sign*.075-.004,-.192,1.758),(.003,.002,.003),'ivory','head',12)
        f.tube('drooping_upper_lid',[(sign*.115,-.174,1.755),(sign*.075,-.187,1.766),(sign*.037,-.173,1.757)],.007,'skin','head',10)
        f.tube('lower_eye_bag',[(sign*.113,-.165,1.727),(sign*.078,-.18,1.718),(sign*.041,-.167,1.727)],.0045,'skin_warm','head',10)
        f.tube('gray_brow',[(sign*.119,-.16,1.789),(sign*.075,-.172,1.799),(sign*.035,-.161,1.787)],[.012,.016,.008],'hair_silver','head',12)
        for z in (1.724,1.735):
            f.tube('crow_foot',[(sign*.12,-.156,z),(sign*.149,-.128,z-.008)],.0027,'skin_shade','head',6)
        f.tube('nasolabial_fold',[(sign*.051,-.18,1.673),(sign*.070,-.168,1.631),(sign*.067,-.157,1.603)],.0034,'skin_shade','head',8)
        f.tube('mustache_half',[(sign*.009,-.202,1.651),(sign*.038,-.202,1.647),(sign*.064,-.185,1.635)],
               [.012,.021,.012],'hair_silver','head',16)
        for i in range(6):
            x=sign*(.013+i*.008)
            f.tube('mustache_strand',[(x,-.218+abs(x)*.19,1.662),(x+sign*.004,-.215+abs(x)*.22,1.638)],.002,'ivory','head',6)
    f.ellipsoid('nose_bridge',(0,-.165,1.735),(.033,.035,.071),'skin_warm','head',28)
    f.ellipsoid('nose_tip',(0,-.202,1.693),(.041,.042,.031),'skin_warm','head',28)
    for sign in (-1,1):
        f.ellipsoid('nostril_wing',(sign*.033,-.187,1.686),(.023,.023,.02),'skin','head',24)
        f.ellipsoid('nostril',(sign*.027,-.209,1.675),(.010,.006,.006),'skin_shade','head',16)
    f.tube('mouth_line',[(-.053,-.175,1.617),(0,-.185,1.613),(.053,-.175,1.617)],.0035,'skin_shade','head',10)
    f.ellipsoid('lower_lip',(0,-.175,1.603),(.038,.013,.010),'skin_warm','head',24)
    f.ellipsoid('chin',(0,-.137,1.555),(.073,.037,.027),'skin_warm','head',24)
    for row in range(3):
        z=1.823+row*.018;u=(z-1.80)/.08;rx=.175*(1-u)+.145*u;ry=.146*(1-u)+.124*u;cy=-.012*(1-u)
        points=[(x,cy-ry*math.sqrt(1-(x/rx)**2)-.0006,z+.001*math.cos(x*20)) for x in (-.09,-.06,-.03,0,.03,.06,.09)]
        f.tube('fine_forehead_crease',points,.0009,'skin_shade','head',6)
    # Low, close-cut silver side hair follows the scalp instead of floating tufts.
    for sign in (-1,1):
        f.ellipsoid('silver_side_hair',(sign*.153,.024,1.807),(.029,.084,.072),'hair_silver','head',32)
        for j in range(9):
            y=-.032+j*.014
            f.tube('silver_hair_grain',[(sign*.174,y,1.78),(sign*.178,y+.003,1.815),
                (sign*.164,y+.008,1.85)],[.002,.003,.001],'ivory','head',6)
        f.tube('silver_sideburn',[(sign*.169,-.022,1.80),(sign*.172,-.025,1.75)],[.012,.006],'hair_silver','head',12)
    # Blend the large facial volumes into one sculpted skin surface. Eyes,
    # lips and fine creases retain separate purposeful material boundaries.
    sculpt_names=('aged_head','cheek_plane','jowl','nose_bridge','nose_tip','nostril_wing','chin')
    sculpt=[o for o in f.parts if any(n in o.name for n in sculpt_names) or ('_ear' in o.name and '_ear_inner' not in o.name)]
    f.parts=[o for o in f.parts if o not in sculpt]
    bpy.ops.object.select_all(action='DESELECT')
    for o in sculpt:o.select_set(True)
    bpy.context.view_layer.objects.active=sculpt[0]
    bpy.ops.object.join();skin=bpy.context.object;skin.name='dart_gun_sculpted_aged_head'
    remesh=skin.modifiers.new('Unified aged facial planes','REMESH');remesh.mode='VOXEL';remesh.voxel_size=.0045
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth=skin.modifiers.new('Soft tissue transitions','SMOOTH');smooth.factor=.48;smooth.iterations=5
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    for group in list(skin.vertex_groups):skin.vertex_groups.remove(group)
    skin.vertex_groups.new(name='head').add(list(range(len(skin.data.vertices))),1,'REPLACE')
    skin.data.materials.clear();skin.data.materials.append(f.materials[0])
    # Remesh destroys UVs; planar palette UV remains within the dark-skin tile.
    uv=skin.data.uv_layers.active or skin.data.uv_layers.new(name='UVMap')
    # skin is palette slot 10 (the existing fixed 8x8 atlas contract).
    for loop in uv.data:loop.uv=(2.5/8,1.5/8)
    for poly in skin.data.polygons:poly.use_smooth=True
    f.parts.append(skin)
    # Rifle: gunmetal receiver, wood stock, gas cylinder, detachable dart cassette.
    hand='hand_r'; x=.235
    f.panel('wooden_rifle_stock',(x,-.19,1.285),(.073,.17,.105),'wood',hand,bevel=.018)
    f.panel('stock_butt_plate',(x,-.098,1.285),(.084,.018,.12),'steel_shadow',hand,bevel=.009)
    f.panel('receiver',(x,-.335,1.315),(.09,.27,.086),'steel',hand,bevel=.015)
    f.tube('barrel',[(x,-.45,1.315),(x,-.90,1.315)],.026,'steel_shadow',hand,24)
    for y in (-.49,-.69,-.88):f.ring('barrel_collar',(x,y,1.315),.032,.008,'steel',hand,'Y')
    f.tube('muzzle',[(x,-.89,1.315),(x,-.945,1.315)],.034,'steel',hand,24)
    f.ellipsoid('muzzle_bore',(x,-.947,1.315),(.021,.003,.021),'black',hand,20)
    f.panel('barrel_handguard',(x,-.565,1.275),(.083,.19,.065),'wood',hand,bevel=.014)
    f.panel('pistol_grip',(x,-.299,1.20),(.043,.055,.13),'wood',hand,rotation=(-.12,0,0),bevel=.009)
    f.tube('trigger_guard',[(x,-.34,1.28),(x,-.39,1.25),(x,-.38,1.19),(x,-.325,1.19)],.007,'steel_shadow',hand,10)
    f.tube('trigger',[(x,-.344,1.27),(x,-.355,1.238)],.005,'steel',hand,8)
    f.panel('dart_cassette',(x,-.435,1.235),(.055,.07,.10),'steel_shadow',hand,bevel=.008)
    f.panel('cassette_brass_latch',(x+.031,-.435,1.245),(.007,.036,.021),'brass',hand,bevel=.003)
    f.tube('bolt_handle',[(x+.042,-.28,1.34),(x+.085,-.28,1.34)],.009,'steel',hand,12)
    f.ellipsoid('bolt_knob',(x+.089,-.28,1.34),(.018,.014,.014),'black',hand,16)
    for y in (-.31,-.78):f.panel('iron_sight',(x,y,1.356),(.015,.018,.035),'steel_shadow',hand,bevel=.003)
    for y in (-.25,-.38): f.rivet((x+.047,y,1.32),hand,'silver',.005)
    # Anatomical grips: palms beside the grip, four fingers curled around it.
    f.ellipsoid('right_palm',(x+.036,-.30,1.22),(.027,.046,.047),'skin','hand_r',24)
    for i in range(4):
        z=1.245-i*.020
        f.tube('right_curled_finger',[(x+.052,-.302,z),(x+.035,-.337,z),(x-.012,-.332,z),(x-.024,-.31,z)],
               [.010,.011,.010,.007],'skin_warm','hand_r',10)
        f.ellipsoid('right_knuckle',(x+.045,-.319,z),(.012,.012,.011),'skin_warm','hand_r',16)
    f.tube('right_thumb',[(x+.044,-.28,1.25),(x+.029,-.259,1.269),(x-.009,-.27,1.264)],.013,'skin','hand_r',12)
    f.ellipsoid('left_support_palm',(.222,-.565,1.224),(.049,.038,.024),'skin','hand_l',24)
    for i in range(4):
        y=-.603+i*.023
        f.tube('left_support_finger',[(.192,y,1.222),(.269,y,1.23),(.282,y,1.263),(.265,y,1.284)],
               [.010,.011,.010,.007],'skin_warm','hand_l',10)
    f.tube('left_thumb',[(.186,-.54,1.23),(.18,-.54,1.267),(.20,-.56,1.286)],.012,'skin','hand_l',12)
    f.expansion_design={'identity':'81-year-old Black WWII veteran; receding silver hair, mustache and aged facial planes',
        'surface':'Dedicated olive cloth, dark skin, weathered leather and gunmetal PBR atlas',
        'equipment':'Two fitted anatomical grips; mechanical dart rifle with cassette, sights and bore',
        'gripPolicy':'Rifle fixed to right hand; left support grip follows evaluated rifle during recoil',
        'animationPolicy':'Short planted recoil, foot-contact slow gait, breathing, deploy, turn and death'}
    return f.finish(True)


def projectile(Forge):
    f=Forge('dart_projectile')
    f.tube('steel_dart_shaft',[(0,.13,0),(0,-.20,0)],.009,'steel',sides=12)
    f.tube('needle_tip',[(0,-.19,0),(0,-.29,0)],[.018,.0005],'silver',sides=12)
    f.ring('brass_ferrule',(0,.08,0),.013,.004,'brass',axis='Y')
    for a in (0,math.tau/3,2*math.tau/3):
        x,z=math.cos(a),math.sin(a)
        f.mesh_data('olive_stabilizer',[(0,.01,0),(x*.042,.10,z*.042),(x*.042,.16,z*.042),(0,.13,0)],[(0,1,2,3)],'cloth_blue_light',smooth=False)
    return f.finish()


def actions(f,scene,set_world_rotation,aim_leg,aim_arm):
    clips={'Idle':121,'Locomotion':73,'Attack':25,'Hit':19,'Death':73,'Deploy':49,'Status':61,'Turn':37,'Acquire':25}
    made=[]
    for name,length in clips.items():
        act=bpy.data.actions.new('AN_dart_gun_'+name);f.rig.animation_data_create();f.rig.animation_data.action=act
        for pb in f.rig.pose.bones:pb.rotation_mode='XYZ'
        for frame in range(1,length+1):
            scene.frame_set(frame);t=(frame-1)/(length-1);phase=t*math.tau
            for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
            def rot(b,x=0,y=0,z=0):f.rig.pose.bones[b].rotation_euler=(x,y,z)
            def move(b,x=0,y=0,z=0):
                pb=f.rig.pose.bones[b];pb.location=pb.bone.matrix_local.to_3x3().inverted()@Vector((x,y,z))
            # A mild forward stoop and calm breathing distinguish his age.
            rot('spine',.055+.008*math.sin(phase));rot('neck',-.035);rot('head',-.025,0,.012*math.sin(phase*.5))
            if name=='Locomotion':
                move('pelvis',z=-.04+.005*(1-math.cos(phase*2)))
                rot('chest',0,.012*math.sin(phase),.015*math.sin(phase))
                rot('coat_hem',.004*math.sin(phase-.6))
                for side,offset in (('l',0.),('r',.5)):
                    u=(t+offset)%1;stance=.68;stride=.54;half=stride*stance*.5
                    if u<stance:y=-half+stride*u;z=0
                    else:v=(u-stance)/(1-stance);y=half-2*half*v*v*(3-2*v);z=.065*math.sin(math.pi*v)
                    target=f.rig.pose.bones['shin_'+side].bone.tail_local+Vector((0,y,z))
                    aim_leg(f,'thigh_'+side,'shin_'+side,'foot_'+side,target,Vector((0,-1,0)))
            if name=='Attack':
                recoil=math.exp(-max(0,t-.48)*11)*math.sin(min(1,max(0,t-.48)/.10)*math.pi*.5) if t>=.48 else 0
                rot('chest',-.025*recoil);rot('head',-.025+.014*recoil)
                move('hand_r',y=.024*recoil,z=.008*recoil)
                # Follow the gun support point without stretching a left hand
                # through the barrel as independent arm rotations would do.
                bpy.context.view_layer.update()
                right=f.rig.pose.bones['hand_r'];transform=right.matrix@right.bone.matrix_local.inverted()
                aim_arm(f,'l',transform@Vector(f.bones['hand_l']['head']),1.)
            if name=='Hit':rot('chest',-.10*math.sin(math.pi*t))
            if name=='Turn':rot('chest',0,0,.14*math.sin(math.pi*t));rot('head',0,0,.20*math.sin(math.pi*t))
            if name=='Acquire':rot('head',-.025,0,.07*math.sin(math.pi*t))
            if name=='Deploy':move('pelvis',z=.04*(1-t));rot('head',.12*(1-t))
            if name=='Death':
                e=min(1,t/.8);e=e*e*(3-2*e)
                rot('pelvis',.17*e,.62*e,.25*e);move('pelvis',z=-.52*e)
                rot('thigh_l',-.65*e);rot('thigh_r',-.4*e);rot('shin_l',.95*e);rot('shin_r',.75*e);rot('head',.20*e)
            for pb in f.rig.pose.bones:
                for prop in ('rotation_euler','location','scale'):pb.keyframe_insert(prop,frame=frame,group=pb.name)
        curves=[c for layer in act.layers for strip in layer.strips for bag in strip.channelbags for c in bag.fcurves]
        for c in curves:
            for p in c.keyframe_points:p.interpolation='LINEAR'
        act.use_fake_user=True
        made.append({'name':name,'action':act.name,'frames':length,'fps':60,'seconds':(length-1)/60,
            'releaseNormalized':.48 if name=='Attack' else None,'loop':name in ('Idle','Locomotion','Status'),
            'strideMeters':.54 if name=='Locomotion' else None,'object':act})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    return made
