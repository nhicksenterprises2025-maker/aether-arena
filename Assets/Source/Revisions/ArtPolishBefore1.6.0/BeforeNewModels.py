"""Approved original expansion anatomy, mechanical panels and in-place joints.

These builders never rebuild the existing boar/Pulverizer collections. Source
axes are -Y forward / Z up; the ordinary production FBX exporter owns conversion.
"""
import math
import bpy
from mathutils import Vector, Quaternion

CONTRACTS = {
    'bone_warden': dict(scale=1.50, windup=.34, recoil=.20, recover=.48,
                        cycle=1.10, speed=1., death=.90),
    'bone_warden_skeleton': dict(scale=.65, windup=.12, recoil=.10, recover=.20,
                                cycle=.55, speed=2., death=.45),
    'aether_reaper': dict(scale=1.10, windup=.22, recoil=.20, recover=.38,
                          cycle=1.20, speed=1.85, death=.75),
    'iron_giant': dict(scale=1., windup=.22, recoil=.20, recover=.38,
                       cycle=.90, speed=1.55, death=.80),
}
STATIC_IDS = ('iron_shield_fragment', 'shadow_surge_wisp', 'shadow_surge_ribbon')


def metadata(name):
    c=CONTRACTS[name]
    marker=c['windup']/(c['windup']+c['recover'])
    species={
        'bone_warden':'Armored skeletal guardian; four rib-cage skull seals; two-handed bone-and-iron maul',
        'bone_warden_skeleton':'Thin ivory skeleton child; oversized skull; one chipped short sword; broken bronze cuff',
        'aether_reaper':'Airborne hooded specter; midnight cloak; one silver crescent scythe; no legs or wings',
        'iron_giant':'Squat furnace-gray iron automaton; warm body beneath six independent steel shield panels',
    }
    return {
        'revision':'approved-expansion-2', 'detailPasses':1,
        'baseModelScale':c['scale'], 'attackContactNormalized':marker,
        'species':species[name], 'rootMotion':False, 'collisionIndependent':True,
        'attachmentPolicy':'Rigid owning-joint weights; weapon hand targets follow the same weapon socket; no cloth physics',
        'animationContract':{
            'schema':1,
            'attack':{'contactNormalized':marker,'anticipationSeconds':c['windup'],
                      'recoilSeconds':c['recoil'],'recoverySeconds':c['recover']},
            'locomotion':{'referenceCycleSeconds':c['cycle'],
                          'referenceSpeedTilesPerSecond':c['speed'],
                          'strideMeters':c['cycle']*c['speed']/c['scale'], 'phaseOffsets':True},
            'death':{'cleanupSeconds':c['death']},
            'forward':{'blender':'-Y','unreal':'+X'},
            'visual':{'baseModelScale':c['scale'],'readabilityMultiplier':1.0},
        },
    }


def proxy(f,center,size):
    bpy.ops.mesh.primitive_cube_add(size=1,location=center,scale=size)
    obj=bpy.context.object;obj.name='PROXY_'+f.name
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    for col in list(obj.users_collection):col.objects.unlink(obj)
    f.collection.objects.link(obj);obj.hide_render=True;obj.display_type='WIRE'
    obj['exportedToGameplay']=False;obj['purpose']='Editable visual proxy; simulation owns collision'
    f.expansion_design['sourceCollisionProxy']=obj.name
    return f


def biped_rig(f,small=False,giant=False):
    """Distinct proportions, shared meaningful anatomy and clean heel contacts."""
    h=.92 if small else 1.37 if giant else 1.98
    pelvis=.41*h;chest=.68*h;shoulder=.73*h
    width=.32*h if giant else .17*h
    f.family='humanoid';f.bone('root',(0,0,0),(0,0,.06))
    f.bone('pelvis',(0,0,pelvis),(0,0,pelvis+.12*h),'root')
    f.bone('spine',(0,0,pelvis+.12*h),(0,0,.59*h),'pelvis')
    f.bone('spine_2',(0,0,.59*h),(0,0,.68*h),'spine')
    f.bone('chest',(0,0,chest),(0,0,shoulder),'spine_2')
    f.bone('neck',(0,0,shoulder),(0,0,.81*h),'chest')
    f.bone('head',(0,0,.81*h),(0,0,.96*h),'neck')
    f.bone('jaw',(0,-.08*h,.84*h),(0,-.11*h,.80*h),'head')
    for s,side in ((-1,'l'),(1,'r')):
        f.bone('upperarm_'+side,(s*width,0,shoulder),(s*(width+.10*h),-.045*h,.59*h),'chest')
        f.bone('forearm_'+side,(s*(width+.10*h),-.045*h,.59*h),(s*(width+.10*h),-.17*h,.50*h),'upperarm_'+side)
        f.bone('hand_'+side,(s*(width+.10*h),-.17*h,.50*h),(s*(width+.10*h),-.22*h,.48*h),'forearm_'+side)
        f.bone('thigh_'+side,(s*.105*h,0,pelvis),(s*.105*h,-.015*h,.225*h),'pelvis')
        f.bone('shin_'+side,(s*.105*h,-.015*h,.225*h),(s*.11*h,0,.060*h),'thigh_'+side)
        f.bone('foot_'+side,(s*.11*h,0,.060*h),(s*.11*h,-.12*h,.025*h),'shin_'+side)
        f.socket('foot_'+side+'_contact',(s*.11*h,-.065*h,.004),'foot_'+side)
    f.socket('hp_anchor',(0,0,1.12*h),'head')
    f.socket('status_anchor',(0,0,1.16*h),'head')
    f.socket('impact_origin',(0,-.12*h,.57*h),'chest')
    return h


def skull(f,center,radii,bone='head',guard=False):
    x,y,z=center;rx,ry,rz=radii
    f.ellipsoid('angular_skull_vault',center,radii,'ivory',bone,20)
    f.panel('skull_face',(x,y-ry*.71,z-rz*.10),(rx*1.48,ry*.55,rz*.89),'ivory',bone,bevel=.035*rz)
    for s in (-1,1):
        f.panel('deep_eye_socket',(x+s*rx*.42,y-ry*.99,z+rz*.08),(rx*.50,.018,rz*.28),'black',bone,rotation=(0,s*.08,0),bevel=.01)
        f.panel('narrow_green_eye',(x+s*rx*.42,y-ry*1.045,z+rz*.08),(rx*.29,.006,rz*.11),'teal_light',bone,bevel=.005)
        f.tube('angular_cheek',[(x+s*rx*.68,y-ry*.82,z-rz*.02),(x+s*rx*.48,y-ry*.92,z-rz*.52)],
               [rx*.16,rx*.09],'ivory',bone,8)
    f.mesh_data('nasal_triangle',[(x-rx*.12,y-ry*1.015,z-rz*.10),(x+rx*.12,y-ry*1.015,z-rz*.10),
                                 (x,y-ry*1.018,z-rz*.35)],[(0,1,2)],'black',bone,False)
    f.panel('bone_jaw',(x,y-ry*.50,z-rz*.62),(rx*1.22,ry*.88,rz*.30),'ivory','jaw',bevel=.017)
    for i in range(5):
        f.panel('square_tooth',(x+(i-2)*rx*.20,y-ry*.99,z-rz*.54),(rx*.14,.014,rz*.15),'ivory','jaw',bevel=.003)
    if guard:
        for s in (-1,1):
            f.panel('split_iron_brow',(s*rx*.47,y-ry*.83,z+rz*.40),(rx*.82,ry*.58,rz*.30),
                    'iron',bone,rotation=(0,s*.17,-s*.13),bevel=.024)
            f.rivet((s*rx*.74,y-ry*.93,z+rz*.38),bone,'brass',.016)


def skeleton(Forge):
    f=Forge('bone_warden_skeleton');h=biped_rig(f,small=True)
    f.tube('connected_cervical_bones',[(0,0,.65),(0,0,.76)],[.026,.024],'ivory','neck',10)
    skull(f,(0,-.025,.835),(.142,.116,.140))
    f.tube('bone_spine',[(0,0,.40),(0,0,.66)],.032,'ivory','spine',10)
    for j in range(4):
        z=.58-j*.048
        for s in (-1,1):
            f.tube('thin_rib',[(0,-.055,z),(s*.098,-.066,z+.009),(s*.126,.015,z+.012),(s*.054,.066,z)],
                   [.014,.017,.015,.012],'ivory','chest',8)
    f.panel('small_pelvic_bone',(0,0,.39),(.19,.10,.075),'ivory','pelvis',bevel=.022)
    for side in ('l','r'):
        for bn in ('upperarm','forearm','thigh','shin'):
            b=f.bones[bn+'_'+side]
            f.tube('thin_'+bn+'_'+side,[b['head'],b['tail']],[.026,.018],'ivory',bn+'_'+side,10)
        b=f.bones['foot_'+side];f.tube('bone_foot_'+side,[b['head'],b['tail']],[.025,.028],'ivory','foot_'+side,10)
        b=f.bones['hand_'+side];f.ellipsoid('bony_hand_'+side,b['head'],(.03,.034,.024),'ivory','hand_'+side,12)
    f.ring('broken_bronze_wrist_cuff',(-.248,-.155,.48),.038,.011,'brass','forearm_l',axis='Y')
    # One chipped blade, fitted to the right hand rather than a floating prop.
    x=.248;y=-.19;z=.46
    f.bone('sword_socket',(x,y,z),(x,y,z+.12),'hand_r')
    f.tube('short_sword_grip',[(x,y,z-.03),(x,y,z+.11)],.021,'leather','sword_socket',10)
    f.panel('short_crossguard',(x,y,z+.12),(.135,.035,.03),'brass','sword_socket',bevel=.009)
    vertices=[(x-.040,y,z+.13),(x+.040,y,z+.13),(x+.038,y,z+.34),(x+.010,y,z+.365),
              (x+.016,y,z+.405),(x-.034,y,z+.43)]
    f.mesh_data('chipped_short_sword',vertices,[(0,1,2,3,4,5),(5,4,3,2,1,0)],'silver','sword_socket',False)
    f.socket('attack_origin',(x,y,z+.36),'sword_socket')
    f.expansion_design=metadata(f.name)
    f.expansion_design['spawnedChild']=True
    return proxy(f.finish(True),(0,0,.46),(.39,.26,.92))


def warden(Forge):
    f=Forge('bone_warden');h=biped_rig(f)
    f.tube('exposed_cervical_bones',[(0,.01,1.46),(0,.01,1.65)],[.067,.055],'ivory','neck',12)
    skull(f,(0,-.03,1.81),(.19,.16,.205),guard=True)
    f.tube('exposed_bone_spine',[(0,.02,.86),(0,.02,1.40)],.070,'ivory','spine',12)
    f.panel('blackened_pelvis',(0,.035,.84),(.45,.27,.20),'iron','pelvis',bevel=.045)
    for i in range(3):
        z=1.36-i*.15
        for s in (-1,1):
            f.tube('exposed_ivory_rib',[(s*.06,-.09,z),(s*.19,-.22,z-.035),
                                       (s*.27,-.05,z+.015),(s*.20,.12,z+.035)],
                   [.025,.030,.027,.021],'ivory','chest' if i<2 else 'spine_2',12)
            f.panel('spaced_rib_side_plate',(s*.28,.02,z),(.10,.23,.10),'iron','chest',rotation=(0,s*.23,0),bevel=.025)
    f.panel('narrow_soul_core',(0,-.12,1.18),(.058,.054,.39),'teal_light','spine_2',bevel=.023)
    for i,(x,z) in enumerate(((-.095,1.25),(.095,1.25),(-.09,1.07),(.09,1.07))):
        bn='soul_seal_'+str(i);f.bone(bn,(x,-.17,z),(x,-.17,z+.04),'chest')
        f.ellipsoid('small_skull_seal_'+str(i),(x,-.18,z),(.039,.031,.045),'ivory',bn,14)
        for s in (-1,1):f.ellipsoid('seal_eye',(x+s*.013,-.21,z+.006),(.006,.004,.006),'teal_light',bn,8,True)
    for s,side in ((-1,'l'),(1,'r')):
        f.panel('layered_iron_collar',(s*.19,.02,1.46),(.29,.33,.13),'iron','chest',rotation=(0,s*.16,0),bevel=.035)
        f.panel('shoulder_armored_cup',(s*.38,.015,1.43),(.33,.34,.24),'iron','upperarm_'+side,bevel=.075)
        for limb in ('upperarm','forearm'):
            b=f.bones[limb+'_'+side]
            f.tube('bone_'+limb,[b['head'],b['tail']],[.065,.046],'ivory',limb+'_'+side,12)
        f.panel('massive_forearm_gauntlet',(s*.535,-.11,1.13),(.24,.25,.31),'iron','forearm_'+side,rotation=(-.24,0,0),bevel=.046)
        f.ellipsoid('heavy_knuckle_hand',(s*.535,-.23,.99),(.11,.13,.095),'iron','hand_'+side,16)
        for limb in ('thigh','shin'):
            b=f.bones[limb+'_'+side]
            f.tube('bone_'+limb,[b['head'],b['tail']],[.069,.045],'ivory',limb+'_'+side,12)
        f.panel('heavy_shin_guard',(s*.215,-.095,.29),(.25,.16,.43),'iron','shin_'+side,bevel=.046)
        f.panel('broad_bone_heel',(s*.218,-.04,.075),(.25,.29,.13),'iron','foot_'+side,bevel=.040)
        for z in (.18,.39):f.rivet((s*.30,-.184,z),'shin_'+side,'brass',.019)
        for y in (-.10,.075):f.rivet((s*.50,y,1.52),'upperarm_'+side,'brass',.021)
    # The weapon has one independent pivot and two solved grips at every pose.
    weapon_start=len(f.parts)
    f.bone('maul_socket',(.07,-.31,.98),(.30,-.31,1.90),'chest')
    f.tube('two_handed_iron_maul_handle',[(.29,-.31,.75),(.58,-.31,1.93)],[.037,.033],'iron','maul_socket',14)
    for j in range(5):f.ring('bronze_handle_grip',(.345+j*.018,-.31,.97+j*.072),.039,.006,'brass','maul_socket')
    f.panel('rectangular_iron_striking_face',(.58,-.35,1.96),(.58,.37,.35),'iron','maul_socket',bevel=.047)
    f.panel('carved_bone_maul_crown',(.58,-.32,2.13),(.61,.39,.076),'ivory','maul_socket',bevel=.020)
    for s in (-1,1):
        f.panel('carved_bone_maul_border',(.58+s*.275,-.36,1.96),(.061,.38,.33),'ivory','maul_socket',bevel=.014)
        for z in (1.88,2.04):f.rivet((.58+s*.21,-.553,z),'maul_socket','brass',.019)
    for part in f.parts[weapon_start:]:part.location.x-=.28
    f.socket('attack_origin',(.30,-.554,1.96),'maul_socket')
    f.expansion_design=metadata(f.name)
    f.expansion_design['twoHandGripsMeters']={'r':[.07,-.31,.99],'l':[.15,-.31,1.31]}
    f.expansion_design['deathSoulSealBones']=['soul_seal_'+str(i) for i in range(4)]
    return proxy(f.finish(True),(0,0,1.06),(1.16,.70,2.15))


def reaper(Forge):
    f=Forge('aether_reaper');f.family='specter'
    f.bone('root',(0,0,0),(0,0,.08))
    f.bone('flight_pivot',(0,0,.70),(0,0,.87),'root')
    f.bone('spine',(0,0,.87),(0,0,1.17),'flight_pivot')
    f.bone('chest',(0,0,1.17),(0,0,1.36),'spine')
    f.bone('head',(0,0,1.36),(0,0,1.62),'chest')
    for s,side in ((-1,'l'),(1,'r')):
        f.bone('upperarm_'+side,(s*.24,0,1.32),(s*.37,-.035,1.09),'chest')
        f.bone('forearm_'+side,(s*.37,-.035,1.09),(s*.35,-.23,.96),'upperarm_'+side)
        f.bone('hand_'+side,(s*.35,-.23,.96),(s*.35,-.29,.95),'forearm_'+side)
    for j in range(6):
        a=j*math.tau/6;point=Vector((math.cos(a)*.27,math.sin(a)*.27,.88))
        f.bone('cloth_'+str(j),point,point+Vector((math.cos(a)*.07,math.sin(a)*.07,-.35)),'spine')
        f.bone('cloth_tip_'+str(j),point+Vector((math.cos(a)*.07,math.sin(a)*.07,-.35)),
               point+Vector((math.cos(a)*.05,math.sin(a)*.05,-.61)),'cloth_'+str(j))
    f.ellipsoid('opaque_midnight_shoulders',(0,.035,1.235),(.34,.23,.25),'cloth_indigo','chest',24)
    f.panel('shoulder_yoke',(0,0,1.39),(.61,.29,.105),'iron','chest',bevel=.037)
    # Open hood with a visibly dark recessed mask, not a solid face sphere.
    hood=[]
    for z,r,front in ((1.35,.19,-.07),(1.62,.245,-.035),(1.78,.09,.015)):
        for i in range(10):
            a=i*math.tau/10;hood.append((math.cos(a)*r,math.sin(a)*r+front,z))
    faces=[]
    for row in range(2):
        for i in range(10):
            if i in (6,7,8) and row==0:continue
            faces.append((row*10+i,row*10+(i+1)%10,(row+1)*10+(i+1)%10,(row+1)*10+i))
    faces.append(tuple(20+i for i in range(10)))
    f.mesh_data('angular_midnight_hood',hood,faces,'cloth_indigo','head',False)
    f.ellipsoid('shadow_mask',(0,-.085,1.49),(.15,.10,.19),'black','head',20)
    for s,side in ((-1,'l'),(1,'r')):
        f.panel('narrow_cyan_eye',(s*.062,-.188,1.515),(.079,.011,.018),'cyan','head',rotation=(0,0,s*.08),bevel=.008)
        f.tube('hood_cyan_seam',[(s*.12,-.14,1.38),(s*.21,-.12,1.60),(s*.08,-.04,1.755)],.007,'cyan','head',8)
        for limb in ('upperarm','forearm'):
            b=f.bones[limb+'_'+side]
            f.tube('layered_cloth_'+limb,[b['head'],b['tail']],[.083,.061],'cloth_indigo',limb+'_'+side,12)
        hand=f.bones['hand_'+side]['head']
        f.ellipsoid('dark_skeletal_hand',hand,(.062,.071,.035),'iron','hand_'+side,14)
        for j in range(3):
            x=hand[0]+(j-1)*.024
            f.tube('attached_bony_finger',[(x,hand[1]-.03,hand[2]),(x,hand[1]-.075,hand[2]-.045)],.011,'iron','hand_'+side,8)
    for j in range(6):
        a=j*math.tau/6;w=.23
        def pt(r,z,da=0):return (r*math.cos(a+da),r*math.sin(a+da),z)
        upper=[pt(.25,1.13,-w),pt(.25,1.13,w),pt(.34,.53,w*.6),pt(.34,.53,-w*.6)]
        f.mesh_data('layered_midnight_cloak_'+str(j),upper,[(0,1,2,3)],'cloth_indigo','cloth_'+str(j),False)
        inner=[pt(.245,1.10,-w),pt(.245,1.10,w),pt(.327,.54,w*.6),pt(.327,.54,-w*.6)]
        f.mesh_data('desaturated_violet_lining_'+str(j),inner,[(3,2,1,0)],'cloth_purple','cloth_'+str(j),False)
        tip=[pt(.34,.55,-w*.6),pt(.34,.55,w*.6),pt(.30,.265,.015)]
        f.mesh_data('partially_translucent_cloak_tip_'+str(j),tip,[(0,1,2),(2,1,0)],'glass','cloth_tip_'+str(j),False)
        f.tube('thin_cyan_cloak_seam_'+str(j),[pt(.254,1.10,-w),pt(.345,.545,-w*.6)],.005,'cyan','cloth_'+str(j),8)
    for i in range(3):
        z=1.29-i*.065
        f.tube('rib_like_chest_clasp',[(-.075,-.205,z),(0,-.22,z-.035),(.075,-.205,z)],.012,'silver','chest',8)
    weapon_start=len(f.parts)
    f.bone('scythe_socket',(-.06,-.28,.98),(.33,-.28,1.87),'chest')
    f.tube('dark_two_handed_scythe_shaft',[(-.13,-.28,.48),(.50,-.28,1.92)],[.023,.027],'iron','scythe_socket',12)
    # An angular crescent with a proper inner cutout and silver edge, one blade.
    outer=[(.50,-.28,1.94),(.75,-.28,2.03),(.96,-.28,1.95),(1.10,-.28,1.77),
           (1.08,-.28,1.53),(.93,-.28,1.35),(.79,-.28,1.30)]
    inner=[(.84,-.28,1.47),(.91,-.28,1.60),(.93,-.28,1.76),(.84,-.28,1.87),(.68,-.28,1.87)]
    flat=outer+inner
    vs=[(x,y+offset,z) for offset in (-.013,.013) for x,y,z in flat];n=len(flat)
    faces=[tuple(range(n)),tuple(reversed(range(n,2*n)))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    f.mesh_data('angular_silver_crescent_scythe',vs,faces,'silver','scythe_socket',False)
    f.tube('restrained_cyan_blade_edge',outer[1:],[.004]*6,'cyan','scythe_socket',6)
    for part in f.parts[weapon_start:]:part.location.x-=.16
    f.socket('attack_origin',(.84,-.28,1.68),'scythe_socket')
    f.socket('impact_origin',(0,-.12,1.05),'chest')
    f.socket('hp_anchor',(0,0,1.95),'head');f.socket('status_anchor',(0,0,2.02),'head')
    f.expansion_design=metadata(f.name)
    f.expansion_design['twoHandGripsMeters']={'r':[-.06,-.28,1.01],'l':[.06,-.28,1.28]}
    f.expansion_design['flightBobTiles']=.08
    f.expansion_design['cloth']={'physics':False,'maximumJointRadians':.11,'translucency':'Only lower tips'}
    return proxy(f.finish(True),(.16,0,1.10),(1.37,.60,1.75))


SHIELD_CENTERS=[(-.31,-.22,.86),(.31,-.22,.86),(-.50,-.05,1.07),(.50,-.05,1.07),(-.28,.22,.92),(.28,.22,.92)]

def giant(Forge):
    f=Forge('iron_giant');h=biped_rig(f,giant=True)
    f.panel('warm_underlying_furnace_body',(0,0,.89),(.82,.49,.64),'iron','chest',bevel=.105)
    f.panel('copper_inner_chest',(0,-.255,.94),(.51,.028,.32),'copper','chest',bevel=.051)
    f.panel('amber_furnace_vent',(0,-.28,.96),(.32,.016,.085),'ember','chest',bevel=.014)
    for j in range(4):f.panel('vent_iron_bar',((j-1.5)*.065,-.293,.96),(.020,.011,.09),'iron','chest',bevel=.004)
    f.panel('squat_head',(0,-.045,1.245),(.37,.33,.24),'iron','head',bevel=.055)
    for s in (-1,1):f.panel('small_amber_eye_slit',(s*.080,-.217,1.26),(.105,.012,.023),'ember','head',bevel=.009)
    f.panel('jaw_guard',(0,-.13,1.15),(.32,.20,.065),'steel_shadow','jaw',bevel=.024)
    f.panel('segmented_pelvic_housing',(0,0,.56),(.55,.35,.20),'iron','pelvis',bevel=.055)
    for s,side in ((-1,'l'),(1,'r')):
        for bn,key,rad in (('upperarm','iron',.13),('forearm','iron',.15),('thigh','steel_shadow',.12),('shin','iron',.13)):
            b=f.bones[bn+'_'+side];f.tube('mechanical_'+bn,[b['head'],b['tail']],[rad,rad*.85],key,bn+'_'+side,12)
        f.panel('blunt_mechanical_fist',(s*.58,-.23,.68),(.27,.27,.25),'iron','hand_'+side,bevel=.053)
        for j in range(3):f.panel('copper_knuckle',(s*.58+(j-1)*.063,-.375,.71),(.046,.014,.041),'copper','hand_'+side,bevel=.012)
        f.panel('heel_to_toe_boot',(s*.15,-.025,.072),(.29,.35,.14),'iron','foot_'+side,bevel=.037)
        f.bone('leg_piston_'+side,(s*.19,.11,.49),(s*.19,.11,.22),'shin_'+side)
        f.tube('copper_leg_piston',[(s*.19,.11,.24),(s*.19,.11,.49)],.026,'copper','leg_piston_'+side,12)
        for z in (.35,.47):f.rivet((s*.205,-.12,z),'shin_'+side,'brass',.021)
    panels=[]
    for i,center in enumerate(SHIELD_CENTERS):
        bn='shield_panel_'+str(i);x,y,z=center
        f.bone(bn,center,(x,y,z+.07),'chest')
        size=(.43,.08,.35) if i<2 or i>3 else (.20,.38,.29)
        rotation=(0,(1 if x>0 else -1)*.10,0)
        obj=f.panel('shield_panel_'+str(i),center,size,'steel',bn,rotation=rotation,bevel=.040)
        # Retain the six genuinely separate panel meshes in the editable source.
        source=obj.copy();source.data=obj.data.copy();source.name='SOURCE_iron_giant_shield_panel_'+str(i)
        f.collection.objects.link(source);source.hide_render=True;source.hide_viewport=True
        source['exportedToGameplay']=False;source['owningBone']=bn
        for dx in (-.12,.12):f.rivet((x+dx,y-.045,z+.12),bn,'brass',.013)
        f.tube('copper_shield_hinge_'+str(i),[(x-.08,y,z+.175),(x+.08,y,z+.175)],.014,'copper',bn,10)
        panels.append({'bone':bn,'sourceMesh':source.name,'centerMeters':list(center),'parent':'chest'})
    f.socket('attack_origin',(.58,-.37,.68),'hand_r')
    f.expansion_design=metadata(f.name)
    f.expansion_design['shieldPanels']=panels
    f.expansion_design['shieldPresentation']={'bodyRemainsVisible':True,'fragmentStatic':'iron_shield_fragment',
                                             'fragmentLifetimeSeconds':.50,'maximumTravelTiles':.70}
    f.finish(True)
    for panel in panels:
        source=bpy.data.objects[panel['sourceMesh']];transform=source.matrix_world.copy()
        source.parent=f.rig;source.parent_type='BONE';source.parent_bone=panel['bone']
        source.matrix_world=transform
    return proxy(f,(0,0,.69),(1.43,.64,1.38))


def effect_static(Forge,name):
    f=Forge(name)
    if name=='iron_shield_fragment':
        f.panel('independent_bevelled_shield_fragment',(0,0,0),(.36,.068,.29),'steel',bevel=.027)
        f.tube('copper_hinge',[(-.095,0,.15),(.095,0,.15)],.012,'copper',sides=10)
        for x in (-.10,.10):f.rivet((x,-.04,.09),key='brass',size=.015)
    elif name=='shadow_surge_wisp':
        pts=[(0,0,0),(.025,.02,.06),(.075,.055,.13),(.13,.06,.23),(.15,.04,.36)]
        f.tube('violet_inward_wisp',pts,[.065,.055,.036,.020,.001],'violet',sides=12,glow=True)
        f.tube('narrow_lavender_wisp_edge',[(x+.025,y-.015,z) for x,y,z in pts[1:]],
               [.008,.009,.006,.001],'cyan',sides=6,glow=True)
    else:
        vertices=[]
        for i in range(33):
            a=i*math.tau/32
            for r,z in ((.90,.005),(1.,.06),(1.06,.015)):
                vertices.append((r*math.cos(a),r*math.sin(a),z+.007*math.sin(3*a)))
        faces=[]
        for i in range(32):
            for j in range(2):faces.append((i*3+j,(i+1)*3+j,(i+1)*3+j+1,i*3+j+1))
        f.mesh_data('low_violet_ground_ribbon',vertices,faces,'violet',smooth=False,glow=True)
    return f.finish()


def build(Forge,name):
    return {'bone_warden':warden,'bone_warden_skeleton':skeleton,
            'aether_reaper':reaper,'iron_giant':giant}[name](Forge)


def actions(f,scene,set_world_rotation,aim_leg,aim_arm):
    c=CONTRACTS[f.name];marker=c['windup']/(c['windup']+c['recover']);fps=100
    durations={'Idle':2.,'Locomotion':c['cycle'],'Attack':c['windup']+c['recover'],
               'Hit':.16,'Death':c['death'],'Deploy':.30,'Status':1.,'Turn':.18,'Acquire':.12}
    result=[]
    def smooth(x):x=max(0.,min(1.,x));return x*x*(3-2*x)
    def move(bn,x=0,y=0,z=0):
        pb=f.rig.pose.bones[bn];pb.location=pb.bone.matrix_local.to_3x3().inverted()@Vector((x,y,z))
    def rotate(bn,x=0,y=0,z=0):f.rig.pose.bones[bn].rotation_euler=(x,y,z)
    def owning_grips(clip):
        grips=f.expansion_design.get('twoHandGripsMeters')
        if not grips:return
        if f.name=='bone_warden' and clip=='Death':return
        weapon=f.rig.pose.bones['maul_socket' if f.name=='bone_warden' else 'scythe_socket']
        # A bounded authored weapon correction keeps both grips reachable.
        # Stretching arms or leaving one hand floating would hide a rig error.
        for iteration in range(12):
            changed=False
            for side,grip in grips.items():
                bpy.context.view_layer.update()
                transform=weapon.matrix@weapon.bone.matrix_local.inverted()
                target=transform@Vector(grip)
                up=f.rig.pose.bones['upperarm_'+side];fore=f.rig.pose.bones['forearm_'+side]
                shoulder=up.matrix.translation;delta=target-shoulder
                reach=up.bone.length+fore.bone.length-.003
                if delta.length>reach:
                    correction=shoulder+delta.normalized()*reach-target
                    base=(weapon.parent.matrix@weapon.parent.bone.matrix_local.inverted())@weapon.bone.matrix_local
                    weapon.location+=base.to_3x3().inverted()@correction;changed=True
            if not changed:break
        bpy.context.view_layer.update();transform=weapon.matrix@weapon.bone.matrix_local.inverted()
        for side in ('r','l'):aim_arm(f,side,transform@Vector(grips[side]),1.)
    for clip,duration in durations.items():
        action=bpy.data.actions.new('AN_'+f.name+'_'+clip);f.rig.animation_data_create();f.rig.animation_data.action=action
        length=round(duration*fps)+1
        for frame in range(1,length+1):
            scene.frame_set(frame);t=(frame-1)/(length-1);clock=t*duration
            for pb in f.rig.pose.bones:
                pb.rotation_mode='XYZ';pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
            if f.name=='aether_reaper':
                # Native owns the <=.08-tile flight bob. Joint motion is bounded.
                rotate('spine',.016*math.sin(t*math.tau));rotate('head',0,0,.016*math.sin(t*math.tau))
                for j in range(6):
                    rotate('cloth_'+str(j),.045*math.sin(t*math.tau+j*.6),.028*math.cos(t*math.tau+j*.6))
                    rotate('cloth_tip_'+str(j),.07*math.sin(t*math.tau+j*.6-.5),.035*math.cos(t*math.tau+j*.6))
                if clip=='Locomotion':rotate('spine',-.035)
            elif clip=='Locomotion':
                stride=c['cycle']*c['speed']/c['scale']
                # Shorter ground contacts let the fast child's run cover its
                # exact reference distance without clamping/skating the stance.
                stance=.18 if f.name=='bone_warden_skeleton' else .26 if f.name=='iron_giant' else .42
                half=stride*stance*.5
                move('pelvis',z=-.025+.009*math.sin(t*math.tau*2))
                for side,phase in (('l',0.),('r',.5)):
                    u=(t+phase)%1
                    if u<stance:y=-half+2*half*u/stance;lift=0.
                    else:v=(u-stance)/(1-stance);y=half-2*half*smooth(v);lift=(.054 if f.name=='bone_warden_skeleton' else .095)*math.sin(v*math.pi)
                    target=f.rig.pose.bones['shin_'+side].bone.tail_local+Vector((0,y,lift))
                    aim_leg(f,'thigh_'+side,'shin_'+side,'foot_'+side,target,Vector((0,-1,0)))
                rotate('pelvis',0,.024*math.sin(t*math.tau),.028*math.sin(t*math.tau))
                rotate('chest',0,0,-.025*math.sin(t*math.tau))
                if f.name=='iron_giant':
                    for side,phase in (('l',0.),('r',math.pi)):
                        move('leg_piston_'+side,z=.023*math.sin(t*math.tau+phase))
                        rotate('upperarm_'+side,.12*math.sin(t*math.tau+phase))
            if clip=='Attack':
                if t<=marker:
                    u=t/marker
                    # Anticipation and acceleration end at one contact marker.
                    lift=smooth(u/.70)*(1-smooth((u-.70)/.30))
                    impact=smooth((u-.70)/.30)
                else:
                    settle=smooth((t-marker)/(1-marker));lift=0.;impact=1-settle
                if f.name=='bone_warden':
                    rotate('maul_socket',-.60*lift+.52*impact,0,-.18*lift)
                    rotate('chest',-.10*lift+.13*impact,0,.07*lift)
                elif f.name=='aether_reaper':
                    rotate('scythe_socket',-.16*lift,0,-.62*lift+.74*impact)
                    rotate('chest',0,0,-.12*lift+.16*impact)
                elif f.name=='iron_giant':
                    rotate('upperarm_r',-.50*lift+.84*impact);rotate('forearm_r',-.18*lift+.20*impact)
                    rotate('chest',-.035*lift+.07*impact)
                else:
                    rotate('upperarm_r',-.43*lift+.68*impact);rotate('forearm_r',-.14*lift+.12*impact)
            elif clip=='Death':
                fold=smooth(t/.75)
                if f.name=='aether_reaper':
                    rotate('chest',.30*fold);rotate('scythe_socket',.40*fold)
                    for j in range(6):rotate('cloth_'+str(j),.20*fold,0,.10*fold)
                else:
                    for side in ('l','r'):
                        rotate('thigh_'+side,-.42*fold);rotate('shin_'+side,.73*fold)
                    move('pelvis',z=-(.17 if f.name=='bone_warden_skeleton' else .36)*fold)
                    rotate('chest',.42*fold,0,.06*fold)
                    if f.name=='bone_warden':
                        move('maul_socket',x=.14*fold,z=-.31*fold);rotate('maul_socket',.80*fold,0,.31*fold)
                        for j in range(4):move('soul_seal_'+str(j),x=(j%2*2-1)*.035*fold,z=.06*fold)
                    elif f.name=='bone_warden_skeleton':move('head',z=.035*fold);rotate('head',0,.20*fold)
                    else:rotate('upperarm_l',.25*fold);rotate('upperarm_r',.25*fold)
            elif clip=='Deploy':
                if f.name!='aether_reaper':move('pelvis',z=.045*(1-smooth(t)))
            elif clip=='Hit':rotate('chest',-.065*math.sin(math.pi*t))
            elif clip=='Turn':rotate('chest',0,0,.12*math.sin(math.pi*t))
            elif clip=='Acquire':rotate('head',0,0,.12*math.sin(math.pi*t))
            elif clip=='Status':rotate('head',.012*math.sin(t*math.tau*3))
            elif clip=='Idle' and f.name!='aether_reaper':rotate('chest',.010*math.sin(t*math.tau))
            owning_grips(clip)
            for pb in f.rig.pose.bones:
                for channel in ('rotation_euler','location','scale'):pb.keyframe_insert(channel,frame=frame,group=pb.name)
        # Dense linear samples prohibit hidden cubic overshoot in scale/motion.
        curves=list(action.fcurves) if hasattr(action,'fcurves') else [c for layer in action.layers for strip in layer.strips for bag in strip.channelbags for c in bag.fcurves]
        for curve in curves:
            for point in curve.keyframe_points:point.interpolation='LINEAR'
        action.use_fake_user=True
        result.append({'name':clip,'action':action.name,'frames':length,'fps':fps,'seconds':(length-1)/fps,
                       'releaseNormalized':marker if clip=='Attack' else None,
                       'loop':clip in ('Idle','Locomotion','Status'),
                       'strideMeters':c['cycle']*c['speed']/c['scale'] if clip=='Locomotion' else None,'object':action})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    return result
