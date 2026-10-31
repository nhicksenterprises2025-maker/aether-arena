"""Species-specific creature finishes, applied to the existing production rigs.

Surface details follow the sculpt; new wing parts use the existing span weights.
No rig, socket, actor scale, collision proxy or animation is changed here.
"""
import math

import bpy
from mathutils import Vector
from mathutils.kdtree import KDTree

from upgrade_card_models import anatomy_height, loft, plate, remove


CHARACTERS = {
    'boulderback', 'rambeast', 'frost_fang', 'sky_manta',
    'vampire_bats', 'storm_raven', 'mini_stampede', 'stampede',
}


def _leaf(f, name, start, end, width, key, bone, normal=(0, 0, 1), lift=.025):
    """A closed sculpted fur lock/feather with a central ridge and tapered end."""
    a, b, n = Vector(start), Vector(end), Vector(normal).normalized()
    side = (b-a).cross(n).normalized()
    vertices, faces = [], []
    for j in range(7):
        t = j/6
        center = a.lerp(b, t) + n * math.sin(t*math.pi) * lift
        w = width * max(.015, math.sin(math.pi*(.12+.88*t)))
        vertices += [center-side*w, center+n*width*.20, center+side*w, center-n*width*.09]
    for j in range(6):
        for k in range(4):
            faces.append((j*4+k, j*4+(k+1)%4, (j+1)*4+(k+1)%4, (j+1)*4+k))
    faces += [(3, 2, 1, 0), (24, 25, 26, 27)]
    return f.mesh_data(name, vertices, faces, key, bone)


def _surface(f, origin, direction, offset=.008):
    anatomy = next(o for o in f.parts if any(token in o.name for token in
                   ('continuous_anatomy', 'continuous_flight_anatomy', 'continuous_anatomical_body')))
    inv = anatomy.matrix_world.inverted()
    hit, point, normal, _ = anatomy.ray_cast(inv @ Vector(origin),
                                           inv.to_3x3() @ Vector(direction).normalized())
    if not hit:
        raise ValueError(f'{f.name}: fitted detail missed its anatomical surface at {origin}')
    normal = (anatomy.matrix_world.to_3x3() @ normal).normalized()
    return anatomy.matrix_world @ point + normal*offset, normal, anatomy


def _skin(f, part, anatomy):
    # Only the new detail receives weights. Existing sculpt weights stay exact.
    cache = getattr(f, '_detail_skin_cache', None)
    if cache is None or cache[0] != anatomy:
        tree = KDTree(len(anatomy.data.vertices))
        for v in anatomy.data.vertices:
            tree.insert(anatomy.matrix_world @ v.co, v.index)
        tree.balance()
        cache = f._detail_skin_cache = (anatomy, tree)
    for group in list(part.vertex_groups):
        part.vertex_groups.remove(group)
    for group in anatomy.vertex_groups:
        part.vertex_groups.new(name=group.name)
    for v in part.data.vertices:
        _, index, _ = cache[1].find(part.matrix_world @ v.co)
        for group in anatomy.data.vertices[index].groups:
            if group.weight > 0:
                part.vertex_groups[group.group].add([v.index], group.weight, 'REPLACE')


def _wing(part):
    # Same continuous gradient as the original membrane/feathers, on new parts only.
    for group in list(part.vertex_groups):
        part.vertex_groups.remove(group)
    for side in ('l', 'r'):
        for suffix in ('', '_outer', '_tip'):
            part.vertex_groups.new(name='wing_'+side+suffix)
    for v in part.data.vertices:
        p = part.matrix_world @ v.co
        side, x = ('l' if p.x < 0 else 'r'), abs(p.x)
        if x < .50:
            weights = (1, 0, 0)
        elif x < .99:
            t = (x-.50)/.49
            weights = (1-t, t, 0)
        else:
            t = min(1, (x-.99)/.43)
            weights = (0, 1-t, t)
        for suffix, weight in zip(('', '_outer', '_tip'), weights):
            if weight > 0:
                part.vertex_groups['wing_'+side+suffix].add([v.index], weight, 'REPLACE')


def _eye(f, center, size, iris, skin, bone='head', slit=False):
    x, y, z = center
    f.ellipsoid('detail_recessed_eye', (x, y, z), (size*1.22, size*.43, size*.84), 'black', bone, 28)
    f.ellipsoid('detail_colored_iris', (x, y-size*.32, z), (size*.75, size*.22, size*.66), iris, bone, 28)
    f.ellipsoid('detail_eye_pupil', (x, y-size*.51, z),
                (size*(.16 if slit else .30), size*.09, size*.48), 'black', bone, 20)
    f.ellipsoid('detail_eye_catchlight', (x-size*.20, y-size*.58, z+size*.24),
                (size*.10, size*.035, size*.11), 'ivory', bone, 12)
    f.tube('detail_upper_lid', [(x-size*1.12,y-.003,z+size*.16),
                               (x,y-size*.19,z+size*.82), (x+size*1.12,y-.003,z+size*.16)],
           [size*.17,size*.23,size*.12], skin, bone, 12)


def _coat(f, rows, key, alternate, length=.12, width=.035):
    # Overlapping directional locks hug actual neck/shoulder/haunch surfaces.
    for row, (y, z, angles) in enumerate(rows):
        for j, angle in enumerate(angles):
            p, n, anatomy = _surface(f, (0,y,z), (math.sin(angle),0,math.cos(angle)), .003)
            q, _, _ = _surface(f, (0,y+length,z), (math.sin(angle),0,math.cos(angle)), .011)
            shade=alternate if (j+row)%7 == 0 else key
            tangent=(q-p).cross(n).normalized()
            if f.name=='rambeast':
                for strand in (-1,1):
                    a=p+tangent*width*.30*strand-n*.004
                    b=q+tangent*width*.22*strand
                    ob=f.tube('detail_curled_wool', [a,a.lerp(b,.3)+n*.017,
                              a.lerp(b,.6)+n*.030,a.lerp(b,.82)+n*.022,b],
                              [.010,.018,.017,.010,.001],shade,None,10)
                    _skin(f,ob,anatomy)
            elif f.name=='storm_raven':
                _skin(f,_leaf(f,'detail_body_contour_feather',p-n*.008,q,width,shade,None,n,.012),anatomy)
            else:
                for strand in (-1,0,1):
                    a=p+tangent*width*.42*strand-n*.006
                    b=q+tangent*width*.31*strand+n*.005*(strand==0)
                    _skin(f,_leaf(f,'detail_tapered_fur_strand',a,b,width*.35,shade,None,n,.009),anatomy)


def _boulderback(f):
    remove(f, 'fitted_carapace_stone', 'single_carapace_crystal', 'single_stone_eye',
           'single_stone_brow', 'hoof_or_paw', 'ear')
    # Nine beveled, stepped scutes create deliberate courses over a dark shell.
    for row in range(3):
        for col in range(3):
            x, y = (col-1)*.337, -.32+row*.40
            z = 1.47 + .055*(col == 1)-.025*abs(row-1)
            loft(f, 'detail_basalt_scute', [(z-.025,.172,.199,x,y),
                 (z+.025,.180,.207,x,y), (z+.082,.151,.175,x,y),
                 (z+.109,.120,.148,x-.012,y+.008)],
                 'limestone' if (row+col)%2 else 'stone', 'spine', 8, False)
            if col != 1:
                f.tube('detail_scute_stratum', [(x-.10,y-.172,z+.070),
                       (x,y-.182,z+.073),(x+.10,y-.172,z+.070)], .005, 'stone_dark', 'spine', 6)
    for x,y,height in ((-.295,0,.44),(0,.28,.53),(.295,.57,.36)):
        z = 1.58
        loft(f,'detail_crystal_socket',[(z-.02,.105,.105,x,y),(z+.025,.112,.112,x,y),
             (z+.053,.080,.080,x,y)],'stone_dark','spine',8,False)
        loft(f,'detail_prismatic_crystal',[(z+.015,.076,.076,x,y),(z+height*.67,.065,.065,x+.012,y+.03),
             (z+height*.86,.043,.043,x+.018,y+.042),(z+height,.002,.002,x+.01,y+.05)],
             'violet','spine',6,False)
        f.tube('detail_crystal_inner_facet',[(x-.022,y-.068,z+.10),
               (x-.016,y-.044,z+height*.75)], [.006,.002], 'ivory','spine',6)
    for s in (-1,1):
        _eye(f,(s*.222,-1.112,1.286),.038,'violet','stone')
        f.ellipsoid('detail_stone_nostril',(s*.050,-1.344,1.137),(.019,.008,.011),'black','jaw',16)
        plate(f,'detail_stone_brow_shield',[(s*.12,-1.108,1.34),(s*.20,-1.10,1.405),
              (s*.31,-1.053,1.38),(s*.30,-1.095,1.32)],'limestone','head',.026,.018)
        plate(f,'detail_stone_jowl',[(s*.21,-1.10,1.17),(s*.34,-.99,1.22),
              (s*.38,-.94,1.08),(s*.28,-1.05,1.02)],'stone','head',.018,.025)
        f.tube('detail_mineral_cheek_seam',[(s*.26,-1.109,1.17),(s*.33,-1.02,1.13),
               (s*.32,-1.04,1.08)],[.006,.008,.002],'violet','head',6,True)
        for row,y in enumerate((-.32,.04,.40)):
            edge=[];normals=[]
            for j in range(7):
                a=j*math.tau/7
                p,n,anatomy=_surface(f,(0,y,1.00),(s,.36*math.cos(a),.34+.27*math.sin(a)),.003)
                edge.append(p);normals.append(n)
            center=sum(edge,Vector())/7
            normal=sum(normals,Vector()).normalized()
            vertices=edge+[center+(p-center)*.82+normal*.027 for p in edge]+[center+normal*.049]
            vertices += [p-normal*.015 for p in edge]
            faces=[]
            for j in range(7):
                k=(j+1)%7
                faces += [(j,k,7+k,7+j),(7+j,7+k,14),(j,15+j,15+k,k)]
            faces.append(tuple(15+j for j in reversed(range(7))))
            volume=sum(Vector(vertices[face[0]]).dot(Vector(vertices[face[k]]).cross(Vector(vertices[face[k+1]])))
                       for face in faces for k in range(1,len(face)-1))
            if volume<0:faces=[tuple(reversed(face)) for face in faces]
            f.mesh_data('detail_fitted_flank_rock',vertices,faces,'stone' if row%2 else 'limestone',
                        'pelvis' if row==2 else 'spine',smooth=False)
        for y,leg in ((-.42,'front'),(.40,'back')):
            bone=leg+'_'+('l' if s<0 else 'r')+'_foot'
            for j in range(3):
                x=s*.45+(j-1)*.105
                loft(f,'detail_stone_toe',[(.012,.048,.12,x,y-.105),(.07,.058,.14,x,y-.12),
                     (.13,.049,.105,x,y-.10)],'stone_dark' if j==1 else 'stone',bone,7,False)
            plate(f,'detail_stone_knee',[(s*.43-.085,y-.10,.42),(s*.43,y-.13,.50),
                  (s*.43+.085,y-.10,.42),(s*.43,y-.125,.32)],'limestone',leg+'_'+('l' if s<0 else 'r')+'_lower',.02,.014)
    plate(f,'detail_forehead_keystone',[(-.13,-1.045,1.41),(0,-1.035,1.48),
          (.13,-1.045,1.41),(0,-1.16,1.33)],'stone','head',.015,.028)


def _rambeast(f):
    remove(f,'single_ram_eye','single_ram_brow','hoof_or_paw')
    _coat(f,[(y,.99,[j*math.pi/18 for j in range(-10,11)]) for y in (-.56,-.48,-.40,-.32,-.24)],
          'fur','fur_cream',.12,.057)
    _coat(f,[(y,.91,[j*math.pi/17 for j in range(-9,10)]) for y in (.37,.45,.53)],
          'fur','fur_cream',.10,.046)
    for s in (-1,1):
        _eye(f,(s*.227,-1.114,1.30),.036,'ember','fur_cream')
        f.ellipsoid('detail_ram_nostril',(s*.044,-1.36,1.107),(.018,.009,.012),'black','jaw',20)
        # Each ridge follows the horn's curved axis, leaving the temple and eye clear.
        for j in range(1,24):
            t=j/27;a=-.58+t*math.pi*1.76;r=.275*(1-.43*t)
            p=Vector((s*(.32+.13*math.sin(t*math.pi)),-.765+math.cos(a)*r,1.427+math.sin(a)*r))
            dt=.001;b=-.58+(t+dt)*math.pi*1.76;rb=.275*(1-.43*(t+dt))
            q=Vector((s*(.32+.13*math.sin((t+dt)*math.pi)),-.765+math.cos(b)*rb,1.427+math.sin(b)*rb))
            axis=(q-p).normalized();u=axis.cross(Vector((1,0,0))).normalized();v=axis.cross(u)
            points=[p+(u*math.cos(k*math.tau/20)+v*math.sin(k*math.tau/20))*(.076*(1-.84*t)+.001) for k in range(21)]
            f.tube('detail_horn_growth_ridge',points,.0038,'fur_cream','head',6)
        for y,leg in ((-.42,'front'),(.40,'back')):
            bone=leg+'_'+('l' if s<0 else 'r')+'_foot'
            for offset in (-.069,.069):
                x=s*.45+offset
                loft(f,'detail_cloven_ram_hoof',[(.009,.057,.155,x,y-.12),(.065,.061,.16,x,y-.12),
                     (.132,.048,.112,x,y-.08)],'black',bone,12)
                f.tube('detail_hoof_growth_edge',[(x-.045,y-.23,.085),(x,y-.268,.075),
                       (x+.045,y-.23,.085)],.006,'horn',bone,8)
        for j in range(11):
            y=-.07+j*.049
            z=1.407+.067*math.sqrt(max(0,1-((y-.21)/.397)**2))
            f.tube('detail_saddle_hand_stitch',[(s*.246,y,z-.014),(s*.262,y+.013,z-.015)],
                   .0035,'horn','spine',6)
        f.panel('detail_harness_buckle',(s*.565,.13,1.12),(.027,.092,.081),'brass','spine',bevel=.009)
        f.panel('detail_buckle_dark_inset',(s*.582,.13,1.12),(.008,.054,.043),'leather','spine',bevel=.006)


def _frost_fang(f):
    remove(f,'feline_eye','predator_brow','single_frost_shoulder_cap','sculpted_frost_mane')
    _coat(f,[(y,1.08,[j*math.pi/17 for j in range(-9,10)]) for y in (-.57,-.49,-.41,-.33)],
          'ice_fur','ivory',.12,.045)
    for s in (-1,1):
        _eye(f,(s*.23,-1.181,1.297),.034,'cyan','ice_fur',slit=True)
        # Fine whisker pads and a split lip read as feline rather than a boar muzzle.
        f.ellipsoid('detail_feline_whisker_pad',(s*.082,-1.251,1.117),(.084,.029,.045),'ivory','jaw',28)
        for j in range(3):
            f.ellipsoid('detail_whisker_root',(s*(.060+j*.028),-1.279,1.126-j*.010),
                        (.004,.002,.004),'ice_fur_shade','jaw',12)
            f.tube('detail_feline_whisker',[(s*.115,-1.272,1.107+j*.015),
                   (s*.20,-1.26,1.11+j*.025),(s*.28,-1.21,1.12+j*.026)],
                   [.0024,.0017,.0005],'ivory','jaw',6)
        for j in range(2):
            points=[_surface(f,(0,-.86,1.19),(s*(.34+j*.18+k*.06),-1,.86-k*.17),.002)[0]
                    for k in range(3)]
            f.tube('detail_feline_forehead_mark',points,[.006,.005,.001],'ice_fur_shade','head',8)
        for row in range(2):
            y=-.20+row*.24;z=anatomy_height(f,s*.32,y,1.24)+.012
            loft(f,'detail_frost_shoulder_scute',[(z-.018,.13,.16,s*.32,y),(z+.027,.123,.15,s*.32,y),
                 (z+.050,.092,.11,s*.32,y)],'ice_steel','spine',8,False)
            f.tube('detail_frost_scute_inlay',[(s*.25,y-.11,z+.035),(s*.32,y-.12,z+.055),
                   (s*.39,y-.11,z+.035)],.006,'silver','spine',8)
    f.tube('detail_feline_philtrum',[(0,-1.312,1.116),(0,-1.287,1.078)],.004,'black','jaw',8)


def _manta(f):
    remove(f,'single_manta_eye','curved_fin_rib')
    fins=[o for o in f.parts if 'curved_manta_wing' in o.name]
    def fin_height(x,y):
        # Both existing fins are solidified; their top shells sit at different
        # offsets. Fit decoration to the actual shell, not the old mid-surface.
        for fin in fins:
            inv=fin.matrix_world.inverted()
            hit,p,_,_=fin.ray_cast(inv@Vector((x,y,3)),inv.to_3x3()@Vector((0,0,-1)))
            if hit:return (fin.matrix_world@p).z
        raise ValueError(f'Manta decoration missed fin at {x},{y}')
    for s,side in ((-1,'l'),(1,'r')):
        _eye(f,(s*.151,-.641,1.13),.027,'cyan','teal')
        for j in range(9):
            t=.08+j*.095;x=s*(.22+1.34*t);center=.05+.17*t
            chord=.89*(1-t**1.65)+.018
            points=[]
            for k in range(9):
                u=.08+k*.105;y=center-chord*.55+chord*u
                points.append((x,y,fin_height(x,y)+.003))
            ob=f.tube('detail_manta_cartilage_ray',points,[.0045*(1-.55*k/8) for k in range(9)],'teal','wing_'+side,8)
            _wing(ob)
        # Dorsal chromatophore markings follow the actual wing curvature.
        for j in range(6):
            t=.17+j*.108;x=s*(.22+1.34*t);y=.05+.17*t
            z=fin_height(x,y)
            ob=f.ellipsoid('detail_manta_dorsal_spot',(x,y,z+.002),
                           (.018+.009*(1-t),.044*(1-t)+.012,.003),'cyan','wing_'+side,16)
            _wing(ob)
        for j in range(4):
            f.tube('detail_manta_gill_rim',[(s*(.17+j*.018),-.35+j*.062,.976),
                   (s*(.22+j*.018),-.32+j*.062,.982)],.006,'teal_light','body',8)
    f.tube('detail_manta_ventral_mouth',[(-.105,-.663,1.036),(0,-.687,1.025),(.105,-.663,1.036)],
           [.005,.007,.005],'stone_dark','jaw',10)
    # Small paired tail fins are continuous with the tail, never a second wing rig.
    for s in (-1,1):
        _leaf(f,'detail_manta_tail_fluke',(0,.88,1.038),(s*.17,1.13,1.02),.075,'teal_light','tail',lift=.02)


def _bats(f):
    remove(f,'scalloped_bat_membrane','wing_finger','single_bat_eye',
           'single_sculpted_bat_ear','single_bat_inner_ear')
    _coat(f,[(y,1.08,[j*math.pi/12 for j in range(-6,7)]) for y in (-.23,-.16,-.09)],
          'plum','membrane_shade',.095,.034)
    for s,side in ((-1,'l'),(1,'r')):
        _eye(f,(s*.109,-.651,1.276),.030,'violet','plum')
        _leaf(f,'detail_cupped_bat_ear',(s*.115,-.40,1.36),(s*.185,-.405,1.64),
              .057,'plum','head',normal=(0,-1,0),lift=.033)
        _leaf(f,'detail_bat_ear_inner_fold',(s*.128,-.425,1.397),(s*.181,-.425,1.603),
              .034,'membrane_shade','head',normal=(0,-1,0),lift=.024)
        root=Vector((s*.23,0,1.07));wrist=Vector((s*.66,-.04,1.10))
        ends=[Vector((s*1.09,-.11,1.07)),Vector((s*1.13,.22,1.03)),
              Vector((s*.93,.53,.99)),Vector((s*.57,.47,.98)),Vector((s*.26,.30,.99))]
        for j in range(4):
            # Separate tensioned panels bow gently between the same four finger rays.
            a,b=ends[j],ends[j+1];verts=[wrist];faces=[]
            for row in range(1,9):
                v=row/8
                for col in range(9):
                    u=col/8
                    boundary=a.lerp(b,u).lerp(wrist,.17*math.sin(math.pi*u))
                    p=wrist.lerp(boundary,v)
                    p.z-=.036*math.sin(math.pi*u)*math.sin(math.pi*v)
                    verts.append(p)
            faces.extend((0,col+1,col+2) for col in range(8))
            for row in range(7):
                for col in range(8):
                    q=1+row*9+col;faces.append((q,q+1,q+10,q+9))
            ob=f.mesh_data('detail_bat_tension_membrane',verts,faces,'membrane' if j%2==0 else 'membrane_shade','wing_'+side)
            mod=ob.modifiers.new('Living membrane thickness','SOLIDIFY');mod.thickness=.005
            bpy.context.view_layer.objects.active=ob;bpy.ops.object.modifier_apply(modifier=mod.name)
            _wing(ob)
            for u in (.30,.67):
                edge=a.lerp(b,u).lerp(wrist,.17*math.sin(math.pi*u))
                points=[]
                for k in range(6):
                    v=.22+k*.14;p=wrist.lerp(edge,v)
                    p.z-=.036*math.sin(math.pi*u)*math.sin(math.pi*v)-.003
                    points.append(p)
                _wing(f.tube('detail_bat_membrane_vein',points,[.003,.003,.0026,.0022,.0016,.0007],
                             'plum','wing_'+side,6))
        web=f.mesh_data('detail_bat_arm_web',[root,wrist,ends[-1]],[(0,1,2)],'membrane_shade','wing_'+side)
        mod=web.modifiers.new('Arm membrane thickness','SOLIDIFY');mod.thickness=.005
        bpy.context.view_layer.objects.active=web;bpy.ops.object.modifier_apply(modifier=mod.name)
        _wing(web)
        _wing(f.tube('detail_bat_forearm',[root,root.lerp(wrist,.54),wrist],
                     [.031,.025,.022],'plum','wing_'+side,16))
        for j,end in enumerate(ends[:4]):
            elbow=wrist.lerp(end,.50)+Vector((0,-.014,.018))
            _wing(f.tube('detail_articulated_wing_digit',[wrist,elbow,end],
                         [.022,.012,.003],'plum','wing_'+side,14))
            _wing(f.ellipsoid('detail_wing_knuckle',elbow,(.019,.017,.018),'membrane_shade','wing_'+side,16))
        _wing(f.tube('detail_bat_thumb_claw',[wrist,wrist+Vector((s*.035,-.075,.045)),
                    wrist+Vector((s*.065,-.115,.016))],[.018,.012,.001],'ivory','wing_'+side,12))
        for j in range(4):
            _leaf(f,'detail_bat_cheek_fur',(s*.13,-.45,1.30-j*.04),
                  (s*.21,-.37,1.25-j*.04),.025,'plum','head',normal=(s,0,.2),lift=.022)
    f.tube('detail_bat_split_upper_lip',[(-.055,-.694,1.203),(0,-.708,1.194),(.055,-.694,1.203)],
           .005,'black','jaw',10)


def _raven(f):
    remove(f,'sculpted_feather','feather_shaft','wing_leading_anatomy','single_raven_eye',
           'single_raven_body_feather','swept_storm_crown_feather','hooked_talon')
    for s,side in ((-1,'l'),(1,'r')):
        _eye(f,(s*.119,-.638,1.302),.028,'cyan','raven')
        # The flight feathers grow out of one tapered wing, rather than hanging
        # from an exposed rod. This closed airfoil keeps the leading silhouette.
        vertices=[];faces=[]
        for row in range(15):
            t=row/14;x=s*(.23+1.24*t)
            front=-.073+.16*t;back=.36+.09*math.sin(t*math.pi)-.16*t
            for underside in (False,True):
                for col in range(7):
                    u=col/6;y=front+(back-front)*u
                    z=1.097-.04*t+(math.sin(u*math.pi)*.022 if not underside
                                    else -.008-math.sin(u*math.pi)*.027)*(1-t*.7)
                    vertices.append((x,y,z))
        for row in range(14):
            for col in range(6):
                q=row*14+col;faces.append((q,q+14,q+15,q+1))
                q+=7;faces.append((q,q+1,q+15,q+14))
            for edge in (0,6):
                q=row*14+edge
                faces.append((q,q+7,q+21,q+14) if edge==0 else (q,q+14,q+21,q+7))
        faces += [tuple(range(7))+tuple(reversed(range(7,14))),
                  tuple(196+j for j in reversed(range(7)))+tuple(203+j for j in range(7))]
        volume=sum(Vector(vertices[face[0]]).dot(Vector(vertices[face[k]]).cross(Vector(vertices[face[k+1]])))
                   for face in faces for k in range(1,len(face)-1))
        if volume<0:faces=[tuple(reversed(face)) for face in faces]
        _wing(f.mesh_data('detail_raven_continuous_wing',vertices,faces,'raven','wing_'+side))
        # Flight feathers overlap in orderly coverts/secondaries/primaries.
        for row in range(3):
            for j in range(13):
                t=j/12;x=s*(.25+1.18*t)
                a=Vector((x,-.015+row*.10,1.13-row*.012))
                b=Vector((x+s*(.07+.18*t),.28+row*.17+.15*math.sin(t*math.pi),1.105-row*.035))
                ob=_leaf(f,'detail_raven_flight_feather',a,b,.070 if row<2 else .085,
                         'raven_blue' if row==1 else 'raven','wing_'+side,lift=.022)
                _wing(ob)
                if row==2:
                    _wing(f.tube('detail_raven_feather_rachis',[a+Vector((0,0,.009)),b],
                                 [.004,.001],'raven_blue','wing_'+side,6))
        for j in range(5):
            x=s*(.048+j*.038)
            _leaf(f,'detail_raven_tail_fan',(x,.35,1.075),(x+s*.055,.96+j*.014,1.012),
                  .042,'raven_blue' if j%2 else 'raven','tail',lift=.018)
        for j in range(3):
            _leaf(f,'detail_raven_crown_plume',(s*(.035+j*.02),-.45,1.38),
                  (s*(.075+j*.035),-.27,1.48+j*.025),.035,'raven_blue','head',lift=.023)
        for j in range(3):
            x=s*.12+(j-1)*.034
            f.tube('detail_raven_scaled_toe',[(s*.12,.01,.83),(x,-.05,.73),(x,-.12,.714)],
                   [.021,.016,.011],'steel_shadow','talon_'+side,12)
            f.tube('detail_raven_curved_claw',[(x,-.12,.714),(x,-.17,.715),(x,-.174,.752)],
                   [.011,.008,.001],'black','talon_'+side,12)
        f.ellipsoid('detail_raven_nare',(s*.041,-.724,1.259),(.013,.010,.006),'black','head',16)
    _coat(f,[(y,1.04,[j*math.pi/16 for j in range(-9,10)]) for y in (-.19,-.12,-.05,.02,.09,.16)],
          'raven','raven_blue',.11,.045)


def _hogs(f):
    royal=f.name=='stampede'
    # Preserve the authored eyes, apertures, tusks and divided hooves. Their
    # semantic region groups are used by the source-asset validation contract.
    _coat(f,[(y,.32,[j*math.pi/12 for j in range(-6,7)]) for y in (-.25,-.19,.22)],
          'fur','fur_dark',.065,.015)
    for s,side in ((-1,'l'),(1,'r')):
        for j in range(3):
            f.tube('detail_boar_cheek_fold',[(s*(.148+j*.008),-.448,.407-j*.014),
                   (s*(.160+j*.010),-.424,.395-j*.014),(s*(.160+j*.012),-.397,.391-j*.014)],
                   [.0025,.004,.001],'fur_dark','head',8)
        for j in range(3):
            f.tube('detail_boar_jowl_whisker',[(s*.132,-.51,.31+j*.014),
                   (s*.178,-.52,.31+j*.019),(s*.204,-.50,.31+j*.019)],
                   [.0018,.0012,.0004],'horn','jaw',6)
        if not royal:
            continue
        # Fitted cloth skirt, outside the barrel and above the moving legs.
        vertices=[]
        for row,y in enumerate((-.055,.055,.165,.245)):
            for col in range(7):
                a=s*(.28+col*.16)
                p,n,_=_surface(f,(0,y,.31),(math.sin(a),0,math.cos(a)),.012)
                vertices.append(p)
        faces=[(r*7+c,r*7+c+1,(r+1)*7+c+1,(r+1)*7+c) for r in range(3) for c in range(6)]
        cloth=f.mesh_data('detail_royal_blue_saddlecloth',vertices,faces,'cloth_blue','spine')
        mod=cloth.modifiers.new('Woven saddlecloth thickness','SOLIDIFY');mod.thickness=.006
        bpy.context.view_layer.objects.active=cloth;bpy.ops.object.modifier_apply(modifier=mod.name)
        anatomy=next(o for o in f.parts if 'continuous_anatomical_body' in o.name)
        _skin(f,cloth,anatomy)
        edge=[vertices[j*7+6] for j in range(4)]
        trim=f.tube('detail_royal_blanket_gold_piping',edge,.004,'gold_edge','spine',8)
        _skin(f,trim,anatomy)
        for j in range(3):
            y=-.015+j*.083
            p,n,_=_surface(f,(0,y,.31),(s*.72,0,.70),.021)
            _skin(f,_leaf(f,'detail_royal_saddle_lamella',p-Vector((0,.035,0)),
                         p+Vector((0,.045,-.006)),.041,'steel','spine',n,.009),anatomy)
    if royal:
        # A low five-point coronet sits between the ears, clear of both pupils.
        y=-.377;z=.502
        loft(f,'detail_boar_coronet_band',[(z,.090,.047,0,y),(z+.018,.093,.049,0,y),
             (z+.023,.087,.044,0,y)],'gold_edge','head',24)
        for j in range(5):
            a=(j-2)*.48;x=math.sin(a)*.079;yy=y-math.cos(a)*.039
            loft(f,'detail_boar_crown_point',[(z+.018,.017,.012,x,yy),(z+.059,.012,.008,x,yy),
                 (z+.075,.002,.002,x,yy)],'brass','head',6)
        f.ellipsoid('detail_boar_crown_sapphire',(0,y-.052,z+.033),(.013,.006,.017),'cyan','head',20)


def polish(f, skinned):
    if not skinned or f.name not in CHARACTERS:
        return
    {'boulderback':_boulderback,'rambeast':_rambeast,'frost_fang':_frost_fang,
     'sky_manta':_manta,'vampire_bats':_bats,'storm_raven':_raven,
     'mini_stampede':_hogs,'stampede':_hogs}[f.name](f)
