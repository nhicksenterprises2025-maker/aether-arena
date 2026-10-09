"""Original Rift Crown production asset forge. Run with Blender 5.2 --background --python.

Every mesh, texture and animation is authored here; the V15 primitive GLBs are not imported.
Exports declare meters in FBX (UnitScaleFactor=100 cm per FBX unit).
The importer must use convert_scene_unit=True and import_uniform_scale=1, never 100.
"""
from pathlib import Path
from types import SimpleNamespace
import bpy, math, json, hashlib, random, argparse, sys, tempfile, os, time
from mathutils import Vector, Quaternion

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'Build'))
from split_guard_cannon import split_guard_mesh, guard_socket_metadata
EXPORT_WORK = Path(tempfile.mkdtemp(prefix='RiftCrown-FBX-'))
SRC, OUT, RENDER = ROOT/'Assets/Source', ROOT/'Assets/Export', ROOT/'Assets/Renders'
for d in (SRC, OUT, RENDER, SRC/'Textures'): d.mkdir(parents=True, exist_ok=True)
ARGS=argparse.ArgumentParser()
ARGS.add_argument('--no-renders',action='store_true')
opts=ARGS.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
random.seed(15082026)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
for c in list(bpy.data.collections):
    if c.name!='Collection': bpy.data.collections.remove(c)
scene=bpy.context.scene; scene.unit_settings.system='METRIC'; scene.unit_settings.scale_length=1
bpy.context.preferences.filepaths.save_version=0
scene.render.engine='BLENDER_EEVEE'; scene.render.resolution_x=1024; scene.render.resolution_y=1024
scene.render.resolution_percentage=100; scene.render.image_settings.file_format='PNG'
scene.render.film_transparent=False; scene.render.fps=30
scene.world.use_nodes=True; scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.045,.065,.095,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.55
scene.view_settings.view_transform='AgX'

PALETTE={
 'iron':(.14,.21,.26,.72,.38), 'steel':(.32,.43,.50,.82,.30),
 'silver':(.70,.82,.87,.88,.23), 'brass':(.65,.44,.16,.74,.38),
 'cloth_blue':(.045,.19,.30,.0,.87), 'cloth_ember':(.43,.085,.044,.0,.9),
 'cloth_purple':(.19,.066,.29,.0,.86), 'cloth_indigo':(.09,.12,.34,.0,.82),
 'leather':(.125,.057,.03,.0,.85), 'wood':(.25,.105,.044,.0,.91),
 'skin':(.67,.40,.25,.0,.78), 'stone':(.24,.30,.285,.0,.95),
 'limestone':(.52,.58,.57,.0,.91), 'stone_dark':(.095,.13,.14,.0,.96),
 'fur':(.235,.115,.048,.0,.94), 'fur_dark':(.095,.039,.021,.0,.94),
 'horn':(.68,.57,.38,.0,.63), 'teal':(.025,.29,.315,.0,.56),
 'teal_light':(.08,.49,.47,.0,.52), 'plum':(.125,.035,.13,.0,.88),
 'membrane':(.32,.055,.16,.0,.69), 'ice_fur':(.59,.77,.82,.0,.86),
 'ice_steel':(.17,.36,.44,.53,.32), 'ice':(.27,.68,.82,.10,.20),
 'raven':(.045,.07,.155,.0,.71), 'raven_blue':(.10,.19,.365,.05,.52),
 'black':(.012,.018,.025,.0,.85), 'ivory':(.82,.87,.85,.0,.42),
 'cyan':(.025,.63,.85,.0,.24), 'violet':(.44,.055,.76,.0,.25),
 'ember':(.94,.22,.045,.0,.30), 'team':(.035,.45,.65,.2,.52),
 'grass':(.11,.25,.08,.0,.94), 'grass_light':(.23,.36,.105,.0,.90),
 'water':(.025,.21,.30,.15,.16), 'glass':(.235,.025,.43,.1,.14),
}
COLOR_INDEX={k:i for i,k in enumerate(PALETTE)}
ATLAS=1024; CELLS=8; CELL=ATLAS//CELLS

def textures():
    # Tile-specific grain/pores and edge wear; texture coordinates remain shared at all LODs.
    images={}
    for channel in ('BaseColor','ORM','Normal','TeamMask'):
        im=bpy.data.images.new('T_RiftAtlas_'+channel,ATLAS,ATLAS,alpha=False)
        pix=[0.0]*(ATLAS*ATLAS*4)
        for key,index in COLOR_INDEX.items():
            base=PALETTE[key]; cx,cy=index%CELLS,index//CELLS
            for yy in range(CELL):
                for xx in range(CELL):
                    u,v=xx/CELL,yy/CELL
                    noise=(math.sin(xx*12.9898+yy*78.233+index*13.0)*43758.5453)%1
                    grain=math.sin(u*70+math.sin(v*11)*2)*.5+.5
                    cloth=math.sin(u*190)*math.sin(v*190)
                    stone=math.sin(u*21+math.sin(v*14))*math.cos(v*26-u*12)
                    texture=grain if key in ('wood','horn') else cloth if key.startswith('cloth') else stone
                    edge=min(u,v,1-u,1-v)
                    value=.89+.12*noise+.055*texture+(.09 if edge<.028 else 0)
                    if channel=='BaseColor': col=[min(1,max(0,b*value)) for b in base[:3]]
                    elif channel=='ORM': col=[.88+.1*noise,min(1,max(.05,base[4]+.06*(noise-.5))),base[3]]
                    elif channel=='Normal': col=[.5+.025*math.sin(u*55+v*11),.5+.025*math.cos(v*59-u*8),.998]
                    else: col=[1,1,1] if key=='team' else [0,0,0]
                    off=(((cy*CELL+yy)*ATLAS)+(cx*CELL+xx))*4
                    pix[off:off+4]=col+[1]
        im.pixels.foreach_set(pix); im.filepath_raw=str(SRC/'Textures'/('T_RiftAtlas_'+channel+'.png'))
        im.file_format='PNG'; im.save(); im.pack()
        if channel!='BaseColor': im.colorspace_settings.name='Non-Color'
        images[channel]=im
    return images

TEX=textures()
def make_material(name,glow=False,glass=False):
    m=bpy.data.materials.new(name); m.use_nodes=True
    nodes=m.node_tree.nodes; links=m.node_tree.links; p=nodes.get('Principled BSDF')
    albedo=nodes.new('ShaderNodeTexImage'); albedo.image=TEX['BaseColor']; albedo.label='Shared authored palette atlas'
    orm=nodes.new('ShaderNodeTexImage'); orm.image=TEX['ORM']
    separate=nodes.new('ShaderNodeSeparateColor'); links.new(orm.outputs['Color'],separate.inputs['Color'])
    links.new(albedo.outputs['Color'],p.inputs['Base Color']); links.new(separate.outputs['Green'],p.inputs['Roughness'])
    links.new(separate.outputs['Blue'],p.inputs['Metallic'])
    normal=nodes.new('ShaderNodeTexImage'); normal.image=TEX['Normal']
    nm=nodes.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value=.25
    links.new(normal.outputs['Color'],nm.inputs['Color']); links.new(nm.outputs['Normal'],p.inputs['Normal'])
    if glow:
        links.new(albedo.outputs['Color'],p.inputs['Emission Color']); p.inputs['Emission Strength'].default_value=2.3
    if glass:
        p.inputs['Transmission Weight'].default_value=.22; p.inputs['IOR'].default_value=1.46
        p.inputs['Roughness'].default_value=.16
    return m
SURFACE=make_material('M_RiftSurface'); GLOW=make_material('M_RiftGlow',glow=True); GLASS=make_material('M_RiftGlass',glass=True)
manifest={'schema':1,'build':'UE-1.2.0','source':'original procedural sculpt and rig authored for Rift Crown Arena',
 'units':{'blender':'meter','fbx':'meter','fbxCentimetersPerUnit':100,'unrealImportUniformScale':1,'convertSceneUnit':True,
          'exportAxisForward':'-Y','exportAxisUp':'Z','unrealForceFrontXAxis':True,'designForward':'-Y in Blender; +X in Unreal'},
 'materials':[{'name':m.name,'baseColor':Path(TEX['BaseColor'].filepath_raw).relative_to(ROOT).as_posix(),
               'orm':Path(TEX['ORM'].filepath_raw).relative_to(ROOT).as_posix(),
               'normal':Path(TEX['Normal'].filepath_raw).relative_to(ROOT).as_posix(),
               'teamMask':Path(TEX['TeamMask'].filepath_raw).relative_to(ROOT).as_posix()} for m in (SURFACE,GLOW,GLASS)],
 'characters':{},'statics':{},'illustrations':{},'renders':[]}

class Forge:
    def __init__(self,name):
        self.name=name; self.collection=bpy.data.collections.new(name); scene.collection.children.link(self.collection)
        self.parts=[]; self.bones={}; self.sockets={}; self.rig=None; self.mesh=None; self.family='static'
    def add(self,obj,key='steel',bone=None,bevel=0,smooth=True,glow=False):
        for c in list(obj.users_collection): c.objects.unlink(obj)
        self.collection.objects.link(obj); obj.name=self.name+'_'+obj.name
        if bevel:
            mod=obj.modifiers.new('Crafted edge radius','BEVEL'); mod.width=bevel; mod.segments=3
            bpy.context.view_layer.objects.active=obj; obj.select_set(True)
            bpy.ops.object.modifier_apply(modifier=mod.name)
        for p in obj.data.polygons: p.use_smooth=smooth
        obj.data.materials.clear(); obj.data.materials.append(GLOW if glow else GLASS if key=='glass' else SURFACE)
        # Preserve per-part UV islands within material palette tile, with a safe padded border.
        if not obj.data.uv_layers:
            uv=obj.data.uv_layers.new(name='UVMap')
            for poly in obj.data.polygons:
                dominant=max(range(3),key=lambda k:abs(poly.normal[k])); axes=[k for k in range(3) if k!=dominant]
                for li in poly.loop_indices:
                    co=obj.data.vertices[obj.data.loops[li].vertex_index].co
                    uv.data[li].uv=((co[axes[0]]*.39)%1,(co[axes[1]]*.39)%1)
        uv=obj.data.uv_layers.active; idx=COLOR_INDEX[key]; cx,cy=idx%CELLS,idx//CELLS
        for loop in uv.data:
            u,v=loop.uv; loop.uv=((cx+.07+u*.86)/CELLS,(cy+.07+v*.86)/CELLS)
        if bone:
            group=obj.vertex_groups.new(name=bone); group.add(list(range(len(obj.data.vertices))),1.0,'REPLACE')
        self.parts.append(obj); return obj
    def bone(self,name,head,tail,parent=None):
        self.bones[name]={'head':list(head),'tail':list(tail),'parent':parent}; return name
    def socket(self,name,loc,parent):
        self.socket_data(name,loc,parent); self.bone(name,loc,Vector(loc)+Vector((0,0,.045)),parent)
    def socket_data(self,name,loc,parent): self.sockets[name]={'bone':name,'parent':parent,'positionMeters':list(loc)}
    def ellipsoid(self,name,loc,scale,key,bone=None,detail=24,glow=False):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=detail,ring_count=max(8,detail//2),radius=1,location=loc)
        o=bpy.context.object; o.name=name; o.scale=scale
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        return self.add(o,key,bone,glow=glow)
    def panel(self,name,loc,size,key,bone=None,rotation=(0,0,0),bevel=.025):
        bpy.ops.mesh.primitive_cube_add(size=1,location=loc,rotation=rotation)
        o=bpy.context.object; o.name=name; o.scale=size; bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        return self.add(o,key,bone,bevel=bevel,smooth=False)
    def mesh_data(self,name,vertices,faces,key,bone=None,smooth=True,glow=False):
        data=bpy.data.meshes.new(name); data.from_pydata(vertices,[],faces); data.update()
        o=bpy.data.objects.new(name,data); self.collection.objects.link(o)
        return self.add(o,key,bone,smooth=smooth,glow=glow)
    def tube(self,name,points,radii,key,bone=None,sides=12,glow=False):
        vs=[]; faces=[]; pts=[Vector(p) for p in points]
        for i,p in enumerate(pts):
            tangent=(pts[min(i+1,len(pts)-1)]-pts[max(0,i-1)]).normalized()
            a=tangent.cross(Vector((0,0,1)))
            if a.length<.01: a=tangent.cross(Vector((0,1,0)))
            a.normalize(); b=tangent.cross(a).normalized(); r=radii[i] if isinstance(radii,(list,tuple)) else radii
            if not isinstance(r,(tuple,list)): r=(r,r)
            for s in range(sides):
                ang=s*2*math.pi/sides; vs.append(p+a*(math.cos(ang)*r[0])+b*(math.sin(ang)*r[1]))
        for i in range(len(pts)-1):
            for s in range(sides): faces.append((i*sides+s,i*sides+(s+1)%sides,(i+1)*sides+(s+1)%sides,(i+1)*sides+s))
        faces.extend([tuple(reversed(range(sides))),tuple((len(pts)-1)*sides+s for s in range(sides))])
        return self.mesh_data(name,vs,faces,key,bone,glow=glow)
    def ring(self,name,loc,radius,thick,key,bone=None,axis='Z',glow=False):
        pts=[]
        for i in range(33):
            a=i*math.pi/16
            offset=Vector((math.cos(a)*radius,math.sin(a)*radius,0))
            if axis=='Y':offset=Vector((offset.x,0,offset.y))
            if axis=='X':offset=Vector((0,offset.x,offset.y))
            pts.append(Vector(loc)+offset)
        return self.tube(name,pts,thick,key,bone,8,glow)
    def rivet(self,loc,bone=None,key='brass',size=.024): return self.ellipsoid('rivet',loc,(size,size*.7,size),key,bone,12)
    def rune(self,loc,size=.18,key='cyan',bone=None):
        x,y,z=loc
        for sign in (-1,1):
            self.tube('rift_rune',[(x-sign*size*.48,y,z-size*.64),(x+sign*size*.48,y,z+size*.64)],[.012,.012],key,bone,6,True)
    def finish(self,skinned=False):
        if skinned: polish_character(self)
        bpy.ops.object.select_all(action='DESELECT')
        for o in self.parts:o.select_set(True)
        bpy.context.view_layer.objects.active=self.parts[0]; bpy.ops.object.join()
        self.mesh=bpy.context.object; self.mesh.name=('SK_' if skinned else 'SM_')+self.name
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        # Export object at origin; generated primitive locations are joined into vertices.
        scene.cursor.location=(0,0,0); bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        if skinned:
            data=bpy.data.armatures.new('SKEL_'+self.name); rig=bpy.data.objects.new('SKEL_'+self.name,data)
            self.collection.objects.link(rig); bpy.context.view_layer.objects.active=rig; rig.select_set(True)
            bpy.ops.object.mode_set(mode='EDIT')
            for name,b in self.bones.items():
                eb=data.edit_bones.new(name); eb.head=b['head']; eb.tail=b['tail']
                if b['parent']: eb.parent=data.edit_bones[b['parent']]
                eb.use_deform=name not in self.sockets
            bpy.ops.object.mode_set(mode='OBJECT'); self.mesh.parent=rig
            mod=self.mesh.modifiers.new('Authored skeletal deformation','ARMATURE');mod.object=rig
            self.rig=rig
        return self

def humanoid_rig(f,height=1.95):
    f.family='humanoid'; k=height/1.95
    f.bone('root',(0,0,0),(0,0,.12))
    f.bone('pelvis',(0,0,.85*k),(0,0,1.0*k),'root')
    f.bone('spine',(0,0,1.0*k),(0,0,1.29*k),'pelvis')
    f.bone('chest',(0,0,1.29*k),(0,0,1.48*k),'spine')
    f.bone('neck',(0,0,1.48*k),(0,0,1.60*k),'chest')
    f.bone('head',(0,0,1.60*k),(0,0,1.88*k),'neck')
    for s,side in ((-1,'l'),(1,'r')):
        f.bone('upperarm_'+side,(s*.26*k,0,1.42*k),(s*.43*k,-.01,1.16*k),'chest')
        f.bone('forearm_'+side,(s*.43*k,-.01,1.16*k),(s*.48*k,-.09, .93*k),'upperarm_'+side)
        f.bone('hand_'+side,(s*.48*k,-.09,.93*k),(s*.49*k,-.14,.83*k),'forearm_'+side)
        f.bone('thigh_'+side,(s*.15*k,0,.89*k),(s*.16*k,-.015,.49*k),'pelvis')
        f.bone('shin_'+side,(s*.16*k,-.015,.49*k),(s*.17*k,0,.14*k),'thigh_'+side)
        f.bone('foot_'+side,(s*.17*k,0,.14*k),(s*.17*k,-.16,.10*k),'shin_'+side)
        f.socket('foot_'+side+'_contact',(s*.17*k,-.10,.035), 'foot_'+side)
    f.bone('cape',(0,.15,1.4*k),(0,.23,.65*k),'chest')
    f.socket('hp_anchor',(0,0,2.2*k),'head'); f.socket('status_anchor',(0,0,1.58*k),'chest')
    f.socket('attack_origin',(.49*k,-.30,1.1*k),'hand_r'); f.socket('impact_origin',(0,-.18,1.05*k),'chest')
    return k

def body_humanoid(f,cloth='cloth_blue',armor=False,height=1.95,masked=False):
    k=humanoid_rig(f,height)
    f.ellipsoid('anatomy_torso',(0,.02,1.16*k),(.29*k,.20*k,.40*k),cloth,'chest')
    f.ellipsoid('pelvic_wrap',(0,0,.83*k),(.27*k,.19*k,.17*k),'leather','pelvis')
    f.tube('neck',[(0,0,1.46*k),(0,0,1.63*k)],[.075*k,.095*k],'skin','neck',16)
    f.ellipsoid('sculpt_head',(0,-.014,1.72*k),(.155*k,.14*k,.22*k),'skin','head',32)
    # Sculpted face with forward nose, cheeks, muzzle line and brows (front is -Y).
    f.ellipsoid('nose',(0,-.143,1.70*k),(.027*k,.044*k,.061*k),'skin','head',20)
    for s in (-1,1):
        f.ellipsoid('cheek_plane',(s*.092*k,-.095,1.685*k),(.043*k,.033*k,.054*k),'skin','head',20)
        f.ellipsoid('eye_socket',(s*.065*k,-.131,1.762*k),(.032*k,.014*k,.018*k),'black','head',20)
        f.ellipsoid('eye',(s*.066*k,-.145,1.765*k),(.012*k,.007*k,.009*k),'cyan' if cloth=='cloth_indigo' else 'ivory','head',16,cloth=='cloth_indigo')
        f.tube('brow',[(s*.039*k,-.14,1.797*k),(s*.097*k,-.126,1.800*k)],[.012*k,.016*k],'leather','head',8)
    f.tube('mouth',[(-.051*k,-.147,1.618*k),(0,-.16,1.611*k),(.051*k,-.147,1.618*k)],.008,'leather','head',8)
    for s,side in ((-1,'l'),(1,'r')):
        f.tube('upper_arm',[(s*.26*k,0,1.42*k),(s*.37*k,-.015,1.3*k),(s*.43*k,-.01,1.16*k)],[.13*k,.115*k,.075*k],cloth,'upperarm_'+side,16)
        f.tube('fore_arm',[(s*.43*k,-.01,1.16*k),(s*.47*k,-.06,1.04*k),(s*.48*k,-.09,.93*k)],[.08*k,.092*k,.067*k],'leather','forearm_'+side,16)
        f.ellipsoid('gloved_hand',(s*.49*k,-.105,.89*k),(.075*k,.07*k,.105*k),'leather','hand_'+side,20)
        for digit in range(3):
            f.tube('glove_finger',[(s*(.45+digit*.024)*k,-.16,.91*k),(s*(.46+digit*.025)*k,-.17,.84*k)],[.017*k,.014*k],'leather','hand_'+side,8)
        f.tube('thigh',[(s*.15*k,0,.84*k),(s*.16*k,-.015,.58*k),(s*.16*k,-.015,.49*k)],[.125*k,.107*k,.075*k],cloth,'thigh_'+side,16)
        f.tube('shin',[(s*.16*k,-.015,.49*k),(s*.17*k,0,.28*k),(s*.17*k,0,.12*k)],[.076*k,.09*k,.07*k],'leather','shin_'+side,16)
        f.ellipsoid('boot',(s*.17*k,-.085,.105*k),(.097*k,.18*k,.095*k),'iron','foot_'+side,20)
        f.panel('boot_sole',(s*.17*k,-.08,.038*k),(.20*k,.32*k,.048),'black','foot_'+side,bevel=.015)
        if armor:
            f.ellipsoid('layered_pauldron',(s*.28*k,0,1.44*k),(.18*k,.22*k,.15*k),'steel','upperarm_'+side)
            f.panel('bracer_plate',(s*.46*k,-.087,1.05*k),(.14*k,.09,.21*k),'steel','forearm_'+side,bevel=.025)
            f.ellipsoid('knee',(s*.16*k,-.068,.50*k),(.10*k,.069,.087*k),'steel','shin_'+side)
            f.panel('shin_plate',(s*.17*k,-.074,.29*k),(.14*k,.056,.24*k),'steel','shin_'+side,bevel=.03)
    f.tube('belt',[(-.26*k,-.15,.85*k),(0,-.205,.85*k),(.26*k,-.15,.85*k)],.035,'leather','pelvis',10)
    f.panel('belt_buckle',(0,-.215,.85*k),(.09,.035,.085),'brass','pelvis',bevel=.011)
    # Curved cloth cape, retaining broad authored folds rather than a flat rectangle.
    vs=[]; faces=[]
    for row in range(9):
        t=row/8; z=(1.39-.75*t)*k; width=(.23+.07*t)*k
        for col in range(11):
            u=col/10*2-1; vs.append((width*u,.18+.085*t+.028*math.cos(u*math.pi*4)*t,z+.025*(1-u*u)*t))
    for row in range(8):
        for col in range(10):
            i=row*11+col;faces.append((i,i+1,i+12,i+11))
    cape=f.mesh_data('sculpted_cape',vs,faces,cloth,'cape')
    sol=cape.modifiers.new('Cloth thickness','SOLIDIFY');sol.thickness=.009
    bpy.context.view_layer.objects.active=cape;bpy.ops.object.modifier_apply(modifier=sol.name)
    if masked:f.panel('lower_face_mask',(0,-.135,1.66*k),(.24*k,.08,.13*k),'black','head',bevel=.035)
    return k

def hood(f,k,key,pointed=False):
    # Curved crown and neck cowl with open face; gives a deliberate hood silhouette.
    vs=[]; faces=[]
    levels=[(1.50,.175,.095),(1.65,.20,.135),(1.83,.185,.15),(1.96,.12,.10),(2.06,.03,.025)]
    if pointed:levels.extend([(2.20,.14,.045),(2.39,.018,.007)])
    for z,rx,ry in levels:
        for j in range(25):
            a=-.10+j/24*(math.pi*1.80);vs.append((rx*math.sin(a)*k,.032+ry*math.cos(a),z*k))
    for row in range(len(levels)-1):
        for j in range(24):i=row*25+j;faces.append((i,i+1,i+26,i+25))
    ob=f.mesh_data('tailored_hood',vs,faces,key,'head')
    sol=ob.modifiers.new('Hood seam thickness','SOLIDIFY');sol.thickness=.014
    bpy.context.view_layer.objects.active=ob;bpy.ops.object.modifier_apply(modifier=sol.name)

def blade(f,loc,length=.72,bone='hand_r',key='silver',curve=0):
    x,y,z=loc
    # Diamond cross-section forged blade with fuller, taper, guard, grip and pommel.
    vs=[]; faces=[]
    for i in range(7):
        t=i/6; width=.052*(1-t*.45) if i<6 else .002; xx=x+curve*t*t
        vs.extend([(xx-width,y,z+t*length),(xx,y-.017,z+t*length),(xx+width,y,z+t*length),(xx,y+.017,z+t*length)])
    for i in range(6):
        for j in range(4):faces.append((i*4+j,i*4+(j+1)%4,(i+1)*4+(j+1)%4,(i+1)*4+j))
    faces.extend([(3,2,1,0),(24,25,26,27)]);f.mesh_data('forged_blade',vs,faces,key,bone,smooth=False)
    f.tube('blade_fuller',[(x,y-.019,z+.07),(x+curve*.7,y-.013,z+length*.83)],[.008,.004],'cyan' if f.name=='ironclad' else 'violet',bone,6,True)
    f.tube('sword_grip',[(x,y,z-.19),(x,y,z-.015)],[.035,.033],'leather',bone,12)
    f.panel('crossguard',(x,y,z),(.26,.065,.048),'brass',bone,bevel=.019)
    f.ellipsoid('pommel',(x,y,z-.20),(.045,.04,.045),'brass',bone,16)

def bow(f,loc,bone='hand_l',scale=1):
    x,y,z=loc; pts=[]
    for i in range(17):
        t=i/16;pts.append((x+math.sin(t*math.pi)*.19*scale,y,z+(t-.5)*1.0*scale))
    f.tube('carved_recurve_bow',pts,[.025+math.sin(i/16*math.pi)*.009 for i in range(17)],'wood',bone,12)
    f.tube('bow_string',[(x,y-.008,z-.50*scale),(x-.025,y-.04,z),(x,y-.008,z+.50*scale)],.004,'ivory',bone,6)
    f.tube('wrapped_bow_grip',[(x+.18*scale,y,z-.09),(x+.18*scale,y,z+.09)],.045,'leather',bone,12)
    for dz in (-.12,.12):f.ring('bow_grip_brass',(x+.18*scale,y,z+dz),.044,.009,'brass',bone)
    f.tube('arrow_shaft',[(x+.04,y+.25,z),(x+.04,y-.55,z)],.007,'wood',bone,8)
    f.tube('arrow_point',[(x+.04,y-.55,z),(x+.04,y-.68,z)],[.026,.001],'silver',bone,10)
    f.ellipsoid('arrow_ember',(x+.04,y-.65,z),(.015,.04,.015),'ember',bone,16,True)

def humanoid(name):
    f=Forge(name); armor=name=='ironclad'; cloth={'ironclad':'cloth_blue','ember_archer':'cloth_ember','twin_blades':'cloth_purple','arc_mage':'cloth_indigo','tower_archer':'cloth_ember'}[name]
    k=body_humanoid(f,cloth,armor,1.95 if armor else 1.85 if name=='arc_mage' else 1.72 if name=='twin_blades' else 1.8, name=='twin_blades')
    if name=='ironclad':
        # Replace visible face and round helmet with a fully enclosed, forged angular helm.
        for o in list(f.parts):
            if any(part in o.name for part in ('sculpt_head','nose','cheek','eye_socket','_eye','brow','mouth')):
                f.parts.remove(o);bpy.data.objects.remove(o,do_unlink=True)
        profile=[(1.54,.12,.13),(1.61,.17,.165),(1.76,.195,.19),(1.90,.175,.18),(2.00,.105,.15),(2.06,.012,.10)]
        vs=[];faces=[]
        for z,rx,ry in profile:
            for j in range(12):
                a=j*math.pi/6;vs.append((math.sin(a)*rx,math.cos(a)*ry,z))
        for row in range(len(profile)-1):
            for j in range(12):q=row*12+j;faces.append((q,row*12+(j+1)%12,(row+1)*12+(j+1)%12,q+12))
        faces.extend([tuple(reversed(range(12))),tuple(60+j for j in range(12))])
        f.mesh_data('forged_enclosed_helmet',vs,faces,'steel','head',False)
        f.panel('angular_visor',(0,-.182,1.80),(.30,.028,.061),'black','head',bevel=.009)
        f.panel('visor_lower_lip',(0,-.192,1.762),(.325,.025,.022),'silver','head',bevel=.007)
        f.tube('visor_glint',[(-.115,-.20,1.81),(.115,-.20,1.81)],.005,'cyan','head',8,True)
        f.tube('helm_nasal_ridge',[(0,-.194,1.78),(0,-.191,1.64),(0,-.162,1.57)],[.023,.018,.012],'brass','head',6)
        for s in (-1,1):
            for j in range(3):f.panel('helmet_breathing_slit',(s*(.05+.026*j),-.176,1.679),(.009,.008,.048),'black','head',bevel=.003)
            f.rivet((s*.133,-.126,1.885),'head')
        f.tube('helm_plume_mount',[(0,.01,2.015),(0,.065,2.15)],[.045,.024],'brass','head',10)
        for j in range(7):f.tube('blue_plume',[(0,.065,2.14),((j-3)*.018,.15,2.13-j*.005),((j-3)*.025,.31,2.0-j*.018)],[.017,.019,.003],'cloth_blue','head',8)
        # Cuirass has sculpted ridge/chamfered facets and a waist taper, not a torso sphere.
        profile=[(.96,.20,.14),(1.08,.24,.17),(1.25,.29,.225),(1.40,.31,.20),(1.48,.24,.14)]
        vs=[];faces=[]
        for z,rx,ry in profile:
            for j in range(16):
                a=j*math.pi/8;x=math.sin(a)*rx;y=math.cos(a)*ry
                if y<0:y-=.025*(1-abs(x)/max(rx,.001))
                vs.append((x,y,z))
        for row in range(4):
            for j in range(16):q=row*16+j;faces.append((q,row*16+(j+1)%16,(row+1)*16+(j+1)%16,q+16))
        faces.extend([tuple(reversed(range(16))),tuple(64+j for j in range(16))])
        f.mesh_data('ridge_forged_cuirass',vs,faces,'steel','chest',False)
        f.panel('cuirass_center',(0,-.236,1.26*k),(.20,.020,.25),'iron','chest',bevel=.036)
        f.tube('breastplate_trim',[(-.24,-.20,1.41*k),(0,-.27,1.45*k),(.24,-.20,1.41*k)],.018,'brass','chest',10)
        f.rune((0,-.261,1.26*k),.15,bone='chest')
        for s,side in ((-1,'l'),(1,'r')):
            # Overlapping armor lames follow upper arm/shoulder articulation.
            for layer in range(3):
                x=s*(.27+.045*layer);z=1.475-.055*layer
                f.panel('pauldron_lame',(x,-.055,z),(.24,.33,.076),'steel','upperarm_'+side,rotation=(0,s*.28,0),bevel=.035)
                f.tube('pauldron_brass_edge',[(x-s*.10,-.225,z-.005),(x+s*.10,-.225,z-.036)],.010,'brass','upperarm_'+side,8)
            f.panel('upper_arm_plate',(s*.37,-.057,1.29),(.13,.14,.20),'steel','upperarm_'+side,rotation=(0,s*.12,0),bevel=.035)
            f.panel('thigh_cuisse',(s*.16,-.088,.665),(.20,.115,.31),'steel','thigh_'+side,bevel=.043)
            for layer in range(3):f.panel('fauld_lame',(s*.14,-.175,.95-layer*.067),(.24,.051,.075),'steel','pelvis',rotation=(0,s*.12,0),bevel=.018)
            f.panel('elbow_couter',(s*.43,-.048,1.15),(.15,.12,.12),'silver','forearm_'+side,bevel=.034)
            f.panel('hand_plate',(s*.49,-.153,.94),(.135,.063,.12),'steel','hand_'+side,bevel=.027)
            for finger in range(3):
                f.tube('gauntlet_finger',[(s*(.453+finger*.025),-.175,.925),(s*(.466+finger*.025),-.183,.86)],[.018,.014],'steel','hand_'+side,8)
            for layer in range(3):f.panel('sabatons',(s*.17,-.055-layer*.06,.16-layer*.012),(.18,.070,.065),'steel','foot_'+side,bevel=.022)
            for z in (.35,.69,1.05):f.rivet((s*.18,-.135,z),'shin_'+side if z<.5 else 'thigh_'+side if z<.8 else 'forearm_'+side,size=.018)
        # Shield kite cross-section: no cuboid slab, shaped shoulders and pointed heel.
        vs=[(-.75,-.14,1.45),(-.46,-.23,1.52),(-.22,-.14,1.40),(-.23,-.20,.94),(-.48,-.27,.72),(-.73,-.20,.95)]
        vs+= [(x,y+.055,z) for x,y,z in vs]; faces=[(0,1,2,3,4,5),(11,10,9,8,7,6)]+[(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
        f.mesh_data('kite_shield',vs,faces,'steel','hand_l',False)
        f.tube('shield_rim',vs[:6]+[vs[0]],.021,'silver','hand_l',10)
        f.rune((-.48,-.275,1.17),.17,bone='hand_l')
        for i in (0,1,2,3,4,5):f.rivet(Vector(vs[i])+Vector((0,-.023,0)),'hand_l')
        blade(f,(.49,-.11,1.00),.78)
    elif name in ('ember_archer','tower_archer'):
        hood(f,k,'cloth_ember')
        f.panel('leather_chest',(0,-.185,1.22*k),(.36,.075,.33),'leather','chest',bevel=.06)
        f.tube('quiver',[(-.24,.14,.90),(-.31,.19,1.47)],[.085,.105],'leather','chest',16)
        for i in range(5):
            x=-.34+i*.028; f.tube('quiver_arrow',[(x,.18,1.22),(x,.20,1.67+i*.014)],.006,'wood','chest',8)
            f.panel('fletching',(x,.20,1.65+i*.014),(.035,.008,.08),'ivory','chest',bevel=.004)
        bow(f,(-.49,-.14,1.15),scale=.93)
        f.ellipsoid('ember_charm',(.16,-.20,1.12),(.038,.02,.045),'ember','chest',16,True)
        for s in (-1,1):f.panel('belt_pouch',(s*.21,-.07,.82),(.12,.10,.14),'leather','pelvis',bevel=.03)
    elif name=='twin_blades':
        hood(f,k,'cloth_purple'); f.rune((0,-.205,1.12*k),.095,'violet','chest')
        blade(f,(-.44,-.11,.93*k),.63,'hand_l',curve=-.065)
        blade(f,(.44,-.11,.93*k),.63,'hand_r',curve=.065)
        for s in (-1,1):f.tube('shoulder_strap',[(s*.24,-.14,1.34*k),(0,-.225,1.08*k)],[.025,.025],'leather','chest',8)
    elif name=='arc_mage':
        hood(f,k,'cloth_indigo',True)
        # Sculpted flowing bell robe with seam strips.
        levels=[(.16,.33),(.35,.34),(.6,.29),(.88,.23),(1.05,.25)]
        vs=[];faces=[]
        for z,r in levels:
            for i in range(33):
                a=i*2*math.pi/32; fold=.013*math.cos(a*9);vs.append(((r+fold)*math.cos(a),(r*.65+fold)*math.sin(a),z*k))
        for j in range(len(levels)-1):
            for i in range(32):q=j*33+i;faces.append((q,q+1,q+34,q+33))
        f.mesh_data('flowing_robe',vs,faces,'cloth_indigo','pelvis')
        f.tube('staff',[(.49,-.10,.23),(.49,-.10,1.93)],[.025,.031],'wood','hand_r',16)
        f.ring('staff_astrolabe',(.49,-.10,1.94),.175,.019,'brass','hand_r','Y')
        f.ring('staff_orbit',(.49,-.10,1.94),.145,.010,'silver','hand_r','X')
        f.ellipsoid('staff_crystal',(.49,-.10,1.94),(.098,.08,.12),'cyan','hand_r',24,True)
        f.panel('spellbook',(-.47,-.02,1.04),(.20,.11,.29),'cloth_purple','hand_l',bevel=.02)
        f.panel('book_pages',(-.47,-.08,1.04),(.17,.024,.25),'ivory','hand_l',bevel=.007)
        f.panel('book_clasp',(-.47,-.10,1.04),(.035,.02,.085),'brass','hand_l',bevel=.007)
        f.rune((0,-.20,1.26*k),.12,bone='chest')
    refine_humanoid(f,k)
    return f.finish(True)

def quadruped_rig(f):
    f.family='quadruped';f.bone('root',(0,0,0),(0,0,.15));f.bone('pelvis',(0,.32,.85),(0,.05,.95),'root')
    f.bone('spine',(0,.05,.95),(0,-.45,1.02),'pelvis');f.bone('neck',(0,-.45,1.02),(0,-.76,1.16),'spine')
    f.bone('head',(0,-.76,1.16),(0,-1.08,1.20),'neck');f.bone('jaw',(0,-.85,1.06),(0,-1.22,1.04),'head')
    for s,side in ((-1,'l'),(1,'r')):
        for y,leg in ((-.42,'front'),(.40,'back')):
            bn=leg+'_'+side
            f.bone(bn+'_upper',(s*.39,y,.80),(s*.43,y+.03,.39),'spine' if leg=='front' else 'pelvis')
            f.bone(bn+'_lower',(s*.43,y+.03,.39),(s*.45,y-.05,.10),bn+'_upper')
            f.bone(bn+'_foot',(s*.45,y-.05,.10),(s*.45,y-.22,.055),bn+'_lower')
            f.socket(bn+'_contact',(s*.45,y-.12,.02),bn+'_foot')
    f.bone('tail',(0,.67,.91),(0,1.08,.73),'pelvis')
    f.socket('attack_origin',(0,-1.26,1.15),'jaw');f.socket('impact_origin',(0,-.51,.95),'spine')
    f.socket('hp_anchor',(0,-.03,1.95),'spine');f.socket('status_anchor',(0,-.43,1.38),'neck')

def quadruped(name):
    f=Forge(name);quadruped_rig(f)
    stone=name=='boulderback'; frost=name=='frost_fang'; key='stone' if stone else 'ice_fur' if frost else 'fur'
    f.ellipsoid('sculpted_body',(0,.1,.98),(.61,.78,.48),key,'spine',40)
    f.ellipsoid('chest',(0,-.39,1.00),(.52,.48,.47),key,'spine',32)
    f.ellipsoid('haunches',(0,.48,.88),(.53,.37,.38),key,'pelvis',32)
    f.ellipsoid('neck_muscle',(0,-.59,1.14),(.34,.36,.35),key,'neck',32)
    f.ellipsoid('sculpted_head',(0,-.86,1.19),(.34 if frost else .40,.32,.31),key,'head',32)
    f.ellipsoid('muzzle',(0,-1.10,1.10),(.26,.24,.18),key,'jaw',28)
    f.ellipsoid('nose',(0,-1.29,1.13),(.10,.060,.057),'stone_dark' if stone else 'black','jaw',24)
    for s in (-1,1):
        f.ellipsoid('eye_brow',(s*.20,-1.045,1.31),(.13,.082,.063),key,'head',20)
        f.ellipsoid('eye',(s*.205,-1.11,1.27),(.032,.014,.022),'violet' if stone else 'cyan' if frost else 'ember','head',20,True)
        f.tube('ear',[(s*.27,-.70,1.38),(s*.38,-.72,1.60),(s*.31,-.78,1.53)],[.07,.014,.012],key,'head',10)
    for s,side in ((-1,'l'),(1,'r')):
        for y,leg in ((-.42,'front'),(.40,'back')):
            bn=leg+'_'+side
            f.tube('muscular_leg',[(s*.38,y,.81),(s*.43,y+.03,.57),(s*.43,y+.03,.38)],[.19,.15,.10],key,bn+'_upper',20)
            f.tube('tapered_leg',[(s*.43,y+.03,.39),(s*.46,y,.24),(s*.45,y-.06,.10)],[.105,.105,.10],key,bn+'_lower',16)
            f.ellipsoid('hoof_or_paw',(s*.45,y-.10,.088),(.17,.23,.094),'stone_dark' if stone else 'ice_steel' if frost else 'black',bn+'_foot',24)
            if frost:
                for dx in (-.07,0,.07):f.tube('claw',[(s*.45+dx,y-.22,.08),(s*.45+dx,y-.32,.04)],[.025,.004],'ivory',bn+'_foot',8)
    if stone:
        # Authored overlapping irregular shell plates with chiseled outer boundaries.
        for row in range(3):
            for col in range(4):
                x=(col-1.5)*.28;y=(row-1)*.33+.17;z=1.34+.21*(1-abs(x)/.7)+.08*math.cos(y*3)
                r=.24+.025*math.sin(row*4+col)
                vs=[(x,y,z+.12)]
                for j in range(7):a=j*math.pi*2/7;vs.append((x+math.cos(a)*r,y+math.sin(a)*r*.8,z-.04+.02*math.sin(j*7+row)))
                vs.append((x,y,z-.17));faces=[(0,j+1,(j+1)%7+1) for j in range(7)]+[(8,(j+1)%7+1,j+1) for j in range(7)]
                f.mesh_data('chiseled_shell_plate',vs,faces,'limestone' if (row+col)%3 else 'stone','spine',False)
        for i,(x,y,z) in enumerate([(-.38,.08,1.65),(0,.18,1.75),(.36,.36,1.62)]):
            f.tube('rift_crystal',[(x,y,z),(x+.03,y+.04,z+.44),(x+.025,y+.06,z+.59)],[.12,.075,.001],'violet','spine',6,True)
        for s in (-1,1):
            f.tube('stone_tusk',[(s*.26,-1.01,1.08),(s*.33,-1.19,1.01),(s*.30,-1.31,1.13)],[.085,.055,.004],'limestone','jaw',10)
            f.tube('rift_vein',[(s*.20,-.68,1.38),(s*.36,-.53,1.18),(s*.46,-.43,1.13)],.009,'violet','spine',6,True)
    else:
        armor='ice_steel' if frost else 'steel'
        f.ellipsoid('saddle_armor',(0,.06,1.33),(.52,.55,.105),armor,'spine',32)
        f.tube('harness',[(0,-.02,1.43),(-.50,-.11,1.15),(-.48,-.14,.83)],[.028,.035,.025],'leather','spine',8)
        f.tube('harness',[(0,-.02,1.43),(.50,-.11,1.15),(.48,-.14,.83)],[.028,.035,.025],'leather','spine',8)
        f.ellipsoid('forehead_armor',(0,-.99,1.39),(.24,.12,.08),armor,'head',24)
        f.tube('tail',[(0,.70,.93),(0,.98,.84),(.08,1.21,.74)],[.08,.055,.013],key,'tail',14)
        if frost:
            for s in (-1,1):
                f.tube('saber_fang',[(s*.16,-1.19,1.06),(s*.175,-1.26,.90),(s*.18,-1.28,.70)],[.052,.04,.002],'ivory','jaw',12)
            for i in range(5):
                x=(i%2*2-1)*.16;y=-.17+i*.18;z=1.39
                f.tube('ice_spine',[(x,y,z),(x*1.2,y+.035,z+.32+(i%2)*.12),(x*1.3,y+.065,z+.44+(i%2)*.12)],[.09,.062,.002],'ice','spine',6)
            f.ring('frost_collar',(0,-.53,1.08),.34,.014,'cyan','neck','Y',True)
        else:
            for s in (-1,1):
                pts=[];rs=[]
                for i in range(18):
                    t=i/17;a=.25+t*5.1;radius=.35*(1-.62*t)
                    pts.append((s*(.32+math.sin(a)*radius),-.88+math.cos(a)*radius,1.42+.04*t));rs.append(.085*(1-.88*t))
                f.tube('sculpted_curled_horn',pts,rs,'horn','head',16)
                for j in range(1,16,2):f.rivet(pts[j],'head','brass',.024)
            f.ellipsoid('harness_gem',(0,-.62,1.26),(.085,.04,.075),'ember','neck',24,True)
            f.panel('saddle_trim',(0,.06,1.435),(.54,.62,.032),'brass','spine',bevel=.065)
        # Individually shaped fur locks around silhouette, not particle fur.
        for s in (-1,1):
            for j in range(12):
                y=-.5+j*.085;x=s*(.48+.025*math.sin(j*2));z=1.06+.09*math.sin(j*.5)
                f.tube('fur_lock',[(x,y,z),(x+s*.04,y+.08,z-.08),(x+s*.035,y+.12,z-.11)],[.035,.026,.001],key,'spine',8)
    refine_quadruped(f,key)
    return f.finish(True)

def flyer_rig(f):
    f.family='flyer'; f.bone('root',(0,0,0),(0,0,.10)); f.bone('body',(0,0,1),(0,-.27,1.09),'root')
    f.bone('neck',(0,-.27,1.09),(0,-.48,1.23),'body');f.bone('head',(0,-.48,1.23),(0,-.70,1.27),'neck')
    f.bone('jaw',(0,-.60,1.19),(0,-.82,1.18),'head');f.bone('tail',(0,.37,1.0),(0,.86,.98),'body')
    for s,side in ((-1,'l'),(1,'r')):
        f.bone('wing_'+side,(s*.24,0,1.05),(s*.73,.04,1.04),'body')
        f.bone('wing_'+side+'_outer',(s*.73,.04,1.04),(s*1.27,.12,1.00),'wing_'+side)
        f.bone('wing_'+side+'_tip',(s*1.27,.12,1),(s*1.56,.18,.98),'wing_'+side+'_outer')
        f.socket('wing_'+side+'_tip_socket',(s*1.56,.18,.98),'wing_'+side+'_tip')
        f.bone('talon_'+side,(s*.12,.02,.84),(s*.13,-.08,.67),'body')
    f.socket('attack_origin',(0,-.85,1.24),'jaw');f.socket('hp_anchor',(0,0,1.72),'body')
    f.socket('status_anchor',(0,-.06,1.3),'body');f.socket('impact_origin',(0,-.35,1.04),'body')

def feather(f,start,end,width,key,bone):
    a,b=Vector(start),Vector(end);d=b-a;side=d.cross(Vector((0,0,1))).normalized()
    vs=[];faces=[]
    for j in range(7):
        t=j/6;p=a+d*t+Vector((0,0,math.sin(t*math.pi)*.055));w=width*math.sin(math.pi*(.11+t*.88))
        vs.extend([p-side*w,p+Vector((0,0,.018)),p+side*w,p-Vector((0,0,.012))])
    for j in range(6):
        for k in range(4):faces.append((j*4+k,j*4+(k+1)%4,(j+1)*4+(k+1)%4,(j+1)*4+k))
    f.mesh_data('sculpted_feather',vs,faces,key,bone)
    f.tube('feather_shaft',[a,b],.004,'raven_blue',bone,6)

def flyer(name):
    f=Forge(name);flyer_rig(f); raven=name=='storm_raven'; manta=name=='sky_manta';bat=name=='vampire_bats'
    key='raven' if raven else 'teal' if manta else 'plum'
    f.ellipsoid('organic_body',(0,.02,1.06),(.31,.49,.25) if raven else (.38,.60,.13) if manta else (.21,.27,.21),key,'body',36)
    f.ellipsoid('chest',(0,-.25,1.09),(.23,.24,.25 if raven else .13 if manta else .18),key,'body',28)
    f.ellipsoid('sculpted_head',(0,-.48,1.24 if raven else 1.08 if manta else 1.24),(.18,.20,.19),key,'head',28)
    for s in (-1,1):
        f.ellipsoid('eye_socket',(s*.12,-.615,1.27 if not manta else 1.14),(.059,.039,.047),'black','head',20)
        f.ellipsoid('glowing_eye',(s*.125,-.643,1.28 if not manta else 1.14),(.033,.016,.025),'cyan' if not bat else 'violet','head',20,True)
    if raven:
        f.tube('sculpted_beak',[(0,-.62,1.23),(0,-.79,1.225),(0,-.87,1.19)],[(.09,.044),(.06,.03),(.002,.004)],'silver','jaw',10)
        for s,side in ((-1,'l'),(1,'r')):
            f.tube('wing_leading_anatomy',[(s*.22,0,1.14),(s*.76,-.02,1.12),(s*1.34,.055,1.09)],[.095,.070,.025],'raven_blue','wing_'+side,16)
            for j in range(14):
                t=j/13;x=s*(.28+t*1.22);y=.02+t*.06
                feather(f,(x,y,1.095),(s*(.44+t*1.22),.47+math.sin(t*math.pi)*.24,1.00-t*.025),.075,'raven_blue' if j%3 else 'raven','wing_'+side if t<.36 else 'wing_'+side+'_outer' if t<.8 else 'wing_'+side+'_tip')
            for j in range(10):
                t=j/9;x=s*(.30+t*1.0)
                feather(f,(x,-.015,1.13),(x+s*.13,.31,1.135),.05,'raven','wing_'+side if t<.4 else 'wing_'+side+'_outer')
            for j in range(4):
                feather(f,(s*.15,.33,1.07),(s*(.20+j*.045),.90+j*.025,1.015),.045,'raven','tail')
            for j in range(3):
                f.tube('hooked_talon',[(s*.12,.02,.83),(s*.12+j*.026,-.08,.72),(s*.12+j*.026,-.15,.73)],[.015,.012,.001],'iron','talon_'+side,8)
            f.tube('electric_wing_conduit',[(s*.28,-.03,1.16),(s*.69,.01,1.16),(s*1.25,.09,1.13)],[.012,.009,.002],'cyan','wing_'+side+'_outer',6,True)
        f.ellipsoid('storm_core',(0,-.395,1.075),(.068,.028,.075),'cyan','body',24,True)
        f.ring('chest_core_mount',(0,-.38,1.075),.11,.013,'brass','body','Y')
    elif manta:
        # Authored continuous wing surface sampled along swept span, with weighted deformation.
        for s,side in ((-1,'l'),(1,'r')):
            vs=[];faces=[]
            for row in range(17):
                t=row/16;x=s*(.22+1.34*t);center=.05+.17*t
                chord=.89*(1-t**1.65)+.018
                front=center-chord*.55;back=center+chord*.45
                for col in range(13):
                    u=col/12;y=front+(back-front)*u
                    z=1.04+.14*math.sin(t*math.pi)-.055*t+math.sin(u*math.pi)*.055*(1-t)
                    vs.append((x,y,z))
            for row in range(16):
                for col in range(12):q=row*13+col;faces.append((q,q+1,q+14,q+13))
            wing=f.mesh_data('curved_manta_wing',vs,faces,'teal_light')
            for b in ('wing_'+side,'wing_'+side+'_outer','wing_'+side+'_tip'):wing.vertex_groups.new(name=b)
            for vi,v in enumerate(wing.data.vertices):
                x=abs(v.co.x); bn='wing_'+side if x<.68 else 'wing_'+side+'_outer' if x<1.23 else 'wing_'+side+'_tip'
                wing.vertex_groups[bn].add([vi],1,'REPLACE')
            solid=wing.modifiers.new('Organic wing thickness','SOLIDIFY');solid.thickness=.017
            bpy.context.view_layer.objects.active=wing;bpy.ops.object.modifier_apply(modifier=solid.name)
            f.tube('cyan_fin_edge',[(s*.27,-.43,1.05),(s*.70,-.30,1.17),(s*1.12,-.10,1.13),(s*1.54,.20,1.01)],[.010,.009,.007,.003],'cyan','wing_'+side+'_outer',8,True)
            for j in range(6):
                x=s*(.38+j*.19);f.tube('fin_vein',[(x,-.28+j*.055,1.06),(x+s*.07,.28,1.075)],.009,'teal','wing_'+side if j<2 else 'wing_'+side+'_outer',8)
        f.tube('flexible_tail',[(0,.48,1.04),(0,.78,1.03),(.035,1.12,1.01),(.08,1.47,.99)],[.075,.052,.025,.002],'teal','tail',16)
        f.ring('dorsal_rift_mark',(0,-.03,1.2),.145,.013,'cyan','body','Z',True)
    else:
        for s,side in ((-1,'l'),(1,'r')):
            f.tube('pointed_ear',[(s*.12,-.40,1.36),(s*.16,-.39,1.56),(s*.18,-.43,1.61)],[.065,.025,.001],'plum','head',12)
            f.tube('vampire_fang',[(s*.055,-.655,1.19),(s*.06,-.67,1.09)],[.022,.001],'ivory','jaw',10)
            # Membrane constrained by anatomically recognizable wing fingers.
            starts=[Vector((s*.23,0,1.07)),Vector((s*.66,-.04,1.10))]
            ends=[Vector((s*1.09,-.11,1.07)),Vector((s*1.13,.22,1.03)),Vector((s*.93,.53,.99)),Vector((s*.57,.47,.98)),Vector((s*.26,.30,.99))]
            for j,end in enumerate(ends[:4]):f.tube('wing_finger',[starts[0],starts[1],end],[.019,.015,.004],'plum','wing_'+side+'_outer' if j<3 else 'wing_'+side,10)
            vs=[tuple(starts[0]),tuple(starts[1])]
            # Scalloped trailing boundary forms five distinct taut membrane panels.
            for j,end in enumerate(ends):
                vs.append(tuple(end))
                if j<len(ends)-1:
                    mid=(end+ends[j+1])*.5;mid+=(starts[0]-mid)*.16;vs.append(tuple(mid))
            faces=[(0,1,2)]+[(0,j,j+1) for j in range(2,len(vs)-1)]
            wing=f.mesh_data('scalloped_bat_membrane',vs,faces,'membrane','wing_'+side+'_outer')
            sol=wing.modifiers.new('Wing membrane thickness','SOLIDIFY');sol.thickness=.006
            bpy.context.view_layer.objects.active=wing;bpy.ops.object.modifier_apply(modifier=sol.name)
        f.ellipsoid('vampire_core',(0,-.215,1.015),(.046,.02,.05),'violet','body',20,True)
    refine_flyer(f)
    return f.finish(True)

def actions(f):
    standard={'Idle':60,'Locomotion':30,'Attack':30,'Hit':16,'Death':45,'Deploy':24,'Status':30,'Turn':24,'Acquire':18}
    special={'rambeast':{'Charge':30},'frost_fang':{'FrostAttack':24,'Breath':60},
             'storm_raven':{'AuraCharge':45,'Pulse':18,'StunDischarge':18,'WingCycle':36},
             'vampire_bats':{'WingCycle':16},'sky_manta':{'WingCycle':42},
             'ember_archer':{'DrawRelease':46},'arc_mage':{'Cast':30},'twin_blades':{'AlternateStrike':44},
             'boulderback':{'HeavyImpact':60},'tower_archer':{'DrawRelease':33}}
    standard.update(special.get(f.name,{})); made=[]
    for name,length in standard.items():
        act=bpy.data.actions.new('AN_'+f.name+'_'+name);f.rig.animation_data_create(); f.rig.animation_data.action=act
        for pb in f.rig.pose.bones:pb.rotation_mode='XYZ';pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
        for frame in range(1,length+1):
            scene.frame_set(frame)
            t=(frame-1)/(length-1);cycle=t*2*math.pi
            for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0)
            def rot(b,x=0,y=0,z=0):
                if b in f.rig.pose.bones:f.rig.pose.bones[b].rotation_euler=(x,y,z)
            def move(b,x=0,y=0,z=0):
                if b in f.rig.pose.bones:
                    f.rig.pose.bones[b].location=f.rig.pose.bones[b].bone.matrix_local.to_3x3().inverted()@Vector((x,y,z))
            attack=name in ('Attack','FrostAttack','HeavyImpact','Cast','DrawRelease','AlternateStrike')
            # A held anticipation and fast contact followed by eased recovery.
            # Release remains .48, matching the authoritative visual event.
            def smooth(v):
                v=max(0,min(1,v));return v*v*(3-2*v)
            wind=smooth(t/.37) if t<.37 else 1-smooth((t-.48)/.40)
            strike=smooth((t-.38)/.10) if t<.48 else 1-smooth((t-.48)/.25)
            if f.family=='humanoid':
                rot('spine',.018*math.sin(cycle));rot('head',0,0,.024*math.sin(cycle+.7));rot('cape',.04*math.sin(cycle+.4))
                if name=='Locomotion':
                    for s,side in ((1,'l'),(-1,'r')):
                        rot('thigh_'+side,.42*math.sin(cycle)*s);rot('shin_'+side,max(0,-math.sin(cycle)*s)*.48)
                        rot('foot_'+side,-.14*math.sin(cycle)*s);rot('upperarm_'+side,-.24*math.sin(cycle)*s)
                    move('pelvis',z=.019*abs(math.sin(cycle)));rot('cape',.10+.065*math.sin(cycle-.35))
                    rot('chest',0,.025*math.sin(cycle),-.045*math.sin(cycle))
                    rot('head',0,0,.025*math.sin(cycle))
                if attack:
                    if f.name in ('ember_archer','tower_archer'):
                        rot('upperarm_l',-1.20*wind,0,-.30*wind);rot('forearm_l',-.22*wind)
                        rot('upperarm_r',-.70*wind,0,.60*wind);rot('forearm_r',-1.15*wind+.50*strike)
                        rot('chest',0,0,-.15*wind)
                    elif f.name=='arc_mage':
                        rot('upperarm_r',-.78*wind-.42*strike);rot('forearm_r',-.47*wind+.35*strike)
                        rot('upperarm_l',-.42*wind);rot('chest',-.08*strike,0,.08*wind)
                    else:
                        rot('upperarm_r',-.95*wind+.95*strike,0,-.30*wind)
                        rot('forearm_r',-.72*wind+.85*strike);rot('chest',-.12*strike,0,.18*wind-.26*strike)
                        if f.name=='twin_blades':
                            alternate=smooth((t-.62)/.13) if t<.75 else 1-smooth((t-.75)/.22);rot('upperarm_l',-.72*wind+.95*alternate);rot('forearm_l',-.48*wind+.65*alternate)
            elif f.family=='quadruped':
                rot('neck',.035*math.sin(cycle));rot('tail',0,.10*math.sin(cycle))
                if name in ('Locomotion','Charge'):
                    charge=name=='Charge'
                    for i,(leg,side) in enumerate((('front','l'),('front','r'),('back','l'),('back','r'))):
                        phase=cycle+(math.pi if i in (1,2) else 0);p=math.sin(phase)
                        rot(leg+'_'+side+'_upper',p*(.46 if charge else .30));rot(leg+'_'+side+'_lower',max(0,-p)*.46)
                        rot(leg+'_'+side+'_foot',-p*.12)
                    move('spine',z=.024*abs(math.sin(cycle)));rot('neck',.20 if charge else -.025*math.sin(cycle))
                    if charge:rot('head',.16);rot('tail',-.20,.1*math.sin(cycle))
                if attack:
                    rot('neck',-.30*wind+.58*strike);rot('head',-.16*wind+.35*strike);rot('jaw',.30*wind-.40*strike)
                    rot('spine',-.055*wind+.10*strike);move('spine',z=.025*wind-.035*strike)
                if name=='Breath':rot('jaw',.10+.035*math.sin(cycle));rot('neck',-.055)
            elif f.family=='flyer':
                fast=f.name=='vampire_bats'; flap=math.sin(cycle)*(0.55 if fast else .29)
                for s,side in ((1,'l'),(-1,'r')):
                    rot('wing_'+side,0,s*flap,0);rot('wing_'+side+'_outer',0,s*(flap*.7+math.sin(cycle-.6)*.12),0)
                    rot('wing_'+side+'_tip',0,s*math.sin(cycle-1.1)*.21,0)
                rot('tail',.08*math.sin(cycle-.5));rot('head',.028*math.sin(cycle));move('body',z=.025*math.sin(cycle))
                if attack:rot('neck',-.18*wind+.27*strike);rot('head',-.12*wind+.2*strike);rot('jaw',.24*wind-.22*strike)
                if name in ('AuraCharge','Pulse','StunDischarge'):
                    openwing=math.sin(t*math.pi)*.25
                    rot('wing_l',0,-openwing);rot('wing_r',0,openwing);rot('head',-.15*math.sin(t*math.pi))
            if name=='Hit':rot('chest' if f.family=='humanoid' else 'spine' if f.family=='quadruped' else 'body',-.16*math.sin(t*math.pi))
            if name=='Status':
                rot('head',.027*math.sin(cycle*5),0,.022*math.sin(cycle*7))
            if name=='Turn':rot('chest' if f.family=='humanoid' else 'spine' if f.family=='quadruped' else 'body',0,0,.22*math.sin(t*math.pi))
            if name=='Acquire':rot('head',-.04*math.sin(t*math.pi),0,.13*math.sin(t*math.pi))
            if name=='Deploy':
                e=math.sin(min(1,t/.65)*math.pi*.5);move('pelvis' if f.family=='humanoid' else 'spine' if f.family=='quadruped' else 'body',z=.08*(1-e))
                rot('head',.15*(1-e))
            if name=='Death':
                e=min(1,t/.7);body='pelvis' if f.family=='humanoid' else 'spine' if f.family=='quadruped' else 'body'
                rot(body,.22*e,.75*e,.23*e);move(body,z=-.38*e if f.family=='humanoid' else -.20*e)
                rot('head',.30*e);rot('neck',.14*e)
                if f.family=='humanoid':rot('thigh_l',-.70*e);rot('thigh_r',-.40*e);rot('shin_l',1.15*e);rot('shin_r',.8*e)
                if f.family=='flyer':rot('wing_l',0,.95*e);rot('wing_r',0,-.95*e)
                bone=f.rig.pose.bones[body]
                desired=Quaternion(Vector((1,0,0)),1.15*e)@Quaternion(Vector((0,1,0)),.25*e)@bone.bone.matrix_local.to_quaternion()
                set_world_rotation(bone,desired)
            if attack and f.family=='humanoid':
                if f.name in ('ember_archer','tower_archer'):
                    aim_arm(f,'l',Vector((-.28,-.46,1.38)),wind)
                    aim_arm(f,'r',Vector((.12,-.30,1.54)),wind)
                    # Keep bow and attached arrow vertical/forward while the shoulder draws.
                    hand=f.rig.pose.bones['hand_l'];set_world_rotation(hand,hand.bone.matrix_local.to_quaternion())
                elif f.name in ('ironclad','twin_blades'):
                    side='r';raised=Vector((.32,-.18,1.61));contact=Vector((.30,-.47,1.20))
                    target=raised.lerp(contact,strike);aim_arm(f,side,target,wind)
                    hand=f.rig.pose.bones['hand_r'];turn=Vector((0,0,1)).rotation_difference(Vector((0,-1,-.20)).normalized())
                    q=hand.bone.matrix_local.to_quaternion();set_world_rotation(hand,q.slerp(turn@q,strike))
                    if f.name=='twin_blades':
                        alternate=smooth((t-.62)/.13) if t<.75 else 1-smooth((t-.75)/.22);aim_arm(f,'l',Vector((-.30,-.45,1.18)),alternate)
                        hand=f.rig.pose.bones['hand_l'];q=hand.bone.matrix_local.to_quaternion();set_world_rotation(hand,q.slerp(turn@q,alternate))
            for pb in f.rig.pose.bones:
                pb.keyframe_insert('rotation_euler',frame=frame,group=pb.name)
                pb.keyframe_insert('location',frame=frame,group=pb.name)
                pb.keyframe_insert('scale',frame=frame,group=pb.name)
        act.use_fake_user=True
        made.append({'name':name,'action':act.name,'frames':length,'seconds':(length-1)/30,
                     'releaseNormalized':.48 if attack else None,'loop':name in ('Idle','Locomotion','Charge','WingCycle','Breath','Status'),
                     'object':act})
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    return made

def export_selected(path,objects,animation=False,start=1,end=1):
    # A cloud-sync filter may briefly lock an existing FBX while Blender opens
    # it for overwrite. Author locally, then atomically replace the owned file.
    path=Path(path).resolve()
    if not path.is_relative_to(ROOT.resolve()):raise ValueError('Export escaped the repository')
    staged=EXPORT_WORK/path.name
    bpy.ops.object.select_all(action='DESELECT')
    for o in objects:o.select_set(True)
    bpy.context.view_layer.objects.active=objects[-1]
    scene.frame_start=start;scene.frame_end=end
    bpy.ops.export_scene.fbx(filepath=str(staged),use_selection=True,object_types={'ARMATURE','MESH'},
        global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
        use_mesh_modifiers=True,add_leaf_bones=False,primary_bone_axis='Y',secondary_bone_axis='X',
        use_armature_deform_only=False,bake_anim=animation,bake_anim_use_all_bones=True,
        bake_anim_use_nla_strips=False,bake_anim_use_all_actions=False,bake_anim_force_startend_keying=True,
        bake_anim_step=1,bake_anim_simplify_factor=0,path_mode='ABSOLUTE',mesh_smooth_type='FACE')
    for attempt in range(12):
        try:os.replace(staged,path);break
        except OSError:
            if attempt==11:raise
            time.sleep(.25)

def triangles(mesh): return sum(len(p.vertices)-2 for p in mesh.data.polygons)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def file_info(path):return {'file':str(path.relative_to(ROOT)).replace('\\','/'),'sha256':sha(path),'bytes':path.stat().st_size}

def export_character(f):
    clips=actions(f); directory=OUT/'Characters'/f.name;directory.mkdir(parents=True,exist_ok=True)
    meshpath=directory/('SK_'+f.name+'.fbx');export_selected(meshpath,[f.mesh,f.rig])
    rec={'id':f.name,'rigFamily':f.family,'mesh':file_info(meshpath),'bones':list(f.bones),'boneCount':len(f.bones),
         'sockets':f.sockets,'triangles':triangles(f.mesh),'materials':[m.name for m in f.mesh.data.materials],
         'boundsMeters':list(f.mesh.dimensions),'lods':[],'animations':[],
         'physics':{'type':'capsule','simulationCollisionAuthoritative':False,'autoCreatePhysicsAsset':True}}
    for level,ratio in ((1,.50),(2,.25),(3,.12)):
        lod=f.mesh.copy();lod.data=f.mesh.data.copy();f.collection.objects.link(lod);lod.name='SK_'+f.name+'_LOD'+str(level)
        bpy.context.view_layer.objects.active=lod;mod=lod.modifiers.new('Silhouette preserving reduction','DECIMATE');mod.ratio=ratio
        bpy.ops.object.modifier_apply(modifier=mod.name)
        p=directory/(lod.name+'.fbx');export_selected(p,[lod,f.rig]);rec['lods'].append({'level':level,'triangles':triangles(lod),**file_info(p)})
        bpy.data.objects.remove(lod,do_unlink=True)
    for clip in clips:
        f.rig.animation_data.action=clip['object'];p=directory/(clip['action']+'.fbx')
        export_selected(p,[f.rig],True,1,clip['frames']);rec['animations'].append({k:v for k,v in clip.items() if k!='object'}|file_info(p))
    f.rig.animation_data.action=None;scene.frame_set(1)
    for pb in f.rig.pose.bones:pb.rotation_euler=(0,0,0);pb.location=(0,0,0);pb.scale=(1,1,1)
    manifest['characters'][f.name]=rec
    return f

def tower(name):
    f=Forge(name);core=name=='tower_core';archer=name=='archer_tower';size=1.14 if core else .91 if archer else 1.0
    if archer:return round_archer_tower(f)
    # Architectural body: authored stone courses, bevelled blocks and buttress assembly.
    for row in range(6 if core else 4):
        z=.34+row*.36
        for side in range(4):
            a=side*math.pi/2
            for col in range(4):
                offset=(col-1.5)*.48*size + (.04 if row%2 else -.04)
                x=math.cos(a)*.83*size+math.sin(a)*offset;y=math.sin(a)*.83*size-math.cos(a)*offset
                f.panel('dressed_masonry',(x,y,z),(.41*size,.25*size,.33),'limestone',rotation=(0,0,a-math.pi/2),bevel=.021)
    f.panel('foundation',(0,0,.13),(2.22*size,2.22*size,.26),'stone_dark',bevel=.11)
    f.panel('plinth_bevel',(0,0,.30),(2.09*size,2.09*size,.10),'limestone',bevel=.055)
    top=2.64 if core else 2.02 if archer else 2.13
    for s in (-1,1):
        for q in (-1,1):
            f.panel('corner_buttress',(s*.92*size,q*.92*size,1.0),( .22,.22,1.9 if core else 1.65),'stone_dark',bevel=.04)
            f.panel('buttress_foot',(s*.94*size,q*.94*size,.45),(.34,.34,.58),'limestone',bevel=.06)
    f.panel('crown_gold_course',(0,0,top),(2.18*size,2.18*size,.105),'brass',bevel=.03)
    f.panel('parapet_floor',(0,0,top+.11),(2.28*size,2.28*size,.14),'stone_dark',bevel=.045)
    for s in (-1,1):
        for col in (-1,0,1):
            f.panel('merlon',(s*1.00*size,col*.80*size,top+.33),(.24,.35,.38),'limestone',bevel=.037)
            if col:f.panel('merlon',(col*.80*size,s*1.00*size,top+.33),(.35,.24,.38),'limestone',bevel=.037)
    f.panel('front_crest_plate',(0,-1.015*size,1.22),(.54,.07,.61),'iron',bevel=.065)
    f.rune((0,-1.057*size,1.24),.20,key='cyan');f.panel('rear_banner',(0,1.02*size,1.23),(.48,.028,.95),'team',bevel=.02)
    for z in (.70,1.40):f.panel('arrow_slit',(.45,-.96*size,z),(.042,.03,.22),'black',bevel=.008)
    if core:
        f.tube('spire_support',[(0,0,2.94),(0,0,3.72)],[.08,.055],'iron',sides=16)
        f.ring('crystal_halo',(0,0,3.87),.40,.035,'brass')
        f.ring('crystal_vertical_ring',(0,0,3.88),.40,.022,'silver',axis='Y')
        f.tube('core_crystal',[(0,0,3.56),(0,0,3.95),(0,0,4.27)],[(.055,.055),(.26,.26),(.001,.001)],'violet',sides=8,glow=True)
        for s in (-1,1):
            for q in (-1,1):f.tube('crown_finial',[(s*.70,q*.70,3.08),(s*.62,q*.62,3.66)],[.10,.002],'brass',sides=8)
    elif not archer:
        f.ellipsoid('cannon_cradle',(0,.05,top+.42),(.42,.40,.23),'iron')
        f.tube('cannon_barrel',[(0,.12,top+.50),(0,-.30,top+.50),(0,-.97,top+.50)],[(.20,.20),(.17,.17),(.16,.16)],'iron',sides=24)
        for y in (-.22,-.71,-.94):f.ring('cannon_band',(0,y,top+.50),.179,.025,'brass',axis='Y')
        f.ellipsoid('muzzle_dark',(0,-.985,top+.50),(.113,.012,.113),'black')
        f.panel('cannon_sight',(0,-.69,top+.69),(.065,.09,.06),'brass',bevel=.012)
    else:
        f.panel('timber_platform',(0,0,top-.03),(2.12,2.12,.14),'wood',bevel=.055)
        for i in range(8):f.panel('platform_board',((i-3.5)*.25,0,top+.065),(.23,1.88,.045),'wood',bevel=.012)
    return f.finish()

def nova():
    f=Forge('nova_flask');f.ellipsoid('glass_bulb',(0,0,.55),(.31,.31,.38),'glass',detail=40)
    f.tube('flask_neck',[(0,0,.79),(0,0,1.05)],[.115,.105],'glass',sides=32)
    f.tube('cork',[(0,0,1.02),(0,0,1.14)],[.098,.11],'wood',sides=20)
    f.ellipsoid('trapped_nova',(0,0,.51),(.17,.17,.21),'violet',detail=32,glow=True)
    for z,r in ((.27,.21),(.78,.23),(1.045,.125)):f.ring('cage_band',(0,0,z),r,.022,'brass')
    for j in range(6):
        a=j*math.pi/3;f.tube('glass_cage',[(math.cos(a)*.21,math.sin(a)*.21,.26),(math.cos(a)*.31,math.sin(a)*.31,.52),(math.cos(a)*.23,math.sin(a)*.23,.78)],.015,'brass',sides=10)
    return f.finish()

def environment(name):
    f=Forge(name)
    if name=='bridge':
        for i in range(12):
            y=(i-5.5)*.34;f.panel('wood_plank',(0,y,.09),(4.2,.315,.18),'wood',bevel=.028)
            for s in (-1,1):f.rivet((s*1.85,y,.186),size=.035)
        for s in (-1,1):
            f.panel('longitudinal_girder',(s*1.7,0,-.22),(.22,4.4,.32),'wood',bevel=.025)
            f.tube('rail',[(s*2.02,-2.05,.82),(s*2.02,2.05,.82)],.055,'wood',sides=12)
            for y in (-1.8,-.6,.6,1.8):
                f.panel('railing_post',(s*2.02,y,.48),(.12,.12,.9),'wood',bevel=.025)
                f.panel('metal_post_cap',(s*2.02,y,.955),(.16,.16,.08),'iron',bevel=.02)
            for y in (-2.0,2.0):f.panel('abutment',(s*.9,y,-.25),(1.9,.45,.65),'limestone',bevel=.085)
    elif name=='floor_tile':
        f.panel('moss_field',(0,0,-.075),(1,1,.15),'grass',bevel=.016)
        for i in range(6):
            a=i*1.71;x=math.cos(a)*.25;y=math.sin(a)*.25
            f.ellipsoid('ground_pebble',(x,y,.003),(.035,.027,.012),'stone',detail=12)
    elif name=='lane_paver':
        f.panel('stone_slab',(0,0,.025),(.79,.72,.06),'limestone',rotation=(0,0,.12),bevel=.055)
        for y in (-.17,.19):f.tube('weathered_crease',[(-.22,y,.059),(.05,y+.035,.06),(.20,y-.01,.059)],.003,'stone_dark',sides=6)
    elif name=='bank_segment':
        for row in range(2):
            for i in range(3):f.panel('bank_masonry',((i-1)*.62+(row*.10),0,-.1+row*.30),(.60,.48,.29),'limestone',bevel=.05)
        f.panel('bank_cap',(0,0,.38),(2,.6,.12),'stone',bevel=.045)
    elif name=='boundary_stone':
        f.ellipsoid('weathered_boundary',(0,0,.25),(.67,.38,.36),'stone',detail=24)
        f.panel('capstone',(0,0,.45),(.96,.44,.13),'limestone',rotation=(0,.08,.10),bevel=.085)
    elif name in ('grass_tuft','shrub'):
        for i in range(16 if name=='grass_tuft' else 35):
            a=i*2.399; r=.045*math.sqrt(i);x=math.cos(a)*r;y=math.sin(a)*r;h=.12+(i%5)*.035 if name=='grass_tuft' else .30+(i%6)*.07
            f.tube('foliage_blade',[(x,y,0),(x+.015,y+.012,h*.5),(x+.06,y+.04,h)],[.018,.025,.001],'grass_light' if i%3 else 'grass',sides=6)
    elif name=='tree':
        f.tube('sculpted_trunk',[(0,0,0),(.08,0,.65),(0,.045,1.4),(.02,.03,2.0)],[.15,.12,.09,.035],'wood',sides=16)
        for i in range(13):
            a=i*2.399;h=1.0+i*.09;x=math.cos(a)*.35;y=math.sin(a)*.35
            f.tube('branch',[(0,0,h),(x,y,h+.17)],[.045,.015],'wood',sides=10)
            f.ellipsoid('leaf_mass',(x,y,h+.25),(.43,.39,.31),'grass' if i%2 else 'grass_light',detail=24)
    elif name=='crystal_plinth':
        f.panel('carved_plinth',(0,0,.20),(.78,.78,.40),'stone_dark',bevel=.10)
        f.panel('plinth_trim',(0,0,.44),(.83,.83,.10),'brass',bevel=.025)
        f.tube('rift_crystal',[(0,0,.47),(0,0,.90),(.03,0,1.25)],[.13,.19,.001],'cyan',sides=6,glow=True)
        for s in (-1,1):f.tube('small_crystal',[(s*.22,.07,.48),(s*.27,.04,.80)],[.07,.001],'violet',sides=6,glow=True)
    elif name=='banner':
        f.tube('banner_pole',[(0,0,0),(0,0,2.5)],[.055,.039],'iron',sides=12)
        f.panel('banner_stone',(0,0,.20),(.55,.55,.40),'limestone',bevel=.07)
        vs=[];faces=[]
        for row in range(9):
            for col in range(13):
                u=col/12;v=row/8;vs.append((u*.91,.045*math.sin(u*math.pi*2.2+v*.3),2.3-v*.72))
        for row in range(8):
            for col in range(12):i=row*13+col;faces.append((i,i+1,i+14,i+13))
        f.mesh_data('draped_banner',vs,faces,'team');f.tube('pole_finial',[(0,0,2.5),(0,0,2.66)],[.065,.001],'brass',sides=8)
    elif name=='ruin':
        for side in (-1,1):
            for row in range(5):f.panel('broken_column',(side*.57,0,.18+row*.30),(.38,.46,.28),'limestone',bevel=.045)
        f.panel('broken_lintel',(0,0,1.70),(1.66,.52,.25),'stone',rotation=(0,.09,.02),bevel=.04)
        f.ellipsoid('ruin_rubble',(.30,-.22,.15),(.48,.30,.22),'stone',detail=16)
    elif name=='distant_island':
        f.tube('eroded_island',[(0,0,-2),(0,0,-1.45),(.08,0,-.72),(0,0,0)],[.08,.85,1.6,2.05],'stone',sides=15)
        f.ellipsoid('island_green_cap',(0,0,.02),(2.02,1.7,.22),'grass',detail=28)
    elif name=='meteor_shard':
        f.tube('fractured_meteor',[(0,0,-.3),(.04,0,0),(-.05,.03,.34),(0,0,.54)],[.01,.19,.11,.002],'stone_dark',sides=7)
        f.tube('molten_fissure',[(-.12,-.08,-.04),(.035,-.145,.1),(.07,-.08,.29)],.009,'ember',sides=6,glow=True)
    elif name=='bullet_round':
        f.tube('brass_round',[(0,0,-.10),(0,0,.02),(0,0,.13)],[.027,.027,.001],'brass',sides=16)
        f.ring('round_rim',(0,0,-.10),.03,.005,'silver')
    elif name=='tower_rubble':
        for i in range(12):
            a=i*2.399;r=.23*math.sqrt(i);f.panel('fallen_masonry',(math.cos(a)*r,math.sin(a)*r,.08+(i%3)*.04),(.28,.35,.17),'limestone',rotation=(.07*i,.12*i,a),bevel=.03)
    elif name=='arrow_projectile':
        f.tube('arrow_shaft',[(0,.25,0),(0,-.20,0)],.011,'wood',sides=12)
        f.tube('broadhead',[(0,-.19,0),(0,-.25,0),(0,-.33,0)],[.014,.042,.001],'silver',sides=4)
        f.ring('arrow_collar',(0,-.19,0),.016,.005,'brass',axis='Y')
        for angle in (0,2*math.pi/3,4*math.pi/3):
            dx,dz=math.cos(angle),math.sin(angle)
            f.mesh_data('fletching',[(0,.14,0),(dx*.055,.19,dz*.055),(dx*.055,.29,dz*.055),(0,.25,0)],[(0,1,2,3)],'cloth_ember',smooth=False)
        f.ellipsoid('ember_arrow_tip',(0,-.29,0),(.017,.031,.017),'ember',detail=16,glow=True)
    elif name in ('arc_projectile','manta_projectile','storm_projectile'):
        key='violet' if name=='arc_projectile' else 'cyan'
        f.tube('rift_bolt_core',[(0,.13,0),(0,.035,0),(0,-.105,0),(0,-.18,0)],[.003,.075,.060,.002],key,sides=8,glow=True)
        for angle in (0,2*math.pi/3,4*math.pi/3):
            dx,dz=math.cos(angle),math.sin(angle)
            f.tube('orbiting_shard',[(dx*.10,.04,dz*.10),(dx*.105,-.035,dz*.105),(dx*.055,-.125,dz*.055)],[.012,.025,.001],'brass' if name=='arc_projectile' else 'ice',sides=5,glow=name!='arc_projectile')
        if name=='storm_projectile':
            for sign in (-1,1):f.tube('lightning_branch',[(0,-.01,0),(sign*.085,-.06,.04),(sign*.035,-.12,.015),(sign*.07,-.18,0)],[.017,.014,.012,.001],'cyan',sides=5,glow=True)
    elif name=='projectile_trail':
        # Local -Y exports as +X forward in Unreal. One meter, centered.
        f.tube('tapered_trail',[(0,.5,0),(0,.15,0),(0,-.5,0)],[.002,.012,.020],'team',sides=8,glow=True)
    return f.finish()

def export_static(f):
    directory=OUT/'Environment';directory.mkdir(parents=True,exist_ok=True)
    p=directory/('SM_'+f.name+'.fbx');export_selected(p,[f.mesh])
    rec={'name':f.name,**file_info(p),'triangles':triangles(f.mesh),'boundsMeters':list(f.mesh.dimensions),
         'materials':[m.name for m in f.mesh.data.materials],'lods':[]}
    for level,ratio in ((1,.5),(2,.22)):
        lod=f.mesh.copy();lod.data=f.mesh.data.copy();f.collection.objects.link(lod);lod.name='SM_'+f.name+'_LOD'+str(level)
        bpy.context.view_layer.objects.active=lod;mod=lod.modifiers.new('Authored static LOD reduction','DECIMATE');mod.ratio=ratio
        bpy.ops.object.modifier_apply(modifier=mod.name);lp=directory/(lod.name+'.fbx');export_selected(lp,[lod]);rec['lods'].append({'level':level,'triangles':triangles(lod),**file_info(lp)})
        bpy.data.objects.remove(lod,do_unlink=True)
    manifest['statics'][f.name]=rec;return f

def render_rig():
    collection=bpy.data.collections.new('RenderStage');scene.collection.children.link(collection)
    def stage(o):
        for c in list(o.users_collection):c.objects.unlink(o)
        collection.objects.link(o);return o
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.025));floor=stage(bpy.context.object)
    m=bpy.data.materials.new('RenderStageFloor');m.use_nodes=True
    m.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.032,.042,.06,1)
    m.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value=.8
    floor.data.materials.append(m)
    for name,loc,power,color,size in [('Key',(3,-4,6),1050,(1,.79,.60),4),('Fill',(-3,-2,3.8),650,(.49,.71,1),3),('Rim',(1,4,5),1300,(.35,.67,1),3)]:
        data=bpy.data.lights.new(name,'AREA');data.energy=power;data.color=color;data.shape='DISK';data.size=size
        o=bpy.data.objects.new(name,data);collection.objects.link(o);o.location=loc;o.rotation_euler=(Vector((0,0,1))-o.location).to_track_quat('-Z','Y').to_euler()
    data=bpy.data.cameras.new('AssetCamera');camera=bpy.data.objects.new('AssetCamera',data);collection.objects.link(camera)
    scene.camera=camera;camera.data.type='ORTHO';return collection,camera

def render_asset(f,stage,camera):
    for col in bpy.data.collections:
        if col!=stage: col.hide_render=col!=f.collection
    for o in f.collection.objects:o.hide_render=False
    height=max(1.2,f.mesh.dimensions.z);width=max(f.mesh.dimensions.x,f.mesh.dimensions.y)
    target=Vector((0,0,sum(corner[2] for corner in f.mesh.bound_box)/8 if f.name=='guard_cannon' else height*.48));camera.location=target+Vector((4.5,-7.0,4.0))
    camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.ortho_scale=max(height*1.5,width*1.38)
    path=RENDER/(f.name+'.png');scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)
    manifest['renders'].append(file_info(path));return path

def remove_parts(f,names):
    for ob in list(f.parts):
        if any('_'+name in ob.name for name in names):
            f.parts.remove(ob);bpy.data.objects.remove(ob,do_unlink=True)

def refine_humanoid(f,k):
    if f.name!='ironclad':
        remove_parts(f,('sculpt_head','nose','cheek','eye','brow','mouth','tailored_hood','lower_face_mask'))
        masked=f.name=='twin_blades'
        # A carved human face with jaw/chin/temple planes instead of sphere-mounted cheeks.
        levels=[(1.53,.062,.081),(1.60,.105,.105),(1.68,.139,.137),(1.76,.148,.136),(1.85,.132,.124),(1.91,.070,.084),(1.94,.006,.028)]
        vs=[];faces=[];segments=32
        for z,rx,ry in levels:
            for j in range(segments):
                a=j*math.pi*2/segments;x=math.sin(a)*rx*k;y=math.cos(a)*ry
                # Flatten forehead, jaw sides and front face deliberately.
                if y<0:y=max(y,-ry*.86)-.01*(1-abs(x)/(rx*k+.0001))
                vs.append((x,y,z*k))
        for row in range(len(levels)-1):
            for j in range(segments):q=row*segments+j;faces.append((q,row*segments+(j+1)%segments,(row+1)*segments+(j+1)%segments,q+segments))
        faces.extend([tuple(reversed(range(segments))),tuple((len(levels)-1)*segments+j for j in range(segments))])
        f.mesh_data('carved_face',vs,faces,'black' if masked else 'skin','head')
        for s in (-1,1):
            f.panel('inset_eye_socket',(s*.059*k,-.128,1.769*k),(.056*k,.009,.022*k),'black','head',rotation=(0,s*.035,s*.07),bevel=.008)
            f.panel('inset_eye',(s*.059*k,-.135,1.769*k),(.027*k,.006,.010*k),'violet' if masked else 'cyan' if f.name=='arc_mage' else 'ivory','head',bevel=.004)
        if not masked:
            nose=[(-.022*k,-.115,1.795*k),(.022*k,-.115,1.795*k),(-.027*k,-.124,1.696*k),(.027*k,-.124,1.696*k),(0,-.169,1.71*k),(0,-.141,1.794*k)]
            f.mesh_data('carved_nose_bridge',nose,[(0,2,4,5),(1,5,4,3),(0,5,1),(2,3,4),(0,1,3,2)],'skin','head')
            f.tube('subtle_lip',[(-.040*k,-.127,1.646*k),(0,-.135,1.642*k),(.040*k,-.127,1.646*k)],[.004,.006,.004],'leather','head',8)
        else:
            f.tube('mask_seam',[(-.12,-.099,1.68*k),(0,-.139,1.675*k),(.12,-.099,1.68*k)],.008,'cloth_purple','head',8)
        cloth='cloth_purple' if masked else 'cloth_indigo' if f.name=='arc_mage' else 'cloth_ember'
        # Explicit open hood with rolled seam; face never protrudes through cloth.
        levels=[(1.48,.18,.13),(1.62,.21,.18),(1.82,.205,.19),(1.94,.15,.15),(2.01,.013,.055)]
        vs=[];faces=[]
        for row,(z,rx,ry) in enumerate(levels):
            for j in range(33):
                a=-2.28+j/32*4.56;fold=.010*math.cos(a*8)*(1-row/5)
                vs.append(((rx+fold)*math.sin(a)*k,.018+(ry+fold)*math.cos(a),z*k+.01*math.sin(a*3)))
        for row in range(4):
            for j in range(32):q=row*33+j;faces.append((q,q+1,q+34,q+33))
        hood_ob=f.mesh_data('open_hood_folds',vs,faces,cloth,'head')
        sol=hood_ob.modifiers.new('Sewn hood thickness','SOLIDIFY');sol.thickness=.013
        bpy.context.view_layer.objects.active=hood_ob;bpy.ops.object.modifier_apply(modifier=sol.name)
        for side in (0,32):f.tube('hood_rolled_seam',[vs[row*33+side] for row in range(5)],.014,cloth,'head',10)
        # Shoulder scarf with layered angled strips and front folds.
        for j in range(5):
            z=(1.49-.045*j)*k
            f.tube('folded_cowl',[(-.25,-.075,z),(0,-.205,z-.033),(.23,-.095,z-.045)],[.027,.028,.021],cloth,'chest',10)
        f.ellipsoid('cloak_clasp',(-.12,-.216,1.34*k),(.042,.012,.042),'brass','chest',20)
        for s in (-1,1):
            f.tube('costume_seam',[(s*.18,-.17,.95*k),(s*.24,-.16,1.26*k)],[.006,.006],'brass','chest',6)
            for j in range(4):f.rivet((s*.47,-.12,1.02*k+j*.032),'forearm_'+('l' if s<0 else 'r'),size=.012)
        if f.name=='arc_mage':
            # A full pointed hat above the hood and a carefully curved brim.
            vs=[];faces=[]
            for row in range(3):
                r=(.21,.31,.33)[row]
                for j in range(49):a=j*math.pi/24;vs.append((math.cos(a)*r,math.sin(a)*r,1.91*k+.028*math.sin(a)*row))
            for row in range(2):
                for j in range(48):q=row*49+j;faces.append((q,q+1,q+50,q+49))
            f.mesh_data('curved_hat_brim',vs,faces,'cloth_purple','head')
            f.tube('crooked_hat_peak',[(0,0,1.94*k),(.02,.015,2.16*k),(.085,.045,2.33*k),(.12,.06,2.39*k)],[.19,.115,.045,.003],'cloth_purple','head',24)
            f.ring('hat_leather_band',(0,0,1.94*k),.201,.015,'leather','head')
            for j in range(9):
                a=j*2*math.pi/9
                f.tube('robe_sculpted_fold',[(math.cos(a)*.29,math.sin(a)*.19,.83*k),(math.cos(a)*.34,math.sin(a)*.22,.42*k),(math.cos(a)*.35,math.sin(a)*.225,.16*k)],[.014,.022,.009],'cloth_indigo','pelvis',10)
            for s in (-1,1):
                f.tube('robe_front_trim',[(s*.15,-.18,.93*k),(s*.18,-.22,.49*k),(s*.23,-.24,.17*k)],[.008,.010,.008],'brass','pelvis',8)
                f.tube('astrolabe_spindle',[(.49+s*.16,-.10,1.86),(.49+s*.16,-.10,2.03)],[.012,.012],'brass','hand_r',8)
            f.ring('secondary_staff_gear',(.49,-.10,1.94),.145,.012,'silver','hand_r','Z')
            for j in range(4):a=j*math.pi/2;f.ellipsoid('astrolabe_rune',(.49+.18*math.cos(a),-.10,1.94+.18*math.sin(a)),(.023,.02,.023),'cyan','hand_r',12,True)
    # Every unit has a small explicit team-mask region in the shared atlas.
    f.tube('team_shoulder_wrap',[(-.24,.04,1.41*k),(-.24,-.15,1.35*k)],[.03,.03],'team','upperarm_l',10)

def refine_quadruped(f,key):
    if f.name=='frost_fang':
        remove_parts(f,('muzzle','nose','ear','eye_brow','eye','hoof_or_paw','saddle_armor','forehead_armor'))
        # Predator silhouette narrows behind a broad shoulder, instead of a barrel/boar body.
        for ob in f.parts:
            if '_sculpted_body' in ob.name:
                for vertex in ob.data.vertices:
                    z=vertex.co.z;vertex.co.x*=.84;vertex.co.z*=.91
            if '_sculpted_head' in ob.name:
                for vertex in ob.data.vertices:vertex.co.x*=.91;vertex.co.y*=.91;vertex.co.z*=.93
        f.ellipsoid('feline_cheek_l',(-.18,-.98,1.235),(.17,.175,.14),key,'head',32)
        f.ellipsoid('feline_cheek_r',(.18,-.98,1.235),(.17,.175,.14),key,'head',32)
        f.ellipsoid('short_feline_muzzle',(0,-1.13,1.13),(.205,.14,.10),key,'jaw',32)
        f.ellipsoid('feline_lower_jaw',(0,-1.07,1.025),(.18,.175,.08),key,'jaw',28)
        f.mesh_data('feline_nose',[(-.072,-1.285,1.17),(.072,-1.285,1.17),(0,-1.31,1.115),(0,-1.33,1.16)],[(0,1,3),(0,3,2),(1,2,3),(0,2,1)],'black','jaw')
        for s in (-1,1):
            ear=[(s*.17,-.80,1.41),(s*.34,-.78,1.43),(s*.29,-.81,1.68),(s*.24,-.71,1.43)]
            f.mesh_data('feline_ear',ear,[(0,1,2),(0,2,3),(1,3,2),(0,3,1)],key,'head')
            f.mesh_data('feline_inner_ear',[(s*.21,-.814,1.44),(s*.31,-.814,1.46),(s*.285,-.825,1.625)],[(0,1,2)],'stone_dark','head')
            f.panel('feline_eye_socket',(s*.23,-1.160,1.297),(.096,.025,.039),'black','head',rotation=(0,0,-s*.26),bevel=.011)
            f.ellipsoid('feline_eye',(s*.23,-1.185,1.297),(.028,.010,.018),'cyan','head',20,True)
            f.tube('predator_brow',[(s*.15,-1.138,1.348),(s*.285,-1.116,1.31)],[.030,.013],key,'head',10)
            for j in range(5):
                f.tube('facial_fur',[(s*.22,-.90,1.33-j*.055),(s*.36,-.97,1.28-j*.055),(s*.40,-.96,1.26-j*.055)],[.026,.028,.001],key,'head',8)
            for j in range(3):f.tube('subtle_face_marking',[(s*(.15+j*.035),-1.078,1.39),(s*(.18+j*.04),-1.140,1.30)],[.011,.004],'stone','head',6)
        f.tube('visible_jaw_gap',[(-.13,-1.259,1.063),(0,-1.281,1.045),(.13,-1.259,1.063)],.012,'black','jaw',10)
        f.mesh_data('frost_brow_armor',[(-.205,-1.048,1.386),(0,-1.133,1.426),(.205,-1.048,1.386),(0,-1.091,1.331)],[(0,1,3),(1,2,3)],'ice_steel','head',False)
        f.rune((0,-1.13,1.375),.054,'cyan','head')
        for s,side in ((-1,'l'),(1,'r')):
            for y,leg in ((-.42,'front'),(.40,'back')):
                bn=leg+'_'+side+'_foot'
                f.ellipsoid('feline_paw',(s*.45,y-.11,.09),(.16,.21,.10),key,bn,28)
                for toe in range(4):
                    x=s*.45+(toe-1.5)*.058
                    f.ellipsoid('feline_toe',(x,y-.245,.075),(.043,.075,.064),key,bn,16)
                    f.tube('predator_claw',[(x,y-.285,.075),(x,y-.35,.045)],[.019,.002],'ivory',bn,8)
            for layer in range(3):
                x=s*(.23+.09*layer);y=-.27+layer*.17;z=1.30-layer*.065
                f.panel('articulated_frost_armor',(x,y,z),(.25,.39,.055),'ice_steel','spine',rotation=(0,s*.42,0),bevel=.026)
                f.tube('frost_armor_edge',[(x-s*.10,y-.20,z+.025),(x+s*.10,y-.20,z-.01)],[.009,.009],'silver','spine',8)
    anatomy=('sculpted_body','chest','haunches','neck_muscle','sculpted_head','muscular_leg','tapered_leg','short_feline_muzzle','feline_cheek','feline_lower_jaw')
    members=[o for o in f.parts if any('_'+n in o.name for n in anatomy)]
    if members:
        # Voxel sculpt union creates continuous muscular anatomy and skin weights are rebuilt.
        bpy.ops.object.select_all(action='DESELECT')
        for o in members:o.select_set(True);f.parts.remove(o)
        bpy.context.view_layer.objects.active=members[0];bpy.ops.object.join();an=bpy.context.object
        scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        mod=an.modifiers.new('Continuous sculpt union','REMESH');mod.mode='VOXEL';mod.voxel_size=.026;mod.use_smooth_shade=True
        bpy.ops.object.modifier_apply(modifier=mod.name)
        sm=an.modifiers.new('Muscle surface polish','SMOOTH');sm.factor=.48;sm.iterations=3;bpy.ops.object.modifier_apply(modifier=sm.name)
        for group in list(an.vertex_groups):an.vertex_groups.remove(group)
        eligible={n:b for n,b in f.bones.items() if n not in f.sockets and n!='root'}
        for n in eligible:an.vertex_groups.new(name=n)
        for vertex in an.data.vertices:
            p=vertex.co;distances=[]
            for n,b in eligible.items():
                a=Vector(b['head']);c=Vector(b['tail']);line=c-a;t=max(0,min(1,(p-a).dot(line)/max(.00001,line.length_squared)))
                dist=(p-(a+line*t)).length;distances.append((dist,n))
            candidates=sorted(distances)[:3];weights=[1/(.018+d)**4 for d,n in candidates];total=sum(weights)
            for (_,n),w in zip(candidates,weights):an.vertex_groups[n].add([vertex.index],w/total,'REPLACE')
        for uv in list(an.data.uv_layers):an.data.uv_layers.remove(uv)
        an.name='continuous_anatomy';f.add(an,key)
    f.tube('team_collar_band',[(-.31,-.62,1.0),(0,-.77,1.05),(.31,-.62,1.0)],.028,'team','neck',10)

def refine_flyer(f):
    if f.name=='storm_raven':
        for row in range(4):
            for j in range(9):
                a=j*math.pi/4.5;start=(math.cos(a)*(.22-row*.013),math.sin(a)*(.25-row*.013)+.01,1.2-row*.045)
                end=(start[0]*1.12,start[1]+.12, start[2]-.16)
                feather(f,start,end,.036,'raven_blue' if (j+row)%3 else 'raven','body')
        for s in (-1,1):
            feather(f,(s*.08,-.35,1.34),(s*.16,-.29,1.48),.032,'raven','head')
        f.ring('team_chest_mount',(0,-.392,1.075),.085,.012,'team','body','Y')
    elif f.name=='vampire_bats':
        for s in (-1,1):
            for j in range(5):
                f.tube('bat_fur_lock',[(s*.13,-.03,1.18-j*.04),(s*.22,.035,1.13-j*.04),(s*.21,.06,1.10-j*.04)],[.025,.021,.001],'plum','body',8)
            for j in range(3):f.tube('bat_claw',[(s*.12,.07,.89),(s*.12+j*.02,-.03,.83),(s*.12+j*.02,-.07,.86)],[.011,.008,.001],'ivory','talon_'+('l' if s<0 else 'r'),8)
        f.ring('team_chest_mount',(0,-.22,1.015),.065,.010,'team','body','Y')
    else:
        remove_parts(f,('fin_vein',))
        for s,side in ((-1,'l'),(1,'r')):
            for j in range(7):
                t=.10+j*.12;x=s*(.22+1.34*t);center=.05+.17*t;chord=.89*(1-t**1.65)+.018
                front=center-chord*.55;back=center+chord*.45;z=1.04+.14*math.sin(t*math.pi)-.055*t
                f.tube('curved_fin_rib',[(x,front+.018,z+.006),(x,center,z+.035),(x,back-.01,z+.006)],.006,'teal','wing_'+side if t<.36 else 'wing_'+side+'_outer' if t<.8 else 'wing_'+side+'_tip',8)
        f.ring('team_dorsal_mount',(0,-.03,1.21),.20,.012,'team','body')

def set_world_rotation(pb,desired):
    bpy.context.view_layer.update()
    rest=pb.bone.matrix_local.to_quaternion()
    if pb.parent:
        base=pb.parent.matrix.to_quaternion()@pb.parent.bone.matrix_local.to_quaternion().inverted()@rest
    else:base=rest
    pb.rotation_euler=(base.inverted()@desired).to_euler('XYZ')

def aim_arm(f,side,target,strength):
    up=f.rig.pose.bones['upperarm_'+side]; fore=f.rig.pose.bones['forearm_'+side]
    shoulder=up.bone.head_local;restWrist=fore.bone.tail_local
    target=restWrist.lerp(target,max(0,min(1,strength)))
    direction=target-shoulder;distance=min(direction.length,up.bone.length+fore.bone.length-.001)
    direction.normalize();l1=up.bone.length;l2=fore.bone.length
    along=(l1*l1-l2*l2+distance*distance)/(2*max(.001,distance))
    pole=Vector((-1 if side=='l' else 1,0,-.25));perp=pole-direction*pole.dot(direction)
    if perp.length<.01:perp=Vector((0,0,1)).cross(direction)
    perp.normalize();elbow=shoulder+direction*along+perp*math.sqrt(max(0,l1*l1-along*along))
    for pb,vector in ((up,elbow-shoulder),(fore,target-elbow)):
        restDir=pb.bone.tail_local-pb.bone.head_local
        desired=restDir.normalized().rotation_difference(vector.normalized())@pb.bone.matrix_local.to_quaternion()
        set_world_rotation(pb,desired)

def round_archer_tower(f):
    for row in range(5):
        for j in range(14):
            a=j*2*math.pi/14+(row%2)*math.pi/14;r=.77
            f.panel('cylindrical_masonry',(math.cos(a)*r,math.sin(a)*r,.40+row*.29),(.36,.22,.27),'limestone',rotation=(0,0,a+math.pi/2),bevel=.026)
    f.tube('round_foundation',[(0,0,.08),(0,0,.22),(0,0,.30)],[1.01,1.02,.91],'stone_dark',sides=40)
    for z in (.32,1.81):f.ring('round_stone_course',(0,0,z),.85,.055,'stone')
    f.tube('timber_platform',[(0,0,1.80),(0,0,1.92)],[1.04,1.05],'wood',sides=40)
    f.ring('platform_brass_trim',(0,0,1.94),1.05,.031,'brass')
    for j in range(8):
        a=j*math.pi/4;f.panel('round_parapet_merlon',(math.cos(a)*.91,math.sin(a)*.91,2.16),(.30,.22,.40),'stone_dark',rotation=(0,0,a+math.pi/2),bevel=.028)
    f.panel('crest_plaque',(0,-.895,1.07),(.35,.05,.40),'iron',bevel=.045);f.rune((0,-.927,1.07),.13)
    f.panel('tower_team_banner',(0,.91,1.16),(.30,.028,.70),'team',bevel=.024)
    return f.finish()

def polish_character(f):
    """Readable, rigged silhouette accents, rather than unskinned attachments."""
    if f.family=='humanoid':
        k=f.bones['head']['head'][2]/1.6
        # Enlarge heads/hats and shoulder equipment about their own pivots.
        for ob in f.parts:
            if ob.vertex_groups.get('head'):
                center=Vector((0,0,1.72 if f.name=='ironclad' else 1.64))
                local=ob.matrix_world.inverted()@center
                for v in ob.data.vertices:v.co=local+(v.co-local)*1.10
        for sign,side in ((-1,'l'),(1,'r')):
            # Cover rigid-segment knee joins through the entire gait cycle.
            f.ellipsoid('articulated_knee',(sign*.16*k,-.01,.49*k),(.091*k,.086*k,.078*k),'steel' if f.name=='ironclad' else 'leather','shin_'+side,20)
            if f.name not in ('ironclad','twin_blades'):
                pivot=Vector((0,0,1.64))
                def face_point(x,y,z):return pivot+(Vector((x,y,z))-pivot)*1.10
                eye=face_point(sign*.061*k,-.144,1.769*k)
                f.ellipsoid('clear_eye_white',eye,(.023*k,.009,.015*k),'ivory','head',20)
                f.ellipsoid('clear_eye_iris',eye+Vector((0,-.008,0)),(.008*k,.005,.010*k),'cyan' if f.name=='arc_mage' else 'leather','head',16)
                f.tube('expressive_brow',[face_point(sign*.032*k,-.141,1.804*k),face_point(sign*.089*k,-.128,1.819*k)],[.009,.012],'leather','head',10)
        cloth='cloth_blue' if f.name=='ironclad' else 'cloth_purple' if f.name in ('arc_mage','twin_blades') else 'cloth_ember'
        for side,sign in (('l',-1),('r',1)):
            f.panel('readable_shoulder_trim',(sign*.28,-.08,1.42),(.22,.20,.062),'brass','upperarm_'+side,bevel=.025)
            f.panel('team_cuff',(sign*.45,-.08,1.06),(.15,.15,.065),'team','forearm_'+side,bevel=.018)
        if f.name=='ironclad':
            for sign in (-1,1):
                f.tube('helm_crown_edge',[(sign*.08,-.13,2.02),(sign*.16,-.03,2.00),(sign*.15,.12,1.90)],[.018,.016,.010],'silver','head',10)
            f.panel('shield_center_crest',(-.48,-.297,1.15),(.19,.027,.25),'brass','hand_l',bevel=.04)
            f.rune((-.48,-.315,1.15),.10,bone='hand_l')
        elif f.name in ('ember_archer','tower_archer'):
            for j in range(3):
                x=.13+j*.048
                f.tube('quiver_arrow',[(x,.23,1.12),(x,.24,1.69)],[.009,.007],'wood','chest',8)
                f.panel('quiver_feather',(x,.24,1.62),(.020,.060,.13),cloth,'chest',bevel=.005)
        elif f.name=='arc_mage':
            for j in range(5):
                a=j*2*math.pi/5
                f.ellipsoid('staff_focus_spark',(.49+math.cos(a)*.12,-.13+math.sin(a)*.12,1.96),(.017,.017,.028),'violet','hand_r',12,True)
        elif f.name=='twin_blades':
            for sign,side in ((-1,'l'),(1,'r')):
                f.tube('blade_energy_edge',[(sign*.49,-.16,.97),(sign*.51,-.15,1.31),(sign*.55,-.14,1.50)],[.010,.008,.003],'violet','hand_'+side,8,True)
    elif f.family=='quadruped':
        if f.name=='boulderback':
            for j in range(5):
                y=-.30+j*.21
                f.tube('crystal_back_ridge',[(0,y,1.52),(0,y,1.72),(0,y-.018,1.85)],[.09,.055,.001],'cyan','spine',sides=5,glow=True)
        elif f.name=='frost_fang':
            for sign in (-1,1):
                f.tube('ice_shoulder_fin',[(sign*.38,-.43,1.07),(sign*.47,-.37,1.40),(sign*.49,-.32,1.55)],[.06,.04,.001],'ice','front_'+('l' if sign<0 else 'r')+'_upper',sides=5,glow=True)
        else:
            f.panel('charge_harness_crest',(0,-.68,1.30),(.30,.065,.24),'brass','neck',bevel=.05)
            f.rune((0,-.72,1.30),.10,'ember','neck')
    elif f.family=='flyer':
        for sign,side in ((-1,'l'),(1,'r')):
            key='cyan' if f.name!='vampire_bats' else 'violet'
            f.tube('wing_identity_edge',[(sign*.30,-.04,1.09),(sign*.68,-.08,1.14),(sign*1.10,-.05,1.11)],[.013,.010,.004],key,'wing_'+side+'_outer',sides=8,glow=True)

forges=[]
for name in ('ironclad','ember_archer','twin_blades','arc_mage','tower_archer','boulderback','rambeast','frost_fang','sky_manta','vampire_bats','storm_raven'):
    print('RIFT_ASSET_BEGIN',name,flush=True)
    f=humanoid(name) if name in ('ironclad','ember_archer','twin_blades','arc_mage','tower_archer') else quadruped(name) if name in ('boulderback','rambeast','frost_fang') else flyer(name)
    export_character(f);forges.append(f);print('RIFT_ASSET_COMPLETE',name,flush=True)
for name in ('tower_guard','tower_core','archer_tower','nova_flask','bridge','floor_tile','lane_paver','bank_segment','boundary_stone','grass_tuft','shrub','tree','crystal_plinth','banner','ruin','distant_island','meteor_shard','bullet_round','tower_rubble','arrow_projectile','arc_projectile','manta_projectile','storm_projectile','projectile_trail'):
    print('RIFT_ASSET_BEGIN',name,flush=True)
    f=tower(name) if name in ('tower_guard','tower_core','archer_tower') else nova() if name=='nova_flask' else environment(name)
    cannon=None
    if name=='tower_guard':
        mesh=split_guard_mesh(f.mesh)
        cannon=SimpleNamespace(name='guard_cannon',mesh=mesh,collection=mesh.users_collection[0])
    export_static(f);forges.append(f);print('RIFT_ASSET_COMPLETE',name,flush=True)
    if cannon:
        export_static(cannon);forges.append(cannon);print('RIFT_ASSET_COMPLETE','guard_cannon',flush=True)
if not opts.no_renders:
    stage,camera=render_rig()
    for f in forges:render_asset(f,stage,camera)
    stage.hide_render=True
for col in bpy.data.collections:col.hide_render=False
scene.frame_start=1;scene.frame_end=60;scene.frame_set(1)
blend=SRC/'RiftCrown_ProductionAssets.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend))
manifest['source']=file_info(blend)
card_bindings={
 'ironclad':{'skeletal':'ironclad'},'ember_archer':{'skeletal':'ember_archer'},
 'twin_blades':{'skeletal':'twin_blades'},'boulderback':{'skeletal':'boulderback'},
 'arc_mage':{'skeletal':'arc_mage'},'rambeast':{'skeletal':'rambeast'},
 'sky_manta':{'skeletal':'sky_manta'},'vampire_bats':{'skeletal':'vampire_bats'},
 'frost_fang':{'skeletal':'frost_fang'},'storm_raven':{'skeletal':'storm_raven'},
 'archer_tower':{'static':'archer_tower','skeletalDecoration':'tower_archer',
                 'decorationBaseMeters':[0,0,1.94],'decorationScale':.40},
 'bullet_burst':{'static':'bullet_round'},'nova_flask':{'static':'nova_flask'},
 'meteor_shards':{'static':'meteor_shard'}}
manifest['cards']=card_bindings
for card in card_bindings:
    illustration=SRC/'CardArt'/(card+'.png')
    if illustration.exists():manifest['illustrations'][card]=file_info(illustration)
guard_socket_metadata(manifest['statics'])
manifest['statics'].get('tower_core',{}).update({'sockets':{'muzzle':{'positionMeters':[0,0,3.88]},'hp_anchor':{'positionMeters':[0,0,4.55]}}})
manifest['statics'].get('archer_tower',{}).update({'sockets':{'archer_base':{'positionMeters':[0,0,1.94]},'hp_anchor':{'positionMeters':[0,0,2.9]}}})
manifestPath=ROOT/'Assets/asset_manifest.json'
manifestPath.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('RIFT_ASSET_FINISHED',len(manifest['characters']),len(manifest['statics']),flush=True)
