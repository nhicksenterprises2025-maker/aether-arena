"""Locked expansion designs, authored in the existing Blender production scene.

Only selected models use these builders. Gameplay collision and timing remain
owned by the simulation; this module authors in-place visual poses and sockets.
"""
import math
import bpy
from mathutils import Vector, Quaternion


CONTRACTS = {
    'mini_stampede': {'scale': 1.30, 'windup': .14, 'recoil': .18,
                     'recover': .30, 'cycle': .42, 'speed': 3.50, 'death': .65},
    'stampede': {'scale': .85, 'windup': .16, 'recoil': .20,
                'recover': .36, 'cycle': .46, 'speed': 3.00, 'death': .60},
    'pulverizer': {'scale': .95, 'windup': .20, 'recoil': .18,
                  'recover': .18, 'cycle': None, 'speed': 0., 'death': .85},
}

# Hidden death debris must stay invertible when FBX evaluates bind transforms.
MIN_DEBRIS_SCALE = .001


def design_metadata(name):
    c = CONTRACTS[name]
    stride = c['cycle'] * c['speed'] / c['scale'] if c['cycle'] else None
    marker = c['windup'] / (c['windup'] + c['recover'])
    return {
        'revision': 'balance-expansion-1', 'detailPasses': 1,
        'baseModelScale': c['scale'], 'attackContactNormalized': marker,
        'animationContract': {
            'schema': 1,
            'attack': {'contactNormalized': marker,
                       'anticipationSeconds': c['windup'],
                       'recoilSeconds': c['recoil'],
                       'recoverySeconds': c['recover']},
            'locomotion': {'referenceCycleSeconds': c['cycle'],
                           'referenceSpeedTilesPerSecond': c['speed'],
                           'strideMeters': stride, 'phaseOffsets': True},
            'death': {'cleanupSeconds': c['death']},
            'forward': {'blender': '-Y', 'unreal': '+X'},
            'visual': {'normalizedBodyMeters': [ .48, .95, .52 ]
                       if name != 'pulverizer' else [1.65, 1.65, 1.30],
                       'baseModelScale': c['scale'], 'readabilityMultiplier': 1.0},
        },
        'architecture': 'Basalt dais, four bronze braces and suspension struts, counter-rotating steel compression ring, suspended violet crystal'
                        if name == 'pulverizer' else None,
        'species': 'Four-legged slate boarlet with flat copper snout guard, two ivory tusks and teal rope harness'
                   if name != 'pulverizer' else None,
        'variant': 'Red-orange pennant knots; same boarlet anatomy' if name == 'stampede'
                   else 'Amber dorsal bristles and plain teal harness' if name == 'mini_stampede'
                   else 'Ground-pulse compression machine',
        'collisionIndependent': True, 'rootMotion': False,
        'attachmentPolicy': 'Each detail is weighted to its owning bone; no shared material mutation',
    }


def source_collision_proxy(f):
    """Editable simple proxy, deliberately excluded from gameplay FBX exports."""
    center=(0,0,.635) if f.family=='mechanism' else (0,-.02,.27)
    size=(1.65,1.65,1.27) if f.family=='mechanism' else (.46,.84,.48)
    bpy.ops.mesh.primitive_cube_add(size=1,location=center,scale=size)
    proxy=bpy.context.object;proxy.name='PROXY_'+f.name
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    for collection in list(proxy.users_collection):collection.objects.unlink(proxy)
    f.collection.objects.link(proxy);proxy.hide_render=True;proxy.display_type='WIRE'
    proxy['purpose']='Editable presentation proxy; simulation collision remains independent'
    proxy['exportedToGameplay']=False
    f.expansion_design['sourceCollisionProxy']=proxy.name
    return f


def boarlet(Forge, name):
    f = Forge(name); f.family = 'quadruped'
    f.bone('root', (0,0,0), (0,0,.06))
    f.bone('pelvis', (0,.21,.29), (0,.02,.33), 'root')
    f.bone('spine', (0,.02,.33), (0,-.20,.36), 'pelvis')
    f.bone('neck', (0,-.20,.36), (0,-.34,.39), 'spine')
    f.bone('head', (0,-.34,.39), (0,-.48,.35), 'neck')
    f.bone('jaw', (0,-.43,.29), (0,-.60,.27), 'head')
    f.bone('tail', (0,.42,.33), (0,.55,.36), 'pelvis')
    for s, side in ((-1,'l'), (1,'r')):
        f.bone('ear_'+side, (s*.12,-.32,.49), (s*.15,-.31,.59), 'head')
        for y, leg in ((-.19,'front'), (.22,'back')):
            bn = leg+'_'+side
            f.bone(bn+'_upper', (s*.15,y,.30), (s*.165,y+.02,.17),
                   'spine' if leg == 'front' else 'pelvis')
            f.bone(bn+'_lower', (s*.165,y+.02,.17), (s*.17,y-.01,.04), bn+'_upper')
            f.bone(bn+'_foot', (s*.17,y-.01,.04), (s*.17,y-.07,.035), bn+'_lower')
            f.socket(bn+'_contact', (s*.17,y-.035,.006), bn+'_foot')
            f.tube('upper_leg_'+bn, [(s*.15,y,.30),(s*.165,y+.02,.17)],
                   [.07,.043], 'fur', bn+'_upper', 12)
            f.tube('lower_leg_'+bn, [(s*.165,y+.02,.17),(s*.17,y-.01,.04)],
                   [.044,.032], 'fur_dark', bn+'_lower', 12)
            for toe in (-1,1):
                f.ellipsoid('cloven_hoof_'+bn, (s*.17+toe*.020,y-.035,.027),
                            (.017,.053,.027), 'black', bn+'_foot', 16)
    f.ellipsoid('tapered_barrel', (0,.035,.325), (.24,.34,.176), 'fur', 'spine', 28)
    f.ellipsoid('broad_shoulders', (0,-.18,.34), (.235,.235,.183), 'fur', 'spine', 28)
    f.ellipsoid('rounded_hindquarters', (0,.27,.30), (.21,.205,.165), 'fur', 'pelvis', 24)
    f.ellipsoid('short_neck', (0,-.30,.36), (.15,.15,.145), 'fur', 'neck', 24)
    f.ellipsoid('flat_wedge_head', (0,-.39,.37), (.155,.18,.136), 'fur', 'head', 28)
    f.ellipsoid('broad_short_snout', (0,-.52,.29), (.13,.12,.089), 'fur_dark', 'jaw', 24)
    # A flat solid plate with two recessed slots, not a ram muzzle or horns.
    f.panel('copper_snout_guard', (0,-.623,.296), (.245,.025,.125), 'copper', 'jaw', bevel=.014)
    for s, side in ((-1,'l'), (1,'r')):
        f.panel('nostril_slot_'+side, (s*.051,-.639,.311), (.042,.003,.012), 'black', 'jaw', bevel=.005)
        for z in (.258,.339):
            f.rivet((s*.097,-.640,z), 'jaw', 'brass', .008)
        f.tube('outward_ivory_tusk_'+side,
               [(s*.113,-.535,.247),(s*.163,-.583,.242),
                (s*.190,-.609,.283),(s*.191,-.609,.348)],
               [.021,.019,.012,.001], 'ivory', 'jaw', 14)
        f.ellipsoid('eye_socket_'+side, (s*.127,-.449,.426), (.032,.025,.028), 'fur_dark', 'head', 16)
        f.ellipsoid('eye_'+side, (s*.145,-.469,.431), (.016,.011,.015), 'ivory', 'head', 16)
        f.ellipsoid('pupil_'+side, (s*.151,-.478,.431), (.009,.0035,.010), 'black', 'head', 12)
        f.tube('brow_'+side, [(s*.102,-.453,.451),(s*.145,-.462,.463),(s*.172,-.429,.449)],
               [.018,.023,.008], 'fur_dark', 'head', 10)
        f.tube('upright_ear_'+side, [(s*.12,-.32,.49),(s*.153,-.31,.583),(s*.163,-.354,.569)],
               [.047,.031,.002], 'fur', 'ear_'+side, 12)
        f.tube('ear_inner_'+side, [(s*.134,-.344,.515),(s*.155,-.335,.564)],
               [.016,.001], 'skin_shade', 'ear_'+side, 8)
        # Separate fitted shoulder straps avoid cutting through the snout or legs.
        f.tube('teal_shoulder_rope_'+side,
               [(s*.08,-.12,.509),(s*.22,-.12,.425),(s*.232,-.12,.289),(s*.15,-.12,.198)],
               [.010,.013,.013,.010], 'teal', 'spine', 10)
        f.tube('teal_rear_rope_'+side,
               [(s*.05,.29,.461),(s*.17,.31,.407),(s*.20,.31,.30)],
               [.011,.012,.010], 'teal', 'pelvis', 10)
    f.tube('harness_back_tie', [(-.065,.30,.464),(0,.30,.482),(.065,.30,.464)], .011, 'teal', 'pelvis', 10)
    f.ellipsoid('rear_harness_knot', (0,.313,.479), (.026,.020,.018), 'teal', 'pelvis', 16)
    for j in range(8):
        y = -.17 + j*.071; z = .489-.046*(j/7)**2
        f.tube('amber_bristle_'+str(j), [(0,y,z),(0,y+.014,z+.038),(0,y+.031,z+.047)],
               [.022,.014,.001], 'fur_cream', 'spine' if j<5 else 'pelvis', 8)
    f.tube('small_curled_tail', [(0,.43,.335),(.015,.50,.355),(.035,.542,.374),
                               (.020,.563,.387),(-.003,.55,.389)],
           [.016,.012,.009,.005,.001], 'fur', 'tail', 10)
    if name == 'stampede':
        for s, side in ((-1,'l'), (1,'r')):
            f.ellipsoid('pennant_knot_'+side, (s*.17,.285,.425), (.021,.020,.018), 'cloth_ember', 'pelvis', 12)
            f.mesh_data('red_orange_pennant_'+side,
                        [(s*.174,.283,.418),(s*.175,.42,.409),(s*.174,.382,.359),(s*.174,.278,.386)],
                        [(0,1,2,3),(3,2,1,0)], 'cloth_ember', 'pelvis', smooth=False)
            f.tube('pennant_trim_'+side, [(s*.177,.282,.416),(s*.178,.414,.407)],
                   .004, 'copper', 'pelvis', 8)
    f.socket('attack_origin', (0,-.64,.296), 'jaw')
    f.socket('impact_origin', (0,-.20,.33), 'spine')
    f.socket('hp_anchor', (0,0,.65), 'spine')
    f.socket('status_anchor', (0,-.14,.69), 'neck')
    f.expansion_design = design_metadata(name)
    return source_collision_proxy(f.finish(True))


def pulse_machine(Forge):
    f = Forge('pulverizer'); f.family = 'mechanism'
    f.bone('root', (0,0,0), (0,0,.08))
    f.bone('hammer', (0,0,1.01), (0,0,1.27), 'root')
    f.bone('compression_ring', (0,0,.987), (0,0,1.09), 'hammer')
    f.bone('crystal', (0,0,.60), (0,0,.91), 'root')
    f.bone('crystal_fractures', (0,0,.60), (0,0,.66), 'crystal')
    for s in (-1,1):
        f.bone('ring_fragment_'+str(s), (s*.57,0,.36), (s*.57,0,.42), 'root')
    for i,(x,y) in enumerate(((-.53,-.53),(.53,-.53),(.53,.53),(-.53,.53))):
        f.bone('piston_'+str(i), (x,y,.65), (x,y,1.015), 'hammer')
    for name,z in (('attack_origin',.08),('impact_origin',.08),('hp_anchor',1.40),('status_anchor',1.46)):
        f.socket(name, (0,0,z), 'root')
    def plinth(name, rings, key):
        vertices=[]
        for z,size in rings:
            cut=size*.24
            vertices.extend((x,y,z) for x,y in ((-size+cut,-size),(size-cut,-size),(size,-size+cut),
                             (size,size-cut),(size-cut,size),(-size+cut,size),(-size,size-cut),(-size,-size+cut)))
        faces=[tuple(reversed(range(8))),tuple((len(rings)-1)*8+i for i in range(8))]
        for row in range(len(rings)-1):
            for i in range(8):
                faces.append((row*8+i,row*8+(i+1)%8,(row+1)*8+(i+1)%8,(row+1)*8+i))
        f.mesh_data(name,vertices,faces,key,'root',smooth=False)
    plinth('dark_basalt_foundation',[(0,.76),(.035,.825),(.17,.825),(.235,.755)],'stone_dark')
    plinth('bevelled_basalt_cap',[(.235,.755),(.27,.77),(.315,.69)],'stone')
    f.tube('recessed_impact_plate',[(0,0,.312),(0,0,.337)],[.54,.54],'steel_shadow','root',32)
    for z,r,key in ((.323,.565,'steel_edge'),(.341,.45,'brass'),(.349,.37,'violet')):
        f.ring('concentric_dais_ring',(0,0,z),r,.018,key,'root',glow=key=='violet')
    for i,(x,y) in enumerate(((-.63,-.63),(.63,-.63),(.63,.63),(-.63,.63))):
        f.panel('bronze_corner_brace_'+str(i),(x,y,.31),(.21,.21,.20),'brass','root',bevel=.023)
        f.panel('corner_anchor_foot_'+str(i),(x,y,.21),(.235,.235,.048),'steel_shadow','root',bevel=.013)
        for dx,dy in ((-.058,-.058),(.058,.058)):
            f.rivet((x+dx,y+dy,.417),'root','copper',.014)
    for i,(x,y) in enumerate(((-.53,-.53),(.53,-.53),(.53,.53),(-.53,.53))):
        bn='piston_'+str(i)
        f.tube('fixed_strut_housing_'+str(i),[(x,y,.335),(x,y,.67)],[.060,.060],'steel','root',16)
        f.ring('strut_gland_'+str(i),(x,y,.666),.064,.013,'brass','root')
        f.tube('compressing_suspension_strut_'+str(i),[(x,y,.625),(x,y,1.02)],[.027,.027],'silver',bn,14)
        f.ring('upper_strut_coupler_'+str(i),(x,y,1.01),.041,.012,'copper',bn)
        f.panel('fitted_compression_corner_arm_'+str(i),(x*.83,y*.83,1.025),
                (.28,.075,.045),'steel_shadow','hammer',rotation=(0,0,math.atan2(y,x)),bevel=.012)
    f.tube('piston_compression_head',[(0,0,.998),(0,0,1.11)],[.58,.58],'steel_shadow','hammer',32)
    f.ring('counter_rotating_top_ring',(0,0,1.055),.602,.038,'steel_edge','compression_ring')
    for j in range(12):
        a=j*math.tau/12
        f.panel('compression_ring_index_'+str(j),(.602*math.cos(a),.602*math.sin(a),1.055),
                (.026,.050,.032),'brass','compression_ring',rotation=(0,0,a),bevel=.006)
    f.tube('low_piston_cap',[(0,0,1.11),(0,0,1.20),(0,0,1.25)],[.45,.45,.37],'stone','hammer',12)
    f.ring('cap_seam',(0,0,1.251),.24,.015,'violet','hammer',glow=True)
    # An annular striking shoe lands on the plate at the release marker. Its
    # open center leaves the suspended crystal visible and avoids intersections.
    f.ring('annular_ground_striker',(0,0,.543),.42,.046,'steel_edge','hammer')
    for j in range(4):
        a=j*math.tau/4+math.pi/4;x,y=.42*math.cos(a),.42*math.sin(a)
        f.tube('piston_shoe_support_'+str(j),[(x,y,.543),(x,y,1.014)],.021,'steel','hammer',12)
    f.tube('crystal_cage_top',[(0,0,.802),(0,0,.823)],[.18,.18],'brass','root',20)
    f.tube('suspended_violet_crystal',[(0,0,.423),(0,0,.527),(0,0,.675),(0,0,.793)],
           [.027,.145,.13,.026],'violet','crystal',8,True)
    for s in (-1,1):
        f.tube('suspended_crystal_chain_'+str(s),[(s*.12,0,.806),(s*.105,0,.753)],.012,'copper','root',10)
    # Three narrow fissures are fitted to the crystal; death lights them briefly.
    for j in range(3):
        a=j*math.tau/3
        points=[(.143*math.cos(a),.143*math.sin(a),.535),(.135*math.cos(a+.10),.135*math.sin(a+.10),.60),
                (.119*math.cos(a-.07),.119*math.sin(a-.07),.685)]
        f.tube('crystal_fracture_'+str(j),points,.004,'violet','crystal_fractures',6,True)
    for s in (-1,1):
        points=[]
        for j in range(7):
            a=(0 if s==1 else math.pi)+(j-3)*.055
            points.append((.57*math.cos(a),.57*math.sin(a),.36))
        f.tube('bounded_ring_death_fragment_'+str(s),points,.018,'steel_edge','ring_fragment_'+str(s),8)
    for s in (-1,1):
        f.panel('team_base_inlay_'+str(s),(s*.50,-.757,.20),(.23,.008,.046),'team','root',bevel=.004)
    f.expansion_design=design_metadata('pulverizer')
    return source_collision_proxy(f.finish(True))


def actions(f, scene, set_world_rotation, aim_leg, only=None):
    """Separate in-place anatomy/mechanism poses; one explicit contact marker."""
    c=CONTRACTS[f.name]; marker=c['windup']/(c['windup']+c['recover'])
    # Dense authoring samples make short contact envelopes stable after import.
    fps=100
    durations={'Idle':2.,'Locomotion':c['cycle'] or 2.,
               'Attack':c['windup']+c['recover'],'Hit':.16,
               'Death':c['death'],'Deploy':.30,'Status':1.,'Turn':.18,'Acquire':.12}
    if only is not None:
        unknown = set(only) - durations.keys()
        if unknown:
            raise ValueError('Unknown expansion animation clips: ' + ', '.join(sorted(unknown)))
        durations = {name: duration for name, duration in durations.items() if name in only}
    made=[]
    def smooth(x):
        x=max(0.,min(1.,x)); return x*x*(3-2*x)
    def rotate(name,x=0.,y=0.,z=0.):
        f.rig.pose.bones[name].rotation_euler=(x,y,z)
    def move(name,x=0.,y=0.,z=0.):
        pb=f.rig.pose.bones[name]
        pb.location=pb.bone.matrix_local.to_3x3().inverted()@Vector((x,y,z))
    for name,duration in durations.items():
        length=round(duration*fps)+1
        act=bpy.data.actions.new('AN_'+f.name+'_'+name)
        f.rig.animation_data_create();f.rig.animation_data.action=act
        for frame in range(1,length+1):
            scene.frame_set(frame); t=(frame-1)/(length-1); clock=t*duration
            for pb in f.rig.pose.bones:
                pb.rotation_mode='XYZ';pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
            if f.family=='mechanism':
                stroke=.005*math.sin(t*math.tau)
                rotate('crystal',0,0,.035*math.sin(t*math.tau))
                f.rig.pose.bones['crystal_fractures'].scale=(MIN_DEBRIS_SCALE,)*3
                for s in (-1,1):f.rig.pose.bones['ring_fragment_'+str(s)].scale=(MIN_DEBRIS_SCALE,)*3
                if name=='Attack':
                    if t<=marker:
                        stroke=.06*smooth(t/marker)
                        stroke-=.22*smooth((t/marker-.60)/.40)
                    else:stroke=-.16*(1-smooth((t-marker)/(1-marker)))
                    rotate('compression_ring',0,0,-.16*math.sin(t*math.pi))
                    rotate('crystal',0,0,.06*math.sin(t*math.pi))
                elif name=='Deploy':stroke=-.15*(1-smooth(t/.80))
                elif name=='Death':
                    stroke=-.24*smooth(t/.55)
                    f.rig.pose.bones['crystal'].scale=(1-.65*smooth((t-.25)/.75),)*3
                    f.rig.pose.bones['crystal_fractures'].scale=(max(MIN_DEBRIS_SCALE, smooth(t/.25)),)*3
                    for s in (-1,1):
                        scale=max(MIN_DEBRIS_SCALE, math.sin(smooth((t-.35)/.65)*math.pi))
                        f.rig.pose.bones['ring_fragment_'+str(s)].scale=(scale,)*3
                        move('ring_fragment_'+str(s),x=s*.14*smooth((t-.35)/.65),z=-.24*smooth((t-.35)/.65))
                        rotate('ring_fragment_'+str(s),.35*smooth(t),s*.22*smooth(t),s*.32*smooth(t))
                    rotate('compression_ring',.12*smooth((t-.55)/.45),0,.18*smooth(t))
                elif name=='Hit':stroke=-.028*math.sin(t*math.pi)
                move('hammer',z=stroke)
            else:
                rotate('ear_l',0,.024*math.sin(t*math.tau),.025*math.sin(t*math.tau))
                rotate('ear_r',0,-.024*math.sin(t*math.tau),-.025*math.sin(t*math.tau))
                rotate('tail',0,.09*math.sin(t*math.tau))
                if name=='Locomotion':
                    # Gather both forelegs, then drive both hindlegs: a gallop,
                    # rather than the previous diagonal walking/trotting pairs.
                    move('pelvis',z=-.022+.012*math.sin(t*math.tau))
                    move('spine',z=.009*math.sin(t*math.tau+.30))
                    rotate('spine',.045*math.sin(t*math.tau))
                    rotate('neck',-.04*math.sin(t*math.tau-.4))
                    stride=design_metadata(f.name)['animationContract']['locomotion']['strideMeters']
                    # Feet plant for a short compact stance; the visual stride
                    # metadata owns cadence while limbs remain anatomically bounded.
                    for leg,side,offset in (('front','l',0.),('front','r',.05),('back','l',.49),('back','r',.54)):
                        u=(t+offset)%1.;step=.18;stance=step/stride
                        if u<stance:y=-step/2+stride*u;lift=0.
                        else:
                            v=(u-stance)/(1-stance);y=step/2-step*smooth(v);lift=.074*math.sin(math.pi*v)
                        bn=leg+'_'+side
                        target=f.rig.pose.bones[bn+'_lower'].bone.tail_local+Vector((0,y,lift))
                        aim_leg(f,bn+'_upper',bn+'_lower',bn+'_foot',target,Vector((0,1 if leg=='front' else -1,0)))
                elif name=='Attack':
                    if t<=marker:
                        strength=smooth(t/marker)
                        rotate('neck',.30*strength);rotate('head',.17*strength)
                        move('spine',z=-.017*strength)
                    else:
                        age=(t-marker)*duration
                        settle=smooth(age/c['recover'])
                        rotate('neck',.30*(1-settle)-.075*math.sin(min(1,age/c['recoil'])*math.pi))
                        rotate('head',.17*(1-settle));move('spine',z=-.017*(1-settle))
                elif name=='Death':
                    fold=smooth(t/.60)
                    for leg in ('front','back'):
                        for side in ('l','r'):
                            rotate(leg+'_'+side+'_upper',-.52*fold)
                            rotate(leg+'_'+side+'_lower',.72*fold)
                    move('pelvis',z=-.15*fold)
                    rotate('pelvis',.12*fold,.65*fold,.08*fold)
                    rotate('neck',.20*fold);rotate('head',.18*fold)
                elif name=='Deploy':
                    move('pelvis',z=.045*(1-smooth(t/.70)))
                    rotate('neck',.08*(1-smooth(t)))
                elif name=='Hit':rotate('spine',-.06*math.sin(t*math.pi))
                elif name=='Turn':rotate('spine',0,0,.10*math.sin(t*math.pi))
                elif name=='Acquire':rotate('head',0,0,.10*math.sin(t*math.pi))
                elif name=='Status':rotate('head',.025*math.sin(t*math.tau*3))
                else:rotate('neck',.012*math.sin(t*math.tau));move('spine',z=.004*math.sin(t*math.tau))
            for pb in f.rig.pose.bones:
                pb.keyframe_insert('rotation_euler',frame=frame,group=pb.name)
                pb.keyframe_insert('location',frame=frame,group=pb.name)
                pb.keyframe_insert('scale',frame=frame,group=pb.name)
        if f.family == 'mechanism' and name == 'Death':
            curves = list(act.fcurves) if hasattr(act, 'fcurves') else [
                curve for layer in act.layers for strip in layer.strips
                for bag in strip.channelbags for curve in bag.fcurves]
            debris = {'crystal_fractures', 'ring_fragment_-1', 'ring_fragment_1'}
            for curve in curves:
                if any(curve.data_path == 'pose.bones["' + bone + '"].scale' for bone in debris):
                    # Linear dense scale keys cannot dip through zero between
                    # samples, unlike unconstrained cubic handles at visibility edges.
                    for point in curve.keyframe_points:
                        point.interpolation = 'LINEAR'
        act.use_fake_user=True
        made.append({'name':name,'action':act.name,'frames':length,'fps':fps,
                     'seconds':(length-1)/fps,'releaseNormalized':marker if name=='Attack' else None,
                     'loop':name in ('Idle','Locomotion','Status'),
                     'strideMeters':design_metadata(f.name)['animationContract']['locomotion']['strideMeters'] if name=='Locomotion' else None,
                     'object':act})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    return made
