"""
Rift Crown Arena — Blender model forge v12.8
Run:
    blender --background --python blender/generate_models.py

Creates the original Rift Crown card/tower models as GLB files and saves a single editable
Blender source file. V9 keeps the authored low-poly identity but adds a third readability pass for the high 2.5D camera:
stronger faces, armor layering, weapon hardware, emissive runes, cloth/trim separation, extra tower
masonry and architectural silhouette detail, plus more deliberate material contrast.
"""
from pathlib import Path
import math
import bpy

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "models"
OUT.mkdir(parents=True, exist_ok=True)

# --------------------------- reset / render defaults ---------------------------
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
# Blender changed the Eevee enum across major versions. Blender 4.x commonly exposes
# BLENDER_EEVEE_NEXT, while Blender 5.x exposes BLENDER_EEVEE. Select the newest
# available Eevee identifier without crashing the entire model-forge run.
for _engine in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE', 'BLENDER_WORKBENCH'):
    try:
        scene.render.engine = _engine
        print(f'[Rift Crown] Render engine: {_engine}')
        break
    except (TypeError, ValueError):
        continue
else:
    print('[Rift Crown] Warning: no Eevee/Workbench engine enum accepted; continuing with Blender default.')
scene.world.color = (0.025, 0.035, 0.055)

# --------------------------- materials ---------------------------
def material(name, color, metallic=0.0, roughness=0.72, emission=None, alpha=1.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = (*color, alpha)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get('Principled BSDF')
    if bsdf:
        bsdf.inputs['Base Color'].default_value = (*color, 1.0)
        bsdf.inputs['Metallic'].default_value = metallic
        bsdf.inputs['Roughness'].default_value = roughness
        if 'Alpha' in bsdf.inputs:
            bsdf.inputs['Alpha'].default_value = alpha
        if emission is not None:
            if 'Emission Color' in bsdf.inputs:
                bsdf.inputs['Emission Color'].default_value = (*emission, 1.0)
                bsdf.inputs['Emission Strength'].default_value = 3.0
            elif 'Emission' in bsdf.inputs:
                bsdf.inputs['Emission'].default_value = (*emission, 1.0)
    if alpha < 1:
        # Blender 4.2+ uses surface_render_method; older builds used blend_method.
        try:
            m.surface_render_method = 'DITHERED'
        except (AttributeError, TypeError, ValueError):
            try:
                m.blend_method = 'BLEND'
            except (AttributeError, TypeError, ValueError):
                pass
    return m

M = {
    'steel': material('SteelBlue', (0.27,0.37,0.44), .55, .28),
    'steel_mid': material('SteelMid', (0.39,0.51,0.58), .48, .34),
    'darksteel': material('DarkSteel', (0.10,0.15,0.19), .60, .24),
    'silver': material('SilverEdge', (0.78,0.86,0.90), .82, .18),
    'skin': material('WarmSkin', (0.66,0.44,0.31), 0, .82),
    'skin_light': material('SkinLight', (0.78,0.55,0.38), 0, .8),
    'black': material('NearBlack', (0.035,0.045,0.06), .05, .74),
    'ember': material('EmberCloth', (0.46,0.10,0.07), 0, .90),
    'ember2': material('EmberTrim', (0.95,0.28,0.08), 0, .64),
    'ember_glow': material('EmberGlow', (1.0,0.35,0.06), 0, .25, emission=(1.0,0.20,0.03)),
    'wood': material('DarkWood', (0.25,0.12,0.055), 0, .96),
    'leather': material('Leather', (0.20,0.105,0.065), 0, .86),
    'violet': material('VioletCloth', (0.20,0.12,0.41), 0, .86),
    'violet2': material('VioletTrim', (0.52,0.29,0.80), .05, .52),
    'stone': material('BoulderStone', (0.30,0.35,0.34), 0, .97),
    'stone2': material('BoulderStoneLight', (0.47,0.51,0.48), 0, .93),
    'stone_dark': material('BoulderStoneDark', (0.19,0.23,0.23), 0, 1.0),
    'arc': material('ArcBlue', (0.11,0.24,0.68), 0, .58),
    'arc2': material('ArcViolet', (0.32,0.22,0.62), 0, .54),
    'arc_glow': material('ArcGlow', (0.10,0.75,1.0), 0, .18, emission=(0.10,0.72,1.0)),
    'ram': material('RambeastFur', (0.37,0.23,0.14), 0, .98),
    'ram2': material('RambeastDark', (0.18,0.10,0.065), 0, .98),
    'horn': material('Horn', (0.78,0.68,0.49), 0, .76),
    'manta': material('MantaTeal', (0.075,0.43,0.47), 0, .61),
    'manta2': material('MantaLight', (0.18,0.74,0.72), 0, .48),
    'eye': material('EyeGlow', (0.66,1.0,1.0), 0, .16, emission=(0.25,0.95,1.0)),
    'tower': material('TowerStone', (0.57,0.62,0.64), 0, .90),
    'tower_light': material('TowerLight', (0.72,0.77,0.77), 0, .86),
    'tower_dark': material('TowerDark', (0.28,0.34,0.38), .08, .80),
    'gold': material('Gold', (0.82,0.57,0.17), .52, .30),
    'flask': material('NovaGlass', (0.38,0.08,0.58), .06, .16, alpha=.72),
    'nova': material('NovaGlow', (0.78,0.22,1.0), 0, .15, emission=(0.78,0.20,1.0)),
    'white': material('EyeWhite', (.92,.95,.96), 0, .55),
}

# --------------------------- geometry helpers ---------------------------
def new_collection(name):
    # Reuse the collections left by the object reset when rerunning the forge.
    # Stable names keep the detail passes and exported GLB paths consistent.
    col = bpy.data.collections.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
    children = bpy.context.scene.collection.children
    if name not in children:
        children.link(col)
    return col

def move_to_collection(obj, col):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    col.objects.link(obj)

def finish(obj, col, matl=None, bevel=0.0, smooth=False):
    move_to_collection(obj, col)
    if matl:
        obj.data.materials.append(matl)
    if bevel > 0:
        mod = obj.modifiers.new('EdgeBevel','BEVEL')
        mod.width = bevel
        mod.segments = 3
    if hasattr(obj.data, 'polygons'):
        for p in obj.data.polygons:
            p.use_smooth = smooth
    return obj

def cube(col, name, loc, scale, matl, rotation=(0,0,0), bevel=.025):
    bpy.ops.mesh.primitive_cube_add(location=loc, rotation=rotation)
    o=bpy.context.object; o.name=name; o.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    return finish(o,col,matl,bevel=bevel)

def sphere(col, name, loc, scale, matl, subdivisions=2):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdivisions, radius=1, location=loc)
    o=bpy.context.object; o.name=name; o.scale=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    return finish(o,col,matl,smooth=False)

def cylinder(col, name, loc, radius, depth, matl, rotation=(0,0,0), vertices=12, bevel=.015):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc, rotation=rotation)
    o=bpy.context.object; o.name=name
    return finish(o,col,matl,bevel=bevel)

def cone(col, name, loc, r1, r2, depth, matl, rotation=(0,0,0), vertices=12, bevel=.01):
    bpy.ops.mesh.primitive_cone_add(vertices=vertices, radius1=r1, radius2=r2, depth=depth, location=loc, rotation=rotation)
    o=bpy.context.object; o.name=name
    return finish(o,col,matl,bevel=bevel)

def torus(col,name,loc,major,minor,matl,rotation=(0,0,0),major_segments=20,minor_segments=8):
    bpy.ops.mesh.primitive_torus_add(major_radius=major,minor_radius=minor,major_segments=major_segments,minor_segments=minor_segments,location=loc,rotation=rotation)
    o=bpy.context.object;o.name=name
    return finish(o,col,matl)

def eye_pair(col, y, z, spread=.12, scale=.055, glow=False):
    mat_eye=M['eye'] if glow else M['black']
    for x in (-spread, spread):
        sphere(col,f'EyeWhite_{x}',(x,y,z),(scale*1.35,scale*.6,scale),M['white'],1)
        sphere(col,f'EyePupil_{x}',(x,y-.035,z),(scale*.55,scale*.28,scale*.55),mat_eye,1)

def parent_all(col, root_name):
    root=bpy.data.objects.new(root_name,None); col.objects.link(root)
    for o in list(col.objects):
        if o is not root and o.parent is None: o.parent=root
    return root

# --------------------------- unit models ---------------------------
def build_ironclad():
    c=new_collection('ironclad')
    # body / armor
    cylinder(c,'Torso',(0,0,.82),.38,.82,M['darksteel'])
    cube(c,'Breastplate',(0,-.12,1.05),(.42,.17,.32),M['steel_mid'],bevel=.06)
    cube(c,'ChestInset',(0,-.30,1.06),(.27,.035,.22),M['darksteel'],bevel=.025)
    cube(c,'Belt',(0,-.02,.70),(.40,.27,.07),M['leather'],bevel=.02)
    sphere(c,'Head',(0,0,1.55),(.29,.28,.31),M['skin'])
    cone(c,'Helmet',(0,.01,1.83),.36,.25,.34,M['darksteel'])
    cube(c,'HelmetBrow',(0,-.265,1.70),(.30,.045,.065),M['steel_mid'])
    cube(c,'Visor',(0,-.305,1.61),(.24,.03,.045),M['black'],bevel=.01)
    # shoulders / arms / legs
    for x,sgn in [(-.43,-1),(.43,1)]:
        sphere(c,f'Pauldron_{sgn}',(x,-.01,1.23),(.24,.27,.20),M['steel_mid'],1)
        cylinder(c,f'Arm_{sgn}',(x,-.01,.93),.10,.50,M['steel'],rotation=(0,0,math.radians(8*sgn)))
        cube(c,f'Gauntlet_{sgn}',(x+.02*sgn,-.02,.66),(.13,.13,.14),M['darksteel'])
    for x in (-.19,.19):
        cylinder(c,f'Leg_{x}',(x,0,.36),.13,.48,M['steel'])
        cube(c,f'Boot_{x}',(x,-.08,.12),(.16,.22,.13),M['darksteel'],bevel=.04)
    # shield
    cube(c,'ShieldCore',(-.59,-.03,1.02),(.105,.38,.50),M['steel_mid'],rotation=(0,0,math.radians(3)),bevel=.08)
    torus(c,'ShieldRim',(-.70,-.035,1.02),.30,.035,M['silver'],rotation=(math.radians(90),0,0))
    cube(c,'ShieldSlashA',(-.705,-.072,1.02),(.035,.02,.30),M['arc_glow'],rotation=(0,0,math.radians(42)),bevel=.008)
    cube(c,'ShieldSlashB',(-.705,-.074,1.02),(.035,.02,.30),M['arc_glow'],rotation=(0,0,math.radians(-42)),bevel=.008)
    # sword
    cube(c,'SwordBlade',(.58,0,1.10),(.045,.035,.54),M['silver'],rotation=(0,math.radians(-17),0),bevel=.015)
    cube(c,'SwordFuller',(.60,-.038,1.13),(.012,.008,.42),M['arc_glow'],rotation=(0,math.radians(-17),0),bevel=.004)
    cube(c,'SwordGrip',(.47,0,.62),(.055,.055,.18),M['leather'],rotation=(0,math.radians(-17),0),bevel=.015)
    cube(c,'SwordGuard',(.49,0,.79),(.18,.045,.045),M['gold'],rotation=(0,math.radians(-17),0),bevel=.015)
    parent_all(c,'Ironclad_Root'); return c

def build_archer():
    c=new_collection('ember_archer')
    cone(c,'Tunic',(0,0,.69),.43,.28,.86,M['ember'])
    cube(c,'ChestLeather',(0,-.18,.94),(.27,.11,.25),M['leather'],bevel=.05)
    cube(c,'Belt',(0,0,.57),(.38,.28,.055),M['ember2'])
    sphere(c,'Head',(0,0,1.40),(.28,.27,.30),M['skin'])
    cone(c,'Hood',(0,.03,1.57),.40,.07,.61,M['ember'])
    cube(c,'HoodBand',(0,-.21,1.42),(.25,.055,.06),M['ember2'])
    eye_pair(c,-.27,1.42,.105,.05)
    for x,sgn in [(-.31,-1),(.31,1)]:
        cylinder(c,f'Arm_{sgn}',(x,-.02,.98),.09,.50,M['skin'],rotation=(0,0,math.radians(18*sgn)))
        cube(c,f'Bracer_{sgn}',(x+.05*sgn,-.02,.74),(.11,.12,.17),M['leather'])
    for x in (-.15,.15):
        cylinder(c,f'Leg_{x}',(x,0,.31),.10,.43,M['leather'])
        cube(c,f'Boot_{x}',(x,-.08,.10),(.13,.20,.12),M['darksteel'])
    # bow and arrow
    torus(c,'Bow',(.52,0,1.03),.48,.035,M['wood'],rotation=(math.radians(90),0,0))
    cube(c,'BowCut',(.52,.01,1.03),(.33,.05,.52),M['ember'],bevel=.0)  # decorative central grip silhouette
    cylinder(c,'Arrow',(.10,-.16,1.06),.018,1.30,M['silver'],rotation=(math.radians(90),0,0),vertices=8,bevel=0)
    cone(c,'ArrowHead',(.10,-.84,1.06),.06,.005,.16,M['silver'],rotation=(math.radians(90),0,0),vertices=6)
    # quiver / ember charm
    cylinder(c,'Quiver',(-.42,.11,.95),.12,.62,M['leather'],rotation=(0,0,math.radians(-14)))
    for i in range(3):
        cylinder(c,f'QuiverArrow_{i}',(-.44+i*.035,.10,1.32),.012,.52,M['silver'],rotation=(0,0,math.radians(-14)),vertices=6,bevel=0)
    sphere(c,'EmberCharm',(.27,-.22,.82),(.10,.06,.10),M['ember_glow'],1)
    parent_all(c,'EmberArcher_Root'); return c

def build_twin():
    c=new_collection('twin_blade')
    cylinder(c,'Body',(0,0,.65),.29,.74,M['violet'])
    cube(c,'ChestWrap',(0,-.16,.91),(.30,.10,.23),M['violet2'],rotation=(0,0,math.radians(6)),bevel=.04)
    sphere(c,'Head',(0,0,1.29),(.265,.255,.28),M['skin'])
    cone(c,'Hood',(0,.03,1.46),.35,.11,.47,M['darksteel'])
    cube(c,'Mask',(0,-.25,1.30),(.24,.035,.075),M['black'])
    for x in (-.105,.105): sphere(c,f'VioletEye_{x}',(x,-.29,1.31),(.045,.02,.035),M['violet2'],1)
    for x,sgn in [(-.36,-1),(.36,1)]:
        cylinder(c,f'Arm_{sgn}',(x,0,.91),.09,.48,M['violet'],rotation=(0,0,math.radians(25*sgn)))
        cube(c,f'Glove_{sgn}',(x+.07*sgn,-.01,.68),(.10,.11,.13),M['darksteel'])
        cube(c,f'Blade_{sgn}',(x+.15*sgn,0,.94),(.035,.025,.48),M['silver'],rotation=(0,math.radians(8*sgn),math.radians(24*sgn)),bevel=.012)
        cube(c,f'BladeGlow_{sgn}',(x+.16*sgn,-.025,.95),(.010,.010,.34),M['violet2'],rotation=(0,math.radians(8*sgn),math.radians(24*sgn)),bevel=.003)
        cube(c,f'Grip_{sgn}',(x+.035*sgn,0,.56),(.05,.05,.16),M['leather'],rotation=(0,0,math.radians(24*sgn)))
    for x in (-.15,.15):
        cylinder(c,f'Leg_{x}',(x,0,.29),.105,.40,M['darksteel'])
        cube(c,f'Boot_{x}',(x,-.07,.09),(.13,.19,.11),M['black'])
    cube(c,'BackSash',(0,.23,.69),(.11,.05,.47),M['violet2'],rotation=(math.radians(-18),0,math.radians(12)))
    parent_all(c,'TwinBlade_Root'); return c

def build_boulderback():
    c=new_collection('boulderback')
    sphere(c,'MainBoulder',(0,.03,.97),(.82,.66,.73),M['stone'])
    sphere(c,'Head',(0,-.54,1.30),(.47,.41,.44),M['stone2'])
    # rocky shell plates
    plates=[(-.45,.18,1.54,.38),(.34,.19,1.66,.43),(0,.42,1.42,.36),(-.16,-.10,1.72,.28),(.50,-.02,1.32,.25)]
    for i,(x,y,z,r) in enumerate(plates): sphere(c,f'ShellRock_{i}',(x,y,z),(r,r*.80,r*.92),M['stone2' if i%2 else 'stone'],1)
    for x in (-.49,.49):
        for y in (-.33,.33):
            cube(c,f'Leg_{x}_{y}',(x,y,.39),(.23,.23,.42),M['stone'],rotation=(0,0,math.radians(x*8)),bevel=.06)
            cube(c,f'Foot_{x}_{y}',(x,y-.07,.10),(.28,.31,.15),M['stone_dark'],bevel=.05)
    # face and tusks
    sphere(c,'EyeL',(-.16,-.89,1.42),(.075,.045,.065),M['nova'],1)
    sphere(c,'EyeR',(.16,-.89,1.42),(.075,.045,.065),M['nova'],1)
    cube(c,'Mouth',(0,-.95,1.18),(.23,.04,.055),M['stone_dark'])
    cone(c,'TuskL',(-.31,-.87,1.23),.09,.018,.40,M['stone2'],rotation=(math.radians(64),0,math.radians(-16)),vertices=8)
    cone(c,'TuskR',(.31,-.87,1.23),.09,.018,.40,M['stone2'],rotation=(math.radians(64),0,math.radians(16)),vertices=8)
    torus(c,'RiftBand',(0,-.03,1.05),.55,.045,M['nova'],rotation=(math.radians(90),0,0))
    parent_all(c,'Boulderback_Root'); return c

def build_arc_mage():
    c=new_collection('arc_mage')
    cone(c,'Robe',(0,0,.67),.53,.20,1.15,M['arc'])
    cube(c,'RobeChest',(0,-.20,.98),(.31,.09,.28),M['arc2'],bevel=.05)
    cube(c,'Belt',(0,0,.57),(.40,.29,.055),M['gold'])
    sphere(c,'Head',(0,0,1.44),(.28,.27,.30),M['skin'])
    cone(c,'Hat',(0,.02,1.82),.49,.035,.75,M['arc2'])
    torus(c,'HatBand',(0,.02,1.59),.29,.035,M['gold'],rotation=(0,0,0))
    eye_pair(c,-.27,1.45,.105,.05,glow=True)
    for x,sgn in [(-.34,-1),(.34,1)]:
        cylinder(c,f'Arm_{sgn}',(x,0,.98),.09,.49,M['arc'],rotation=(0,0,math.radians(17*sgn)))
        cube(c,f'Glove_{sgn}',(x+.04*sgn,-.02,.74),(.10,.11,.12),M['darksteel'])
    for x in (-.14,.14): cube(c,f'Boot_{x}',(x,-.08,.11),(.13,.20,.12),M['darksteel'])
    cylinder(c,'Staff',(.51,0,1.02),.045,1.66,M['wood'],vertices=12)
    torus(c,'OrbRingA',(.51,0,1.90),.26,.028,M['silver'],rotation=(math.radians(90),0,0))
    torus(c,'OrbRingB',(.51,0,1.90),.26,.028,M['gold'],rotation=(0,math.radians(90),0))
    sphere(c,'ArcOrb',(.51,0,1.90),(.17,.17,.17),M['arc_glow'],2)
    cube(c,'ChestRuneA',(0,-.30,.99),(.035,.025,.18),M['arc_glow'],rotation=(0,0,math.radians(50)),bevel=.004)
    cube(c,'ChestRuneB',(0,-.30,.99),(.035,.025,.18),M['arc_glow'],rotation=(0,0,math.radians(-50)),bevel=.004)
    parent_all(c,'ArcMage_Root'); return c

def build_rambeast():
    c=new_collection('rambeast')
    sphere(c,'Body',(0,.08,.82),(.73,.98,.57),M['ram'])
    sphere(c,'Chest',(0,-.46,.93),(.58,.52,.48),M['ram2'])
    sphere(c,'Head',(0,-.78,1.08),(.50,.46,.44),M['ram2'])
    sphere(c,'Muzzle',(0,-1.16,.99),(.30,.19,.24),M['ram'])
    for x in (-.44,.44):
        for y in (-.34,.42):
            cylinder(c,f'Leg_{x}_{y}',(x,y,.34),.15,.55,M['ram2'])
            cube(c,f'Hoof_{x}_{y}',(x,y-.04,.08),(.18,.22,.12),M['black'])
    # curved horns composed of torus + tips
    torus(c,'HornLoopL',(-.34,-.80,1.35),.40,.085,M['horn'],rotation=(math.radians(80),math.radians(10),math.radians(35)))
    torus(c,'HornLoopR',(.34,-.80,1.35),.40,.085,M['horn'],rotation=(math.radians(80),math.radians(-10),math.radians(-35)))
    cone(c,'HornTipL',(-.58,-1.08,1.16),.10,.015,.43,M['horn'],rotation=(math.radians(62),0,math.radians(-30)),vertices=10)
    cone(c,'HornTipR',(.58,-1.08,1.16),.10,.015,.43,M['horn'],rotation=(math.radians(62),0,math.radians(30)),vertices=10)
    eye_pair(c,-1.17,1.16,.16,.055,glow=True)
    # armored charging harness
    cube(c,'HeadPlate',(0,-1.10,1.39),(.34,.11,.15),M['darksteel'],bevel=.05)
    cube(c,'BackPlate',(0,.14,1.23),(.55,.62,.12),M['steel'],bevel=.07)
    cube(c,'GoldHarness',(0,-.44,1.02),(.63,.08,.07),M['gold'],bevel=.025)
    sphere(c,'HarnessGem',(0,-.55,1.04),(.12,.06,.12),M['ember_glow'],1)
    parent_all(c,'Rambeast_Root'); return c

def build_manta():
    c=new_collection('sky_manta')
    sphere(c,'Body',(0,0,1.05),(.55,1.02,.24),M['manta'])
    # layered swept wings
    left=cube(c,'WingL',(-.72,.05,1.04),(.72,.55,.075),M['manta2'],rotation=(0,math.radians(8),math.radians(-10)),bevel=.09)
    right=cube(c,'WingR',(.72,.05,1.04),(.72,.55,.075),M['manta2'],rotation=(0,math.radians(-8),math.radians(10)),bevel=.09)
    cube(c,'WingMarkL',(-.72,-.05,1.11),(.52,.36,.025),M['eye'],rotation=(0,math.radians(8),math.radians(-10)),bevel=.05)
    cube(c,'WingMarkR',(.72,-.05,1.11),(.52,.36,.025),M['eye'],rotation=(0,math.radians(-8),math.radians(10)),bevel=.05)
    cone(c,'Tail',(0,.95,1.04),.12,.018,1.02,M['manta'],rotation=(math.radians(90),0,0),vertices=10)
    sphere(c,'Face',(0,-.64,1.04),(.31,.32,.20),M['manta'])
    sphere(c,'EyeL',(-.18,-.87,1.12),(.075,.045,.06),M['eye'],1)
    sphere(c,'EyeR',(.18,-.87,1.12),(.075,.045,.06),M['eye'],1)
    torus(c,'RiftRing',(0,-.08,1.12),.31,.028,M['eye'],rotation=(math.radians(90),0,0))
    # small stabilizer fins
    cube(c,'FinL',(-.32,.62,1.07),(.18,.28,.035),M['manta2'],rotation=(0,math.radians(18),math.radians(-22)),bevel=.04)
    cube(c,'FinR',(.32,.62,1.07),(.18,.28,.035),M['manta2'],rotation=(0,math.radians(-18),math.radians(22)),bevel=.04)
    parent_all(c,'SkyManta_Root'); return c

def build_vampire_bat():
    c=new_collection('vampire_bat')
    sphere(c,'BatBody',(0,0,1.02),(.34,.42,.28),M['violet2'])
    sphere(c,'BatHead',(0,-.24,1.28),(.23,.20,.21),M['violet'])
    cone(c,'EarL',(-.13,-.22,1.52),.10,.015,.27,M['darksteel'],rotation=(0,0,math.radians(-8)),vertices=7)
    cone(c,'EarR',(.13,-.22,1.52),.10,.015,.27,M['darksteel'],rotation=(0,0,math.radians(8)),vertices=7)
    cube(c,'WingL',(-.48,.02,1.08),(.55,.30,.025),M['violet2'],rotation=(0,math.radians(8),math.radians(-20)),bevel=.04)
    cube(c,'WingR',(.48,.02,1.08),(.55,.30,.025),M['violet2'],rotation=(0,math.radians(-8),math.radians(20)),bevel=.04)
    sphere(c,'EyeL',(-.085,-.43,1.31),(.055,.032,.045),M['nova'],1)
    sphere(c,'EyeR',(.085,-.43,1.31),(.055,.032,.045),M['nova'],1)
    cone(c,'FangL',(-.065,-.45,1.17),.035,.005,.16,M['white'],rotation=(math.radians(90),0,0),vertices=6)
    cone(c,'FangR',(.065,-.45,1.17),.035,.005,.16,M['white'],rotation=(math.radians(90),0,0),vertices=6)
    sphere(c,'HeartCore',(0,-.20,1.00),(.07,.04,.07),M['nova'],1)
    parent_all(c,'VampireBat_Root'); return c


def build_frost_fang():
    c=new_collection('frost_fang')
    # upgraded silhouette with extra face definition, ice armor and more readable crystal spines
    sphere(c,'FrostBody',(0,.08,.78),(.64,.88,.44),M['tower_light'])
    sphere(c,'FrostChest',(0,-.42,.92),(.48,.48,.46),M['steel_mid'])
    sphere(c,'FrostHead',(0,-.82,1.12),(.41,.36,.35),M['tower_light'])
    sphere(c,'FrostSnout',(0,-1.02,1.08),(.24,.16,.18),M['tower_light'])
    cube(c,'FrostBrow',(0,-1.06,1.26),(.32,.06,.08),M['steel_mid'],bevel=.035)
    for x in (-.14,.14):
        sphere(c,f'FrostEye{x}',(x,-1.10,1.23),(.065,.035,.055),M['eye'],1)
        cone(c,f'Fang{x}',(x,-1.12,.95),.075,.008,.38,M['white'],rotation=(math.radians(90),0,0),vertices=7)
    for x in (-.44,.44):
        for y in (-.32,.30):
            cylinder(c,f'Leg{x}{y}',(x,y,.35),.12,.54,M['steel_mid'])
            cube(c,f'Paw{x}{y}',(x,y-.06,.09),(.18,.24,.11),M['darksteel'],bevel=.04)
    cube(c,'FrostHarness',(0,-.02,1.18),(.60,.60,.09),M['steel'],bevel=.08)
    cube(c,'ShoulderIceL',(-.43,-.18,1.12),(.18,.22,.17),M['silver'],bevel=.05)
    cube(c,'ShoulderIceR',(.43,-.18,1.12),(.18,.22,.17),M['silver'],bevel=.05)
    torus(c,'FrostCollar',(0,-.55,1.12),.34,.025,M['eye'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=6)
    for i,(x,y,z,h) in enumerate(((-.32,.14,1.48,.44),(-.10,.28,1.42,.34),(.10,.30,1.50,.48),(.32,.14,1.46,.42))):
        cone(c,f'IceSpine{i}',(x,y,z),.13,.012,h,M['eye'],rotation=(0,math.radians(x*18),math.radians(x*12)),vertices=7)
    cone(c,'TailIce',(0,.88,.88),.12,.012,.28,M['eye'],rotation=(math.radians(-12),0,0),vertices=7)
    parent_all(c,'FrostFang_Root'); return c


def build_storm_raven():
    c=new_collection('storm_raven')
    sphere(c,'StormBody',(0,.02,1.10),(.54,.76,.42),M['arc2'])
    sphere(c,'StormChest',(0,-.34,1.20),(.38,.36,.37),M['arc'])
    sphere(c,'StormHead',(0,-.66,1.42),(.31,.28,.28),M['darksteel'])
    cone(c,'StormBeak',(0,-.95,1.40),.11,.015,.45,M['silver'],rotation=(math.radians(90),0,0),vertices=7)
    sphere(c,'StormEyeL',(-.10,-.88,1.49),(.055,.032,.045),M['eye'],1)
    sphere(c,'StormEyeR',(.10,-.88,1.49),(.055,.032,.045),M['eye'],1)
    cube(c,'WingL',(-.80,.05,1.18),(.76,.38,.04),M['arc'],rotation=(0,math.radians(8),math.radians(-16)),bevel=.07)
    cube(c,'WingR',(.80,.05,1.18),(.76,.38,.04),M['arc'],rotation=(0,math.radians(-8),math.radians(16)),bevel=.07)
    cube(c,'WingGlowL',(-.82,-.03,1.23),(.58,.20,.018),M['arc_glow'],rotation=(0,math.radians(8),math.radians(-16)),bevel=.03)
    cube(c,'WingGlowR',(.82,-.03,1.23),(.58,.20,.018),M['arc_glow'],rotation=(0,math.radians(-8),math.radians(16)),bevel=.03)
    cube(c,'BackPlate',(0,.02,1.16),(.42,.12,.10),M['arc2'],bevel=.05)
    for i,x in enumerate((-.60,-.36,-.12,.12,.36,.60)):
        cone(c,f'StormFeather{i}',(x,.30,1.46),.07,.008,.30,M['arc_glow'],rotation=(0,math.radians(x*9),math.radians(x*18)),vertices=7)
    sphere(c,'StormCore',(0,-.36,1.18),(.12,.06,.12),M['arc_glow'],1)
    torus(c,'StormRing',(0,.0,.98),.74,.028,M['arc_glow'],rotation=(math.radians(90),0,0),major_segments=22,minor_segments=6)
    torus(c,'StormRingInner',(0,.0,.98),.50,.014,M['silver'],rotation=(math.radians(90),0,0),major_segments=20,minor_segments=5)
    cone(c,'TailFeatherL',(-.16,.70,1.02),.09,.012,.64,M['arc2'],rotation=(math.radians(90),0,math.radians(-8)),vertices=7)
    cone(c,'TailFeatherR',(.16,.70,1.02),.09,.012,.64,M['arc2'],rotation=(math.radians(90),0,math.radians(8)),vertices=7)
    parent_all(c,'StormRaven_Root'); return c


def build_nova_flask():
    c=new_collection('nova_flask')
    sphere(c,'FlaskBody',(0,0,.62),(.43,.43,.50),M['flask'],subdivisions=3)
    cylinder(c,'Neck',(0,0,1.13),.16,.46,M['flask'],vertices=16)
    cylinder(c,'Cork',(0,0,1.39),.18,.18,M['wood'],vertices=12)
    sphere(c,'Core',(0,0,.59),(.20,.20,.24),M['nova'],2)
    torus(c,'BandTop',(0,0,.96),.29,.045,M['gold'])
    torus(c,'BandBottom',(0,0,.35),.31,.035,M['gold'])
    torus(c,'CoreRingA',(0,0,.59),.30,.022,M['nova'],rotation=(math.radians(90),0,0))
    torus(c,'CoreRingB',(0,0,.59),.30,.022,M['nova'],rotation=(0,math.radians(90),0))
    for a in (0,math.pi/2,math.pi,3*math.pi/2):
        sphere(c,f'Rune_{a}',(.36*math.cos(a),.36*math.sin(a),.62),(.055,.055,.055),M['nova'],1)
    parent_all(c,'NovaFlask_Root'); return c

# --------------------------- towers ---------------------------
def tower_common(c, core=False):
    foundation_r=1.52 if core else 1.34
    cylinder(c,'Foundation',(0,0,.27),foundation_r,.54,M['tower_dark'],vertices=12)
    cylinder(c,'FoundationTrim',(0,0,.58),foundation_r*.91,.16,M['tower_light'],vertices=12)
    body=(1.02,1.02,1.38) if core else (.84,.84,1.08)
    cube(c,'TowerBody',(0,0,1.80 if core else 1.47),body,M['tower'],bevel=.08)
    # corner columns
    corner=.92 if core else .75
    h=2.55 if core else 2.00
    for x in (-corner,corner):
        for y in (-corner,corner):
            cylinder(c,f'Corner_{x}_{y}',(x,y,1.65 if core else 1.38),.18,h,M['tower_dark'],vertices=8)
            cone(c,f'CornerCap_{x}_{y}',(x,y,3.02 if core else 2.50),.25,.16,.28,M['tower_light'],vertices=8)
    band_z=3.13 if core else 2.58
    cube(c,'CrownBand',(0,0,band_z),(1.24,1.24,.17) if core else (1.02,1.02,.16),M['gold'],bevel=.04)
    cube(c,'CrownStone',(0,0,band_z+.18),(1.31,1.31,.10) if core else (1.08,1.08,.09),M['tower_dark'],bevel=.025)
    m=1.03 if core else .84
    for x in (-m,0,m):
        for y in (-m,m): cube(c,f'MerlonA_{x}_{y}',(x,y,band_z+.52),(.18,.18,.30),M['tower_dark'],bevel=.025)
    for x in (-m,m): cube(c,f'MerlonB_{x}',(x,0,band_z+.52),(.18,.18,.30),M['tower_dark'],bevel=.025)
    # front crest
    cube(c,'CrestPlate',(0,-(1.04 if core else .86),1.85 if core else 1.53),(.38,.07,.48),M['tower_dark'],bevel=.045)
    sphere(c,'CrestGem',(0,-(1.12 if core else .94),1.86 if core else 1.54),(.19,.055,.19),M['nova'],1)


def build_guard_tower():
    c=new_collection('tower_guard')
    tower_common(c,False)
    cube(c,'CannonDeck',(0,0,2.80),(.55,.60,.13),M['tower_dark'],bevel=.04)
    cylinder(c,'CannonBase',(0,-.06,2.94),.28,.22,M['steel'],rotation=(math.radians(90),0,0),vertices=12)
    cylinder(c,'CannonBarrel',(0,-.62,2.97),.15,.85,M['darksteel'],rotation=(math.radians(90),0,0),vertices=12)
    torus(c,'CannonMuzzle',(0,-1.08,2.97),.18,.035,M['silver'],rotation=(math.radians(90),0,0))
    parent_all(c,'GuardTower_Root'); return c

def build_core_tower():
    c=new_collection('tower_core')
    tower_common(c,True)
    cylinder(c,'Spire',(0,0,3.94),.11,1.02,M['darksteel'],vertices=10)
    torus(c,'SpireRing',(0,0,4.17),.40,.045,M['gold'])
    torus(c,'SpireRingTilt',(0,0,4.17),.40,.035,M['silver'],rotation=(math.radians(72),0,0))
    sphere(c,'CoreGem',(0,0,4.50),(.30,.30,.30),M['nova'],2)
    cone(c,'CrownTip',(0,0,4.96),.30,.025,.58,M['gold'],vertices=10)
    parent_all(c,'CoreTower_Root'); return c

def build_archer_tower():
    c=new_collection('archer_tower')
    cylinder(c,'Foundation',(0,0,.18),1.12,.36,M['tower_dark'],vertices=10)
    cylinder(c,'Body',(0,0,1.10),.84,1.75,M['tower'],vertices=10)
    for z in (.52,1.02,1.52):
        cube(c,f'MasonryBand{z}',(0,0,z),(.88,.88,.04),M['tower_dark'],bevel=.012)
    cube(c,'Platform',(0,0,2.12),(1.02,.92,.12),M['wood'],bevel=.04)
    cube(c,'PlatformTrim',(0,0,2.28),(1.10,1.0,.06),M['gold'],bevel=.02)
    for x in (-.78,.78):
        for y in (-.68,.68): cube(c,f'Merlon{x}{y}',(x,y,2.55),(.17,.17,.30),M['tower_dark'],bevel=.025)
    # archer
    sphere(c,'ArcherHead',(0,-.12,2.72),(.22,.21,.23),M['skin'])
    cone(c,'ArcherHood',(0,-.08,2.92),.28,.06,.36,M['ember'])
    cube(c,'ArcherTorso',(0,.02,2.40),(.24,.16,.25),M['ember'],bevel=.04)
    torus(c,'Bow',(.48,-.05,2.52),.40,.026,M['wood'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=6)
    cylinder(c,'BowString',(.48,-.05,2.52),.012,.78,M['white'],vertices=6,bevel=0)
    cylinder(c,'Arrow',(0.12,-.30,2.50),.015,.92,M['silver'],rotation=(math.radians(90),0,0),vertices=6,bevel=0)
    sphere(c,'CrestGem',(0,-.86,1.12),(.13,.05,.13),M['arc_glow'],1)
    parent_all(c,'ArcherTower_Root'); return c

collections = [
    build_ironclad(), build_archer(), build_twin(), build_boulderback(), build_arc_mage(),
    build_rambeast(), build_manta(), build_vampire_bat(), build_frost_fang(), build_storm_raven(), build_nova_flask(), build_archer_tower(), build_guard_tower(), build_core_tower()
]

# --------------------------- V3 secondary detail pass ---------------------------
def root_of(col):
    for obj in col.objects:
        if obj.type == 'EMPTY' and obj.parent is None:
            return obj
    return None

def parent_unparented(col):
    root = root_of(col)
    if not root:
        return
    for obj in list(col.objects):
        if obj is not root and obj.parent is None:
            obj.parent = root

def enhance_v3(col):
    n = col.name
    if n == 'ironclad':
        cube(col,'Cape',(0,.26,1.03),(.33,.055,.55),M['arc'],rotation=(math.radians(-7),0,0),bevel=.045)
        cube(col,'KneeL',(-.19,-.12,.42),(.15,.13,.11),M['steel_mid'],rotation=(math.radians(8),0,0),bevel=.035)
        cube(col,'KneeR',(.19,-.12,.42),(.15,.13,.11),M['steel_mid'],rotation=(math.radians(8),0,0),bevel=.035)
        cone(col,'HelmetPlume',(0,.05,2.08),.11,.018,.33,M['gold'],rotation=(0,0,0),vertices=8)
        sphere(col,'SwordPommel',(.43,0,.44),(.075,.075,.075),M['arc_glow'],1)
        for x in (-.67,-.58,-.49): sphere(col,f'ShieldRivet{x}',(x,-.415,1.02),(.035,.022,.035),M['silver'],1)
    elif n == 'ember_archer':
        cube(col,'ShoulderCape',(0,.18,1.18),(.36,.07,.23),M['ember'],rotation=(math.radians(-8),0,0),bevel=.045)
        cube(col,'PouchL',(-.24,-.03,.55),(.12,.12,.14),M['leather'],bevel=.035)
        cube(col,'PouchR',(.24,-.03,.55),(.12,.12,.14),M['leather'],bevel=.035)
        sphere(col,'ArrowFlame',(.10,-.95,1.06),(.09,.055,.09),M['ember_glow'],2)
        torus(col,'BowGripRing',(.52,0,1.03),.09,.022,M['gold'],rotation=(math.radians(90),0,0))
    elif n == 'twin_blade':
        cube(col,'ShoulderWrap',(0,.18,1.02),(.32,.065,.20),M['violet2'],rotation=(math.radians(-8),0,0),bevel=.04)
        cube(col,'KneeL',(-.15,-.12,.35),(.13,.12,.10),M['darksteel'],bevel=.03)
        cube(col,'KneeR',(.15,-.12,.35),(.13,.12,.10),M['darksteel'],bevel=.03)
        cone(col,'HoodSpikeL',(-.16,.01,1.78),.075,.012,.23,M['violet2'],rotation=(0,0,math.radians(-9)),vertices=7)
        cone(col,'HoodSpikeR',(.16,.01,1.78),.075,.012,.23,M['violet2'],rotation=(0,0,math.radians(9)),vertices=7)
        sphere(col,'BeltGem',(0,-.29,.63),(.075,.035,.075),M['nova'],1)
    elif n == 'boulderback':
        for i,(x,y,z,h) in enumerate([(-.43,.18,1.94,.45),(.40,.28,2.00,.52),(.02,.48,1.90,.38)]):
            cone(col,f'RiftCrystal_{i}',(x,y,z),.14,.018,h,M['nova'],rotation=(0,math.radians(x*15),math.radians(x*18)),vertices=7)
        cube(col,'JawPlate',(0,-.94,1.29),(.30,.08,.12),M['stone_dark'],bevel=.04)
        cube(col,'ForePlateL',(-.48,-.32,.70),(.24,.13,.15),M['stone2'],rotation=(0,0,math.radians(12)),bevel=.05)
        cube(col,'ForePlateR',(.48,-.32,.70),(.24,.13,.15),M['stone2'],rotation=(0,0,math.radians(-12)),bevel=.05)
    elif n == 'arc_mage':
        cube(col,'Spellbook',(-.49,.08,.93),(.23,.08,.31),M['arc2'],rotation=(0,math.radians(12),math.radians(7)),bevel=.035)
        cube(col,'BookPage',(-.49,-.015,.93),(.19,.018,.27),M['white'],rotation=(0,math.radians(12),math.radians(7)),bevel=.012)
        torus(col,'OrbitRing',(0,0,1.26),.44,.018,M['arc_glow'],rotation=(math.radians(68),math.radians(10),math.radians(18)))
        sphere(col,'OrbitRuneL',(-.39,-.20,1.32),(.065,.045,.065),M['arc_glow'],1)
        sphere(col,'OrbitRuneR',(.38,-.22,1.18),(.065,.045,.065),M['arc_glow'],1)
    elif n == 'rambeast':
        cube(col,'SideArmorL',(-.58,.08,1.00),(.31,.34,.10),M['steel'],rotation=(0,math.radians(-7),math.radians(5)),bevel=.055)
        cube(col,'SideArmorR',(.58,.08,1.00),(.31,.34,.10),M['steel'],rotation=(0,math.radians(7),math.radians(-5)),bevel=.055)
        torus(col,'HornBandL',(-.48,-.91,1.35),.13,.026,M['gold'],rotation=(math.radians(70),0,math.radians(-25)))
        torus(col,'HornBandR',(.48,-.91,1.35),.13,.026,M['gold'],rotation=(math.radians(70),0,math.radians(25)))
        cone(col,'BackSpikeL',(-.34,.30,1.58),.10,.015,.32,M['silver'],rotation=(0,0,math.radians(-12)),vertices=8)
        cone(col,'BackSpikeR',(.34,.30,1.58),.10,.015,.32,M['silver'],rotation=(0,0,math.radians(12)),vertices=8)
    elif n == 'sky_manta':
        for i,x in enumerate((-1.08,-.66,.66,1.08)):
            sphere(col,f'WingGlow_{i}',(x,-.06,1.11),(.075,.045,.055),M['eye'],1)
        cube(col,'TailFinL',(-.22,.73,1.06),(.18,.27,.035),M['manta2'],rotation=(0,math.radians(18),math.radians(-20)),bevel=.04)
        cube(col,'TailFinR',(.22,.73,1.06),(.18,.27,.035),M['manta2'],rotation=(0,math.radians(-18),math.radians(20)),bevel=.04)
        torus(col,'CoreTrim',(0,-.10,1.12),.38,.018,M['silver'],rotation=(math.radians(90),0,0))
    elif n == 'nova_flask':
        for a in (math.pi/4,3*math.pi/4,5*math.pi/4,7*math.pi/4):
            cube(col,f'GoldBrace_{a}',(.31*math.cos(a),.31*math.sin(a),.62),(.035,.035,.29),M['gold'],rotation=(0,0,a),bevel=.012)
        sphere(col,'CorkGem',(0,0,1.51),(.075,.075,.075),M['nova'],1)
    elif n == 'tower_guard':
        for z in (.92,1.48,2.02):
            cube(col,f'ArrowSlit_{z}',(0,-.87,z),(.12,.025,.17),M['black'],bevel=.008)
        cube(col,'CannonGoldBand',(0,-.80,2.97),(.21,.055,.21),M['gold'],rotation=(math.radians(90),0,0),bevel=.02)
        cube(col,'RearBanner',(0,.99,1.95),(.42,.035,.62),M['arc2'],rotation=(math.radians(-4),0,0),bevel=.02)
    elif n == 'tower_core':
        for z in (1.10,1.78,2.46):
            cube(col,f'CoreSlit_{z}',(0,-1.05,z),(.14,.025,.19),M['black'],bevel=.008)
        torus(col,'CoreHaloA',(0,0,4.50),.52,.022,M['nova'],rotation=(math.radians(70),0,0))
        torus(col,'CoreHaloB',(0,0,4.50),.52,.022,M['nova'],rotation=(0,math.radians(70),0))
        cube(col,'RearBanner',(0,1.12,2.15),(.50,.035,.78),M['arc2'],rotation=(math.radians(-4),0,0),bevel=.025)
    parent_unparented(col)

for col in collections:
    enhance_v3(col)

# --------------------------- V4 readability / finish pass ---------------------------
def enhance_v4(col):
    n=col.name
    if n=='ironclad':
        eye_pair(col,-.30,1.63,.105,.045,glow=True)
        cube(col,'GoldChestRail',(0,-.315,1.22),(.33,.024,.035),M['gold'],bevel=.008)
        cube(col,'JawGuard',(0,-.285,1.47),(.20,.038,.075),M['darksteel'],bevel=.015)
        for x in (-.43,.43):
            torus(col,f'PauldronTrim{x}',(x,-.03,1.24),.18,.018,M['gold'],rotation=(math.radians(90),0,0),major_segments=16,minor_segments=6)
        sphere(col,'SwordRune',(.59,-.045,1.12),(.035,.018,.10),M['arc_glow'],1)
    elif n=='ember_archer':
        cube(col,'GoldChestPin',(0,-.305,1.02),(.20,.022,.032),M['gold'],bevel=.006)
        for x in (-.32,.32): cube(col,f'ShoulderPad{x}',(x,-.02,1.18),(.14,.15,.085),M['ember2'],rotation=(0,0,math.radians(8 if x<0 else -8)),bevel=.035)
        for i,x in enumerate((-.06,.02,.10)):
            cone(col,f'FlameFeather{i}',(x,-.89,1.08+i*.02),.045,.006,.18,M['ember_glow'],rotation=(math.radians(90),0,0),vertices=7)
        torus(col,'BowGoldRing',(.52,-.02,1.03),.12,.018,M['gold'],rotation=(math.radians(90),0,0),major_segments=16,minor_segments=6)
    elif n=='twin_blade':
        cube(col,'MaskTrim',(0,-.292,1.31),(.25,.012,.020),M['violet2'],bevel=.004)
        for x,sgn in ((-.36,-1),(.36,1)):
            cube(col,f'BladeGuard{sgn}',(x+.02*sgn,-.01,.68),(.13,.045,.045),M['gold'],rotation=(0,0,math.radians(24*sgn)),bevel=.01)
            sphere(col,f'BladePommel{sgn}',(x-.05*sgn,.0,.52),(.055,.055,.055),M['nova'],1)
        cube(col,'BackBuckle',(0,.29,.69),(.10,.028,.09),M['silver'],bevel=.015)
    elif n=='boulderback':
        torus(col,'FaceRiftRing',(0,-.94,1.31),.31,.022,M['nova'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=6)
        for i,(x,y,z) in enumerate(((-.28,-.58,1.50),(.30,-.56,1.55),(-.18,.12,1.20),(.24,.20,1.12))):
            cube(col,f'Vein{i}',(x,y,z),(.035,.020,.18),M['nova'],rotation=(0,math.radians(15*i),math.radians(18*(-1 if i%2 else 1))),bevel=.005)
        cube(col,'JawArmor',(0,-.99,1.18),(.24,.055,.08),M['stone_dark'],bevel=.025)
    elif n=='arc_mage':
        cube(col,'BookClasp',(-.49,-.105,.93),(.055,.016,.11),M['gold'],rotation=(0,math.radians(12),math.radians(7)),bevel=.006)
        sphere(col,'StaffRune',(.51,-.045,1.90),(.065,.025,.065),M['arc_glow'],1)
        for i,a in enumerate((0,2.1,4.2)):
            sphere(col,f'OrbitGlyph{i}',(.44*math.cos(a),-.16,.0+1.26+.20*math.sin(a)),(.045,.025,.045),M['arc_glow'],1)
        cube(col,'RobeGoldRail',(0,-.31,.95),(.27,.018,.025),M['gold'],bevel=.005)
    elif n=='rambeast':
        cube(col,'BrowArmor',(0,-.94,1.34),(.31,.055,.09),M['steel'],bevel=.03)
        cube(col,'NoseGuard',(0,-1.10,1.12),(.11,.06,.16),M['darksteel'],bevel=.025)
        for x in (-.48,.48): sphere(col,f'HornCap{x}',(x,-1.13,1.53),(.075,.075,.075),M['gold'],1)
        for x in (-.52,.52): cube(col,f'LegArmor{x}',(x,-.25,.48),(.18,.14,.13),M['steel'],bevel=.04)
    elif n=='sky_manta':
        for i,x in enumerate((-.95,-.55,.55,.95)):
            cube(col,f'WingRib{i}',(x,-.02,1.08),(.30,.022,.035),M['silver'],rotation=(0,math.radians(-15 if x<0 else 15),math.radians(9 if x<0 else -9)),bevel=.006)
        torus(col,'EyeHalo',(0,-.54,1.10),.24,.015,M['eye'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=6)
        sphere(col,'BackCore',(0,.24,1.12),(.12,.08,.12),M['eye'],1)
    elif n=='nova_flask':
        torus(col,'NeckGoldRing',(0,0,1.23),.18,.025,M['gold'],major_segments=18,minor_segments=7)
        for i,a in enumerate((0,math.pi/2,math.pi,3*math.pi/2)):
            sphere(col,f'FlaskRune{i}',(.34*math.cos(a),.34*math.sin(a),.72),(.045,.045,.045),M['nova'],1)
    elif n=='archer_tower':
        for z in (.70,1.20,1.70): cube(col,f'ArrowTowerRail{z}',(0,0,z),(.82,.82,.022),M['tower_light'],bevel=.006)
        cube(col,'ArrowTowerBanner',(0,.76,1.36),(.28,.025,.48),M['ember2'],bevel=.02)
        torus(col,'BowGoldGrip',(.48,-.05,2.52),.09,.015,M['gold'],rotation=(math.radians(90),0,0),major_segments=14,minor_segments=5)
        sphere(col,'ArcherTowerRune',(0,-.90,1.12),(.06,.025,.06),M['eye'],1)
    elif n=='tower_guard':
        for z in (.78,1.22,1.67,2.12): cube(col,f'MasonryRail{z}',(0,0,z),(1.00,.90,.025),M['tower_dark'],bevel=.008)
        cube(col,'CannonSight',(0,-1.12,3.12),(.055,.055,.12),M['gold'],bevel=.008)
        for x in (-.82,.82): cube(col,f'Buttress{x}',(x,.12,.74),(.16,.36,.48),M['tower_light'],rotation=(0,0,math.radians(3 if x<0 else -3)),bevel=.035)
    elif n=='tower_core':
        for z in (.82,1.35,1.90,2.45,2.88): cube(col,f'CoreMasonryRail{z}',(0,0,z),(1.17,1.08,.025),M['tower_dark'],bevel=.008)
        for x in (-1.02,1.02): cube(col,f'CoreButtress{x}',(x,.14,.92),(.18,.42,.58),M['tower_light'],rotation=(0,0,math.radians(3 if x<0 else -3)),bevel=.04)
        sphere(col,'CoreInnerGem',(0,-.02,4.50),(.15,.15,.15),M['eye'],2)
    parent_unparented(col)

for col in collections:
    enhance_v4(col)

# --------------------------- V8 silhouette / readability polish ---------------------------
def enhance_v8(col):
    n=col.name
    if n=='ironclad':
        cube(col,'V8ShieldBoss',(-.70,-.43,1.02),(.09,.035,.09),M['gold'],bevel=.015)
        cube(col,'V8ChestRune',(0,-.345,1.08),(.045,.012,.15),M['arc_glow'],rotation=(0,0,math.radians(45)),bevel=.004)
    elif n=='ember_archer':
        cone(col,'V8FlameTip',(.10,-1.02,1.06),.055,.006,.22,M['ember_glow'],rotation=(math.radians(90),0,0),vertices=7)
        cube(col,'V8QuiverBand',(-.44,.06,1.02),(.14,.025,.035),M['gold'],rotation=(0,0,math.radians(-14)),bevel=.006)
    elif n=='twin_blade':
        cube(col,'V8Scarf',(0,.25,1.15),(.12,.035,.38),M['violet2'],rotation=(math.radians(-22),0,math.radians(10)),bevel=.018)
    elif n=='boulderback':
        sphere(col,'V8CoreCrystal',(0,-.18,1.86),(.14,.11,.20),M['nova'],1)
    elif n=='arc_mage':
        torus(col,'V8StaffHalo',(.51,0,1.90),.34,.012,M['arc_glow'],rotation=(math.radians(62),0,math.radians(18)),major_segments=20,minor_segments=5)
    elif n=='rambeast':
        cube(col,'V8SaddleTrim',(0,.04,1.26),(.44,.30,.035),M['gold'],bevel=.018)
    elif n=='sky_manta':
        cube(col,'V8WingEdgeL',(-.84,-.05,1.10),(.52,.025,.025),M['eye'],rotation=(0,math.radians(-8),math.radians(8)),bevel=.004)
        cube(col,'V8WingEdgeR',(.84,-.05,1.10),(.52,.025,.025),M['eye'],rotation=(0,math.radians(8),math.radians(-8)),bevel=.004)
    elif n=='archer_tower':
        cube(col,'V8RoofBraceL',(-.52,-.02,2.28),(.035,.65,.05),M['gold'],rotation=(0,0,math.radians(5)),bevel=.006)
        cube(col,'V8RoofBraceR',(.52,-.02,2.28),(.035,.65,.05),M['gold'],rotation=(0,0,math.radians(-5)),bevel=.006)
    elif n=='tower_guard':
        torus(col,'V8MuzzleGlow',(0,-1.10,2.97),.21,.018,M['arc_glow'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=5)
    elif n=='tower_core':
        sphere(col,'V8CoreSpark',(0,-.08,4.50),(.08,.05,.08),M['eye'],1)
    parent_unparented(col)

for col in collections:
    enhance_v8(col)

# --------------------------- V9 premium silhouette / material pass ---------------------------
def enhance_v9(col):
    n=col.name
    if n=='ironclad':
        torus(col,'V9ChestHalo',(0,-.33,1.10),.20,.014,M['gold'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=5)
        cube(col,'V9HelmRail',(0,-.30,1.78),(.27,.018,.028),M['silver'],bevel=.005)
        sphere(col,'V9ShieldRune',(-.70,-.43,1.02),(.045,.025,.045),M['arc_glow'],1)
    elif n=='ember_archer':
        cube(col,'V9BowInlay',(.52,-.035,1.03),(.020,.018,.32),M['gold'],rotation=(0,0,math.radians(4)),bevel=.004)
        sphere(col,'V9ArrowGlow',(.10,-1.03,1.06),(.055,.035,.055),M['ember_glow'],1)
        cube(col,'V9HoodPin',(0,-.25,1.54),(.055,.020,.055),M['gold'],bevel=.008)
    elif n=='twin_blade':
        torus(col,'V9MaskHalo',(0,-.30,1.31),.17,.012,M['violet2'],rotation=(math.radians(90),0,0),major_segments=16,minor_segments=5)
        cube(col,'V9BladeRailL',(-.47,-.03,.98),(.010,.012,.31),M['nova'],rotation=(0,0,math.radians(-24)),bevel=.002)
        cube(col,'V9BladeRailR',(.47,-.03,.98),(.010,.012,.31),M['nova'],rotation=(0,0,math.radians(24)),bevel=.002)
    elif n=='boulderback':
        for i,x in enumerate((-.34,0,.34)):
            cone(col,f'V9SpineCrystal{i}',(x,.18,2.02),.085,.010,.30,M['nova'],rotation=(0,math.radians(x*15),math.radians(x*12)),vertices=7)
        cube(col,'V9FaceRail',(0,-.99,1.36),(.28,.025,.035),M['stone2'],bevel=.006)
    elif n=='arc_mage':
        torus(col,'V9StaffOrbit',(.51,-.02,1.90),.31,.012,M['arc_glow'],rotation=(math.radians(58),math.radians(15),math.radians(15)),major_segments=20,minor_segments=5)
        sphere(col,'V9StaffCore',(.51,-.08,1.90),(.055,.035,.055),M['white'],1)
        cube(col,'V9RobeClasp',(0,-.31,1.03),(.08,.016,.08),M['gold'],bevel=.008)
    elif n=='rambeast':
        torus(col,'V9ForeheadRing',(0,-.96,1.32),.23,.014,M['gold'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=5)
        sphere(col,'V9ForeheadGem',(0,-1.00,1.32),(.050,.025,.050),M['ember_glow'],1)
        cube(col,'V9SaddleRail',(0,.02,1.26),(.46,.30,.020),M['silver'],bevel=.006)
    elif n=='sky_manta':
        torus(col,'V9CoreHalo',(0,-.50,1.10),.22,.012,M['eye'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=5)
        cube(col,'V9WingGlowL',(-.83,-.07,1.10),(.50,.018,.018),M['eye'],rotation=(0,math.radians(-8),math.radians(8)),bevel=.003)
        cube(col,'V9WingGlowR',(.83,-.07,1.10),(.50,.018,.018),M['eye'],rotation=(0,math.radians(8),math.radians(-8)),bevel=.003)
    elif n=='vampire_bat':
        torus(col,'V12BatHalo',(0,-.18,1.02),.20,.012,M['nova'],rotation=(math.radians(90),0,0),major_segments=16,minor_segments=5)
        sphere(col,'V12BatCore',(0,-.24,1.02),(.045,.025,.045),M['eye'],1)
    elif n=='archer_tower':
        torus(col,'V9TowerBowHalo',(.48,-.05,2.52),.26,.014,M['gold'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=5)
        cube(col,'V9TowerFrontRail',(0,-.91,1.25),(.35,.018,.030),M['gold'],bevel=.005)
        sphere(col,'V9TowerEye',(0,-.94,1.13),(.05,.025,.05),M['eye'],1)
    elif n=='tower_guard':
        torus(col,'V9GuardCannonRing',(0,-1.10,2.97),.25,.014,M['gold'],rotation=(math.radians(90),0,0),major_segments=18,minor_segments=5)
        cube(col,'V9GuardCrest',(0,-.95,1.55),(.22,.020,.22),M['gold'],bevel=.025)
    elif n=='tower_core':
        torus(col,'V9CoreDormantRing',(0,0,4.50),.62,.014,M['gold'],rotation=(math.radians(90),0,0),major_segments=22,minor_segments=5)
        sphere(col,'V9CoreGemBright',(0,-.10,4.50),(.10,.06,.10),M['eye'],1)
    parent_unparented(col)

for col in collections:
    enhance_v9(col)

# --------------------------- export ---------------------------
def export_collection(col):
    bpy.ops.object.select_all(action='DESELECT')
    selectable=[]
    for obj in col.objects:
        obj.hide_set(False)
        obj.select_set(True)
        selectable.append(obj)
    if selectable:
        bpy.context.view_layer.objects.active = selectable[0]
    out = OUT / f"{col.name}.glb"
    bpy.ops.export_scene.gltf(
        filepath=str(out),
        export_format='GLB',
        use_selection=True,
        export_apply=True,
        export_yup=True,
        export_materials='EXPORT',
    )
    print(f"Exported {out}")

for col in collections:
    export_collection(col)

blend_path = ROOT / 'blender' / 'rift_crown_models.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))
print(f"Saved editable Blender source: {blend_path}")
print("Rift Crown Arena model forge v12.8 complete.")
