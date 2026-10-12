"""Locked expansion designs, authored in the existing Blender production scene.

Only selected models use these builders. Gameplay collision and timing remain
owned by the simulation; this module authors in-place visual poses and sockets.
"""
import math
import json
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
        'revision': 'balance-expansion-crafted-2', 'detailPasses': 2,
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
        'species': 'Four-legged slate boarlet with fitted curved copper snout guard, two ivory tusks and braided teal rope harness'
                   if name != 'pulverizer' else None,
        'variant': 'Red-orange pennant knots; same boarlet anatomy' if name == 'stampede'
                   else 'Amber dorsal bristles and plain teal harness' if name == 'mini_stampede'
                   else 'Ground-pulse compression machine',
        'collisionIndependent': True, 'rootMotion': False,
        'attachmentPolicy': 'Each detail is weighted to its owning bone; no shared material mutation',
    }


def semantic(f, obj, name, bones, indices=None):
    """Keep a measurable part identity after Forge joins the source meshes."""
    group = obj.vertex_groups.get('art_'+name) or obj.vertex_groups.new(name='art_'+name)
    indices=list(range(len(obj.data.vertices))) if indices is None else list(indices)
    group.add(indices, 1., 'REPLACE')
    points = [obj.matrix_world @ obj.data.vertices[index].co for index in indices]
    f.art_regions[name] = {'vertexGroup': group.name, 'owningBones':
                          [bones] if isinstance(bones, str) else list(bones),
                          'restBoundsMeters': {'min': [min(p[a] for p in points) for a in range(3)],
                                               'max': [max(p[a] for p in points) for a in range(3)]}}
    return obj


def loft_y(f, name, rings, key, bone, sides=32):
    """One continuous, tapered anatomical shell, with explicit cross sections."""
    vertices=[]
    for y, width, center, top, bottom in rings:
        for j in range(sides):
            angle=j*math.tau/sides; wave=math.sin(angle)
            vertices.append((width*math.cos(angle), y,
                             center + wave*(top if wave >= 0 else bottom)))
    faces=[tuple(range(sides)),tuple((len(rings)-1)*sides+j for j in reversed(range(sides)))]
    for row in range(len(rings)-1):
        for j in range(sides):
            faces.append((row*sides+j,(row+1)*sides+j,
                          (row+1)*sides+(j+1)%sides,row*sides+(j+1)%sides))
    obj=f.mesh_data(name,vertices,faces,key,bone)
    if bone is None:
        # Smooth anatomical bending at the waist; equipment retains rigid owners.
        pelvis=obj.vertex_groups.new(name='pelvis'); spine=obj.vertex_groups.new(name='spine');neck=obj.vertex_groups.new(name='neck');head=obj.vertex_groups.new(name='head')
        for index,vertex in enumerate(vertices):
            weight=max(0.,min(1.,(vertex[1]-.06)/.24))
            neck_weight=max(0.,min(1.,(-vertex[1]-.24)/.13))
            head_weight=max(0.,min(1.,(-vertex[1]-.32)/.11))
            pelvis.add([index],weight,'REPLACE');spine.add([index],(1-weight)*(1-neck_weight)*(1-head_weight),'REPLACE')
            neck.add([index],(1-weight)*neck_weight*(1-head_weight),'REPLACE');head.add([index],head_weight,'REPLACE')
    modifier=obj.modifiers.new('Sculpted anatomical surface refinement','SUBSURF')
    modifier.levels=1;modifier.render_levels=1
    bpy.context.view_layer.objects.active=obj;bpy.ops.object.modifier_apply(modifier=modifier.name)
    return semantic(f,obj,name,bone or ('head','neck','spine','pelvis'))


def arc_band(f, name, center, radius, width, height, start, end, key, bone, segments=12):
    """A chamfered curved plate rather than a torus or rectangular slab."""
    vertices=[]
    # Bevelled rectangular cross-section, eight corners around each arc sample.
    bevel=min(width,height)*.18
    profile=[(-width/2+bevel,-height/2),(width/2-bevel,-height/2),
             (width/2,-height/2+bevel),(width/2,height/2-bevel),
             (width/2-bevel,height/2),(-width/2+bevel,height/2),
             (-width/2,height/2-bevel),(-width/2,-height/2+bevel)]
    for j in range(segments+1):
        a=start+(end-start)*j/segments
        vertices.extend((center[0]+(radius+r)*math.cos(a),center[1]+(radius+r)*math.sin(a),center[2]+z)
                        for r,z in profile)
    faces=[tuple(range(8)),tuple(segments*8+j for j in reversed(range(8)))]
    for row in range(segments):
        for j in range(8):faces.append((row*8+j,(row+1)*8+j,(row+1)*8+(j+1)%8,row*8+(j+1)%8))
    return semantic(f,f.mesh_data(name,vertices,faces,key,bone,smooth=False),name,bone)


def braided_rope(f, name, points, bone, radius=.009):
    path=[Vector(point) for point in points]
    # Three independently visible strands follow the same fitted harness path.
    for strand in range(3):
        samples=[]
        for section in range(len(path)-1):
            tangent=(path[section+1]-path[section]).normalized()
            across=tangent.cross(Vector((0,0,1)))
            if across.length<.01:across=tangent.cross(Vector((0,1,0)))
            across.normalize();other=tangent.cross(across).normalized()
            for j in range(9):
                t=j/9;a=(section+t)*math.tau*2+strand*math.tau/3
                samples.append(path[section].lerp(path[section+1],t)+
                               radius*.55*(across*math.cos(a)+other*math.sin(a)))
        samples.append(path[-1])
        semantic(f,f.tube(name+'_strand_'+str(strand),samples,radius*.55,'teal',bone,6),
                 name+'_strand_'+str(strand),bone)


def bored_housing(f, name, center, profile, bore, bone, sides=24):
    """An actual open guide sleeve with a separate inner cylindrical wall."""
    vertices=[];faces=[]
    for z,radius in profile:
        for r in (radius,bore):
            vertices.extend((center[0]+r*math.cos(j*math.tau/sides),
                             center[1]+r*math.sin(j*math.tau/sides),z) for j in range(sides))
    for row in range(len(profile)-1):
        a=row*sides*2;b=(row+1)*sides*2
        for j in range(sides):
            k=(j+1)%sides
            faces.append((a+j,a+k,b+k,b+j))
            faces.append((a+sides+j,b+sides+j,b+sides+k,a+sides+k))
    top=(len(profile)-1)*sides*2
    for j in range(sides):
        k=(j+1)%sides
        faces.append((j,sides+j,sides+k,k))
        faces.append((top+j,top+k,top+sides+k,top+sides+j))
    return f.mesh_data(name,vertices,faces,'steel',bone)


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
    f.mesh['artQualityContract']=json.dumps(f.expansion_design['artQuality'],sort_keys=True)
    f.mesh['modelDesignContract']=json.dumps(f.expansion_design,sort_keys=True)
    return f


def boarlet(Forge, name):
    f = Forge(name); f.family = 'quadruped'
    f.art_regions={}
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
            # Broad muscular shoulders taper into short hocks, with a readable
            # pastern between the joint and the two separated hoof toes.
            semantic(f,f.tube('upper_leg_'+bn, [(s*.15,y,.30),(s*.158,y+.016,.25),
                            (s*.165,y+.02,.17)], [.080,.067,.044], 'fur', bn+'_upper', 20),
                     'upper_leg_'+bn,bn+'_upper')
            semantic(f,f.tube('lower_leg_'+bn, [(s*.165,y+.02,.17),(s*.173,y+.013,.125),
                            (s*.17,y-.01,.052)], [.047,.042,.033], 'fur_dark', bn+'_lower', 16),
                     'lower_leg_'+bn,bn+'_lower')
            f.ellipsoid('rounded_hock_'+bn,(s*.166,y+.016,.166),(.045,.046,.041),'fur',bn+'_lower',20)
            for toe in (-1,1):
                x=s*.17+toe*.025
                semantic(f,f.panel('carved_cloven_hoof_'+bn+'_'+str(toe),
                                   (x,y-.039,.028),(.043,.083,.048),'black',bn+'_foot',bevel=.012),
                         'hoof_'+bn+'_'+str(toe),bn+'_foot')
                # Raised horn lip outlines a separate hoof cap; the central
                # 7 mm cleft remains open instead of being painted on a sphere.
                f.tube('hoof_growth_lip_'+bn,[(x-.016,y-.065,.047),(x,y-.075,.050),
                                           (x+.016,y-.065,.047)],.0035,'fur_dark',bn+'_foot',8)
    head=loft_y(f,'continuous_anatomical_body',[
        (-.57,.085,.315,.065,.068),(-.515,.133,.339,.094,.091),
        (-.45,.157,.371,.128,.112),(-.38,.141,.389,.113,.105),
        (-.32,.154,.361,.143,.093),
        (-.295,.178,.338,.160,.112),
        (-.245,.202,.327,.172,.120),(-.18,.231,.324,.194,.119),
        (-.09,.237,.322,.182,.133),(.01,.223,.311,.166,.137),
        (.12,.211,.301,.171,.126),(.23,.218,.293,.169,.120),
        (.32,.192,.290,.141,.110),(.39,.118,.296,.098,.079),
        (.43,.032,.310,.039,.035)],'fur',None,40)
    # Shape the eyelid shelf, hollow socket and rounded jowl in the head
    # surface itself. No rods or detached eyebrows sit above the face.
    for vertex in head.data.vertices:
        x,y,z=vertex.co
        brow=math.exp(-((y+.446)/.060)**2-((z-.456)/.027)**2)
        cheek=math.exp(-((y+.447)/.060)**2-((z-.376)/.044)**2)
        socket=math.exp(-((y+.448)/.030)**2-((z-.430)/.018)**2)
        vertex.co.x+=(1 if x>0 else -1)*(.007*brow+.009*cheek-.008*socket)
    head.data.update();semantic(f,head,'head_anatomical_shell',('head','neck'),
                                [vertex.index for vertex in head.data.vertices if vertex.co.y<-.315])
    loft_y(f,'snout_anatomical_shell',[(-.638,.090,.301,.060,.044),
                                     (-.605,.125,.300,.080,.063),
                                     (-.55,.130,.301,.085,.065),
                                     (-.49,.077,.316,.063,.064)],'fur_dark','jaw',32)
    # Curved fitted faceplate with actual open nostril apertures. A rounded
    # connected perimeter surrounds two dark inner wells, keeping metal out
    # of the pupils and leaving a distinct fleshy lower lip.
    guard_vertices=[];guard_faces=[]
    columns=[-.122,-.096,-.078,-.028,0,.028,.078,.096,.122]
    rows=[.254,.276,.288,.315,.334,.357]
    def guard_y(x,z):return -.650+.036*(abs(x)/.122)**2+.016*((z-.305)/.06)**2
    for z in rows:
        for x in columns:guard_vertices.append((x,guard_y(x,z),z))
    for row in range(len(rows)-1):
        for col in range(len(columns)-1):
            # The two openings are geometry, not black decals over a slab.
            if row==2 and col in (2,5):continue
            a=row*len(columns)+col;guard_faces.append((a,a+1,a+1+len(columns),a+len(columns)))
    guard=f.mesh_data('fitted_copper_snout_guard',guard_vertices,guard_faces,'copper','jaw')
    solid=guard.modifiers.new('Cast faceplate thickness','SOLIDIFY');solid.thickness=.007
    bpy.context.view_layer.objects.active=guard;bpy.ops.object.modifier_apply(modifier=solid.name)
    bevel=guard.modifiers.new('Rounded cast aperture edges','BEVEL');bevel.width=.004;bevel.segments=3
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    semantic(f,guard,'snout_guard','jaw')
    f.tube('lower_snout_lip',[(-.105,-.622,.247),(0,-.641,.242),(.105,-.622,.247)],
           [.012,.016,.012],'fur_dark','jaw',16)
    for s, side in ((-1,'l'), (1,'r')):
        semantic(f,f.ellipsoid('nostril_inner_well_'+side,(s*.053,-.631,.300),
                               (.024,.009,.014),'black','jaw',20),'nostril_'+side,'jaw')
        for z in (.267,.344):f.rivet((s*.102,guard_y(s*.102,z)-.004,z),'jaw','brass',.008)
        semantic(f,f.tube('tusk_root_gum_'+side,[(s*.112,-.533,.250),(s*.146,-.561,.242)],
                          [.026,.022],'fur_dark','jaw',16),'tusk_gum_'+side,'jaw')
        semantic(f,f.ring('tusk_base_collar_'+side,(s*.148,-.566,.248),.026,.006,
                          'copper','jaw',axis='Y'),'tusk_collar_'+side,'jaw')
        semantic(f,f.tube('outward_ivory_tusk_'+side,
                          [(s*.124,-.540,.249),(s*.162,-.577,.243),(s*.188,-.602,.263),
                           (s*.201,-.610,.299),(s*.202,-.610,.333),(s*.196,-.605,.355)],
                          [.023,.021,.017,.012,.006,.001],'ivory','jaw',18),'tusk_'+side,'jaw')
        # Side-oriented eyes stay exposed above the cheek and beside the guard.
        semantic(f,f.ellipsoid('eye_socket_'+side,(s*.133,-.449,.429),(.014,.025,.023),
                               'fur_dark','head',24),'eye_socket_'+side,'head')
        f.ellipsoid('eye_'+side,(s*.143,-.452,.431),(.008,.012,.011),'ivory','head',24)
        semantic(f,f.ellipsoid('pupil_'+side,(s*.151,-.455,.432),(.002,.006,.007),
                               'black','head',20),'pupil_'+side,'head')
        f.ellipsoid('eye_glint_'+side,(s*.154,-.457,.436),(.001,.002,.002),'ivory','head',12)
        f.tube('fitted_upper_eyelid_'+side,[(s*.148,-.463,.436),(s*.151,-.452,.443),
                                         (s*.146,-.441,.437)],[.0025,.004,.0025],'fur_dark','head',12)
        semantic(f,f.tube('cupped_leaf_ear_'+side,
                          [(s*.104,-.328,.476),(s*.139,-.322,.513),
                           (s*.169,-.336,.564),(s*.160,-.351,.599)],
                          [(.041,.024),(.044,.018),(.022,.010),(.001,.001)],
                          'fur','ear_'+side,20),'ear_'+side,'ear_'+side)
        f.tube('inset_ear_'+side,[(s*.134,-.350,.514),(s*.157,-.358,.555),(s*.158,-.361,.579)],
               [(.016,.003),(.014,.003),(.001,.001)],'skin_shade','ear_'+side,12)
        braided_rope(f,'shoulder_harness_'+side,
                     [(s*.03,-.116,.520),(s*.126,-.116,.501),(s*.213,-.116,.437),
                      (s*.247,-.116,.337),(s*.226,-.116,.247),(s*.16,-.116,.215)],'spine')
        braided_rope(f,'hind_harness_'+side,[(s*.025,.266,.472),(s*.121,.278,.456),
                                           (s*.192,.292,.404),(s*.220,.292,.306)],'pelvis')
        f.ring('harness_side_buckle_'+side,(s*.25,-.118,.337),.018,.004,'copper','spine',axis='X')
    braided_rope(f,'harness_back_tie',[(-.065,.30,.464),(0,.30,.480),(.065,.30,.464)],'pelvis')
    f.ellipsoid('rear_harness_knot',(0,.313,.480),(.024,.022,.017),'teal','pelvis',20)
    bristle_tops=[.499,.517,.509,.493,.480,.479,.472,.468,.461,.444,.413]
    for j in range(11):
        y=-.245+j*.060; z=bristle_tops[j]-.006
        for offset in (-1,0,1):
            x=offset*.017
            bristle=f.tube('amber_dorsal_bristle_'+str(j)+'_'+str(offset),
                   [(x,y,z-.005),(x*.85,y+.016,z+.023),(x*.72,y+.035,z+.040-abs(offset)*.006)],
                   [.010,.006,.001],'fur_cream',None,7)
            # Match the exact underlying waist weights at each hair root,
            # so the coat cannot detach as pelvis and spine counter-rotate.
            pelvis_weight=max(0.,min(1.,(y-.06)/.24))
            neck_weight=max(0.,min(1.,(-y-.24)/.13))
            ids=list(range(len(bristle.data.vertices)))
            for bone,weight in (('pelvis',pelvis_weight),('spine',(1-pelvis_weight)*(1-neck_weight)),
                                ('neck',(1-pelvis_weight)*neck_weight)):
                group=bristle.vertex_groups.new(name=bone);group.add(ids,weight,'REPLACE')
    f.tube('small_curled_tail', [(0,.43,.335),(.015,.50,.355),(.035,.542,.374),
                               (.020,.563,.387),(-.003,.55,.389)],
           [.016,.012,.009,.005,.001], 'fur', 'tail', 10)
    if name == 'stampede':
        for s, side in ((-1,'l'), (1,'r')):
            f.ellipsoid('pennant_knot_'+side,(s*.20,.285,.415),(.021,.020,.018),'cloth_ember','pelvis',16)
            f.mesh_data('red_orange_pennant_'+side,
                        [(s*.205,.286,.413),(s*.226,.352,.407),(s*.236,.422,.413),
                         (s*.227,.392,.369),(s*.226,.352,.373),(s*.204,.286,.384)],
                        [(0,1,4,5),(1,2,3,4),(5,4,1,0),(4,3,2,1)],'cloth_ember','pelvis')
            f.tube('pennant_sewn_trim_'+side,[(s*.211,.291,.413),(s*.232,.352,.408),
                                           (s*.242,.422,.413)],.003,'copper','pelvis',8)
    f.socket('attack_origin', (0,-.64,.296), 'jaw')
    f.socket('impact_origin', (0,-.20,.33), 'spine')
    f.socket('hp_anchor', (0,0,.65), 'spine')
    f.socket('status_anchor', (0,-.14,.69), 'neck')
    f.expansion_design = design_metadata(name)
    f.expansion_design['artQuality']={'schema':1,'purpose':'Source geometry measurement contract; no visual acceptance claim',
        'regions':f.art_regions,'landmarksMeters':{'pupil_l':[-.153,-.455,.432],
          'pupil_r':[.153,-.455,.432],'snoutCenter':[0,-.650,.301],'faceCenter':[0,-.43,.392]},
        'intendedClearancesMeters':{'hoofCleft':.007,'nostrilApertureWidth':.050,
          'nostrilApertureHeight':.027,'guardToPupilMinimum':.085,'harnessSurfaceOffset':[.006,.024]},
        'anatomy':'Continuous shaped shoulder/barrel/hind shell; articulated hocks; two carved hoof toes per foot'}
    return source_collision_proxy(f.finish(True))


def pulse_machine(Forge):
    f = Forge('pulverizer'); f.family = 'mechanism'
    f.art_regions={}
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
        return semantic(f,f.mesh_data(name,vertices,faces,key,'root',smooth=False),name,'root')
    plinth('dark_basalt_foundation',[(0,.76),(.027,.815),(.058,.825),(.156,.825),(.192,.795),(.225,.755)],'stone_dark')
    plinth('bevelled_basalt_cap',[(.225,.755),(.247,.775),(.277,.757),(.315,.690)],'stone')
    for side in range(4):
        a=side*math.pi/2;radial=Vector((math.cos(a),math.sin(a),0));tangent=Vector((-math.sin(a),math.cos(a),0))
        for z in (.080,.139):
            points=[radial*.826+tangent*u+Vector((0,0,z)) for u in (-.44,-.10,.20,.44)]
            f.tube('basalt_carved_course_'+str(side)+'_'+str(z),points,.003,'black','root',6)
        for sign in (-1,1):
            p=radial*.828+tangent*(sign*.29)+Vector((0,0,.110))
            f.panel('foundation_anchor_stud_'+str(side)+'_'+str(sign),p,(.020,.008,.020),
                    'brass','root',rotation=(0,0,a-math.pi/2),bevel=.005)
    semantic(f,f.tube('recessed_impact_plate',[(0,0,.308),(0,0,.325),(0,0,.337)],
                      [.526,.548,.535],'steel_shadow','root',48),'impact_plate','root')
    # Engraved radial plate panels surround a dark recess and keep a single
    # restrained pulse channel, instead of several stacked neon doughnuts.
    for j in range(12):
        a=j*math.tau/12;gap=.027
        arc_band(f,'dais_inset_sector_'+str(j),(0,0,.323),.581,.055,.019,
                 a+gap,a+math.tau/12-gap,'steel_edge','root',6)
        f.tube('recessed_radial_groove_'+str(j),[(.46*math.cos(a),.46*math.sin(a),.338),
                                                (.517*math.cos(a),.517*math.sin(a),.338)],
               .004,'black','root',6)
    f.ring('single_recessed_pulse_channel',(0,0,.340),.358,.007,'violet','root',glow=True)
    for i,(x,y) in enumerate(((-.63,-.63),(.63,-.63),(.63,.63),(-.63,.63))):
        # Seated curved cast braces rise from four chamfered anchor shoes.
        angle=math.atan2(y,x);radial=Vector((math.cos(angle),math.sin(angle),0))
        tangent=Vector((-math.sin(angle),math.cos(angle),0));vertices=[]
        for z,length,width,shift in ((.207,.23,.23,0),(.237,.22,.21,0),
                                     (.294,.17,.19,-.009),(.382,.12,.15,-.040),
                                     (.421,.09,.12,-.057)):
            center=Vector((x,y,z))+radial*shift
            for u,v in ((-.5,-.3),(-.3,-.5),(.3,-.5),(.5,-.3),
                        (.5,.3),(.3,.5),(-.3,.5),(-.5,.3)):
                vertices.append(center+radial*(u*length)+tangent*(v*width))
        faces=[tuple(reversed(range(8))),tuple(32+j for j in range(8))]
        for row in range(4):
            for j in range(8):faces.append((row*8+j,row*8+(j+1)%8,(row+1)*8+(j+1)%8,(row+1)*8+j))
        semantic(f,f.mesh_data('cast_bronze_corner_brace_'+str(i),vertices,faces,'brass','root',False),
                 'corner_brace_'+str(i),'root')
        f.panel('corner_anchor_foot_'+str(i),(x,y,.211),(.248,.248,.029),
                'steel_shadow','root',rotation=(0,0,angle),bevel=.014)
        f.rivet((x,y,.235),'root','copper',.016)
        for sign in (-1,1):
            p=Vector((x,y,.271))+tangent*(sign*.067)
            f.rivet(p,'root','steel_edge',.012)
    for i,(x,y) in enumerate(((-.53,-.53),(.53,-.53),(.53,.53),(-.53,.53))):
        bn='piston_'+str(i)
        semantic(f,bored_housing(f,'fixed_strut_housing_'+str(i),(x,y),
                                [(.335,.078),(.360,.079),(.400,.061),(.604,.061),
                                 (.644,.070),(.664,.070)],.038,'root'),
                 'strut_housing_'+str(i),'root')
        semantic(f,f.ring('strut_guide_collar_'+str(i),(x,y,.660),.072,.012,'brass','root'),
                 'guide_collar_'+str(i),'root')
        f.ring('strut_lower_seal_'+str(i),(x,y,.402),.063,.008,'steel_shadow','root')
        semantic(f,f.tube('sliding_polished_rod_'+str(i),[(x,y,.590),(x,y,1.020)],
                          [.028,.028],'silver',bn,20),'sliding_rod_'+str(i),bn)
        f.ring('upper_strut_coupler_'+str(i),(x,y,.999),.042,.010,'copper',bn)
        f.ring('rod_gland_inner_'+str(i),(x,y,.675),.031,.005,'steel_shadow','root')
        # Four external ribs use a shaped chamfered radial arm and stay clear
        # of the smaller central crystal cage throughout the full stroke.
        f.panel('fitted_compression_corner_arm_'+str(i),(x*.86,y*.86,1.014),
                (.28,.084,.057),'steel_shadow','hammer',rotation=(0,0,math.atan2(y,x)),bevel=.017)
        f.rivet((x*.91,y*.91,1.048),'hammer','brass',.010)
    semantic(f,f.tube('piston_compression_head',[(0,0,.986),(0,0,1.008),(0,0,1.078),(0,0,1.100)],
                      [.515,.555,.555,.511],'steel_shadow','hammer',48),'compression_head','hammer')
    for j in range(12):
        a=j*math.tau/12
        arc_band(f,'compression_rim_segment_'+str(j),(0,0,1.051),.581,.092,.080,
                 a+.026,a+math.tau/12-.026,'steel_edge','compression_ring',8)
        f.rivet((.597*math.cos(a+.25),.597*math.sin(a+.25),1.097),'compression_ring','brass',.011)
        arc_band(f,'rim_inset_bronze_'+str(j),(0,0,1.052),.631,.007,.025,
                 a+.12,a+math.tau/12-.12,'brass','compression_ring',5)
    semantic(f,f.tube('low_chamfered_piston_cap',[(0,0,1.105),(0,0,1.134),(0,0,1.216),(0,0,1.249)],
                      [.465,.475,.424,.367],'stone','hammer',16),'piston_cap','hammer')
    for j in range(4):
        a=j*math.pi/2+math.pi/4; vertices=[]
        for radius,z,width in ((.281,1.254,.040),(.369,1.254,.048),(.419,1.223,.057),(.463,1.161,.062)):
            center=Vector((radius*math.cos(a),radius*math.sin(a),z))
            tangent=Vector((-math.sin(a),math.cos(a),0))
            vertices.extend((center-tangent*width/2,center+tangent*width/2))
        plate=f.mesh_data('cap_seated_radial_buttress_'+str(j),vertices,
                          [(0,1,3,2),(2,3,5,4),(4,5,7,6)],'steel_edge','hammer')
        solid=plate.modifiers.new('Machined buttress plate depth','SOLIDIFY');solid.thickness=.012
        bpy.context.view_layer.objects.active=plate;bpy.ops.object.modifier_apply(modifier=solid.name)
        bevel=plate.modifiers.new('Crafted buttress radius','BEVEL');bevel.width=.004;bevel.segments=3
        bpy.ops.object.modifier_apply(modifier=bevel.name)
        semantic(f,plate,'cap_radial_buttress_'+str(j),'hammer')
        f.rivet((.365*math.cos(a),.365*math.sin(a),1.266),'hammer','copper',.010)
    for j in range(8):
        a=j*math.tau/8
        f.tube('cap_carved_flute_'+str(j),[(.477*math.cos(a),.477*math.sin(a),1.139),
                                         (.449*math.cos(a),.449*math.sin(a),1.183),
                                         (.404*math.cos(a),.404*math.sin(a),1.223)],
               [.005,.007,.005],'stone_dark','hammer',8)
    arc_band(f,'top_crown_inset',(0,0,1.251),.242,.037,.009,0,math.tau,'brass','hammer',32)
    f.ring('cap_restrained_rune',(0,0,1.258),.214,.004,'violet','hammer',glow=True)
    # An annular striking shoe lands on the plate at the release marker. Its
    # open center leaves the suspended crystal visible and avoids intersections.
    arc_band(f,'annular_ground_striker',(0,0,.543),.418,.089,.092,0,math.tau,'steel_edge','hammer',40)
    for j in range(8):
        a=j*math.tau/8
        f.rivet((.420*math.cos(a),.420*math.sin(a),.592),'hammer','copper',.012)
    for j in range(4):
        a=j*math.tau/4+math.pi/4;x,y=.42*math.cos(a),.42*math.sin(a)
        semantic(f,f.tube('piston_shoe_support_'+str(j),[(x,y,.563),(x,y,.598),(x,y,.951),(x,y,.986)],
                          [.035,.026,.026,.036],'steel','hammer',16),'striker_support_'+str(j),'hammer')
    semantic(f,f.tube('crystal_cage_top',[(0,0,.800),(0,0,.812),(0,0,.823)],
                      [.187,.197,.178],'brass','root',24),'crystal_cage_top','root')
    for j in range(3):
        a=math.pi/2+j*math.tau/3
        f.tube('crystal_cage_suspension_'+str(j),[(.171*math.cos(a),.171*math.sin(a),.807),
                                                (.196*math.cos(a),.196*math.sin(a),.743),
                                                (.190*math.cos(a),.190*math.sin(a),.493)],
               [.010,.008,.006],'copper','root',12)
    semantic(f,f.tube('suspended_violet_crystal',[(0,0,.423),(0,0,.493),(0,0,.545),
                                                (0,0,.675),(0,0,.756),(0,0,.793)],
                      [.012,.104,.143,.125,.075,.012],'violet','crystal',8,True),
             'suspended_crystal','crystal')
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
    f.expansion_design['artQuality']={'schema':1,'purpose':'Source geometry measurement contract; no visual acceptance claim',
        'regions':f.art_regions,'landmarksMeters':{'crystalCenter':[0,0,.608],
          'strikerBottomRest':[0,0,.497],'impactPlane':[0,0,.337]},
        'intendedClearancesMeters':{'hammerStroke':[-.160,.060],
          'strikerImpactPlane':.337,'crystalToCageRadialMinimum':.029,
          'compressionHeadToCageAtContact':.003,'rodSlidesInsideHousing':True,
          'guideOuterRadius':.084,'rodRadius':.028,'fourStruts':4,'boundedDeathFragments':2},
        'architecture':'Four cast corner braces, fixed guide housings with sliding rods, twelve separated compression rim segments and annular impact shoe'}
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
                stroke=.004*math.sin(t*math.tau)
                rotate('crystal',0,0,.026*math.sin(t*math.tau))
                f.rig.pose.bones['crystal_fractures'].scale=(MIN_DEBRIS_SCALE,)*3
                for s in (-1,1):f.rig.pose.bones['ring_fragment_'+str(s)].scale=(MIN_DEBRIS_SCALE,)*3
                if name=='Attack':
                    if t<=marker:
                        u=t/marker
                        # The first 65% of anticipation tensions the visible
                        # rods; the final 70 ms is the decisive down stroke.
                        stroke=.06*smooth(u/.65)-.22*smooth((u-.65)/.35)
                    else:
                        u=(t-marker)/(1-marker)
                        # Brief compression, positive spring overshoot, then
                        # critically damped settling at the exact clip end.
                        stroke=-.16*(1-smooth(u/.72))+.014*math.sin(math.pi*u)*math.exp(-3*u)
                    rotate('compression_ring',0,0,-.13*math.sin(t*math.pi))
                    rotate('crystal',0,0,.045*math.sin(t*math.pi))
                elif name=='Deploy':
                    stroke=-.15*(1-smooth(t/.75))+.009*math.sin(t*math.tau)*math.sin(t*math.pi)
                elif name=='Death':
                    # The failed piston seats on its physical plate rather
                    # than sinking through the basalt foundation.
                    stroke=-.158*smooth(t/.55)
                    f.rig.pose.bones['crystal'].scale=(1-.65*smooth((t-.25)/.75),)*3
                    f.rig.pose.bones['crystal_fractures'].scale=(max(MIN_DEBRIS_SCALE, smooth(t/.25)),)*3
                    for s in (-1,1):
                        scale=max(MIN_DEBRIS_SCALE, math.sin(smooth((t-.35)/.65)*math.pi))
                        f.rig.pose.bones['ring_fragment_'+str(s)].scale=(scale,)*3
                        move('ring_fragment_'+str(s),x=s*.14*smooth((t-.35)/.65),z=-.24*smooth((t-.35)/.65))
                        rotate('ring_fragment_'+str(s),.35*smooth(t),s*.22*smooth(t),s*.32*smooth(t))
                    rotate('compression_ring',.035*smooth((t-.55)/.45),0,.18*smooth(t))
                elif name=='Hit':stroke=-.023*math.sin(t*math.pi)
                elif name=='Acquire':rotate('compression_ring',0,0,.028*math.sin(t*math.pi))
                elif name=='Turn':rotate('compression_ring',0,0,-.044*math.sin(t*math.pi))
                elif name=='Status':rotate('crystal',0,0,.032*math.sin(t*math.tau*2))
                move('hammer',z=stroke)
            else:
                rotate('ear_l',.018*math.sin(t*math.tau),.027*math.sin(t*math.tau),.022*math.sin(t*math.tau))
                rotate('ear_r',-.018*math.sin(t*math.tau),-.027*math.sin(t*math.tau),-.022*math.sin(t*math.tau))
                rotate('tail',.05*math.sin(t*math.tau),.085*math.sin(t*math.tau+.6))
                if name=='Locomotion':
                    # Staggered front landing, compact rear drive and two
                    # gathered suspension beats; all four hoof paths use IK.
                    phase=t*math.tau
                    move('pelvis',z=-.025+.017*math.sin(phase-.35))
                    rotate('pelvis',.035*math.sin(phase-.6),.008*math.sin(phase))
                    move('spine',z=.007*math.sin(phase+.25))
                    rotate('spine',.060*math.sin(phase))
                    rotate('neck',-.045*math.sin(phase-.35))
                    rotate('head',.018*math.sin(phase-.85))
                    rotate('jaw',.012*math.sin(phase-.85))
                    rotate('ear_l',.105*math.sin(phase-.4),.035*math.sin(phase),.040*math.sin(phase))
                    rotate('ear_r',.09*math.sin(phase-.55),-.035*math.sin(phase),-.040*math.sin(phase))
                    rotate('tail',.12*math.sin(phase-.9),.10*math.sin(phase-.3))
                    stride=design_metadata(f.name)['animationContract']['locomotion']['strideMeters']
                    for leg,side,offset in (('front','l',0.),('front','r',.05),('back','l',.46),('back','r',.535)):
                        u=(t+offset)%1.;step=.18 if leg=='front' else .215;stance=step/stride
                        if u<stance:y=-step/2+stride*u;lift=0.
                        else:
                            v=(u-stance)/(1-stance);y=step/2-step*smooth(v)
                            lift=(.086 if leg=='front' else .077)*math.sin(math.pi*v)
                        bn=leg+'_'+side
                        target=f.rig.pose.bones[bn+'_lower'].bone.tail_local+Vector((0,y,lift))
                        aim_leg(f,bn+'_upper',bn+'_lower',bn+'_foot',target,Vector((0,1 if leg=='front' else -1,0)))
                        if u>=stance:
                            # Toe flex occurs only in flight; planted soles
                            # retain the world-flat orientation from aim_leg.
                            foot=f.rig.pose.bones[bn+'_foot']
                            desired=Quaternion(Vector((1,0,0)),.24*math.sin(math.pi*v))@foot.bone.matrix_local.to_quaternion()
                            set_world_rotation(foot,desired)
                elif name=='Attack':
                    if t<=marker:
                        u=t/marker;coil=math.sin(math.pi*min(1.,u/.52)) if u<.52 else 0.
                        strength=smooth((u-.37)/.63)
                        rotate('neck',-.065*coil+.29*strength);rotate('head',-.045*coil+.16*strength)
                        rotate('jaw',.025*strength)
                        move('spine',y=.013*coil-.022*strength,z=-.018*strength)
                    else:
                        age=(t-marker)*duration
                        settle=smooth(age/c['recover'])
                        rotate('neck',.30*(1-settle)-.075*math.sin(min(1,age/c['recoil'])*math.pi))
                        rotate('head',.17*(1-settle));move('spine',y=-.022*(1-settle),z=-.018*(1-settle))
                    rotate('ear_l',.09*math.sin(t*math.pi));rotate('ear_r',.09*math.sin(t*math.pi))
                elif name=='Death':
                    fold=smooth(t/.60)
                    for leg in ('front','back'):
                        for side in ('l','r'):
                            rotate(leg+'_'+side+'_upper',-.82*fold)
                            rotate(leg+'_'+side+'_lower',.96*fold)
                    move('pelvis',z=-.15*fold)
                    pelvis=f.rig.pose.bones['pelvis']
                    desired=Quaternion(Vector((0,1,0)),1.15*fold)@Quaternion(Vector((1,0,0)),.055*fold)@pelvis.bone.matrix_local.to_quaternion()
                    set_world_rotation(pelvis,desired)
                    rotate('neck',.03*fold);rotate('head',.05*fold)
                elif name=='Deploy':
                    settle=.045*(1-smooth(t/.65))-.006*math.sin(math.pi*t)
                    move('pelvis',z=settle)
                    rotate('neck',.08*(1-smooth(t)));rotate('head',-.028*math.sin(t*math.pi))
                elif name=='Hit':
                    rotate('spine',-.055*math.sin(t*math.pi));rotate('neck',-.060*math.sin(t*math.pi))
                    rotate('ear_l',-.09*math.sin(t*math.pi));rotate('ear_r',-.09*math.sin(t*math.pi))
                elif name=='Turn':
                    rotate('spine',0,0,.075*math.sin(t*math.pi));rotate('neck',0,0,-.045*math.sin(t*math.pi))
                    rotate('tail',0,.09*math.sin(t*math.pi))
                elif name=='Acquire':
                    rotate('head',-.035*math.sin(t*math.pi),0,.07*math.sin(t*math.pi))
                    rotate('ear_l',.09*math.sin(t*math.pi));rotate('ear_r',.09*math.sin(t*math.pi))
                elif name=='Status':rotate('head',.018*math.sin(t*math.tau*3))
                else:
                    rotate('neck',.009*math.sin(t*math.tau));move('spine',z=.003*math.sin(t*math.tau))
                    rotate('jaw',.005*math.sin(t*math.tau))
            if f.family=='quadruped' and name=='Death':
                # Solve the authored collapsed pose against its actual sole
                # and body surfaces. Root remains fixed; the dying body rolls
                # and settles above the ground rather than clipping below it.
                bpy.context.view_layer.update()
                mesh=getattr(f,'mesh',None) or bpy.data.objects['SK_'+f.name]
                evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
                minimum=min((evaluated.matrix_world@vertex.co).z for vertex in evaluated.data.vertices)
                if minimum<.004:
                    move('pelvis',z=-.15*fold+.004-minimum)
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
        if f.family == 'quadruped' and name == 'Locomotion':
            # These dense IK poses form a periodic gallop. Automatic clamped
            # handles flatten joint extrema and the loop endpoints, making a
            # planted hoof oscillate between otherwise exact authored poses.
            # Centered periodic tangents retain the actual contact velocity;
            # the integer poses and exported cadence remain unchanged.
            curves = list(act.fcurves) if hasattr(act, 'fcurves') else [
                curve for layer in act.layers for strip in layer.strips
                for bag in strip.channelbags for curve in bag.fcurves]
            for curve in curves:
                points = list(curve.keyframe_points)
                values = [point.co.y for point in points]
                for index, point in enumerate(points):
                    before = values[index - 1] if index else values[-2]
                    after = values[index + 1] if index + 1 < len(points) else values[1]
                    slope = (after - before) / 2
                    point.handle_left_type = 'FREE'
                    point.handle_right_type = 'FREE'
                    point.handle_left = (point.co.x - 1 / 3, point.co.y - slope / 3)
                    point.handle_right = (point.co.x + 1 / 3, point.co.y + slope / 3)
        act.use_fake_user=True
        made.append({'name':name,'action':act.name,'frames':length,'fps':fps,
                     'seconds':(length-1)/fps,'releaseNormalized':marker if name=='Attack' else None,
                     'loop':name in ('Idle','Locomotion','Status'),
                     'strideMeters':design_metadata(f.name)['animationContract']['locomotion']['strideMeters'] if name=='Locomotion' else None,
                     'object':act})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    return made
