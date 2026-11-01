"""Fitted 2.0 humanoid craftsmanship, before Forge joins its authored parts.

Attachments follow the existing bones and actual surfaces.  This pass does not
change a rig, socket, weapon grip, animation, material atlas or gameplay scale.
"""
import math
import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
from upgrade_card_models import grip_center, bone_band, thin_cloth, remove


CHARACTERS = frozenset(('ironclad', 'ember_archer', 'tower_archer', 'twin_blades', 'arc_mage',
                        'bone_warden', 'bone_warden_skeleton', 'aether_reaper', 'iron_giant'))


def _parts(f, text):
    return [p for p in f.parts if text in p.name]


def _part(f, text):
    found = _parts(f, text)
    if not found:
        raise ValueError(f'{f.name}: required authored surface missing: {text}')
    return found[0]


def _bounds(ob):
    points = [ob.matrix_world @ v.co for v in ob.data.vertices]
    return (Vector(tuple(min(p[i] for p in points) for i in range(3))),
            Vector(tuple(max(p[i] for p in points) for i in range(3))))


def _bone(ob, f):
    return next((g.name for g in ob.vertex_groups if g.name in f.bones), None)


def _surface(ob, points, lift=.003):
    """Project front-facing ornament onto the actual plate/garment, not a box."""
    verts = [ob.matrix_world @ v.co for v in ob.data.vertices]
    bvh = BVHTree.FromPolygons(verts, [list(p.vertices) for p in ob.data.polygons])
    result = []
    for x, z in points:
        hit, normal, _, _ = bvh.ray_cast(Vector((x, -4., z)), Vector((0, 1, 0)), 8.)
        if hit is None:
            return []
        result.append(hit + Vector((0, -lift, 0)))
    return result


def _line(f, ob, name, points, key, radius=.0035, bone=None, glow=False):
    # Long chords disappear into rounded armor. Sample the complete curve before
    # projection, so every segment stays on the same underlying authored surface.
    dense=[]
    for a,b in zip(points,points[1:]):
        count=max(1,math.ceil(math.dist(a,b)/.009))
        dense.extend((a[0]+(b[0]-a[0])*j/count,a[1]+(b[1]-a[1])*j/count) for j in range(count))
    dense.append(points[-1])
    fitted = _surface(ob, dense, radius*.83)
    if len(fitted) > 1:
        return f.tube('detail_'+name, fitted, radius, key, bone or _bone(ob, f), 8, glow)
    return None


def _inset(f, ob, name, points, key, bone=None):
    # A radial patch follows curvature across its complete face, avoiding the
    # floating corners and buried center of a flat plate laid on a rounded vest.
    area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points,points[1:]+points[:1]))
    if area<0:points=list(reversed(points))
    center=tuple(sum(p[i] for p in points)/len(points) for i in (0,1))
    border=[]
    for a,b in zip(points,points[1:]+points[:1]):
        count=max(2,math.ceil(math.dist(a,b)/.012))
        border.extend((a[0]+(b[0]-a[0])*j/count,a[1]+(b[1]-a[1])*j/count) for j in range(count))
    uv=[center]
    rings=8;n=len(border)
    for row in range(1,rings+1):
        t=row/rings
        uv.extend((center[0]+(p[0]-center[0])*t,center[1]+(p[1]-center[1])*t) for p in border)
    fitted=_surface(ob,uv,.0045)
    if len(fitted)!=len(uv):return None
    faces=[(0,1+j,1+(j+1)%n) for j in range(n)]
    for row in range(rings-1):
        a=1+row*n;b=a+n
        faces.extend((a+j,b+j,b+(j+1)%n,a+(j+1)%n) for j in range(n))
    return f.mesh_data('detail_'+name,fitted,faces,key,bone or _bone(ob,f),True)
    return None


def _seams(f, ob, name, count=6, key='leather_edge', inset=.16):
    lo, hi = _bounds(ob); w, h = hi.x-lo.x, hi.z-lo.z
    for sign in (-1, 1):
        x = (lo.x+hi.x)/2 + sign*w*(.5-inset)
        for i in range(count):
            z = lo.z+h*(.17+.66*i/max(1, count-1))
            _line(f, ob, name, [(x-w*.014, z), (x+w*.014, z+h*.014)], key, .0018)


def _unify(f, parts, name, bone, voxel):
    """One continuous sculpted surface; material-tile UV and rigid weight survive."""
    if len(parts) < 2:
        return parts[0] if parts else None
    uv = parts[0].data.uv_layers.active.data[0].uv
    tile = ((math.floor(uv.x*8)+.5)/8, (math.floor(uv.y*8)+.5)/8)
    f.parts = [p for p in f.parts if p not in parts]
    bpy.ops.object.select_all(action='DESELECT')
    for ob in parts:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join(); ob = bpy.context.object; ob.name = f.name+'_detail_'+name
    remesh = ob.modifiers.new('Continuous sculpted anatomy', 'REMESH')
    remesh.mode = 'VOXEL'; remesh.voxel_size = voxel
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth = ob.modifiers.new('Soft anatomical transitions', 'SMOOTH')
    smooth.factor = .42; smooth.iterations = 4
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    # Voxel fusion supplies continuous anatomy, not an excuse to inflate the
    # roster's triangle count. Preserve the silhouette with a bounded head mesh.
    triangles=sum(len(p.vertices)-2 for p in ob.data.polygons)
    budget=9000 if f.name=='bone_warden_skeleton' else 14000
    if triangles>budget:
        reduction=ob.modifiers.new('Bounded sculpt surface density','DECIMATE')
        reduction.ratio=budget/triangles
        bpy.ops.object.modifier_apply(modifier=reduction.name)
    for group in list(ob.vertex_groups):
        ob.vertex_groups.remove(group)
    ob.vertex_groups.new(name=bone).add(list(range(len(ob.data.vertices))), 1, 'REPLACE')
    uv = ob.data.uv_layers.active or ob.data.uv_layers.new(name='UVMap')
    for loop in uv.data:
        loop.uv = tile
    ob.data.materials.clear(); ob.data.materials.append(f.materials[0])
    for face in ob.data.polygons:
        face.use_smooth = True
    f.parts.append(ob)
    return ob


def _follow_weights(f, ob, source):
    tree = KDTree(len(source.data.vertices))
    for v in source.data.vertices:
        tree.insert(source.matrix_world @ v.co, v.index)
    tree.balance()
    for group in list(ob.vertex_groups):
        ob.vertex_groups.remove(group)
    groups = {g.index: ob.vertex_groups.new(name=g.name) for g in source.vertex_groups if g.name in f.bones}
    for v in ob.data.vertices:
        _, index, _ = tree.find(ob.matrix_world @ v.co)
        for weight in source.data.vertices[index].groups:
            if weight.group in groups:
                groups[weight.group].add([v.index], weight.weight, 'REPLACE')


def _expressive_face(f, k):
    face = _part(f, 'sculpted_face_planes')
    if f.name != 'twin_blades':
        # The original seven-ring head loft has visible horizontal changes of
        # slope. Round that underlying skin before fusion, rather than trying
        # to hide the bands with tiny wrinkles or denser voxel tessellation.
        bpy.context.view_layer.objects.active=face
        smooth_skin=face.modifiers.new('Rounded facial section transitions','SUBSURF')
        smooth_skin.levels=2;smooth_skin.render_levels=2
        bpy.ops.object.modifier_apply(modifier=smooth_skin.name)
        # Integrating nose and chin eliminates the old pasted-on angular nose.
        face = _unify(f, [face]+_parts(f, 'single_nose_bridge')+_parts(f, 'chin_plane'),
                      'continuous_face', 'head', .0065*k)
    for eye in _parts(f, 'single_eye_white'):
        lo, hi = _bounds(eye); c = (lo+hi)/2; sign = -1 if c.x < 0 else 1
        f.ellipsoid('detail_eye_catchlight', (c.x-.006*k, lo.y-.014, c.z+.004*k),
                    (.0028*k, .0015, .0028*k), 'ivory', 'head', 12)
        _line(f, face, 'lower_lid_anatomy', [(c.x-sign*.025*k,c.z-.016*k),
              (c.x,c.z-.022*k),(c.x+sign*.021*k,c.z-.017*k)],
              'skin_shade' if f.name != 'twin_blades' else 'cloth_purple_light', .0028*k, 'head')
        if f.name == 'arc_mage':
            for j in range(2):
                _line(f, face, 'aged_crow_foot', [(c.x+sign*.029*k,c.z-j*.008*k),
                      (c.x+sign*.048*k,c.z-(j+1)*.009*k)], 'skin_shade', .0014*k, 'head')
    # Small rounded lips retain the character's expression without a broad mouth slab.
    if f.name == 'ember_archer':
        _line(f, face, 'lower_lip', [(-.024*k,1.631*k),(0,1.626*k),(.026*k,1.635*k)], 'skin', .0038*k, 'head')
    if f.name == 'arc_mage':
        for z in (1.845,1.861):
            _line(f, face, 'forehead_crease', [(-.067*k,z*k),(0,(z+.003)*k),(.067*k,z*k)], 'skin_shade', .0012*k, 'head')
        # Shaped beard locks already exist; incision lines follow each lock's front.
        for lock in _parts(f, 'single_mage_beard_lock'):
            lo,hi = _bounds(lock); x=(lo.x+hi.x)/2
            _line(f, lock, 'beard_division', [(x,lo.z+(hi.z-lo.z)*.18),(x,lo.z+(hi.z-lo.z)*.83)], 'ivory', .0018, 'head')


def _archer_head(f, k):
    # The old two-row fringe was a floating strip across the forehead. Fit a
    # connected swept cap to the evaluated scalp, including its crown beneath
    # the hood, so skin rounding cannot leave a bald gap above the fringe.
    skin=next(p for p in f.parts if any(n in p.name for n in ('continuous_face','sculpted_face_planes')))
    if f.name=='tower_archer':
        # The following prop pass still owns its existing face union/details.
        bpy.context.view_layer.objects.active=skin
        smooth=skin.modifiers.new('Rounded archer facial sections','SUBSURF')
        smooth.levels=2;smooth.render_levels=2
        bpy.ops.object.modifier_apply(modifier=smooth.name)
    remove(f,'single_connected_hair_fringe','subtle_hair_strand','single_expressive_brow',
           'swept_auburn_ponytail')
    # The legacy ponytail pierced the closed upper rear hood. Let a short tail
    # emerge below its nape hem, retaining the existing secondary-motion joint.
    tail=f.tube('detail_nape_ponytail',[(0,.135,1.565*k),(0,.205,1.525*k),
                (.015,.265,1.475*k),(.022,.28,1.425*k)],
                [.024,.029,.021,.001],'hair_auburn','crest_tip',16)
    tip=tail.vertex_groups['crest_tip'];base=tail.vertex_groups.new(name='head')
    for vertex in tail.data.vertices:
        t=min(1.,max(0.,(1.56*k-(tail.matrix_world@vertex.co).z)/(.12*k)))
        tip.add([vertex.index],t,'REPLACE');base.add([vertex.index],1-t,'REPLACE')
    points=[skin.matrix_world@v.co for v in skin.data.vertices]
    bvh=BVHTree.FromPolygons(points,[list(p.vertices) for p in skin.data.polygons])
    origin=Vector((0,0,1.76*k))
    def hairline(angle):
        return .50+.11*math.sin(angle)+.032*math.cos(5*angle+.4)
    def scalp(angle,elevation,lift=.008):
        direction=Vector((math.sin(angle)*math.cos(elevation),-math.cos(angle)*math.cos(elevation),math.sin(elevation)))
        point,normal,_,_=bvh.ray_cast(origin,direction,2)
        if point is None:raise ValueError('Archer hair missed its scalp')
        return point+normal*lift*k
    vertices=[];faces=[]
    columns,rows=41,17
    for row in range(rows):
        t=row/(rows-1)
        for col in range(columns):
            angle=-1.45+2.90*col/(columns-1)
            line=hairline(angle)
            lift=.008+.0035*math.sin(math.pi*t)*(.5+.5*math.cos(angle*15-t*1.8))
            vertices.append(scalp(angle,line+(1.55-line)*t,lift))
    for row in range(rows-1):
        for col in range(columns-1):
            q=row*columns+col;faces.append((q,q+1,q+columns+1,q+columns))
    thin_cloth(f,'detail_fitted_swept_hair',vertices,faces,'hair_auburn','head',.005*k)
    # Broad tapered locks share the same scalp projection and sweep. They grow
    # from the cap instead of forming separate straight sticks across the face.
    for angle in (-1.04,-.69,-.34,.01,.36,.71,1.06):
        locks=[];quads=[]
        for row in range(17):
            t=row/16;a=angle+.24*(1-t);line=hairline(a)
            elevation=1.35*(1-t)+(line+.008)*t
            width=.082*math.sin(math.pi*(.04+.96*t))
            for col in (-1,0,1):
                lift=.009+(1-abs(col))*.006*math.sin(math.pi*t)
                locks.append(scalp(a+col*width,elevation,lift))
        for row in range(16):
            for col in range(2):
                q=row*3+col;quads.append((q,q+3,q+4,q+1))
        thin_cloth(f,'detail_swept_forelock',locks,quads,'hair_auburn','head',.002*k)
    for sign in (-1,1):
        _line(f,skin,'fitted_archer_brow',[(sign*.037*k,1.827*k),
              (sign*.072*k,1.838*k),(sign*.111*k,1.827*k)],'hair_auburn',.006*k,'head')


def _cloth_finish(f, k):
    cloth = 'cloth_ember_light' if f.name == 'ember_archer' else 'cloth_purple_light' if f.name == 'twin_blades' else 'cloth_blue_light'
    for sleeve in _parts(f, 'tailored_sleeve'):
        lo,hi = _bounds(sleeve); c=(lo+hi)/2
        _line(f, sleeve, 'tailored_sleeve_seam', [(c.x,lo.z+(hi.z-lo.z)*.18),
              (c.x,hi.z-(hi.z-lo.z)*.16)], cloth, .0032*k)
    for boot in _parts(f, 'single_boot_shaft'):
        lo,hi = _bounds(boot); x=(lo.x+hi.x)/2
        for t in (.35,.49,.63):
            z=lo.z+(hi.z-lo.z)*t
            _line(f, boot, 'crossed_boot_lacing', [(x-.026*k,z),(x+.026*k,z+.025*k)], 'leather_edge', .0032*k)
            _line(f, boot, 'crossed_boot_lacing', [(x+.026*k,z),(x-.026*k,z+.025*k)], 'leather_edge', .0032*k)
    for cloak in _parts(f, 'tailored_back_cloak'):
        # An inset woven hem shares the original blended cape/cape_tip weights.
        lo,hi=_bounds(cloak)
        for sign in (-1,1):
            vs=[cloak.matrix_world@v.co for v in cloak.data.vertices]
            edge=[p for p in vs if sign*p.x > (hi.x-lo.x)*.40]
            edge.sort(key=lambda p:p.z)
            if len(edge)>3:
                pts=[p+Vector((0,.007,0)) for p in edge[::max(1,len(edge)//8)]]
                ob=f.tube('detail_cloak_bound_selvedge',pts,.0032,cloth,'cape',8)
                _follow_weights(f,ob,cloak)


def _ironclad(f):
    k=f.bones['head']['head'][2]/1.6
    for name in ('single_forged_closed_helmet','single_tapered_cuirass'):
        for ob in _parts(f,name):
            for face in ob.data.polygons:
                face.use_smooth=True
    cuirass=_part(f,'single_tapered_cuirass')
    for sign in (-1,1):
        _inset(f,cuirass,'inlaid_cuirass_flute',[(sign*.042*k,1.12*k),(sign*.078*k,1.16*k),
               (sign*.19*k,1.33*k),(sign*.19*k,1.385*k),(sign*.145*k,1.34*k)],'steel_shadow')
        _line(f,cuirass,'forged_flute_edge',[(sign*.061*k,1.13*k),(sign*.117*k,1.25*k),(sign*.188*k,1.37*k)],'steel_edge',.0038)
    helmet=_part(f,'single_forged_closed_helmet')
    _line(f,helmet,'helm_crown_weld',[(0,1.91*k),(0,2.005*k),(0,2.045*k)],'steel_shadow',.005,'head')
    for side in ('l','r'):
        bone_band(f,'detail_articulated_elbow_collar','forearm_'+side,.055,.077*k,'steel_shadow',.008*k)
        for t in (.12,.19):
            bone_band(f,'detail_gauntlet_wrist_lame','forearm_'+side,1-t,.067*k,'steel_edge',.005*k)
    shield=_part(f,'single_kite_shield');g=grip_center(f,'l')
    for sign in (-1,1):
        _inset(f,shield,'shield_enamel_quarter',[(g.x+sign*.105*k,g.z+.38*k),
               (g.x+sign*.167*k,g.z+.36*k),(g.x+sign*.15*k,g.z+.095*k),
               (g.x+sign*.052*k,g.z-.075*k),(g.x+sign*.060*k,g.z+.055*k)],'cloth_blue','hand_l')
        _line(f,shield,'shield_heraldic_border',[(g.x+sign*.17*k,g.z+.35*k),
              (g.x+sign*.15*k,g.z+.08*k),(g.x+sign*.03*k,g.z-.12*k)],'gold_edge',.004,'hand_l')
    sword=_part(f,'single_forged_sword');g=grip_center(f,'r')
    _line(f,sword,'sword_recessed_fuller',[(g.x,g.z+.23*k),(g.x,g.z+.68*k)],'steel_shadow',.004,'hand_r')
    _cloth_finish(f,k)


def _archer(f,k):
    vest=_part(f,'single_fitted_archer_vest')
    for sign in (-1,1):
        _inset(f,vest,'reinforced_leather_yoke',[(sign*.055*k,1.37*k),(sign*.19*k,1.36*k),
               (sign*.225*k,1.29*k),(sign*.16*k,1.27*k),(sign*.055*k,1.31*k)],'leather_edge')
        _line(f,vest,'tailored_vest_dart',[(sign*.11*k,1.04*k),(sign*.155*k,1.15*k),(sign*.16*k,1.25*k)],'leather_edge',.004)
    _seams(f,vest,'saddle_stitch_vest',9,inset=.29)
    old_bow=_part(f,'single_recurve_bow')
    f.parts.remove(old_bow);bpy.data.objects.remove(old_bow,do_unlink=True)
    g=grip_center(f,'l');points=[];vertices=[];faces=[]
    # A planar recurve requires a constant X cross-section. The generic tube
    # flips its frame at the grip where the Y tangent changes sign.
    for j in range(49):
        t=j/24-1
        points.append(Vector((g.x,g.y+.155*t*t-.021*math.sin(abs(t)*math.pi),g.z+t*.456*k)))
    for j,p in enumerate(points):
        tangent=(points[min(j+1,48)]-points[max(0,j-1)]).normalized()
        transverse=tangent.cross(Vector((1,0,0)))
        radius=.016+.011*(1-abs(j/24-1))
        for n in range(16):
            angle=n*math.tau/16
            vertices.append(p+Vector((math.cos(angle)*radius,0,0))+transverse*math.sin(angle)*radius)
    for j in range(48):
        faces.extend((j*16+n,j*16+(n+1)%16,(j+1)*16+(n+1)%16,(j+1)*16+n) for n in range(16))
    faces.extend([tuple(reversed(range(16))),tuple(48*16+n for n in range(16))])
    f.mesh_data('detail_continuous_recurve_bow',vertices,faces,'wood','hand_l')
    for sign in (-1,1):
        pts=[]
        for j in range(7):
            t=sign*(.22+j*.10);pts.append((g.x-.014,g.y+.155*t*t-.021*math.sin(abs(t)*math.pi),g.z+t*.456*k))
        f.tube('detail_bow_laminated_limb',pts,.0042,'horn','hand_l',8)
        z=g.z+sign*.325*k;y=g.y+.155*(.325/.456)**2-.021*math.sin(.325/.456*math.pi)
        f.tube('detail_bow_tip_binding',[(g.x-.019,y,z-.018*k),(g.x-.019,y,z+.018*k)],.006,'leather_edge','hand_l',8)
    quiver=_part(f,'single_back_quiver')
    # Backside reinforcement is deliberately behind the shoulder, outside draw-hand sweep.
    for z in (1.08,1.31):
        f.ring('detail_quiver_reinforcing_band',(.225*k,.311,z*k),.074*k,.006,'leather_edge','chest')
    _seams(f,quiver,'quiver_seam',7,inset=.35)


def _twins(f,k):
    guard=_part(f,'single_assassin_chest_guard')
    for sign in (-1,1):
        _inset(f,guard,'assassin_lamellar_panel',[(sign*.045*k,1.29*k),(sign*.14*k,1.30*k),
               (sign*.125*k,1.10*k),(sign*.06*k,1.075*k)],'steel_shadow')
        _line(f,guard,'asymmetric_violet_lacing',[(sign*.05*k,1.30*k),(sign*.105*k,1.20*k),
              (sign*.055*k,1.085*k)],'cloth_purple_light',.004)
    mask=_part(f,'fitted_lower_face_mask');_seams(f,mask,'tailored_mask_stitch',5,'cloth_purple_light',.29)
    for sword in _parts(f,'single_forged_sword'):
        lo,hi=_bounds(sword);sign=-1 if (lo.x+hi.x)<0 else 1
        g=grip_center(f,'l' if sign<0 else 'r');length=.58*k
        pts=[(g.x+sign*.07*k*t*t+sign*.018*k,g.z+.124*k+t*length) for t in (.18,.4,.62,.83)]
        _line(f,sword,'tempered_blade_bevel',pts,'steel_edge',.004)
    for side in ('l','r'):
        bone_band(f,'detail_assassin_sleeve_binding','upperarm_'+side,.38,.103*k,'leather_edge',.007)


def _mage(f,k):
    # Preserve the robe length and folds, but let the existing walking knees
    # travel through a real front split instead of through opaque cloth.
    for ob in _parts(f,'single_split_mage_robe')+_parts(f,'single_robe_front_trim'):
        f.parts.remove(ob);bpy.data.objects.remove(ob,do_unlink=True)
    vertices=[];faces=[]
    for row,(z,radius) in enumerate(((.15,.326),(.34,.319),(.61,.277),(.79,.260),(.88,.257))):
        for j in range(41):
            angle=-.70*math.pi+j/40*1.40*math.pi
            fold=.009*math.cos(angle*7)
            vertices.append(((radius+fold)*math.sin(angle)*k,
                             (radius*.64+.035+fold)*math.cos(angle),
                             z*k+.017*math.cos(angle*3)*(1-row/5)))
    for row in range(4):
        for j in range(40):
            q=row*41+j;faces.append((q,q+1,q+42,q+41))
    thin_cloth(f,'detail_split_mage_robe',vertices,faces,'cloth_indigo','pelvis',.012,True)
    for col in (0,40):
        f.tube('detail_robe_front_trim',[vertices[row*41+col] for row in range(5)],.007*k,'brass','pelvis',10)
    torso=_part(f,'single_tailored_torso')
    for sign in (-1,1):
        _line(f,torso,'robe_embroidered_stole',[(sign*.16*k,1.4*k),(sign*.17*k,1.29*k),
              (sign*.13*k,1.13*k),(sign*.10*k,.98*k)],'gold_edge',.006)
        for z in (1.13,1.26,1.36):
            _line(f,torso,'stole_woven_chevron',[(sign*.115*k,z*k),(sign*.14*k,(z-.025)*k),
                  (sign*.16*k,z*k)],'cloth_blue_light',.004)
    book=_part(f,'single_book_pages');lo,hi=_bounds(book);c=(lo+hi)/2
    for z in (lo.z+.025*k,lo.z+.065*k,hi.z-.065*k,hi.z-.025*k):
        # Page-edge grooves are on the exposed side, clear of the gripping fingers.
        f.tube('detail_bound_page_signatures',[(lo.x-.001,c.y-.025,z),(lo.x-.001,c.y+.025,z)],.0016,'horn','hand_l',6)
    covers=_parts(f,'single_book_cover');front=min(covers,key=lambda ob:_bounds(ob)[0].y)
    lo,hi=_bounds(front)
    _line(f,front,'book_gold_tooled_border',[(lo.x+.013,lo.z+.014),(lo.x+.013,hi.z-.014),
          (hi.x-.013,hi.z-.014),(hi.x-.013,lo.z+.014),(lo.x+.013,lo.z+.014)],'gold_edge',.0032,'hand_l')
    g=grip_center(f,'r')
    for sign in (-1,1):
        f.tube('detail_staff_crystal_prong',[(g.x+sign*.031*k,g.y,1.87*k),
               (g.x+sign*.068*k,g.y,1.93*k),(g.x+sign*.062*k,g.y,2.005*k)],
               [.010,.008,.003],'brass','hand_r',10)


def _skull_detail(f, child=False):
    base=_part(f,'sculpted_cranial_vault')
    skull=_unify(f,[base]+_parts(f,'maxillary_bone')+_parts(f,'angular_cheek')+_parts(f,'supraorbital_ridge'),
                 'sculpted_skull', 'head', .0045 if child else .006)
    # Preserve the existing semantic surface contract used by production QA.
    skull.vertex_groups.new(name='QA_face').add(list(range(len(skull.data.vertices))),1,'REPLACE')
    lo,hi=_bounds(skull);cx=(lo.x+hi.x)/2;w=hi.x-lo.x;h=hi.z-lo.z
    _line(f,skull,'cranial_suture',[(cx,lo.z+h*.70),(cx+w*.025,lo.z+h*.80),
          (cx-w*.015,lo.z+h*.87),(cx+w*.01,lo.z+h*.94)],'horn',.0015 if child else .0022,'head')
    for sign in (-1,1):
        _line(f,skull,'temporal_bone_suture',[(cx+sign*w*.24,lo.z+h*.77),
              (cx+sign*w*.32,lo.z+h*.70),(cx+sign*w*.37,lo.z+h*.61)],'horn',.0014 if child else .002,'head')
    for side in ('l','r'):
        for limb in ('thigh','upperarm'):
            b=f.bones[limb+'_'+side];a=Vector(b['head']);end=Vector(b['tail']);d=end-a
            r=(.027 if limb=='thigh' else .022) if child else (.060 if limb=='thigh' else .050)
            f.tube('detail_anatomical_bone_flute',[a+d*.25+Vector((0,-r*.82,0)),
                   a+d*.50+Vector((0,-r*.72,0)),a+d*.73+Vector((0,-r*.75,0))],
                   [r*.12,r*.16,r*.10],'horn',limb+'_'+side,8)


def _warden(f):
    _skull_detail(f)
    for index in range(4):
        skull=_part(f,'captive_cranium_'+str(index));lo,hi=_bounds(skull);cx=(lo.x+hi.x)/2
        _line(f,skull,'captive_cranial_suture',[(cx,hi.z-.007),(cx-.003,hi.z-.017),
              (cx+.003,hi.z-.026)],'horn',.0011,'captive_head_'+str(index))
    for token in ('curved_iron_pauldron','tapered_shin_greave','forged_hip_fan'):
        for shell in _parts(f,token):
            lo,hi=_bounds(shell);cx=(lo.x+hi.x)/2;w=hi.x-lo.x;h=hi.z-lo.z
            _line(f,shell,'carved_guard_armor_border',[(cx-w*.24,lo.z+h*.23),(cx-w*.27,lo.z+h*.63),
                  (cx,lo.z+h*.82),(cx+w*.27,lo.z+h*.63),(cx+w*.24,lo.z+h*.23)],'steel_edge',.0036)
    maul=_part(f,'octagonal_forged_maul_head')
    _inset(f,maul,'maul_soul_escutcheon',[(.52,1.875),(.58,1.845),(.64,1.875),(.64,1.975),(.58,2.005),(.52,1.975)],'steel_shadow','maul_socket')
    _line(f,maul,'maul_carved_soul_sigil',[(.55,1.885),(.58,1.925),(.55,1.965),(.61,1.965),
          (.58,1.925),(.61,1.885)],'teal_light',.0045,'maul_socket')


def _skeleton(f):
    _skull_detail(f,True)
    blade=_part(f,'tapered_chipped_short_sword');lo,hi=_bounds(blade);x=(lo.x+hi.x)/2
    _line(f,blade,'worn_sword_edge',[(x-.012,lo.z+.025),(x-.008,hi.z-.065)],'steel_edge',.0022,'sword_socket')
    for side in ('l','r'):
        knee=Vector(f.bones['shin_'+side]['head'])
        f.ellipsoid('detail_anatomical_patella',knee+Vector((0,-.023,0)),(.017,.011,.020),'ivory','shin_'+side,16)


def _reaper(f):
    cowl=_part(f,'draped_open_cowl')
    # Paired stitched darts follow the hood's side panels, preserving the open face.
    vertices=[cowl.matrix_world@v.co for v in cowl.data.vertices]
    for col in (2,22):
        pts=[]
        for row in range(4):
            p=vertices[row*25+col]
            pts.append(p+Vector((p.x,p.y-.02,0)).normalized()*.003)
        f.tube('detail_tailored_cowl_dart',pts,.0028,'cloth_purple_light','head',8)
    for j in range(6):
        gore=_part(f,'curved_midnight_cloak_gore_'+str(j))
        verts=[gore.matrix_world@v.co for v in gore.data.vertices]
        # Each strip has nine columns/five rows. Surface weights follow the existing cloth rig.
        for col in (1,7):
            pts=[verts[row*9+col]+Vector((verts[row*9+col].x,verts[row*9+col].y-.015,0)).normalized()*.003 for row in range(1,5)]
            ob=f.tube('detail_cloak_woven_panel_edge',pts,.0028,'cloth_purple_light','cloth_'+str(j),8)
            _follow_weights(f,ob,gore)
    blade=_part(f,'tapered_silver_crescent')
    points=[]
    for i in range(18):
        u=.10+i*.039;angle=math.radians(88-184*u);thick=.120*(1-u)**.58+.026*math.sin(math.pi*u)
        r=.535-thick*.43;points.append((.555+r*math.cos(angle),1.645+r*math.sin(angle)))
    _line(f,blade,'forged_scythe_fuller',points,'steel_shadow',.0034,'scythe_socket')
    for t in (.18,.36,.54):
        angle=math.radians(88-184*t);r=.535-(.120*(1-t)**.58+.026*math.sin(math.pi*t))*.56
        x=.555+r*math.cos(angle);z=1.645+r*math.sin(angle)
        _line(f,blade,'scythe_forged_inlay',[(x-.009,z-.012),(x+.009,z+.012)],'cloth_purple_light',.003,'scythe_socket')


def _giant(f):
    head=_part(f,'forged_helmet')
    for sign in (-1,1):
        _line(f,head,'forged_helmet_crown_channel',[(sign*.048,1.315),(sign*.042,1.347),
              (sign*.030,1.362)],'steel_edge',.0035,'head')
    # Existing six shield parts keep their original owning bones and QA groups.
    for i in range(6):
        ob=_part(f,'raised_forged_shield_face_'+str(i));lo,hi=_bounds(ob);c=(lo+hi)/2;w=hi.x-lo.x;h=hi.z-lo.z
        # Front relief only; rear panels receive the matching existing reverse-facing construction.
        if i<2:
            relief=_inset(f,ob,'shield_stepped_service_hatch_'+str(i),
                          [(c.x-w*.19,c.z-h*.20),(c.x-w*.22,c.z+h*.19),
                           (c.x+w*.22,c.z+h*.19),(c.x+w*.19,c.z-h*.20)],'steel_shadow','shield_panel_'+str(i))
            if relief:
                relief.vertex_groups.new(name='QA_shield_panel_'+str(i)).add(list(range(len(relief.data.vertices))),1,'REPLACE')
    for side in ('l','r'):
        sign=-1 if side=='l' else 1
        elbow=Vector(f.bones['forearm_'+side]['head'])
        # Machined retaining washer on the exposed axle, outside the forearm sweep.
        f.ring('detail_elbow_axle_retaining_washer',elbow+Vector((sign*.086,0,0)),.039,.004,'steel_edge','forearm_'+side,'X')
        fore=f.bones['forearm_'+side];a=Vector(fore['head']);b=Vector(fore['tail'])
        f.tube('detail_forearm_service_hose',[a.lerp(b,.22)+Vector((0,.10,0)),
               a.lerp(b,.48)+Vector((0,.13,0)),a.lerp(b,.72)+Vector((0,.09,0))],
               [.012,.014,.012],'black','forearm_'+side,10)
    torso=_part(f,'forged_barrel_torso')
    for sign in (-1,1):
        _line(f,torso,'cast_body_section_joint',[(sign*.31,.81),(sign*.33,.90),(sign*.30,1.02)],'steel_edge',.004,'chest')


def polish(f, skinned):
    if not skinned or f.name not in CHARACTERS:
        return
    before=len(f.parts)
    if f.name=='ironclad':
        _ironclad(f)
    elif f.name=='tower_archer':
        _archer_head(f,f.bones['head']['head'][2]/1.6)
    elif f.name in ('ember_archer','twin_blades','arc_mage'):
        k=f.bones['head']['head'][2]/1.6
        _expressive_face(f,k);_cloth_finish(f,k)
        {'ember_archer':_archer,'twin_blades':_twins,'arc_mage':_mage}[f.name](f,k)
        if f.name=='ember_archer':_archer_head(f,k)
    else:
        {'bone_warden':_warden,'bone_warden_skeleton':_skeleton,
         'aether_reaper':_reaper,'iron_giant':_giant}[f.name](f)
    if hasattr(f,'expansion_design'):
        f.expansion_design['surfaceCraftRevision']='roster-humanoid-2.0.0'
    print('RIFT_HUMANOID_DETAIL',f.name,'parts',before,'->',len(f.parts),flush=True)
