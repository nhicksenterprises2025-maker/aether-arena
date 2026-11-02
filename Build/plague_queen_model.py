"""Plague Queen: a fitted slime archer, independent atlas and authored bow rig."""
import math
import bpy
from mathutils import Vector
from upgrade_card_models import loft

CHARACTERS = {'plague_queen'}
STATICS = {'plague_arrow', 'toxic_bubble', 'poison_splash'}
PALETTE = {
    'skin': (.075, .40, .16, .0, .24),
    'skin_warm': (.14, .53, .22, .0, .23),
    'skin_shade': (.025, .16, .08, .0, .32),
    'hair_auburn': (.028, .19, .095, .0, .21),
    'cloth_blue': (.018, .12, .075, .08, .37),
    'cloth_blue_light': (.035, .245, .13, .05, .30),
    'cloth_indigo': (.024, .07, .045, .0, .43),
    'cloth_purple': (.16, .045, .18, .0, .46),
    'cloth_purple_light': (.26, .08, .28, .0, .39),
    'brass': (.60, .37, .075, .72, .25),
    'gold_edge': (.90, .68, .22, .73, .23),
    'cyan': (.42, .86, .055, .0, .20),
    'violet': (.28, .58, .025, .0, .22),
    'glass': (.20, .60, .055, .0, .13),
    'wood': (.055, .12, .065, .03, .32),
    'leather': (.06, .085, .038, .0, .55),
    'ivory': (.85, .94, .69, .0, .24),
}
RELEASE = .42


def _gem(f, name, center, size, bone='head'):
    x,y,z=center;rx,ry,rz=size
    vertices=[(x-rx,y,z),(x,y,z+rz),(x+rx,y,z),(x,y,z-rz),
              (x,y-ry,z),(x,y+ry*.35,z)]
    f.mesh_data(name,vertices,[(i,(i+1)%4,4) for i in range(4)]+
                [((i+1)%4,i,5) for i in range(4)],'cyan',bone,smooth=False)


def _merge_face(f, names):
    selected=[o for o in f.parts if any(n in o.name for n in names)]
    f.parts=[o for o in f.parts if o not in selected]
    bpy.ops.object.select_all(action='DESELECT')
    for o in selected:o.select_set(True)
    bpy.context.view_layer.objects.active=selected[0]
    bpy.ops.object.join();face=bpy.context.object;face.name='plague_queen_unified_slime_face'
    remesh=face.modifiers.new('Continuous sculpted cheek and jaw planes','REMESH')
    remesh.mode='VOXEL';remesh.voxel_size=.004
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth=face.modifiers.new('Soft liquid transitions','SMOOTH');smooth.factor=.50;smooth.iterations=5
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    for group in list(face.vertex_groups):face.vertex_groups.remove(group)
    face.vertex_groups.new(name='head').add(list(range(len(face.data.vertices))),1,'REPLACE')
    face.data.materials.clear();face.data.materials.append(f.materials[0])
    uv=face.data.uv_layers.active or face.data.uv_layers.new(name='UVMap')
    for loop in uv.data:loop.uv=(2.5/8,1.5/8)
    for poly in face.data.polygons:poly.use_smooth=True
    f.parts.append(face)


def build(Forge, humanoid_rig):
    f=Forge('plague_queen');humanoid_rig(f,1.95)
    # Bow plane sits ahead of the torso. The draw wrist and forearm stay clear
    # of the breastplate; no second hand is buried inside a solid bow handle.
    arms=(('l',(-.245,0,1.42),(-.46,-.32,1.34),(-.33,-.59,1.30),(-.33,-.665,1.30)),
          ('r',(.245,0,1.42),(.35,-.22,1.34),(-.29,-.275,1.30),(-.33,-.30,1.30)))
    for side,shoulder,elbow,wrist,fingers in arms:
        f.bone('upperarm_'+side,shoulder,elbow,'chest')
        f.bone('forearm_'+side,elbow,wrist,'upperarm_'+side)
        f.bone('hand_'+side,wrist,fingers,'forearm_'+side)
    f.bone('skirt_l',(-.14,.03,.88),(-.22,.06,.40),'pelvis')
    f.bone('skirt_r',(.14,.03,.88),(.22,.06,.40),'pelvis')
    f.bone('slime_lock_l',(-.16,.06,1.75),(-.25,.13,1.25),'head')
    f.bone('slime_lock_r',(.16,.06,1.75),(.25,.13,1.25),'head')
    f.bone('arrow',(-.33,-.29,1.30),(-.33,-.96,1.30),'hand_l')
    f.socket('attack_origin',(-.33,-1.04,1.30),'hand_l')
    f.socket('hp_anchor',(0,0,2.43),'head')
    f.socket('status_anchor',(0,0,1.76),'chest')
    body=loft(f,'shaped_emerald_bodice',[(.83,.235,.15),(.91,.225,.15),(.99,.19,.13),
         (1.10,.19,.13),(1.22,.23,.155),(1.34,.245,.15),(1.43,.21,.12),
         (1.48,.115,.085)],'cloth_blue','chest',40)
    chest=body.vertex_groups['chest'];pelvis=body.vertex_groups.new(name='pelvis')
    for v in body.data.vertices:
        w=max(0,min(1,(v.co.z-.96)/.25));w=w*w*(3-2*w)
        chest.add([v.index],w,'REPLACE');pelvis.add([v.index],1-w,'REPLACE')
    # A tapered gown with a front walking slit and weighted side panels.
    verts=[];faces=[];segments=48
    rings=[(.20,.37,.28),(.32,.36,.27),(.51,.32,.24),(.70,.28,.20),(.89,.23,.16)]
    for z,rx,ry in rings:
        for j in range(segments):
            a=j*math.tau/segments
            verts.append((math.sin(a)*rx,math.cos(a)*ry+.025,z+.012*math.sin(a*6)))
    for row in range(len(rings)-1):
        for j in range(segments):
            if row<3 and 21<=j<=26:continue
            q=row*segments+j;n=row*segments+(j+1)%segments
            faces.append((q,q+segments,n+segments,n))
    skirt=f.mesh_data('flowing_split_gown',verts,faces,'cloth_blue_light')
    for b in ('pelvis','skirt_l','skirt_r'):skirt.vertex_groups.new(name=b)
    for v in skirt.data.vertices:
        w=max(0,min(1,(v.co.z-.65)/.24))
        skirt.vertex_groups['pelvis'].add([v.index],w,'REPLACE')
        skirt.vertex_groups['skirt_l' if v.co.x<0 else 'skirt_r'].add([v.index],1-w,'REPLACE')
    solid=skirt.modifiers.new('Tailored gown thickness','SOLIDIFY');solid.thickness=.012
    bpy.context.view_layer.objects.active=skirt;bpy.ops.object.modifier_apply(modifier=solid.name)
    for sign,side in ((-1,'l'),(1,'r')):
        # Brocade seams and drip-shaped hem pendants are fitted to the gown.
        for angle in (.64,1.25,2.05):
            a=angle*sign
            points=[(math.sin(a)*rx*1.025,math.cos(a)*ry*1.025+.025,z) for z,rx,ry in rings]
            f.tube('gown_gold_seam_'+side,points,.006,'brass','skirt_'+side,8)
            f.ellipsoid('liquid_hem_pearl_'+side,points[0],(.022,.022,.04),'cyan','skirt_'+side,16)
        x=sign*.16
        f.tube('slime_thigh_'+side,[f.bones['thigh_'+side]['head'],f.bones['thigh_'+side]['tail']],
               [.085,.06],'skin','thigh_'+side,24)
        f.tube('slime_calf_'+side,[f.bones['shin_'+side]['head'],f.bones['shin_'+side]['tail']],
               [.064,.049],'skin','shin_'+side,24)
        f.ellipsoid('pointed_royal_shoe_'+side,(x,-.08,.085),(.071,.17,.075),'cloth_indigo','foot_'+side,24)
        f.tube('shoe_gold_vamp_'+side,[(x-.05,-.11,.10),(x,-.17,.14),(x+.05,-.11,.10)],.009,'brass','foot_'+side,10)
        f.ellipsoid('shoe_emerald_'+side,(x,-.17,.143),(.023,.025,.012),'cyan','foot_'+side,16)
        a,b,c=map(Vector,(f.bones['upperarm_'+side]['head'],f.bones['upperarm_'+side]['tail'],f.bones['forearm_'+side]['tail']))
        f.ellipsoid('liquid_shoulder_'+side,a,(.10,.10,.11),'skin','upperarm_'+side,28)
        f.tube('slender_upper_arm_'+side,[a,a.lerp(b,.35),b],[.081,.077,.051],'skin','upperarm_'+side,24)
        f.ellipsoid('elbow_'+side,b,(.052,.053,.052),'skin','forearm_'+side,24)
        f.tube('liquid_forearm_'+side,[b,b.lerp(c,.52),c],[.052,.057,.034],'skin','forearm_'+side,24)
        f.tube('gold_wrist_cuff_'+side,[b.lerp(c,.80),b.lerp(c,.88)],[.047,.043],'brass','forearm_'+side,24)
        f.tube('cuff_emerald_inlay_'+side,[b.lerp(c,.82),b.lerp(c,.85)],[.049,.047],'cyan','forearm_'+side,24)
        # Small crescent shoulder guard follows the shoulder, not the neck.
        f.ellipsoid('shoulder_mantle_'+side,a+Vector((0,.015,.04)),(.126,.12,.057),'brass','upperarm_'+side,28)
        f.ellipsoid('shoulder_enamel_'+side,a+Vector((0,-.005,.076)),(.086,.078,.022),'cloth_purple','upperarm_'+side,24)
    for z,rx,ry in ((.94,.228,.157),(1.40,.221,.14)):
        f.tube('bodice_gold_binding',[(rx*math.cos(i*math.tau/48),ry*math.sin(i*math.tau/48),z) for i in range(49)],.009,'brass','chest' if z>1 else 'pelvis',10)
    for sign in (-1,1):
        f.tube('front_bodice_seam',[(sign*.14,-.119,1.42),(sign*.19,-.155,1.26),
              (sign*.13,-.132,1.04),(sign*.16,-.148,.95)],.007,'gold_edge','chest',10)
    _gem(f,'royal_brooch',(0,-.169,1.33),(.048,.022,.069),'chest')
    f.ring('brooch_setting',(0,-.166,1.33),.071,.007,'brass','chest','Y')
    f.ellipsoid('neck',(0,0,1.51),(.071,.068,.11),'skin','neck',32)
    # Original adult face: unified jaw, cheek, nose and ears; independent eyes.
    loft(f,'face_main',[(1.56,.047,.055,0,-.017),(1.60,.10,.093,0,-.017),
        (1.67,.143,.116,0,-.01),(1.76,.165,.135),(1.86,.158,.125),
        (1.94,.113,.095),(1.975,.025,.025)],'skin','head',48)
    for sign in (-1,1):
        f.ellipsoid('face_cheek',(sign*.108,-.105,1.71),(.035,.020,.026),'skin','head',28)
        f.ellipsoid('face_ear',(sign*.164,.012,1.754),(.033,.026,.065),'skin','head',24)
    f.ellipsoid('face_nose_bridge',(0,-.132,1.759),(.024,.026,.058),'skin','head',24)
    f.ellipsoid('face_nose_tip',(0,-.161,1.722),(.029,.031,.022),'skin','head',24)
    _merge_face(f,('face_main','face_cheek','face_ear','face_nose_bridge','face_nose_tip'))
    for sign in (-1,1):
        x=sign*.073
        f.ellipsoid('eye_socket',(x,-.117,1.790),(.046,.014,.022),'skin_shade','head',24)
        f.ellipsoid('almond_eye',(x,-.130,1.792),(.038,.012,.018),'ivory','head',32)
        f.ellipsoid('gold_iris',(x,-.141,1.792),(.015,.0035,.016),'gold_edge','head',24)
        f.ellipsoid('vertical_pupil',(x,-.145,1.792),(.005,.002,.012),'black','head',16)
        f.ellipsoid('eye_glint',(x-.005,-.147,1.798),(.003,.0015,.003),'ivory','head',12)
        f.tube('upper_eyelid',[(x-sign*.036,-.124,1.792),(x,-.142,1.808),(x+sign*.040,-.120,1.800)],.004,'skin_shade','head',10)
        f.tube('swept_royal_brow',[(x-sign*.034,-.119,1.831),(x,-.129,1.842),(x+sign*.047,-.106,1.835)],
               [.005,.009,.003],'hair_auburn','head',12)
        f.ellipsoid('ear_jewel',(sign*.178,-.016,1.703),(.016,.012,.024),'brass','head',20)
        f.ellipsoid('ear_poison_drop',(sign*.183,-.018,1.657),(.021,.018,.038),'cyan','head',24)
    f.tube('quiet_smile',[(-.040,-.126,1.665),(0,-.141,1.660),(.040,-.126,1.665)],.004,'skin_shade','head',10)
    f.ellipsoid('lower_lip',(0,-.137,1.651),(.030,.009,.008),'skin_warm','head',24)
    # Swept liquid locks merge behind the crown; the forehead stays open.
    f.ellipsoid('slime_hair_cap',(0,.041,1.885),(.163,.119,.117),'hair_auburn','head',40)
    for sign,side in ((-1,'l'),(1,'r')):
        f.tube('cascading_slime_lock_'+side,[(sign*.139,.025,1.91),(sign*.179,.085,1.77),
            (sign*.192,.126,1.58),(sign*.251,.153,1.40),(sign*.247,.154,1.25)],
            [(.048,.041),(.056,.043),(.045,.034),(.037,.026),(.009,.010)],'hair_auburn','slime_lock_'+side,24)
        f.tube('wet_lock_highlight_'+side,[(sign*.173,.036,1.82),(sign*.199,.088,1.63),
            (sign*.246,.124,1.41)],[.005,.007,.002],'skin_warm','slime_lock_'+side,10)
    # Crown is a genuine open band with eight prongs and a front central jewel.
    f.ring('crown_lower_gold_band',(0,0,1.937),.167,.014,'brass','head')
    f.ring('crown_upper_gold_band',(0,0,1.988),.176,.010,'gold_edge','head')
    for i in range(8):
        a=i*math.tau/8;h=.15 if i%2==0 else .095
        radial=Vector((math.sin(a),math.cos(a),0));tangent=Vector((math.cos(a),-math.sin(a),0))
        base=radial*.171+Vector((0,0,1.954));tip=radial*.205+Vector((0,0,2.006+h))
        vertices=[base-tangent*.044,base+tangent*.044,tip,
                  base-tangent*.038-radial*.014,base+tangent*.038-radial*.014,tip-radial*.014]
        f.mesh_data('eight_point_gold_crown',vertices,[(0,1,2),(5,4,3),(0,3,4,1),(1,4,5,2),(2,5,3,0)],'brass','head',False)
        f.ellipsoid('crown_tip_bead',tip,(.020,.020,.022),'gold_edge','head',16)
    _gem(f,'crown_venom_diamond',(0,-.187,2.017),(.042,.019,.062))
    # Purposeful recurve bow: left hand on leather wrap, fingertips around sides.
    bx,by,bz=-.33,-.665,1.30
    for sign in (-1,1):
        points=[(bx,by,bz),(bx,by-.08,bz+sign*.22),(bx,by-.045,bz+sign*.43),(bx,by+.085,bz+sign*.64)]
        f.tube('recurved_living_bow',points,[.028,.031,.023,.009],'wood','hand_l',20)
        f.tube('bow_gold_edge',[(x+.026,y,z) for x,y,z in points],[.006,.007,.005,.003],'brass','hand_l',10)
        f.ellipsoid('venom_bow_pod',(bx,by-.063,bz+sign*.34),(.047,.035,.075),'glass','hand_l',24)
        f.tube('pod_gold_lattice',[(bx-.034,by-.07,bz+sign*.28),(bx,by-.10,bz+sign*.34),(bx+.034,by-.07,bz+sign*.40)],.005,'brass','hand_l',8)
    f.tube('bow_leather_grip',[(bx,by,bz-.065),(bx,by,bz+.065)],.034,'leather','hand_l',24)
    for z in [bz-.052+i*.018 for i in range(7)]:
        f.ring('grip_thread',(bx,by,z),.035,.0025,'gold_edge','hand_l')
    f.ellipsoid('left_palm',(bx+.032,by+.026,bz),(.028,.035,.050),'skin','hand_l',28)
    for i in range(4):
        z=bz+.040-i*.025
        f.tube('left_curled_finger',[(bx+.052,by+.016,z),(bx+.035,by-.029,z),(bx-.012,by-.035,z),
            (bx-.030,by-.013,z)],[.008,.010,.009,.006],'skin_warm','hand_l',10)
    f.tube('left_thumb',[(bx+.047,by+.042,bz+.028),(bx+.021,by+.038,bz+.065),(bx-.006,by+.014,bz+.060)],.011,'skin','hand_l',12)
    f.ellipsoid('right_draw_palm',(-.29,-.275,1.30),(.032,.034,.042),'skin','hand_r',24)
    for i in range(3):
        z=1.325-i*.022
        f.tube('draw_finger',[(-.313,-.28,z),(-.339,-.305,z),(-.349,-.283,z),(-.333,-.268,z)],
               [.008,.009,.008,.006],'skin_warm','hand_r',10)
    f.tube('draw_thumb',[(-.272,-.286,1.32),(-.301,-.313,1.34),(-.326,-.30,1.332)],.010,'skin','hand_r',12)
    string=f.tube('flexible_bow_string',[(bx,by+.085,bz+.64),(bx,-.29,bz),(bx,by+.085,bz-.64)],.0032,'ivory',sides=8)
    string.vertex_groups.new(name='hand_l');string.vertex_groups.new(name='hand_r')
    for v in string.data.vertices:
        string.vertex_groups['hand_r' if abs(v.co.z-bz)<.1 else 'hand_l'].add([v.index],1,'REPLACE')
    f.tube('nocked_poison_arrow',[(bx,-.29,bz),(bx,-.97,bz)],.009,'brass','arrow',12)
    f.tube('toxic_arrowhead',[(bx,-.96,bz),(bx,-1.05,bz)],[.033,.001],'cyan','arrow',16)
    for sign in (-1,1):
        f.mesh_data('nocked_arrow_fletch',[(bx,-.32,bz),(bx+sign*.036,-.38,bz),(bx,-.43,bz)],[(0,1,2)],'cloth_purple','arrow',False)
    # Back quiver is attached to the torso and clear of both bow arms.
    f.tube('royal_quiver',[(.16,.19,1.03),(.20,.22,1.48)],[.071,.081],'cloth_purple','chest',24)
    for z in (1.05,1.44):f.ring('quiver_gold_rim',(.18,.21,z),.080,.010,'brass','chest')
    for i in range(3):
        x=.16+(i-1)*.035
        f.tube('reserve_arrow',[(x,.22,1.37),(x,.22,1.67)],.006,'brass','chest',10)
        f.tube('reserve_feather',[(x,.22,1.56),(x,.22,1.68)],[.027,.006],'cloth_purple_light','chest',12)
    f.expansion_design={'identity':'Original adult slime queen archer; emerald liquid skin, sculpted face and eight-point gold crown',
        'surface':'Dedicated emerald/jade lacquer, polished antique gold and plum enamel PBR atlas',
        'equipment':'Recurved venom bow with anatomical leather-wrap grip and deforming drawn string; quiver behind torso',
        'gripPolicy':'Bow follows left hand, drawn string center follows right hand; release arrow uses independent bone',
        'animationPolicy':'Foot-contact slow walk, flowing gown/locks, controlled draw-release, breathing and liquid collapse'}
    return f.finish(True)


def effect_static(Forge,name):
    f=Forge(name)
    if name=='plague_arrow':
        f.tube('arrow_shaft',[(0,.35,0),(0,-.34,0)],.014,'brass',sides=12)
        f.tube('venom_arrowhead',[(0,-.32,0),(0,-.50,0)],[.067,.001],'cyan',sides=16)
        for sign in (-1,1):
            f.mesh_data('plum_fletching',[(0,.12,0),(sign*.065,.26,0),(0,.35,0)],[(0,1,2)],'cloth_purple_light',smooth=False)
        f.ellipsoid('venom_bead',(0,-.29,0),(.032,.055,.032),'glass',detail=20)
    elif name=='toxic_bubble':
        f.ellipsoid('liquid_bubble',(0,0,0),(.5,.5,.5),'glass',detail=24)
        # Authored glints remain readable when instanced small at arena distance.
        f.ellipsoid('bubble_highlight',(-.19,-.37,.25),(.075,.021,.11),'ivory',detail=16)
        f.ellipsoid('bubble_small_glint',(.16,-.43,.18),(.027,.012,.035),'cyan',detail=12)
    else:
        # Open crown of liquid, sized radius1. Native animation expands to6 tiles.
        for i in range(9):
            a=i*math.tau/9;dx,dy=math.cos(a),math.sin(a)
            f.tube('splash_jet',[(dx*.13,dy*.13,.02),(dx*.43,dy*.43,.23),
                (dx*.77,dy*.77,.14),(dx,dy,.04)],[.065,.055,.03,.005],'skin_warm',sides=10)
            f.ellipsoid('splash_bead',(dx*.76,dy*.76,.33+(.09 if i%2 else 0)),(.045,.045,.07),'glass',detail=16)
    return f.finish()


def actions(f,scene,set_world_rotation,aim_leg,aim_arm):
    clips={'Idle':121,'Locomotion':97,'Attack':61,'DrawRelease':61,'Hit':19,
           'Death':79,'Deploy':49,'Status':61,'Turn':37,'Acquire':25}
    made=[]
    for name,length in clips.items():
        act=bpy.data.actions.new('AN_plague_queen_'+name);f.rig.animation_data_create();f.rig.animation_data.action=act
        for pb in f.rig.pose.bones:pb.rotation_mode='XYZ'
        for frame in range(1,length+1):
            scene.frame_set(frame);t=(frame-1)/(length-1);phase=t*math.tau
            for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
            def rot(b,x=0,y=0,z=0):f.rig.pose.bones[b].rotation_euler=(x,y,z)
            def move(b,x=0,y=0,z=0):
                pb=f.rig.pose.bones[b];pb.location=pb.bone.matrix_local.to_3x3().inverted()@Vector((x,y,z))
            rot('chest',.007*math.sin(phase));rot('head',-.016,0,.012*math.sin(phase))
            for side,offset in (('l',0),('r',math.pi)):
                rot('slime_lock_'+side,.025*math.sin(phase+offset),.016*math.cos(phase+offset))
                rot('skirt_'+side,.009*math.sin(phase+offset),0,.012*math.cos(phase+offset))
            if name=='Locomotion':
                move('pelvis',z=-.035+.009*(1-math.cos(phase*2)))
                rot('chest',0,.016*math.sin(phase),.018*math.sin(phase))
                for side,offset in (('l',0.),('r',.5)):
                    u=(t+offset)%1;stance=.66;stride=.66;half=stride*stance*.5
                    if u<stance:y=-half+stride*u;z=0
                    else:v=(u-stance)/(1-stance);y=half-2*half*v*v*(3-2*v);z=.075*math.sin(math.pi*v)
                    target=f.rig.pose.bones['shin_'+side].bone.tail_local+Vector((0,y,z))
                    aim_leg(f,'thigh_'+side,'shin_'+side,'foot_'+side,target,Vector((0,-1,0)))
                    rot('skirt_'+side,.055*math.sin(phase+offset*math.tau-.4),0,.025*math.cos(phase+offset*math.tau))
            if name in ('Attack','DrawRelease'):
                draw=math.sin(math.pi*.5*min(1,t/RELEASE)) if t<RELEASE else max(0,1-(t-RELEASE)/.075)
                recovery=max(0,1-(t-RELEASE)/.22) if t>=RELEASE else 0
                rot('chest',0,0,-.025*draw);rot('head',-.016-.022*draw)
                bpy.context.view_layer.update()
                right=f.rig.pose.bones['hand_r'];parent=right.parent.matrix@right.parent.bone.matrix_local.inverted()
                target=parent@Vector(f.bones['hand_r']['head'])+Vector((0,.11*draw,.009*draw))
                aim_arm(f,'r',target,1)
                move('arrow',y=.11*draw)
                if RELEASE<=t<.80:f.rig.pose.bones['arrow'].scale=(.001,)*3
                rot('slime_lock_l',.025+.04*recovery);rot('slime_lock_r',-.025-.04*recovery)
            if name=='Hit':rot('chest',-.09*math.sin(math.pi*t));rot('head',.06*math.sin(math.pi*t))
            if name=='Turn':rot('chest',0,0,.13*math.sin(math.pi*t));rot('head',0,0,.18*math.sin(math.pi*t))
            if name=='Acquire':rot('head',-.04*math.sin(math.pi*t),0,.07*math.sin(math.pi*t))
            if name=='Deploy':move('pelvis',z=.055*(1-t));rot('head',.10*(1-t))
            if name=='Death':
                e=min(1,t/.85);e=e*e*(3-2*e)
                move('pelvis',z=-.70*e);rot('pelvis',.15*e,.45*e,.19*e)
                rot('thigh_l',-.68*e);rot('thigh_r',-.52*e);rot('shin_l',.90*e);rot('shin_r',.78*e)
                rot('head',.26*e);rot('slime_lock_l',-.12*e);rot('slime_lock_r',.13*e)
            for pb in f.rig.pose.bones:
                for prop in ('rotation_euler','location','scale'):pb.keyframe_insert(prop,frame=frame,group=pb.name)
        curves=[c for layer in act.layers for strip in layer.strips for bag in strip.channelbags for c in bag.fcurves]
        for curve in curves:
            for key in curve.keyframe_points:key.interpolation='LINEAR'
        act.use_fake_user=True
        made.append({'name':name,'action':act.name,'frames':length,'fps':60,'seconds':(length-1)/60,
            'releaseNormalized':RELEASE if name in ('Attack','DrawRelease') else None,
            'loop':name in ('Idle','Locomotion','Status'),'strideMeters':.66 if name=='Locomotion' else None,'object':act})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    return made
