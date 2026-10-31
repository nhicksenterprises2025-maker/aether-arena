"""Fitted building and spell craftsmanship; no rig, scale or gameplay changes."""
import math
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from upgrade_card_models import grip_center, remove, loft

MODELS = {'tower_archer', 'archer_tower', 'pulverizer', 'nova_flask',
          'meteor_shard', 'bullet_round', 'shadow_surge_wisp', 'shadow_surge_ribbon'}


def _bolts(f, center, radius, z, count, bone=None, size=.012):
    for n in range(count):
        a = math.tau * n / count
        x, y = center[0] + radius * math.cos(a), center[1] + radius * math.sin(a)
        f.tube('detail_hex_fastener', [(x, y, z), (x, y, z + size * .65)],
               size, 'brass', bone, 6)
        f.tube('detail_fastener_slot', [(x-size*.42, y, z+size*.67),
                                      (x+size*.42, y, z+size*.67)],
               size*.10, 'steel_shadow', bone, 6)


def _tower_archer(f):
    k = f.bones['head']['head'][2] / 1.6
    # Existing face elements are already registered by the silhouette pass.
    # Weld the face, chin and nose into one continuous skin surface instead of
    # layering another face over it. Eyes, brows and hair retain their weights.
    def head(x, y, z):
        return Vector((x*k*1.10, y*1.035, (1.72+(z-1.72)*1.025)*k))
    skin = [p for p in f.parts if any(name in p.name for name in
            ('sculpted_face_planes', 'single_nose_bridge', 'chin_plane'))]
    bpy.ops.object.select_all(action='DESELECT')
    for part in skin:
        part.select_set(True)
        f.parts.remove(part)
    bpy.context.view_layer.objects.active = skin[0]
    bpy.ops.object.join()
    joined = bpy.context.object
    joined.name = 'detail_continuous_archer_face'
    remesh = joined.modifiers.new('Continuous sculpted face', 'REMESH')
    remesh.mode = 'VOXEL'; remesh.voxel_size = .0065*k; remesh.use_smooth_shade = True
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth = joined.modifiers.new('Soft cheek and nasal planes', 'SMOOTH')
    smooth.factor = .38; smooth.iterations = 4
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    joined.vertex_groups.clear()
    # Reset atlas coordinates after remeshing, then assign the existing skin tile.
    for layer in list(joined.data.uv_layers):
        joined.data.uv_layers.remove(layer)
    f.add(joined, 'skin_warm', 'head')
    for sign in (-1, 1):
        f.tube('detail_lower_eyelid', [head(sign*.039,-.166,1.774),
               head(sign*.068,-.170,1.765),head(sign*.096,-.158,1.775)],
               [.003,.0045,.0025], 'skin_warm', 'head', 10)
        f.ellipsoid('detail_nostril', head(sign*.014,-.194,1.704),
                    (.005*k,.0025,.003*k), 'skin_shade', 'head', 16)
    f.tube('detail_lower_lip', [head(-.027,-.149,1.642),head(0,-.155,1.638),
                               head(.030,-.145,1.649)],
           [.002,.004,.001], 'skin_warm', 'head', 10)
    grip = grip_center(f, 'l')
    # The original generic tube's frame flips where a recurve changes slope.
    # A fixed X normal keeps this planar bow continuous through its grip.
    remove(f,'single_recurve_bow')
    points=[];vertices=[];faces=[]
    for n in range(49):
        t=n/24-1
        points.append(Vector((grip.x,grip.y+.155*t*t-.021*math.sin(abs(t)*math.pi),
                              grip.z+t*.456*k)))
    for n,p in enumerate(points):
        tangent=(points[min(n+1,48)]-points[max(n-1,0)]).normalized()
        b=tangent.cross(Vector((1,0,0)))
        radius=.016+.011*(1-abs(n/24-1))
        for j in range(16):
            a=j*math.tau/16
            vertices.append(p+Vector((math.cos(a)*radius,0,0))+b*math.sin(a)*radius)
    for n in range(48):
        for j in range(16):faces.append((n*16+j,n*16+(j+1)%16,(n+1)*16+(j+1)%16,(n+1)*16+j))
    faces.extend([tuple(reversed(range(16))),tuple(48*16+j for j in range(16))])
    f.mesh_data('detail_continuous_recurve_bow',vertices,faces,'wood','hand_l')
    # Narrow laminated limb strips follow the existing bow, leaving its draw
    # string and dynamically weighted nock completely unobstructed.
    for side in (-1, 1):
        for edge in (-1,1):
            points = []
            for n in range(14):
                t = side*(.18+n/13*.72)
                radius=.016+.011*(1-abs(t))
                points.append((grip.x+edge*(radius+.001),
                               grip.y+.155*t*t-.021*math.sin(abs(t)*math.pi),
                               grip.z+t*.456*k))
            f.tube('detail_bow_laminated_edge',points,.003,'gold_edge','hand_l',8)
        z = grip.z+side*.095*k
        f.ring('detail_bow_grip_ferrule',(grip.x,grip.y,z),.031*k,.004,'brass','hand_l')
    for n in range(5):
        f.ring('detail_bow_bound_grip',(grip.x,grip.y,grip.z+(n-2)*.024*k),
               .0315*k,.0025,'leather','hand_l')
    # Stitched leather follows each existing forearm's own bone; no new glove
    # volume is introduced around either fitted grip.
    for sign, side in ((-1,'l'),(1,'r')):
        bone='forearm_'+side
        forearm=next(p for p in f.parts if 'single_forearm' in p.name and bone in p.vertex_groups)
        surface=BVHTree.FromPolygons([forearm.matrix_world@v.co for v in forearm.data.vertices],
                                     [list(p.vertices) for p in forearm.data.polygons])
        for n in range(5):
            z=(1.112-n*.027)*k; x=sign*(.444+n*.006)*k
            stitches=[]
            for j in range(7):
                t=j/6
                hit=surface.ray_cast(Vector((x+(-.015+.03*t)*k,-1,z-.009*k*t)),Vector((0,1,0)))
                if hit[0] is None:raise RuntimeError('Archer stitch missed its bracer surface')
                stitches.append(hit[0]+Vector((0,-.0018,0)))
            f.tube('detail_bracer_lacing',stitches,.0022,'leather_edge',bone,8)
    quiver=next(p for p in f.parts if 'single_back_quiver' in p.name)
    quiver_surface=BVHTree.FromPolygons([quiver.matrix_world@v.co for v in quiver.data.vertices],
                                       [list(p.vertices) for p in quiver.data.polygons])
    for n in range(4):
        hit=quiver_surface.ray_cast(Vector(((.217+n*.006)*k,1,(1.01+n*.10)*k)),Vector((0,-1,0)))
        if hit[0] is None:raise RuntimeError('Archer rivet missed its quiver surface')
        f.rivet(hit[0]+Vector((0,.002,0)),'chest','brass',.006)
    # A small closed ranger clasp sits on the existing quiver strap.
    f.panel('detail_ranger_strap_clasp',(-.085*k,-.211,1.29*k),(.055*k,.012,.058*k),
            'brass','chest',rotation=(0,0,.16),bevel=.006)
    f.panel('detail_clasp_leather_inset',(-.085*k,-.219,1.29*k),(.029*k,.004,.031*k),
            'leather','chest',rotation=(0,0,.16),bevel=.003)


def _archer_tower(f):
    # Platform joints stay inside the established dressed lip and archer base.
    for n in range(-4,5):
        y=n*.205; half=math.sqrt(max(0,.97*.97-y*y))
        f.tube('detail_platform_board_joint',[(-half,y,1.926),(half,y,1.926)],
               .004,'stone_dark',sides=6)
        for x in (-half+.065,half-.065):
            f.tube('detail_deck_nail',[(x,y+.035,1.924),(x,y+.035,1.931)],
                   .008,'steel_shadow',sides=8)
    for n in range(8):
        a=math.tau*n/8; r=Vector((math.cos(a),math.sin(a),0)); t=Vector((-r.y,r.x,0))
        f.tube('detail_platform_corbel',[r*.85+Vector((0,0,1.55)),
               r*.91+Vector((0,0,1.69)),r*.98+Vector((0,0,1.80))],
               [(.05,.08),(.06,.07),(.07,.04)],'wood',sides=12)
        for side in (-1,1):
            p=r*.985+t*(side*.065)+Vector((0,0,1.842))
            f.rivet(p,key='steel_shadow',size=.013)
    # Three recessed shutter panels read as defensive architecture. The front
    # crest remains clear; all framing stays within the existing tower radius.
    for a in (0,math.pi,math.pi*.5):
        r=Vector((math.cos(a),math.sin(a),0));t=Vector((-r.y,r.x,0))
        c=r*.884+Vector((0,0,1.075))
        f.panel('detail_arrow_slit_well',c,(.14,.013,.40),'stone_dark',
                rotation=(0,0,a+math.pi/2),bevel=.018)
        for sign in (-1,1):
            p=r*.894+t*(sign*.085)
            f.tube('detail_slit_dressed_jamb',[p+Vector((0,0,.85)),p+Vector((0,0,1.23)),
                   r*.895+t*(sign*.045)+Vector((0,0,1.30)),r*.895+Vector((0,0,1.315))],
                   [.020,.020,.018,.015],'limestone',sides=10)
        f.tube('detail_shutter_crossbar',[r*.898-t*.071+Vector((0,0,1.04)),
                   r*.898+t*.071+Vector((0,0,1.04))],.009,'steel_edge',sides=8)
    for sign in (-1,1):
        f.tube('detail_banner_hinge',[(sign*.16,.89,.96),(sign*.16,.89,1.43)],
               .012,'brass',sides=12)
        for z in (.98,1.41): f.rivet((sign*.16,.903,z),key='gold_edge',size=.012)


def _pulverizer(f):
    # Service collars attach to stationary housings, leaving the polished rods
    # and the entire animated 0.22 m hammer stroke free.
    for x,y in ((-.53,-.53),(.53,-.53),(.53,.53),(-.53,.53)):
        for z in (.435,.574):f.ring('detail_strut_service_collar',(x,y,z),.063,.006,'copper','root')
        for sign in (-1,1):
            f.tube('detail_fixed_guide_seam',[(x+sign*.063,y,.451),(x+sign*.063,y,.558)],
                   .003,'steel_shadow','root',6)
        _bolts(f,(x,y),.043,.366,4,'root',.009)
    # One readable pressure dial and two connected feed pipes establish how
    # the crystal drives the pulse, without covering the central moving cage.
    f.tube('detail_gauge_mount',[(0,-.57,.48),(0,-.66,.48)],[.066,.087],
           'steel_shadow','root',24)
    f.tube('detail_pressure_face',[(0,-.663,.48),(0,-.674,.48)],.074,'ivory','root',32)
    f.ring('detail_pressure_bezel',(0,-.676,.48),.080,.010,'brass','root',axis='Y')
    for n in range(7):
        a=math.pi*.15+n*math.pi*.70/6
        f.tube('detail_pressure_tick',[(math.cos(a)*.051,-.680,.48+math.sin(a)*.051),
               (math.cos(a)*.062,-.680,.48+math.sin(a)*.062)],.002,'steel_shadow','root',6)
    f.tube('detail_amethyst_pressure_needle',[(0,-.683,.48),(.030,-.683,.518)],
           [.0035,.0015],'violet','root',8,True)
    for sign in (-1,1):
        f.tube('detail_crystal_feed_pipe',[(sign*.068,-.636,.47),(sign*.24,-.681,.44),
               (sign*.40,-.67,.44),(sign*.50,-.585,.48)],.017,'copper','root',12)
        f.tube('detail_amethyst_feed_window',[(sign*.25,-.691,.445),(sign*.36,-.687,.445)],
               .004,'violet','root',8,True)
    _bolts(f,(0,0),.304,1.255,8,'hammer',.009)


def _nova(f):
    remove(f,'single_trapped_nova')
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=.15,location=(0,0,.49))
    crystal=bpy.context.object;crystal.name='detail_faceted_nova_heart'
    crystal.scale=(1,1,1.13);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    f.add(crystal,'violet',smooth=False,glow=True)
    for n in range(3):
        a=n*math.tau/3
        f.tube('detail_contained_nova_spark',[(math.cos(a)*.03,math.sin(a)*.03,.49),
               (math.cos(a)*.17,math.sin(a)*.17,.49+.035*math.sin(a)),
               (math.cos(a)*.195,math.sin(a)*.195,.54)],
               [.008,.006,.001],'cyan',sides=8,glow=True)
    for z in (.953,1.006): f.ring('detail_neck_engraved_collar',(0,0,z),.113,.004,'brass')
    for n in range(10):
        a=n*math.tau/10
        f.tube('detail_seal_knurl',[(math.cos(a)*.113,math.sin(a)*.113,1.020),
               (math.cos(a)*.113,math.sin(a)*.113,1.035)],.0028,'gold_edge',sides=6)
    f.tube('detail_stopple_cap',[(0,0,1.125),(0,0,1.131)],[.090,.084],'brass',sides=20)
    f.ring('detail_stopple_crest_ring',(0,0,1.132),.052,.003,'cloth_purple')
    # A retained stopper tether is attached at both ends and follows the cage.
    for n in range(21):
        t=n/20
        f.ring('detail_stopple_tether_link',(.105+.15*t+.025*math.sin(math.pi*t),-.03,1.09-.33*t),
               .012,.0022,'brass',axis='Y' if n%2 else 'X')
    for n in range(5):
        a=n*math.tau/5
        f.rivet((math.cos(a)*.268,math.sin(a)*.268,.76),key='brass',size=.010)


def _meteor(f):
    rock=next(p for p in f.parts if 'single_fractured_meteor' in p.name)
    # Split crust plates derive from actual facets. Shrinking their perimeters
    # exposes controlled fracture channels instead of arbitrary floating rocks.
    for index,poly in enumerate(rock.data.polygons):
        if len(poly.vertices)!=3 or index%3==0:continue
        original=[rock.matrix_world@rock.data.vertices[i].co for i in poly.vertices]
        center=sum(original,Vector())/3
        if not -.14<center.z<.39:continue
        normal=(original[1]-original[0]).cross(original[2]-original[0]).normalized()
        if normal.dot(Vector((center.x,center.y,0)))<0:normal=-normal
        face=[center+(v-center)*.82+normal*.003 for v in original]
        back=[v-normal*.004 for v in face]
        f.mesh_data('detail_lifted_volcanic_crust',back+face,
                    [(2,1,0),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)],
                    'meteor_edge' if index%4==0 else 'meteor',smooth=False)
        if index%4==0:
            f.tube('detail_crust_ember_seam',[face[0],face[1],face[2]],
                   [.0015,.003,.001],'ember',sides=6,glow=True)


def _bullet(f):
    for z in (-.047,-.039,-.031):
        f.ring('detail_cartridge_cannelure',(0,0,z),.0254,.0009,'copper')
    f.ring('detail_primer_seating_ring',(0,0,-.104),.012,.001,'steel_shadow')
    for sign in (-1,1):
        f.tube('detail_headstamp',[(sign*.016,-.004,-.1045),(sign*.016,.004,-.1045)],
               .001,'steel_shadow',sides=6)
    loft(f,'detail_copper_jacket_shoulder',[(.031,.0217,.0217),(.035,.0216,.0216)],
         'gold_edge',segments=24)
    # A fine restrained tracer insert follows the established +Z nose axis.
    f.tube('detail_tracer_tip',[(0,0,.128),(0,0,.140)],[.0044,.0008],
           'ember',sides=10,glow=True)


def _shadow(f):
    if f.name=='shadow_surge_wisp':
        for sign in (-1,1):
            points=[]
            for n in range(17):
                t=n/16; a=t*math.pi*1.35+sign*.8
                points.append((.025+.12*t+math.cos(a)*.018*(1-t),
                               .02+.04*math.sin(math.pi*t)+sign*.018*math.sin(a),.06+.285*t))
            f.tube('detail_wisp_twined_filament',points,
                   [.004*(1-n/17)+.0005 for n in range(17)],'violet',sides=6,glow=True)
        f.tube('detail_wisp_leaf_eye',[(.016,-.018,.078),(.040,-.020,.101),
               (.062,-.016,.084),(.040,-.020,.064),(.016,-.018,.078)],
               .0025,'cyan',sides=6,glow=True)
    else:
        # Eight open sigils form a coherent interrupted chain within the
        # existing annulus; no filled disc obscures troops beneath the spell.
        for n in range(8):
            a=n*math.tau/8
            def p(da,r,z=.067):return (r*math.cos(a+da),r*math.sin(a+da),z)
            f.tube('detail_shadow_open_sigil',[p(-.035,.951),p(0,.980),p(.035,.951),
                   p(0,.924),p(-.018,.941)],.004,'violet',sides=6,glow=True)
            f.tube('detail_shadow_seal_stroke',[p(.050,.976),p(.065,1.010),p(.080,.976)],
                   [.002,.003,.001],'cyan',sides=6,glow=True)


def polish(f, skinned):
    if f.name not in MODELS:return
    before_bones=dict(f.bones);before_sockets=dict(f.sockets)
    if f.name=='tower_archer':_tower_archer(f)
    elif f.name=='archer_tower':_archer_tower(f)
    elif f.name=='pulverizer':_pulverizer(f)
    elif f.name=='nova_flask':_nova(f)
    elif f.name=='meteor_shard':_meteor(f)
    elif f.name=='bullet_round':_bullet(f)
    else:_shadow(f)
    if f.bones!=before_bones or f.sockets!=before_sockets:
        raise RuntimeError('Prop detail pass changed the animation/attachment contract: '+f.name)
