"""Approved original expansion anatomy, mechanical panels and in-place joints.

These builders never rebuild the existing boar/Pulverizer collections. Source
axes are -Y forward / Z up; the ordinary production FBX exporter owns conversion.
"""
import math
import json
import bpy
import bmesh
from mathutils import Vector, Quaternion, Matrix

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
        'revision':'crafted-expansion-rework-1', 'detailPasses':3,
        'baseModelScale':c['scale'], 'attackContactNormalized':marker,
        'species':species[name], 'rootMotion':False, 'collisionIndependent':True,
        'attachmentPolicy':'Rigid fitted weapon/armor attachments; Reaper cloak rows blend chest/gore/tip joints; weapon palms follow one socket; no cloth physics',
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
    f.mesh['artQualityContract']=json.dumps(f.expansion_design.get('artQuality',{}),sort_keys=True)
    f.mesh['modelDesignContract']=json.dumps(f.expansion_design,sort_keys=True)
    bpy.ops.mesh.primitive_cube_add(size=1,location=center,scale=size)
    obj=bpy.context.object;obj.name='PROXY_'+f.name
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    for col in list(obj.users_collection):col.objects.unlink(obj)
    f.collection.objects.link(obj);obj.hide_render=True;obj.display_type='WIRE'
    obj['exportedToGameplay']=False;obj['purpose']='Editable visual proxy; simulation owns collision'
    f.expansion_design['sourceCollisionProxy']=obj.name
    return f


def semantic(obj, name):
    """Preserve measured surface identity after Forge joins the mesh."""
    group=obj.vertex_groups.get(name) or obj.vertex_groups.new(name=name)
    group.add(list(range(len(obj.data.vertices))),1.,'REPLACE')
    return obj


def plate(f,name,outline,depth,key,bone=None,bevel=.008,group=None):
    """Forged closed plate with an authored outline, rather than a box."""
    n=len(outline)
    vertices=[(x,y+dy,z) for dy in (-depth*.5,depth*.5) for x,y,z in outline]
    faces=[tuple(reversed(range(n))),tuple(range(n,n*2))]
    faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
    ob=f.mesh_data(name,vertices,faces,key,bone,False)
    if bevel:
        bpy.context.view_layer.objects.active=ob;ob.select_set(True)
        mod=ob.modifiers.new('Forged rolled edges','BEVEL');mod.width=bevel;mod.segments=3
        bpy.ops.object.modifier_apply(modifier=mod.name);ob.select_set(False)
    # The mirrored/folded plate outlines can reverse their original winding.
    # Keep closed shell normals outward without relying on two-sided shading.
    bm=bmesh.new();bm.from_mesh(ob.data)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(ob.data);bm.free();ob.data.update()
    if group:semantic(ob,group)
    return ob


def barrel(f,name,rings,key,bone=None,sides=24,group=None):
    """Elliptical section loft. Each ring is (z, rx, ry, center_x, center_y)."""
    vertices=[]
    for z,rx,ry,x,y in rings:
        for i in range(sides):
            a=math.tau*i/sides;vertices.append((x+rx*math.cos(a),y+ry*math.sin(a),z))
    faces=[tuple(reversed(range(sides))),tuple((len(rings)-1)*sides+i for i in range(sides))]
    for j in range(len(rings)-1):
        for i in range(sides):faces.append((j*sides+i,j*sides+(i+1)%sides,(j+1)*sides+(i+1)%sides,(j+1)*sides+i))
    ob=f.mesh_data(name,vertices,faces,key,bone,True)
    if group:semantic(ob,group)
    return ob


def bone_limb(f,name,start,end,radius,bone,paired=False):
    a,b=Vector(start),Vector(end);d=b-a
    # Rounded epiphyses and a waisted shaft give exposed bone an anatomical read.
    offsets=(-1,1) if paired else (0,)
    for side in offsets:
        offset=Vector((radius*.54*side,0,0));r=radius*.68 if paired else radius
        f.tube(name,[a+offset,a+d*.16+offset,a+d*.48+offset,a+d*.83+offset,b+offset],
               [r*.95,r*.75,r*.56,r*.78,r],'ivory',bone,14)
    for p in (a,b):f.ellipsoid(name+'_joint',p,(radius*1.04,radius*.86,radius*.91),'ivory',bone,16)


def fingers(f,side,center,scale=1.,key='ivory'):
    """Separated articulated-looking phalanges fitted to the owning palm."""
    x,y,z=center;bn='hand_'+side
    semantic(f.ellipsoid('metacarpal_palm_'+side,center,(.041*scale,.037*scale,.040*scale),key,bn,16),'QA_palm_'+side)
    for j in range(4):
        dx=(j-1.5)*.020*scale
        pts=[(x+dx,y-.021*scale,z-.014*scale),(x+dx,y-.041*scale,z-.043*scale),(x+dx,y-.024*scale,z-.070*scale)]
        f.tube('finger_phalanges_'+side+'_'+str(j),pts,[.008*scale,.008*scale,.006*scale],key,bn,8)
    f.tube('opposed_thumb_'+side,[(x-.046*scale,y,z),(x-.053*scale,y-.027*scale,z-.026*scale)],
           [.012*scale,.009*scale],key,bn,10)


def socket_grip(f,side,tangent,shaft_radius,palm_offset,scale=1.,palm_direction=(0,-1,0)):
    """A solid palm outside the shaft and separated fingers around an aperture.

    The hand joint is the palm target, not the shaft attachment point. All
    surfaces follow its owning bone; the weapon follows its independent socket.
    """
    bone='hand_'+side;center=Vector(f.bones[bone]['head'])
    tangent=Vector(tangent).normalized();front=Vector(palm_direction)
    front=(front-tangent*front.dot(tangent)).normalized()
    across=tangent.cross(front).normalized();shaft=center-front*palm_offset
    palm=semantic(f.ellipsoid('outside_socket_palm_'+side,center,
                  (.027*scale,.015*scale,.035*scale),'iron',bone,24),'QA_palm_'+side)
    palm.rotation_euler=Matrix((across,-front,tangent)).transposed().to_euler()
    # Four independent finger chains leave a genuine continuous shaft cavity.
    # Their inner surface sits just outside the measured wrapped shaft radius.
    thickness=.006*scale;radius=shaft_radius+thickness+.0024
    for j in range(4):
        axis_offset=(j-1.5)*.018*scale
        angles=(0,24,57,88,119,150,180,207)
        # The metacarpal starts on the outside palm, then curls inward toward
        # the staff. This leaves the wrist farther from the handle without a
        # detached palm or a solid hand volume around its centerline.
        points=[shaft+tangent*axis_offset+front*(palm_offset-.011*scale),
                shaft+tangent*axis_offset+front*(radius+.010*scale)]
        points += [shaft+tangent*axis_offset+radius*(front*math.cos(math.radians(a))+across*math.sin(math.radians(a))) for a in angles]
        semantic(f.tube('curled_socket_phalanges_'+side+'_'+str(j),points,
                       [thickness*1.08,thickness,thickness*.98,thickness,thickness*.91,thickness*.95,thickness*.87,thickness*.89,thickness*.77,thickness*.70],
                       'iron',bone,12),'QA_grip_fingers_'+side)
        # Small fitted joint faces preserve each finger's separation in closeup.
        for k in (4,6):
            semantic(f.ellipsoid('socket_knuckle_'+side+'_'+str(j)+'_'+str(k),points[k],
                      (thickness*1.05,thickness*.90,thickness*1.05),'steel_shadow',bone,12),'QA_grip_fingers_'+side)
    thumb_angles=(-12,-36,-64,-88,-110)
    thumb=[shaft+tangent*.038*scale+front*(palm_offset-.011*scale)]
    thumb += [shaft+tangent*(.038*scale-i*.004*scale)+radius*(front*math.cos(math.radians(a))+across*math.sin(math.radians(a)))
           for i,a in enumerate(thumb_angles)]
    semantic(f.tube('opposed_socket_thumb_'+side,thumb,[.008*scale,.007*scale,.007*scale,.0068*scale,.0064*scale,.0055*scale],
                   'iron',bone,12),'QA_grip_thumb_'+side)


def shaft_wrap(f,name,center,tangent,radius,bone):
    """Leather band perpendicular to the real shaft, included in grip proof."""
    axis=Vector(tangent).normalized();u=Vector((0,-1,0));v=axis.cross(u).normalized()
    points=[Vector(center)+radius*(u*math.cos(i*math.tau/32)+v*math.sin(i*math.tau/32)) for i in range(33)]
    return semantic(f.tube(name,points,.0025,'leather',bone,10),'QA_weapon_handle')


def grip_wrist(obj,side):
    """Include adjacent wrist/cuff bulk in the actual hand contact surface."""
    semantic(obj,'QA_grip_wrist_'+side)
    return semantic(obj,'QA_palm_'+side)


def grip_contract(f,grips,tangents,radius,palm_offset,scale,palm_directions=None):
    f.expansion_design['twoHandGripsMeters']=grips
    palm_directions=palm_directions or {side:(0,-1,0) for side in grips}
    f.expansion_design['twoHandPalmDirections']={side:list(Vector(palm_directions[side]).normalized()) for side in grips}
    f.expansion_design['twoHandJointTargetsMeters']={side:list(Vector(p)+Vector(palm_directions[side]).normalized()*palm_offset) for side,p in grips.items()}
    f.expansion_design['gripContactContract']={'schema':1,'mode':'socket-contact','hands':{
        side:{'handBone':'hand_'+side,'palmGroup':'QA_palm_'+side,'fingersGroup':'QA_grip_fingers_'+side,
              'thumbGroup':'QA_grip_thumb_'+side,'handleGroup':'QA_weapon_handle',
              'shaftTangentMeters':list(Vector(tangents[side]).normalized()),'shaftOuterRadiusMeters':radius[side] if isinstance(radius,dict) else radius,
              'cavityHalfLengthMeters':.032*scale} for side in ('l','r')}}


def captive_skeleton(f,index,x,z):
    """An enclosed miniature body, using the four authoritative soul roots."""
    root='soul_seal_'+str(index);head='captive_head_'+str(index);arm='captive_arm_'+str(index)
    y=-.035;begin=len(f.parts)
    f.bone(root,(x,y,z-.085),(x,y,z-.030),'chest')
    f.bone(head,(x,y,z-.029),(x,y,z+.042),root)
    side=-1 if index%2==0 else 1
    shoulder=(x+side*.033,y,z-.051);elbow=(x+side*.049,y+.012,z-.087)
    f.bone(arm,shoulder,(x+side*.051,y-.006,z-.118),root)
    f.ellipsoid('captive_cranium_'+str(index),(x,y,z),(.035,.024,.041),'ivory',head,20)
    f.ellipsoid('captive_maxilla_'+str(index),(x,y-.014,z-.020),(.023,.014,.019),'ivory',head,16)
    for sign in (-1,1):
        f.ellipsoid('captive_eye_recess_'+str(index),(x+sign*.012,y-.024,z+.007),(.010,.005,.011),'black',head,12)
        semantic(f.ellipsoid('captive_soul_eye_'+str(index),(x+sign*.012,y-.029,z+.007),(.006,.003,.006),
                            'teal_light',head,12,True),'QA_captive_eyes_'+str(index))
    plate(f,'captive_nasal_recess_'+str(index),[(x-.0035,y-.030,z-.004),(x+.0035,y-.030,z-.004),(x,y-.030,z-.013)],
          .003,'black',head,.0004)
    f.tube('captive_mandible_'+str(index),[(x-.021,y-.006,z-.027),(x-.015,y-.024,z-.035),
              (x+.015,y-.024,z-.035),(x+.021,y-.006,z-.027)],.0045,'ivory',head,10)
    for j in range(3):f.panel('captive_tooth_'+str(index),(x+(j-1)*.009,y-.027,z-.025),(.005,.004,.008),'ivory',head,bevel=.001)
    for j in range(4):
        pz=z-.060-j*.015
        f.ellipsoid('captive_vertebra_'+str(index),(x,y+.018,pz),(.008,.008,.0045),'ivory',root,10)
        for sign in (-1,1):
            f.tube('captive_rib_'+str(index),[(x+sign*.007,y-.015,pz-.003),(x+sign*.027,y-.015,pz),
                  (x+sign*.031,y+.006,pz+.002),(x+sign*.010,y+.023,pz)],.0042,'ivory',root,10)
    f.tube('captive_clavicles_'+str(index),[(x-.032,y-.005,z-.046),(x,y-.012,z-.049),(x+.032,y-.005,z-.046)],.005,'ivory',root,10)
    for sign in (-1,1):
        owned=arm if sign==side else root
        a=(x+sign*.033,y,z-.051);b=(x+sign*.049,y+.012,z-.087);c=(x+sign*.051,y-.006,z-.118)
        f.tube('captive_arm_bones_'+str(index),[a,b,c],[.0048,.0053,.0042],'ivory',owned,10)
        f.ellipsoid('captive_little_palm_'+str(index),c,(.006,.004,.008),'ivory',owned,10)
        for finger in (-1,0,1):
            f.tube('captive_fingers_'+str(index),[(c[0]+finger*.004,c[1]-.002,c[2]),(c[0]+finger*.004,c[1]-.006,c[2]-.008)],.0019,'ivory',owned,8)
        f.tube('captive_folded_leg_'+str(index),[(x+sign*.017,y,z-.119),(x+sign*.024,y-.008,z-.133),
                  (x+sign*.029,y+.009,z-.153)],[.0055,.0052,.0045],'ivory',root,10)
        f.tube('captive_toe_'+str(index),[(x+sign*.029,y+.009,z-.153),(x+sign*.029,y-.008,z-.155)],.004,'ivory',root,10)
    f.tube('captive_pelvis_'+str(index),[(x-.021,y,z-.116),(x,y-.007,z-.126),(x+.021,y,z-.116)],.006,'ivory',root,10)
    for part in f.parts[begin:]:semantic(part,'QA_captive_skeleton_'+str(index))


def art_quality(f,landmarks,clearances=None):
    f.expansion_design['artQuality']={'schema':1,'authoringIntent':'Fitted anatomy and separate weapon/face silhouettes; acceptance requires evaluated surfaces and rendered review',
        'faceLandmarks':landmarks,'clearances':clearances or [],'semanticGroupsPreserved':True,
        'animationIntent':'Foot plants, body-led anticipation/contact/recovery and bounded secondary joint lag'}


def biped_rig(f,small=False,giant=False):
    """Distinct proportions, shared meaningful anatomy and clean heel contacts."""
    h=.92 if small else 1.37 if giant else 1.98
    pelvis=.41*h;chest=.68*h;shoulder=.73*h
    width=.32*h if giant else .165*h
    f.family='humanoid';f.bone('root',(0,0,0),(0,0,.06))
    f.bone('pelvis',(0,0,pelvis),(0,0,pelvis+.12*h),'root')
    f.bone('spine',(0,0,pelvis+.12*h),(0,0,.59*h),'pelvis')
    f.bone('spine_2',(0,0,.59*h),(0,0,.68*h),'spine')
    f.bone('chest',(0,0,chest),(0,0,shoulder),'spine_2')
    f.bone('neck',(0,0,shoulder),(0,0,.81*h),'chest')
    f.bone('head',(0,0,.81*h),(0,0,.96*h),'neck')
    f.bone('jaw',(0,-.08*h,.84*h),(0,-.11*h,.80*h),'head')
    for s,side in ((-1,'l'),(1,'r')):
        elbow=(s*(width+.07*h),.035*h,.57*h)
        wrist=(s*(width+.10*h),-.15*h,.43*h)
        f.bone('upperarm_'+side,(s*width,0,shoulder),elbow,'chest')
        f.bone('forearm_'+side,elbow,wrist,'upperarm_'+side)
        f.bone('hand_'+side,wrist,(wrist[0],wrist[1]-.035*h,wrist[2]-.045*h),'forearm_'+side)
        hip=.15*h if giant else .105*h;foot=.165*h if giant else .11*h
        f.bone('thigh_'+side,(s*hip,0,pelvis),(s*hip,-.015*h,.225*h),'pelvis')
        f.bone('shin_'+side,(s*hip,-.015*h,.225*h),(s*foot,0,.060*h),'thigh_'+side)
        f.bone('foot_'+side,(s*foot,0,.060*h),(s*foot,-.12*h,.025*h),'shin_'+side)
        f.socket('foot_'+side+'_contact',(s*foot,-.065*h,.004),'foot_'+side)
    f.socket('hp_anchor',(0,0,1.12*h),'head')
    f.socket('status_anchor',(0,0,1.16*h),'head')
    f.socket('impact_origin',(0,-.12*h,.57*h),'chest')
    return h


def skull(f,center,radii,bone='head',guard=False):
    x,y,z=center;rx,ry,rz=radii
    semantic(f.ellipsoid('sculpted_cranial_vault',center,radii,'ivory',bone,32),'QA_face')
    f.ellipsoid('maxillary_bone',(x,y-ry*.72,z-rz*.28),(rx*.69,ry*.38,rz*.43),'ivory',bone,24)
    for s in (-1,1):
        f.ellipsoid('recessed_orbit',(x+s*rx*.43,y-ry*.965,z+rz*.10),(rx*.29,.020,rz*.23),'black',bone,20)
        eye=semantic(f.ellipsoid('green_eye_'+str(s),(x+s*rx*.43,y-ry*1.082,z+rz*.10),(rx*.16,.007,rz*(.052 if guard else .090)),'teal_light',bone,16,True),'QA_eyes')
        if guard:eye.rotation_euler.y=s*.18
        f.tube('supraorbital_ridge',[(x+s*rx*.16,y-ry*.98,z+rz*.28),(x+s*rx*.46,y-ry*.98,z+rz*.33),(x+s*rx*.72,y-ry*.83,z+rz*.21)],
               [rx*.075,rx*.11,rx*.07],'ivory',bone,12)
        f.tube('angular_cheek',[(x+s*rx*.68,y-ry*.82,z-rz*.02),(x+s*rx*.48,y-ry*.92,z-rz*.52)],
               [rx*.16,rx*.09],'ivory',bone,8)
    plate(f,'recessed_nasal_opening',[(x-rx*.105,y-ry*1.105,z-rz*.08),(x+rx*.105,y-ry*1.105,z-rz*.08),
                                    (x,y-ry*1.105,z-rz*.33)],.012,'black',bone,.001)
    f.tube('mandible_horseshoe',[(x-rx*.66,y-ry*.25,z-rz*.30),(x-rx*.53,y-ry*.81,z-rz*.69),(x,y-ry*.94,z-rz*.74),
                                 (x+rx*.53,y-ry*.81,z-rz*.69),(x+rx*.66,y-ry*.25,z-rz*.30)],rx*.13,'ivory','jaw',14)
    for i in range(5):
        f.panel('square_tooth',(x+(i-2)*rx*.20,y-ry*.99,z-rz*.54),(rx*.14,.014,rz*.15),'ivory','jaw',bevel=.003)
    if guard:
        for s in (-1,1):
            plate(f,'fitted_temple_guard',[(x+s*rx*.55,y-ry*.56,z+rz*.71),(x+s*rx*.94,y-ry*.22,z+rz*.54),
                  (x+s*rx*.95,y-ry*.20,z-rz*.13),(x+s*rx*.75,y-ry*.50,z-rz*.15)],.038,'iron',bone,.007)
            f.tube('temple_bronze_inlay',[(x+s*rx*.61,y-ry*.58,z+rz*.68),(x+s*rx*.83,y-ry*.39,z+rz*.44)],.008,'brass',bone,8)


def skeleton(Forge):
    f=Forge('bone_warden_skeleton');h=biped_rig(f,small=True)
    f.tube('connected_cervical_bones',[(0,0,.65),(0,0,.76)],[.022,.019],'ivory','neck',12)
    skull(f,(0,-.025,.835),(.142,.116,.140))
    for j in range(6):f.ellipsoid('lumbar_vertebra_'+str(j),(0,.025,.425+j*.035),(.025,.025,.015),'ivory','spine' if j<3 else 'spine_2',12)
    for s in (-1,1):
        f.tube('curved_clavicle',[(0,-.025,.655),(s*.08,-.035,.67),(s*.15,-.005,.67)],[.019,.023,.018],'ivory','chest',12)
    for j in range(4):
        z=.61-j*.045
        for s in (-1,1):
            f.tube('individual_curved_rib_'+str(j),[(s*.023,-.055,z-.016),(s*.070,-.088,z-.003),(s*.120,-.045,z+.015),
                   (s*.121,.017,z+.021),(s*.052,.063,z)], [.013,.014,.014,.012,.011],'ivory','chest',12)
        f.ellipsoid('sternum_segment_'+str(j),(0,-.063,z),(.020,.014,.022),'ivory','chest',12)
    for s in (-1,1):
        plate(f,'flared_ilium',[(s*.026,-.005,.425),(s*.109,.015,.452),(s*.119,.011,.391),(s*.061,-.005,.347),(s*.025,-.012,.362)],
              .045,'ivory','pelvis',.008)
    f.tube('pubic_arch',[(-.055,-.025,.355),(0,-.038,.339),(.055,-.025,.355)],.016,'ivory','pelvis',12)
    for side in ('l','r'):
        for bn in ('upperarm','forearm','thigh','shin'):
            b=f.bones[bn+'_'+side]
            bone_limb(f,'anatomical_'+bn+'_'+side,b['head'],b['tail'],.027 if bn=='thigh' else .022,bn+'_'+side,bn in ('forearm','shin'))
        b=f.bones['foot_'+side]
        f.tube('metatarsal_foot_'+side,[b['head'],b['tail']],[.020,.027],'ivory','foot_'+side,12)
        for j in range(3):
            x=b['tail'][0]+(j-1)*.016
            f.tube('separated_toe',[(x,b['tail'][1]+.025,.026),(x,b['tail'][1]-.030,.013)],[.010,.007],'ivory','foot_'+side,8)
        fingers(f,side,f.bones['hand_'+side]['head'],.68)
    cuff=Vector(f.bones['forearm_l']['tail'])+Vector((0,.015,.04))
    # A visibly broken cuff is an open authored arc, not a closed torus.
    points=[cuff+Vector((.030*math.cos(.28+i*math.tau/18),0,.030*math.sin(.28+i*math.tau/18))) for i in range(15)]
    f.tube('broken_bronze_wrist_cuff',points,.008,'brass','forearm_l',10)
    # One chipped blade, fitted to the right hand rather than a floating prop.
    x,y,z=f.bones['hand_r']['head']
    f.bone('sword_socket',(x,y,z),(x,y,z+.12),'hand_r')
    semantic(f.tube('leather_bound_sword_grip',[(x,y,z-.049),(x,y,z+.068)],.017,'leather','sword_socket',14),'QA_weapon_handle')
    f.ellipsoid('bronze_sword_pommel',(x,y,z-.061),(.024,.022,.022),'brass','sword_socket',16)
    f.tube('curved_crossguard',[(x-.073,y,z+.074),(x-.046,y,z+.090),(x+.046,y,z+.090),(x+.073,y,z+.074)],.011,'brass','sword_socket',10)
    blade=[(x-.031,y,z+.102),(x+.031,y,z+.102),(x+.025,y,z+.270),(x+.011,y,z+.280),
           (x+.020,y,z+.300),(x+.016,y,z+.375),(x,y,z+.408),(x-.021,y,z+.360)]
    plate(f,'tapered_chipped_short_sword',blade,.014,'silver','sword_socket',.002,'QA_weapon_head')
    f.tube('blade_center_ridge',[(x,y-.008,z+.105),(x,y-.008,z+.375)],[.007,.001],'steel','sword_socket',8)
    f.socket('attack_origin',(x,y,z+.36),'sword_socket')
    f.expansion_design=metadata(f.name)
    f.expansion_design['spawnedChild']=True
    f.expansion_design['oneHandGripMeters']={'hand':'r','weaponBone':'sword_socket','positionMeters':[x,y,z]}
    art_quality(f,[{'name':'left_eye','bone':'head','positionMeters':[-.06106,-.157512,.849],'group':'QA_eyes'},
                   {'name':'right_eye','bone':'head','positionMeters':[.06106,-.157512,.849],'group':'QA_eyes'}])
    return proxy(f.finish(True),(0,0,.46),(.39,.26,.92))


def warden(Forge):
    f=Forge('bone_warden');h=biped_rig(f)
    for j in range(5):
        f.ellipsoid('exposed_cervical_vertebra',(0,.005,1.47+j*.032),(.052,.044,.019),'ivory','neck',16)
    skull(f,(0,-.025,1.815),(.185,.155,.207),guard=True)
    for j in range(8):
        f.ellipsoid('exposed_spinal_vertebra',(0,.040,.875+j*.064),(.058,.050,.025),'ivory','spine' if j<3 else 'spine_2',16)
    for s,side in ((-1,'l'),(1,'r')):
        plate(f,'forged_hip_fan',[(s*.05,.0,.91),(s*.205,.025,.94),(s*.265,.028,.855),(s*.235,.005,.755),(s*.075,-.025,.765)],
              .15,'iron','pelvis',.013)
        f.tube('hip_brass_binding',[(s*.07,-.078,.892),(s*.18,-.072,.912),(s*.228,-.050,.850)],.009,'brass','pelvis',10)
    # Captives live behind the front ribs, rather than being pasted on the cage.
    for j in range(4):
        z=1.405-j*.118
        for s in (-1,1):
            points=[(s*.055,-.115,z-.048),(s*.17,-.204,z-.032),(s*.278,-.145,z+.018),
                    (s*.285,.004,z+.054),(s*.20,.125,z+.058),(s*.053,.110,z+.009)]
            semantic(f.tube('sculpted_exposed_rib_'+str(j),points,[.026,.030,.029,.027,.024,.022],
                     'ivory','chest' if j<2 else 'spine_2',16),'QA_rib_cage')
    f.tube('restrained_vertical_soul_core',[(0,-.085,.965),(0,-.101,1.33)],[.026,.024],'teal_light','spine_2',12,True)
    for i,(x,z) in enumerate(((-.120,1.387),(.120,1.387),(-.120,1.177),(.120,1.177))):
        captive_skeleton(f,i,x,z)
    for sign,side in ((-1,'l'),(1,'r')):
        # Tapered shoulder cups and separate lames follow the arm anatomy.
        x=sign*.337
        barrel(f,'curved_iron_pauldron_'+side,[(1.365,.125,.135,x,.015),(1.415,.180,.165,x,.015),
                 (1.495,.163,.148,x,.023),(1.535,.090,.096,x,.025)],'iron','upperarm_'+side,24)
        f.tube('pauldron_bronze_edge',[(x-sign*.12,-.108,1.405),(x,-.150,1.400),(x+sign*.13,-.099,1.410)],.012,'brass','upperarm_'+side,12)
        plate(f,'shoulder_bone_inset',[(x-.072,-.151,1.443),(x,-.155,1.489),(x+.072,-.151,1.443),(x+.054,-.157,1.416),(x-.054,-.157,1.416)],
              .012,'ivory','upperarm_'+side,.004)
        for limb in ('upperarm','forearm','thigh','shin'):
            b=f.bones[limb+'_'+side]
            if limb=='forearm':
                a=Vector(b['head']);end=Vector(b['tail']);d=end-a
                for s in (-1,1):
                    offset=Vector((s*.012,0,0))
                    f.tube('tapered_radius_ulna_'+side,[a+offset,a+d*.18+offset,a+d*.76+offset],
                           [.028,.024,.013],'ivory','forearm_'+side,14)
                    grip_wrist(f.tube('narrow_distal_wrist_bone_'+side,[a+d*.76+offset,end+offset],
                                    [.013,.0085],'ivory','forearm_'+side,12),side)
                f.ellipsoid('fitted_elbow_epiphysis_'+side,a,(.048,.043,.045),'ivory','forearm_'+side,16)
                grip_wrist(f.ellipsoid('narrow_wrist_epiphysis_'+side,end,(.017,.018,.018),'ivory','forearm_'+side,16),side)
            else:
                bone_limb(f,'exposed_'+limb+'_'+side,b['head'],b['tail'],.060 if limb=='thigh' else .050,limb+'_'+side,limb=='shin')
        b=f.bones['forearm_'+side];a=Vector(b['head']);end=Vector(b['tail']);d=end-a
        f.tube('tapered_forearm_vambrace',[a+d*.20,a+d*.42,a+d*.78],[(.060,.064),(.064,.068),(.039,.044)],'iron','forearm_'+side,20)
        grip_wrist(f.tube('narrow_vambrace_cuff',[a+d*.78,a+d*.91],[(.039,.044),(.023,.028)],'iron','forearm_'+side,20),side)
        f.tube('vambrace_inlaid_rail',[a+d*.28+Vector((0,-.069,0)),a+d*.73+Vector((0,-.049,0))],.008,'brass','forearm_'+side,10)
        socket_grip(f,side,(.525,0,1.14),.0315 if side=='r' else .029,.085,1.10,
                    (.91,0,-.419079) if side=='r' else (-.91,0,.419079))
        knee=f.bones['shin_'+side]['head']
        kx,ky,kz=knee
        plate(f,'fitted_iron_knee_cup',[(kx-.082,ky-.090,kz+.036),(kx,ky-.103,kz+.083),(kx+.082,ky-.090,kz+.036),
                  (kx+.062,ky-.094,kz-.057),(kx,ky-.113,kz-.084),(kx-.062,ky-.094,kz-.057)],.045,'iron','shin_'+side,.011)
        f.tube('knee_bone_crest',[(kx-.040,ky-.120,kz+.029),(kx,ky-.132,kz+.052),(kx+.040,ky-.120,kz+.029)],.007,'ivory','shin_'+side,10)
        x=sign*.218
        plate(f,'tapered_shin_greave',[(x-.095,-.083,.430),(x,-.111,.471),(x+.095,-.083,.430),
                  (x+.073,-.104,.124),(x,-.129,.091),(x-.073,-.104,.124)],.046,'iron','shin_'+side,.013)
        f.tube('shin_ivory_flute',[(x,-.143,.129),(x,-.139,.394)],[.012,.018],'ivory','shin_'+side,10)
        barrel(f,'rolled_ankle_cuff',[(.099,.086,.093,x,0),(.132,.099,.105,x,0),(.155,.083,.087,x,0)],'brass','foot_'+side,20)
        barrel(f,'fitted_armored_skeletal_sabatons',[(.008,.095,.160,x,-.040),(.031,.111,.178,x,-.040),
                     (.072,.101,.150,x,-.028),(.104,.068,.088,x,.004)],'iron','foot_'+side,24)
        f.tube('sabatons_rolled_toe_edge',[(x-.075,-.178,.041),(x,-.205,.041),(x+.075,-.178,.041)],.009,'steel','foot_'+side,12)
    # The maul is carried entirely to the right of the eye/skull silhouette.
    f.bone('maul_socket',(.162,-.34,1.055),(.56,-.34,1.92),'chest')
    semantic(f.tube('two_handed_iron_maul_handle',[(.025789,-.34,.76),(.56,-.34,1.92)],[.029,.028],'iron','maul_socket',16),'QA_weapon_handle')
    for j in range(7):
        z=.94+j*.037;x=.035+(z-.78)*(.525/1.14)
        shaft_wrap(f,'bound_maul_grip',(x,-.34,z),(.525,0,1.14),.029,'maul_socket')
    head=[(.335,-.34,1.840),(.390,-.34,1.790),(.780,-.34,1.790),(.825,-.34,1.840),
          (.825,-.34,2.015),(.770,-.34,2.065),(.390,-.34,2.065),(.335,-.34,2.010)]
    plate(f,'octagonal_forged_maul_head',head,.275,'iron','maul_socket',.014,'QA_weapon_head')
    for sign in (-1,1):
        x=.580+sign*.197
        plate(f,'bone_maul_endcap',[(x-sign*.014,-.34,1.802),(x+sign*.027,-.34,1.850),(x+sign*.027,-.34,2.006),(x-sign*.014,-.34,2.052)],
              .288,'ivory','maul_socket',.007,'QA_weapon_head')
    semantic(f.tube('carved_maul_center_band',[(.470,-.487,1.821),(.580,-.487,1.844),(.690,-.487,1.821)],.011,'brass','maul_socket',10),'QA_weapon_head')
    semantic(f.tube('carved_maul_center_band',[(.470,-.487,2.033),(.580,-.487,2.010),(.690,-.487,2.033)],.011,'brass','maul_socket',10),'QA_weapon_head')
    f.socket('attack_origin',(.580,-.485,1.925),'maul_socket')
    f.expansion_design=metadata(f.name)
    # Dominant hand above the support hand keeps the lower forearm from
    # sharing the maul's centerline and leaves the cage's front openings free.
    grip_contract(f,{'l':[.05112,-.34,.815],'r':[.11329,-.34,.950]},
                  {'r':[.525,0,1.14],'l':[.525,0,1.14]},{'l':.029,'r':.0315},.085,1.10,
                  {'r':(.91,0,-.419079),'l':(-.91,0,.419079)})
    f.expansion_design['deathSoulSealBones']=['soul_seal_'+str(i) for i in range(4)]
    f.expansion_design['captiveSkeletons']={'count':4,'gameplayEntities':False,'placement':'Recessed behind the Warden front rib surfaces',
        'rootBones':['soul_seal_'+str(i) for i in range(4)],'headBones':['captive_head_'+str(i) for i in range(4)],
        'armBones':['captive_arm_'+str(i) for i in range(4)],'livingMotion':'Deterministic phase-offset body shifts, head turns and a small reaching arm',
        'surfaceGroups':['QA_captive_skeleton_'+str(i) for i in range(4)]}
    art_quality(f,[{'name':'left_eye','bone':'head','positionMeters':[-.07955,-.19971,1.8357],'group':'QA_eyes'},
                   {'name':'right_eye','bone':'head','positionMeters':[.07955,-.19971,1.8357],'group':'QA_eyes'}],
                  [{'name':'weapon_clear_of_eyes','groupsA':['QA_weapon_head'],'groupsB':['QA_eyes'],'minimumMeters':.12,'clips':['Idle','Locomotion','Deploy','Status','Turn','Acquire'],'ignorePhases':[]}])
    return proxy(f.finish(True),(.14,0,1.03),(1.35,.78,2.10))


def reaper(Forge):
    f=Forge('aether_reaper');f.family='specter'
    f.bone('root',(0,0,0),(0,0,.08))
    f.bone('flight_pivot',(0,0,.70),(0,0,.87),'root')
    f.bone('spine',(0,0,.87),(0,0,1.17),'flight_pivot')
    f.bone('chest',(0,0,1.17),(0,0,1.36),'spine')
    f.bone('head',(0,0,1.36),(0,0,1.62),'chest')
    for sign,side in ((-1,'l'),(1,'r')):
        f.bone('upperarm_'+side,(sign*.235,0,1.325),(sign*.455,.025,1.075),'chest')
        f.bone('forearm_'+side,(sign*.455,.025,1.075),(sign*.475,-.250,.865),'upperarm_'+side)
        f.bone('hand_'+side,(sign*.475,-.250,.865),(sign*.475,-.285,.825),'forearm_'+side)
    for j in range(6):
        a=j*math.tau/6;point=Vector((math.cos(a)*.26,math.sin(a)*.26,.90))
        f.bone('cloth_'+str(j),point,point+Vector((math.cos(a)*.06,math.sin(a)*.06,-.32)),'spine')
        f.bone('cloth_tip_'+str(j),point+Vector((math.cos(a)*.06,math.sin(a)*.06,-.32)),
               point+Vector((math.cos(a)*.05,math.sin(a)*.05,-.60)),'cloth_'+str(j))
    barrel(f,'tapered_cloth_shoulder_mantle',[(1.05,.215,.168,0,.045),(1.21,.310,.210,0,.035),
             (1.31,.322,.208,0,.018),(1.39,.230,.157,0,.015)],'cloth_indigo','chest',32)
    # Open draped cowl: rounded cross-sections and an arched front edge.
    rows=[(1.355,.213,.190,-.005),(1.515,.263,.232,.014),(1.690,.245,.205,.038),
          (1.802,.148,.137,.047),(1.846,.027,.030,.045)]
    vertices=[];cols=25
    for z,rx,ry,y in rows:
        for i in range(cols):
            a=-math.pi*.19+i/(cols-1)*math.pi*1.38
            vertices.append((rx*math.cos(a),y+ry*math.sin(a),z+.015*math.cos(2*a)))
    faces=[(r*cols+i,r*cols+i+1,(r+1)*cols+i+1,(r+1)*cols+i) for r in range(len(rows)-1) for i in range(cols-1)]
    faces.append(tuple((len(rows)-1)*cols+i for i in range(cols)))
    semantic(f.mesh_data('draped_open_cowl',vertices,faces,'cloth_indigo','head',True),'QA_hood')
    for side,idx in ((-1,cols-1),(1,0)):
        pts=[Vector(vertices[r*cols+idx])+Vector((0,-.005,0)) for r in range(len(rows))]
        f.tube('hood_rolled_silver_selvedge',pts,[.006,.007,.008,.007,.005],'steel_shadow','head',12)
    # The face is recessed into the opening; the shaft never crosses it.
    semantic(barrel(f,'tapered_recessed_spectral_mask',[(1.369,.031,.035,0,-.020),(1.445,.086,.065,0,-.027),
                    (1.562,.133,.092,0,-.038),(1.650,.098,.070,0,-.021),(1.698,.031,.034,0,-.008)],'black','head',28),'QA_face')
    for sign in (-1,1):
        semantic(f.ellipsoid('inset_cyan_eye',(sign*.059,-.135,1.554),(.042,.008,.010),'cyan','head',18,True),'QA_eyes')
        f.tube('mask_brow',[(sign*.023,-.133,1.584),(sign*.060,-.136,1.595),(sign*.103,-.112,1.580)],.012,'iron','head',12)
    for sign,side in ((-1,'l'),(1,'r')):
        for limb in ('upperarm','forearm'):
            b=f.bones[limb+'_'+side];a=Vector(b['head']);end=Vector(b['tail']);d=end-a
            if limb=='upperarm':
                f.tube('tailored_upperarm_sleeve',[a,a+d*.33,a+d*.70,end],[.084,.090,.070,.052],'cloth_indigo',limb+'_'+side,20)
            else:
                f.tube('tailored_forearm_sleeve',[a,a+d*.33,a+d*.78],[.071,.072,.030],'cloth_indigo',limb+'_'+side,20)
                grip_wrist(f.tube('fitted_scythe_wrist_cuff',[a+d*.78,a+d*.92,end],[.030,.022,.015],
                                'cloth_indigo',limb+'_'+side,20),side)
            f.tube('silver_sleeve_clasp',[a+d*.64+Vector((0,-.049,0)),a+d*.73+Vector((0,-.041,0))],.007,'silver',limb+'_'+side,10)
        socket_grip(f,side,(.067,0,.25) if side=='r' else (.137,0,.49),.030,.075,.95,
                    (.25,0,-.067) if side=='r' else (-.49,0,.137))
    # Six continuous curved gores, with blended shoulder/cloth/tip weights.
    for j in range(6):
        a=j*math.tau/6;cols=9;zs=(1.24,1.06,.86,.66,.47)
        vs=[]
        for r,z in enumerate(zs):
            radius=(.312,.286,.292,.336,.358)[r]
            for i in range(cols):
                angle=a+(i/(cols-1)-.5)*math.tau/6*.99
                fold=.013*math.cos(i/(cols-1)*math.pi*4+j*.4)*(r/4)
                vs.append(((radius+fold)*math.cos(angle),(radius+fold)*math.sin(angle)+.015,z+.014*math.sin(angle*3)*(r/4)))
        # Rows descend, so the outward winding is the reverse of an ascending
        # barrel loft. Inner lining deliberately keeps the opposite winding.
        fs=[(r*cols+i,(r+1)*cols+i,(r+1)*cols+i+1,r*cols+i+1) for r in range(4) for i in range(cols-1)]
        ob=semantic(f.mesh_data('curved_midnight_cloak_gore_'+str(j),vs,fs,'cloth_indigo',smooth=True),'QA_cloak')
        weights=((1.,0.,0.),(.48,.52,0.),(0.,.94,.06),(0.,.50,.50),(0.,.10,.90))
        for row,(w0,w1,w2) in enumerate(weights):
            indices=list(range(row*cols,(row+1)*cols))
            for bn,w in (('chest',w0),('cloth_'+str(j),w1),('cloth_tip_'+str(j),w2)):
                if w:(ob.vertex_groups.get(bn) or ob.vertex_groups.new(name=bn)).add(indices,w,'REPLACE')
        # Opaque lining is separated inward, with the same deforming weights.
        inner=[(x*.968,y*.968,z+.006) for x,y,z in vs]
        lining=f.mesh_data('curved_violet_cloak_lining_'+str(j),inner,[tuple(reversed(face)) for face in fs],'cloth_purple',smooth=True)
        for group in ob.vertex_groups:
            if group.name.startswith('QA_'):continue
            vg=lining.vertex_groups.new(name=group.name)
            for v in ob.data.vertices:
                match=next((g.weight for g in v.groups if g.group==group.index),0.)
                if match:vg.add([v.index],match,'REPLACE')
        tips=[]
        for row,z in enumerate((.47,.355,.255)):
            for i in range(cols):
                angle=a+(i/(cols-1)-.5)*math.tau/6*.99
                r=.358-row*.021;fade=math.sin(i/(cols-1)*math.pi)
                tips.append((r*math.cos(angle),r*math.sin(angle)+.015,z+.036*(1-fade)*row))
        tf=[(r*cols+i,(r+1)*cols+i,(r+1)*cols+i+1,r*cols+i+1) for r in range(2) for i in range(cols-1)]
        semantic(f.mesh_data('restrained_translucent_cloak_tail_'+str(j),tips,tf,'glass','cloth_tip_'+str(j),True),'QA_cloak_tips')
        # A short hem accent replaces the visually noisy six vertical cyan bars.
        hem=[Vector(vs[4*cols+i])+Vector((0,-.002,.006)) for i in range(cols)]
        f.tube('subtle_cyan_hem_'+str(j),hem,.0035,'cyan','cloth_tip_'+str(j),8)
    for i in range(3):
        z=1.285-i*.048
        f.tube('rib_shaped_silver_clasp',[(-.081,-.201,z),(0,-.224,z-.024),(.081,-.201,z)],.010,'silver','chest',12)
    f.ellipsoid('quiet_cyan_clasp_core',(0,-.233,1.240),(.020,.009,.032),'cyan','chest',16,True)
    f.bone('scythe_socket',(.222,-.32,1.12),(.570,-.31,1.985),'chest')
    shaft=[(.025,-.32,.40),(.155,-.32,.87),(.222,-.32,1.12),(.292,-.32,1.36),(.510,-.31,1.60),(.570,-.31,1.985)]
    semantic(f.tube('crooked_silver_bound_scythe_shaft',shaft,[.022,.026,.026,.026,.026,.025],'iron','scythe_socket',18),'QA_weapon_handle')
    for z in (.970,1.010,1.050,1.090,1.130,1.170):
        x=.155+(z-.87)*(.067/.25);shaft_wrap(f,'scythe_leather_grip',(x,-.32,z),(.067,0,.25),.0275,'scythe_socket')
    # Smooth crescent, a sharpened inner curve and a thickness taper to the tip.
    outer=[];inner=[];steps=41
    for i in range(steps):
        u=i/(steps-1);angle=math.radians(88-184*u);radius=.535
        outer.append((.555+radius*math.cos(angle),-.31,1.645+radius*math.sin(angle)))
        thickness=.120*(1-u)**.58+.026*math.sin(math.pi*u)
        r=radius-thickness;inner.append((.555+r*math.cos(angle),-.31,1.645+r*math.sin(angle)))
    outline=outer+list(reversed(inner));plate(f,'tapered_silver_crescent',outline,.026,'silver','scythe_socket',.003,'QA_weapon_head')
    semantic(f.tube('quiet_cyan_cutting_edge',inner,[.005*(1-i/(steps-1))+.001 for i in range(steps)],'cyan','scythe_socket',8,True),'QA_weapon_head')
    semantic(f.ellipsoid('scythe_bronze_binding',(.570,-.31,1.985),(.043,.035,.072),'brass','scythe_socket',20),'QA_weapon_head')
    f.socket('attack_origin',(.980,-.31,1.610),'scythe_socket')
    f.socket('impact_origin',(0,-.12,1.05),'chest')
    f.socket('hp_anchor',(0,0,1.95),'head');f.socket('status_anchor',(0,0,2.02),'head')
    f.expansion_design=metadata(f.name)
    grip_contract(f,{'r':[.1818,-.32,.970],'l':[.222,-.32,1.120]},
                  {'r':[.067,0,.25],'l':[.137,0,.49]},.030,.075,.95,
                  {'r':(.25,0,-.067),'l':(-.49,0,.137)})
    f.expansion_design['flightBobTiles']=.08
    f.expansion_design['cloth']={'physics':False,'maximumJointRadians':.30,'translucency':'Only curved lower tail strips','weights':'Blended chest/gore/lower-tip rows'}
    art_quality(f,[{'name':'left_eye','bone':'head','positionMeters':[-.059,-.143,1.554],'group':'QA_eyes'},
                   {'name':'right_eye','bone':'head','positionMeters':[.059,-.143,1.554],'group':'QA_eyes'}],
                  [{'name':'scythe_clear_of_eyes','groupsA':['QA_weapon_head','QA_weapon_handle'],'groupsB':['QA_eyes'],'minimumMeters':.09,'clips':['Idle','Locomotion','Deploy','Status','Turn','Acquire'],'ignorePhases':[]}])
    return proxy(f.finish(True),(.18,0,1.10),(1.42,.78,1.93))


SHIELD_CENTERS=[(-.31,-.22,.86),(.31,-.22,.86),(-.50,-.05,1.07),(.50,-.05,1.07),(-.28,.22,.92),(.28,.22,.92)]

def giant(Forge):
    f=Forge('iron_giant');h=biped_rig(f,giant=True)
    semantic(barrel(f,'forged_barrel_torso',[(.630,.248,.180,0,.030),(.735,.373,.243,0,.017),
             (.910,.413,.251,0,.010),(1.068,.376,.224,0,.020),(1.140,.244,.170,0,.020)],'iron','chest',32),'QA_giant_body')
    # A recessed furnace door has a lip, inner chamber and curved grate.
    plate(f,'copper_furnace_door',[( -.157,-.252,.790),(-.185,-.259,.835),(-.175,-.261,1.014),(-.114,-.250,1.049),
              (.114,-.250,1.049),(.175,-.261,1.014),(.185,-.259,.835),(.157,-.252,.790)],.025,'copper','chest',.009)
    plate(f,'black_recessed_firebox',[(-.118,-.279,.836),(-.121,-.279,1.004),(.121,-.279,1.004),(.118,-.279,.836)],.012,'black','chest',.004)
    f.ellipsoid('warm_furnace_core',(0,-.291,.918),(.092,.010,.061),'ember','chest',24,True)
    for j in range(5):
        x=(j-2)*.046
        f.tube('arched_furnace_grate',[(x,-.306,.839),(x,-.316,.919),(x,-.305,1.003)],.009,'steel_shadow','chest',12)
    for side in (-1,1):
        f.tube('door_rolled_border',[(side*.142,-.293,.803),(side*.160,-.296,.838),(side*.154,-.296,1.010),(side*.115,-.278,1.029)],.010,'brass','chest',12)
    # Rounded helmet and a distinct brow/jaw leave the eyes readable.
    semantic(barrel(f,'forged_helmet',[(1.123,.135,.124,0,-.024),(1.155,.182,.160,0,-.025),
                     (1.300,.180,.153,0,-.015),(1.373,.120,.106,0,.007)],'iron','head',28),'QA_face')
    plate(f,'angled_helmet_brow',[(-.164,-.154,1.300),(-.125,-.183,1.324),(.125,-.183,1.324),(.164,-.154,1.300),
                   (.147,-.177,1.277),(-.147,-.177,1.277)],.021,'steel','head',.006)
    for side in (-1,1):
        semantic(f.ellipsoid('small_amber_eye',(side*.073,-.189,1.260),(.046,.008,.017),'ember','head',20,True),'QA_eyes')
    plate(f,'rounded_jaw_guard',[(-.139,-.155,1.205),(-.119,-.185,1.162),(0,-.194,1.145),(.119,-.185,1.162),(.139,-.155,1.205)],
              .040,'steel_shadow','jaw',.009)
    for j in range(3):f.tube('jaw_vent',( ( (j-1)*.038,-.219,1.166),((j-1)*.038,-.215,1.190)),.004,'black','jaw',8)
    # Exposed waist spindle and separated hip drives make the body mechanical.
    barrel(f,'waist_rotation_spindle',[(.505,.132,.110,0,0),(.549,.166,.140,0,0),(.602,.143,.119,0,0)],'steel_shadow','pelvis',24)
    f.ring('copper_waist_bearing',(0,0,.548),.147,.012,'copper','pelvis')
    for sign,side in ((-1,'l'),(1,'r')):
        hip=f.bones['thigh_'+side]['head'];knee=f.bones['shin_'+side]['head'];ankle=f.bones['foot_'+side]['head']
        f.ellipsoid('hip_drive_housing',hip,(.118,.122,.100),'steel_shadow','pelvis',24)
        f.ring('hip_axle_copper_seal',hip,.087,.012,'copper','thigh_'+side,'X')
        for bn in ('upperarm','forearm','thigh','shin'):
            b=f.bones[bn+'_'+side];a=Vector(b['head']);end=Vector(b['tail']);d=end-a
            # Armor stops before each ball joint, leaving readable mechanical gaps.
            r=.105 if bn in ('thigh','upperarm') else .115
            f.tube('forged_'+bn+'_sleeve',[a+d*.16,a+d*.31,a+d*.68,a+d*.85],
                   [(r*.76,r*.86),(r,r*1.10),(r*.91,r),(r*.70,r*.81)],'iron',bn+'_'+side,20)
            f.tube('steel_'+bn+'_spindle',[a,a+d*.22,end-d*.14,end],.045,'steel_shadow',bn+'_'+side,16)
            rail=[a+d*.26+Vector((0,-r*1.07,0)),a+d*.67+Vector((0,-r*1.02,0))]
            f.tube('copper_'+bn+'_service_inlay',rail,.009,'copper',bn+'_'+side,10)
        shoulder=f.bones['upperarm_'+side]['head'];elbow=f.bones['forearm_'+side]['head']
        for name,pos,bn,r in (('shoulder',shoulder,'upperarm_'+side,.118),('elbow',elbow,'forearm_'+side,.093),('knee',knee,'shin_'+side,.092)):
            f.ellipsoid(name+'_axle_hub',pos,(r,r*.90,r),'steel_shadow',bn,24)
            f.ring(name+'_exposed_bearing',Vector(pos)+Vector((sign*r*.52,0,0)),r*.66,.012,'steel',bn,'X')
            f.ellipsoid(name+'_hex_axle_cap',Vector(pos)+Vector((sign*r*.72,0,0)),(.020,r*.39,r*.39),'copper',bn,12)
        hand=Vector(f.bones['hand_'+side]['head'])+Vector((0,-.015,-.030));x,y,z=hand
        plate(f,'forged_punch_fist',[(x-.108,y,z-.082),(x-.135,y,z-.047),(x-.123,y,z+.085),
                   (x-.083,y,z+.110),(x+.095,y,z+.110),(x+.129,y,z+.074),(x+.130,y,z-.049),(x+.089,y,z-.085)],.225,'iron','hand_'+side,.014)
        for j in range(4):
            xx=x+(j-1.5)*.047
            f.tube('separate_knuckle_ridge',[(xx,y-.128,z-.030),(xx,y-.137,z+.056)],[.017,.020],'steel','hand_'+side,12)
        f.tube('copper_thumb_hinge',[(x-sign*.097,y-.090,z-.055),(x-sign*.121,y-.108,z+.014)],.022,'copper','hand_'+side,12)
        footx=ankle[0]
        barrel(f,'rounded_heel_toe_boot',[(.014,.120,.181,footx,-.055),(.048,.139,.203,footx,-.055),
                   (.115,.112,.165,footx,-.040),(.145,.070,.091,footx,.000)],'iron','foot_'+side,24)
        f.tube('boot_steel_toecap',[(footx-.087,-.227,.060),(footx,-.244,.060),(footx+.087,-.227,.060)],.013,'steel','foot_'+side,12)
        f.bone('leg_piston_'+side,(sign*.243,.116,.495),(sign*.243,.116,.220),'shin_'+side)
        f.tube('telescopic_piston_cylinder',[(sign*.243,.116,.270),(sign*.243,.116,.455)],.034,'steel_shadow','leg_piston_'+side,16)
        f.tube('polished_piston_rod',[(sign*.243,.116,.218),(sign*.243,.116,.330)],.017,'silver','leg_piston_'+side,16)
        for zz in (.271,.443):f.ring('piston_copper_gland',(sign*.243,.116,zz),.035,.007,'copper','leg_piston_'+side)
    panels=[]
    for i,center in enumerate(SHIELD_CENTERS):
        bn='shield_panel_'+str(i);x,y,z=center;sign=1 if x>0 else -1
        f.bone(bn,center,(x,y,z+.07),'chest')
        if i<2:
            outline=[(sign*.095,-.282,.756),(sign*.153,-.293,1.063),(sign*.300,-.213,1.126),
                     (sign*.412,-.128,1.011),(sign*.388,-.179,.784),(sign*.270,-.224,.692)]
        elif i<4:
            outline=[(sign*.385,-.167,1.048),(sign*.492,-.169,1.145),(sign*.590,-.060,1.130),
                     (sign*.580,.100,1.053),(sign*.466,.146,1.028),(sign*.405,.054,.988)]
        else:
            outline=[(sign*.084,.244,.743),(sign*.154,.247,1.081),(sign*.307,.174,1.096),
                     (sign*.391,.120,.958),(sign*.345,.195,.735),(sign*.221,.232,.682)]
            outline=[(px,py+.065,pz) for px,py,pz in outline]
        obj=plate(f,'shield_panel_'+str(i),outline,.040,'steel',bn,.008,'QA_shield_panel_'+str(i))
        source=obj.copy();source.data=obj.data.copy();source.name='SOURCE_iron_giant_shield_panel_'+str(i)
        f.collection.objects.link(source);source.hide_render=True;source.hide_viewport=True
        source['exportedToGameplay']=False;source['owningBone']=bn
        # Meaningful folded edge and central embossed rail, not scattered bolts.
        first,last=Vector(outline[1]),Vector(outline[2]);front=Vector((0,-.025 if i<4 else .025,0))
        f.tube('rolled_shield_top_edge_'+str(i),[first+front,(first+last)*.5+front,last+front],.010,'steel_shadow',bn,12)
        f.tube('shield_embossed_copper_rail_'+str(i),[Vector(outline[0])+front,Vector(outline[1])+front],.007,'copper',bn,10)
        hinge=(first+last)*.5
        f.tube('functional_shield_hinge_'+str(i),[hinge-Vector((.042,0,0)),hinge+Vector((.042,0,0))],.018,'copper',bn,12)
        panels.append({'bone':bn,'sourceMesh':source.name,'centerMeters':list(center),'parent':'chest'})
    f.socket('attack_origin',tuple(Vector(f.bones['hand_r']['head'])+Vector((0,-.17,0))),'hand_r')
    f.expansion_design=metadata(f.name)
    f.expansion_design['shieldPanels']=panels
    f.expansion_design['shieldPresentation']={'bodyRemainsVisible':True,'fragmentStatic':'iron_shield_fragment',
                                             'fragmentLifetimeSeconds':.50,'maximumTravelTiles':.70}
    art_quality(f,[{'name':'left_eye','bone':'head','positionMeters':[-.073,-.197,1.260],'group':'QA_eyes'},
                   {'name':'right_eye','bone':'head','positionMeters':[.073,-.197,1.260],'group':'QA_eyes'}])
    f.finish(True)
    for panel in panels:
        source=bpy.data.objects[panel['sourceMesh']];transform=source.matrix_world.copy()
        source.parent=f.rig;source.parent_type='BONE';source.parent_bone=panel['bone']
        source.matrix_world=transform
    return proxy(f,(0,0,.69),(1.43,.78,1.39))


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
    """Authored poses sampled at 100 Hz; the simulation retains every timing."""
    c=CONTRACTS[f.name];marker=c['windup']/(c['windup']+c['recover']);fps=100
    durations={'Idle':2.,'Locomotion':c['cycle'],'Attack':c['windup']+c['recover'],
               'Hit':.16,'Death':c['death'],'Deploy':.30,'Status':1.,'Turn':.18,'Acquire':.12}
    result=[];specter=f.name=='aether_reaper';child=f.name=='bone_warden_skeleton';giant=f.name=='iron_giant'
    def smooth(x):x=max(0.,min(1.,x));return x*x*(3-2*x)
    def track(t,keys):
        for (a,x),(b,y) in zip(keys,keys[1:]):
            if t<=b:return x+(y-x)*smooth((t-a)/(b-a))
        return keys[-1][1]
    def move(bn,x=0,y=0,z=0):
        pb=f.rig.pose.bones[bn];pb.location=pb.bone.matrix_local.to_3x3().inverted()@Vector((x,y,z))
    def rotate(bn,x=0,y=0,z=0):f.rig.pose.bones[bn].rotation_euler=(x,y,z)
    def world_rotate(bn,x=0,y=0,z=0):
        pb=f.rig.pose.bones[bn];bpy.context.view_layer.update()
        parent=pb.parent.matrix.to_quaternion()@pb.parent.bone.matrix_local.to_quaternion().inverted() if pb.parent else Quaternion()
        q=Quaternion(Vector((0,0,1)),z)@Quaternion(Vector((0,1,0)),y)@Quaternion(Vector((1,0,0)),x)
        set_world_rotation(pb,q@parent@pb.bone.matrix_local.to_quaternion())
    def feet(phase=None):
        # Plant targets are solved AFTER all pelvis/spine weight shifts.
        stride=c['cycle']*c['speed']/c['scale']
        stance=.18 if child else .38 if giant else .56;half=stride*stance*.5
        for side,offset in (('l',0.),('r',.5)):
            lift=0.;y=0.;u=(phase+offset)%1 if phase is not None else 0.
            if phase is not None:
                if u<stance:y=-half+stride*u
                else:
                    v=(u-stance)/(1-stance);y=half-2*half*smooth(v)
                    lift=(.100 if child else .112 if giant else .100)*math.sin(v*math.pi)**1.25
            target=f.rig.pose.bones['shin_'+side].bone.tail_local+Vector((0,y,lift))
            aim_leg(f,'thigh_'+side,'shin_'+side,'foot_'+side,target,Vector((0,-1,0)))
            if phase is not None and u>=stance:
                v=(u-stance)/(1-stance);foot=f.rig.pose.bones['foot_'+side]
                # Absolute sole frame, retaining the IK counter-rotation of the
                # shin. A parent-relative frame would make the sole dive.
                desired=Quaternion(Vector((1,0,0)),-.24*math.sin(math.pi*v))@foot.bone.matrix_local.to_quaternion()
                set_world_rotation(foot,desired)
    def owning_grips(clip):
        grips=f.expansion_design.get('twoHandGripsMeters')
        if not grips or (f.name=='bone_warden' and clip=='Death'):return
        joint_targets=f.expansion_design.get('twoHandJointTargetsMeters',grips)
        weapon=f.rig.pose.bones['maul_socket' if f.name=='bone_warden' else 'scythe_socket']
        # Solve both hands against the SAME rigid weapon surface. The small
        # correction is evaluated, rather than stretching or detaching a wrist.
        for iteration in range(18):
            changed=False
            for side,grip in joint_targets.items():
                bpy.context.view_layer.update();transform=weapon.matrix@weapon.bone.matrix_local.inverted()
                target=transform@Vector(grip);up=f.rig.pose.bones['upperarm_'+side];fore=f.rig.pose.bones['forearm_'+side]
                shoulder=up.matrix.translation;delta=target-shoulder;reach=up.bone.length+fore.bone.length-.006
                if delta.length>reach:
                    correction=shoulder+delta.normalized()*reach-target
                    base=(weapon.parent.matrix@weapon.parent.bone.matrix_local.inverted())@weapon.bone.matrix_local
                    weapon.location+=base.to_3x3().inverted()@correction;changed=True
            if not changed:break
        bpy.context.view_layer.update();transform=weapon.matrix@weapon.bone.matrix_local.inverted()
        for side in ('r','l'):
            # The forearm approaches the outside palm from the front of the
            # staff. A side-only elbow pole bends the lower hand's cuff back
            # through the handle even when every finger surface is clear.
            up=f.rig.pose.bones['upperarm_'+side];fore=f.rig.pose.bones['forearm_'+side]
            target=transform@Vector(joint_targets[side]);shoulder=up.matrix.translation
            delta=target-shoulder;l1=up.bone.length;l2=fore.bone.length
            distance=max(abs(l1-l2)+.001,min(delta.length,l1+l2-.001))
            direction=delta.normalized();along=(l1*l1-l2*l2+distance*distance)/(2*distance)
            pole=Vector((-1 if side=='l' else 1,-.15,-.25))
            perpendicular=pole-direction*pole.dot(direction)
            if perpendicular.length<.01:perpendicular=Vector((1,0,0)).cross(direction)
            elbow=shoulder+direction*along+perpendicular.normalized()*math.sqrt(max(0,l1*l1-along*along))
            for pb,vector in ((up,elbow-shoulder),(fore,target-elbow)):
                rest=pb.bone.tail_local-pb.bone.head_local
                desired=rest.normalized().rotation_difference(vector.normalized())@pb.bone.matrix_local.to_quaternion()
                set_world_rotation(pb,desired)
            # Palm frames follow the staff so fingers wrap the actual handle.
            hand=f.rig.pose.bones['hand_'+side]
            set_world_rotation(hand,transform.to_quaternion()@hand.bone.matrix_local.to_quaternion())
    for clip,duration in durations.items():
        action=bpy.data.actions.new('AN_'+f.name+'_'+clip);f.rig.animation_data_create();f.rig.animation_data.action=action
        length=round(duration*fps)+1
        for frame in range(1,length+1):
            scene.frame_set(frame);t=(frame-1)/(length-1);clock=t*duration
            for pb in f.rig.pose.bones:
                pb.rotation_mode='XYZ';pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
            if f.name=='bone_warden' and clip!='Death':
                # Captive anatomy remains recessed in the chest. These are
                # cosmetic owning joints, never additional simulation units.
                for j in range(4):
                    restless=t*math.tau*(1+j%2)+j*1.57
                    rotate('soul_seal_'+str(j),.023*math.sin(restless+.5),.025*math.cos(restless),.026*math.sin(restless))
                    move('soul_seal_'+str(j),x=.0035*math.sin(restless),z=.0025*math.cos(restless+.7))
                    rotate('captive_head_'+str(j),.09*math.sin(restless+.5),.16*math.sin(restless+j),.06*math.cos(restless))
                    rotate('captive_arm_'+str(j),.14*math.sin(restless+j*.4),0,.12*math.sin(restless+.8))
            phase=t*math.tau
            if specter:
                # Native owns the bounded flight bob; joints own glide/cloth lag.
                rotate('spine',-.026+.023*math.sin(phase));rotate('head',.010*math.sin(phase+.7),0,-.019*math.sin(phase))
                for j in range(6):
                    lag=phase+j*.55
                    rotate('cloth_'+str(j),.078*math.sin(lag),.045*math.cos(lag),.030*math.sin(lag-.35))
                    rotate('cloth_tip_'+str(j),.120*math.sin(lag-.65),.070*math.cos(lag-.65),.045*math.sin(lag-.8))
                if clip=='Locomotion':
                    rotate('spine',-.090+.015*math.sin(phase));rotate('chest',.034,0,.024*math.sin(phase))
                    move('scythe_socket',z=.013*math.sin(phase-.3));rotate('head',.050,0,-.022*math.sin(phase))
                    for j in range(6):
                        rotate('cloth_'+str(j),.110+.065*math.sin(phase+j*.55),.042*math.cos(phase+j*.55))
                        rotate('cloth_tip_'+str(j),.160+.115*math.sin(phase+j*.55-.8),.055*math.cos(phase+j*.55-.8))
            else:
                move('pelvis',z=-.026 if child else -.055 if giant else -.035)
                if clip=='Locomotion':
                    sway=math.sin(phase);bounce=math.cos(phase*2)
                    move('pelvis',x=(.012 if child else .023 if giant else .024)*sway,
                         z=(-.020+.032*abs(sway)) if child else -.092+.014*bounce if giant else -.060+.014*bounce)
                    rotate('pelvis',0,.025*sway,(.055 if child else .036)*sway)
                    rotate('spine',-.080 if child else -.035 if giant else -.025,0,-.021*sway)
                    rotate('chest',.028 if giant else .018,0,-.042*sway);rotate('head',.016,0,.022*sway)
                    if child or giant:
                        for side,offset in (('l',0.),('r',math.pi)):
                            wave=math.sin(phase+offset)
                            world_rotate('upperarm_'+side,x=(.42 if child else .24)*wave)
                            rotate('forearm_'+side,-.16-.09*max(0.,wave))
                            rotate('hand_'+side,.05*wave)
                    else:world_rotate('maul_socket',x=.045*math.sin(phase-.5),z=.028*sway)
                    feet(t)
                    if giant:
                        for side,offset in (('l',0.),('r',math.pi)):move('leg_piston_'+side,z=.024*math.sin(phase+offset))
                elif clip=='Idle':
                    rotate('spine',.008*math.sin(phase));rotate('chest',-.010*math.sin(phase+.4));rotate('head',0,.014*math.sin(phase))
                    if giant:
                        rotate('forearm_l',-.04);rotate('forearm_r',-.04)
                    elif child:rotate('upperarm_l',.040*math.sin(phase+.8))
            if clip=='Attack':
                # Anticipation takes ~2/3 of windup; acceleration reaches the
                # authoritative contact frame, then recoil and settle remain.
                pre=marker*.66;recoil=min(.96,marker+c['recoil']/duration*.72)
                body=track(t,[(0,0),(pre,-1),(marker,1),(recoil,.35),(1,0)])
                drive=track(t,[(0,0),(pre,1),(marker,-1),(recoil,-.30),(1,0)])
                if f.name=='bone_warden':
                    move('pelvis',x=-.034*drive,y=.024*drive,z=-.075-.026*abs(body))
                    rotate('pelvis',0,0,.085*drive);rotate('spine',.065*body,0,-.10*drive)
                    rotate('chest',.11*body,0,-.13*drive);rotate('head',-.055*body,0,.075*drive)
                    world_rotate('maul_socket',x=-.70*drive,z=.17*drive)
                    move('maul_socket',x=.040*drive,y=.030*drive,z=.080*max(0.,drive))
                elif specter:
                    move('flight_pivot',y=.032*drive,z=.018*drive)
                    rotate('spine',.042*body,0,.13*drive);rotate('chest',-.030*body,0,.23*drive)
                    rotate('head',0,0,-.19*drive);world_rotate('scythe_socket',x=-.090*drive,z=.78*drive)
                    for j in range(6):
                        delayed=track(max(0,t-.065),[(0,0),(pre,1),(marker,-1),(recoil,-.30),(1,0)])
                        rotate('cloth_'+str(j),.065*body,.04*math.sin(j),-.15*delayed)
                        rotate('cloth_tip_'+str(j),.12*body,.04*math.cos(j),-.26*delayed)
                elif giant:
                    move('pelvis',x=-.026*drive,y=.028*drive,z=-.094-.014*abs(body))
                    rotate('pelvis',0,0,-.065*drive);rotate('spine',.040*body,0,.12*drive)
                    rotate('chest',.055*body,0,.20*drive);rotate('head',-.032*body,0,-.08*drive)
                    world_rotate('upperarm_r',x=.90*drive,z=-.10*drive)
                    rotate('forearm_r',-.54*max(0.,drive)+.22*max(0.,-drive))
                    rotate('upperarm_l',-.12*drive);rotate('forearm_l',-.14)
                else:
                    move('pelvis',x=-.017*drive,y=.024*drive,z=-.047)
                    rotate('pelvis',0,0,-.16*drive);rotate('spine',.04*body,0,.18*drive)
                    rotate('chest',.040*body,0,.19*drive);rotate('head',0,0,-.13*drive)
                    world_rotate('upperarm_r',x=.36*drive,z=-.62*drive);rotate('forearm_r',-.34*max(0.,drive)+.16*max(0.,-drive))
                    rotate('upperarm_l',-.18*drive);rotate('forearm_l',-.20)
            elif clip=='Death':
                buckle=smooth(t/.46);fall=smooth((t-.25)/.60);settle=math.sin(max(0.,t-.75)*math.pi*4)*(1-smooth((t-.75)/.25))
                if specter:
                    move('flight_pivot',z=-.22*buckle)
                    rotate('spine',.22*fall,0,-.13*fall);rotate('chest',.26*fall);rotate('head',.19*fall)
                    world_rotate('scythe_socket',x=.44*fall,z=.20*fall)
                    for j in range(6):
                        rotate('cloth_'+str(j),.22*buckle,.12*math.sin(j)*fall,.13*fall)
                        rotate('cloth_tip_'+str(j),-.24*fall,.07*math.cos(j)*fall,.19*fall)
                else:
                    move('pelvis',x=(.040 if giant else -.024)*fall,y=.03*fall,z=-(.200 if child else .300 if giant else .500)*buckle)
                    rotate('pelvis',.14*fall,0,.06*fall);rotate('spine',.19*fall,0,-.06*fall)
                    rotate('chest',.30*fall+.012*settle);rotate('head',.19*fall,0,.12*fall)
                    if f.name=='bone_warden':
                        move('maul_socket',x=.16*fall,z=-.11*fall);world_rotate('maul_socket',x=.72*fall,z=.21*fall)
                        rotate('upperarm_l',.28*fall,0,.16*fall);rotate('upperarm_r',.41*fall,0,-.12*fall)
                        rotate('forearm_l',.17*fall);rotate('forearm_r',.15*fall)
                        for j in range(4):move('soul_seal_'+str(j),x=(j%2*2-1)*.080*smooth((t-.32)/.45),z=.13*smooth((t-.32)/.45))
                    elif child:
                        rotate('upperarm_r',.75*fall,0,-.23*fall);rotate('upperarm_l',.44*fall,0,.21*fall);rotate('jaw',.15*fall)
                        # The brief skull/rib separation is authored on their
                        # existing owning joints, then native dust/cleanup ends
                        # the .45-second visual. Nothing spawns or collides.
                        fracture=smooth((t-.40)/.42)
                        move('chest',x=-.022*fracture,z=.035*fracture)
                        move('head',x=.060*fracture,y=.018*fracture,z=.090*fracture)
                    else:
                        rotate('upperarm_l',.35*fall);rotate('upperarm_r',.42*fall);rotate('forearm_l',.16*fall);rotate('forearm_r',.17*fall)
            elif clip=='Deploy':
                settle=math.sin(math.pi*t)*(1-t)
                if specter:
                    move('flight_pivot',z=-.085*(1-smooth(t)));rotate('chest',-.09*(1-smooth(t)))
                else:
                    move('pelvis',z=-(.047 if child else .110 if giant else .092)*settle-.025)
                    rotate('spine',.065*settle);rotate('head',-.08*settle)
                    if giant:rotate('upperarm_l',.12*settle);rotate('upperarm_r',.12*settle)
            elif clip=='Hit':
                recoil=track(t,[(0,0),(.22,1),(.60,.36),(1,0)])
                rotate('chest',-.115*recoil,0,.035*recoil);rotate('head',.08*recoil)
                if not specter:move('pelvis',y=.024*recoil,z=-.040)
            elif clip=='Turn':
                look=math.sin(math.pi*t)
                rotate('head',0,0,.23*look);rotate('chest',0,0,.13*look)
                if not specter:rotate('pelvis',0,0,.065*look)
            elif clip=='Acquire':
                focus=math.sin(math.pi*t)
                rotate('head',-.040*focus,0,.18*focus);rotate('chest',-.024*focus,0,.075*focus)
            elif clip=='Status':
                pulse=math.sin(phase*2)
                rotate('head',.015*pulse);rotate('chest',.012*pulse)
            if not specter and clip!='Locomotion':feet()
            owning_grips(clip)
            for pb in f.rig.pose.bones:
                for channel in ('rotation_euler','location','scale'):pb.keyframe_insert(channel,frame=frame,group=pb.name)
        curves=list(action.fcurves) if hasattr(action,'fcurves') else [c for layer in action.layers for strip in layer.strips for bag in strip.channelbags for c in bag.fcurves]
        for curve in curves:
            for point in curve.keyframe_points:point.interpolation='LINEAR'
        action.use_fake_user=True
        result.append({'name':clip,'action':action.name,'frames':length,'fps':fps,'seconds':(length-1)/fps,
                       'releaseNormalized':marker if clip=='Attack' else None,'loop':clip in ('Idle','Locomotion','Status'),
                       'strideMeters':c['cycle']*c['speed']/c['scale'] if clip=='Locomotion' else None,'object':action})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    return result
