"""Import authored source assets with the installed UE editor Python runtime.

Run with UnrealEditor-Cmd RiftCrownArena.uproject -run=pythonscript
-script=<absolute file> -unattended -NullRHI. RIFT_REPO_ROOT may override root.
"""
import os
import json
import sys
import re
import math
from pathlib import Path
from datetime import datetime, timezone
import unreal

ROOT = Path(os.environ.get("RIFT_REPO_ROOT", Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(ROOT / "Build"))
import asset_import_provenance as source_provenance
MANIFEST = json.loads((ROOT / "Assets/asset_manifest.json").read_text(encoding="utf-8"))
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary
REPORT = {"imported": [], "errors": [], "skeletal": {}, "animations": {}, "statics": {}, "cards": {}}
REPORT.update({"schema": 2, "generatedUTC": datetime.now(timezone.utc).isoformat(),
               "passed": False, "sourceImports": [],
               "mode": "PulverizerOnly" if "-RiftPulverizerOnly" in unreal.SystemLibrary.get_command_line() else "ModelsOnly" if "-RiftModelsOnly" in unreal.SystemLibrary.get_command_line() else "Full"})
REPORT["provenance"] = source_provenance.freeze(ROOT, MANIFEST, __file__)

def save(asset):
    if not LIB.save_loaded_asset(asset, False):
        raise RuntimeError(f"Could not save imported asset {asset.get_path_name()}")
    return asset

def import_file(source, destination, name, options=None):
    source_pin = source_provenance.pin(ROOT, source)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", True)
    if str(source).lower().endswith(".fbx"):
        task.set_editor_property("factory", unreal.FbxFactory())
    if options is not None:
        task.set_editor_property("options", options)
    TOOLS.import_asset_tasks([task])
    paths = task.get_editor_property("imported_object_paths")
    if not paths:
        raise RuntimeError(f"Importer returned no assets for {source}")
    REPORT["sourceImports"].append({"source": source_pin, "assets": list(paths), "method": "AssetImportTask", "succeeded": True})
    REPORT["imported"].extend(paths)
    for path in paths:
        asset = LIB.load_asset(path)
        if asset is not None and asset.get_name() == name:
            return asset
    return LIB.load_asset(paths[0])

def mesh_options(skeletal=False, animation=False, skeleton=None, sample_rate=0):
    options = unreal.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("import_mesh", not animation)
    options.set_editor_property("import_as_skeletal", skeletal or animation)
    options.set_editor_property("import_animations", animation)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION if animation else unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH)
    # Explicitly clear editor-cached references for fresh mesh imports.
    options.set_editor_property("skeleton", skeleton)
    options.set_editor_property("physics_asset", None)
    data = options.get_editor_property("anim_sequence_import_data" if animation else "skeletal_mesh_import_data" if skeletal else "static_mesh_import_data")
    data.set_editor_property("convert_scene", True)
    data.set_editor_property("convert_scene_unit", True)
    data.set_editor_property("force_front_x_axis", True)
    data.set_editor_property("import_uniform_scale", 1.0)
    if not animation:
        data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    if skeletal:
        options.set_editor_property("create_physics_asset", True)
    if animation:
        sample_rate = max(0, int(finite_float(sample_rate)))
        data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property("use_default_sample_rate", not bool(sample_rate))
        if sample_rate:
            data.set_editor_property("custom_sample_rate", int(sample_rate))
    if not skeletal and not animation:
        data.set_editor_property("combine_meshes", True)
        data.set_editor_property("auto_generate_collision", True)
    return options

def finite_float(value, default=0):
    try:
        result = float(value)
    except (TypeError, ValueError):
        return float(default)
    return result if math.isfinite(result) else float(default)

def animation_contract_properties(contract, canonical_scale, authored_flight_bob=None):
    if not contract:
        return {}
    attack = contract.get("attack") or {}
    locomotion = contract.get("locomotion") or {}
    death = contract.get("death") or {}
    visual = contract.get("visual") or {}
    stride = max(0, finite_float(locomotion.get("strideMeters")))
    canonical_scale = finite_float(canonical_scale)
    # Runtime multiplies this source-space stride by the actual model scale.
    # Retain the measured stride when a model gets larger so planted feet do
    # not slide from preserving the old, smaller body's world-space stride.
    stride = stride if canonical_scale > 0 else 0.0
    return {"AttackContactNormalized":finite_float(attack.get("contactNormalized"), .48),
            "AttackAnticipationSeconds":finite_float(attack.get("anticipationSeconds")),
            "AttackRecoverySeconds":finite_float(attack.get("recoverySeconds")),
            "GaitStrideMeters":stride,
            "DeathCleanupSeconds":finite_float(death.get("cleanupSeconds"), 1.05),
            "ReadabilityMultiplier":finite_float(visual.get("readabilityMultiplier"), 1),
            "FlightBobTiles":finite_float(visual.get("flightBobTiles"), finite_float(authored_flight_bob,.09))}

def texture(filename, name, ui=False, linear=False):
    t = import_file(filename, "/Game/Rift/CardArt" if ui else "/Game/Rift/Textures", name)
    t.set_editor_property("srgb", not linear)
    if ui:
        t.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        t.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    if "Normal" in name:
        t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property("flip_green_channel", True)
    return save(t)

def arena_tile_size():
    source = ROOT / "Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation/RiftArenaGeometry.h"
    match = re.search(r"\bUnitsPerTile\s*=\s*([0-9]+(?:\.[0-9]*)?)f\s*;", source.read_text(encoding="utf-8"))
    if not match:
        raise RuntimeError("Arena tile size is missing from the shared native definition")
    value = finite_float(match.group(1))
    if value <= 0:
        raise RuntimeError("Arena tile size must be finite and positive")
    return value

def material(name, base, normal, orm, mask, glow=False, glass=False):
    path = "/Game/Rift/Materials/" + name
    m = LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, "/Game/Rift/Materials", unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    unreal.MaterialEditingLibrary.set_base_material_usage(m, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    unreal.MaterialEditingLibrary.set_base_material_usage(m, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    def sample(tex, x, y):
        n = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionTextureSample, x, y)
        n.set_editor_property("texture", tex)
        if tex == normal:
            n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        elif tex == orm or tex == mask:
            n.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        return n
    b = sample(base, -800, 0)
    n = sample(normal, -800, 250)
    o = sample(orm, -800, 500)
    team = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -800, -400)
    team.set_editor_property("parameter_name", "TeamColor")
    team.set_editor_property("default_value", unreal.LinearColor(0.18, 0.66, 1.0, 1))
    mk = sample(mask, -800, -200)
    lerp = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -350, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(b, "RGB", lerp, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(team, "", lerp, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(mk, "R", lerp, "Alpha")
    slow = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -350, -300)
    slow.set_editor_property("parameter_name", "SlowAmount")
    slow.set_editor_property("default_value", 0.0)
    frost = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -350, -180)
    frost.set_editor_property("constant", unreal.LinearColor(.4,.8,1,1))
    frost_lerp = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -150, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(lerp, "", frost_lerp, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(frost, "", frost_lerp, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(slow, "", frost_lerp, "Alpha")
    flash = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -150, -300)
    flash.set_editor_property("parameter_name", "HitFlash")
    flash.set_editor_property("default_value", 0.0)
    highlight = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -150, -180)
    highlight.set_editor_property("parameter_name", "HitTint")
    highlight.set_editor_property("default_value", unreal.LinearColor(1,.82,.56,1))
    flash_lerp = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, 70, 0)
    unreal.MaterialEditingLibrary.connect_material_expressions(frost_lerp, "", flash_lerp, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(highlight, "", flash_lerp, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(flash, "", flash_lerp, "Alpha")
    grid = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, 70, -500)
    grid.set_editor_property("parameter_name", "RiftGridStrength")
    grid.set_editor_property("default_value", 0.0)
    gain = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, 70, -620)
    gain.set_editor_property("parameter_name", "RiftBaseColorGain")
    gain.set_editor_property("default_value", 1.0)
    world = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionWorldPosition, 70, -750)
    field = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionCustom, 300, 0)
    field.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    field_inputs = []
    for input_name in ("Base", "World", "Grid", "Gain"):
        value = unreal.CustomInput()
        value.set_editor_property("input_name", input_name)
        field_inputs.append(value)
    field.set_editor_property("inputs", field_inputs)
    # One-meter boundaries surround half-integer gameplay tile centers. The
    # derivative softens the narrow seams at distant camera resolutions.
    if name == "M_RiftArena":
        tile = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, 70, -1030)
        tile.set_editor_property("parameter_name", "RiftTileSize")
        tile.set_editor_property("default_value", arena_tile_size())
        tile_input = unreal.CustomInput(); tile_input.set_editor_property("input_name", "TileSize")
        field.set_editor_property("inputs", field_inputs + [tile_input])
        field.set_editor_property("code", "float tile=max(TileSize,1.0); float2 d=abs(frac(World.xy/tile+0.5)-0.5)*tile; float aa=max(length(fwidth(World.xy)),0.25); float seam=1.0-smoothstep(1.0,2.5+aa,min(d.x,d.y)); return Base*Gain*(1.0-0.58*saturate(Grid)*seam);")
        connected = unreal.MaterialEditingLibrary.connect_material_expressions(tile, "", field, "TileSize")
        REPORT.setdefault("arenaGridConnections", []).append({"input": "TileSize", "connected": bool(connected)})
        if not connected:
            raise RuntimeError("Arena tile size shader connection failed")
    else:
        field.set_editor_property("code", "float2 d=abs(frac(World.xy/100.0+0.5)-0.5)*100.0; float aa=max(length(fwidth(World.xy)),0.25); float seam=1.0-smoothstep(1.0,2.5+aa,min(d.x,d.y)); return Base*Gain*(1.0-0.58*saturate(Grid)*seam);")
    for source, output, input_name in ((flash_lerp,"","Base"),(world,"","World"),(grid,"","Grid"),(gain,"","Gain")):
        connected = unreal.MaterialEditingLibrary.connect_material_expressions(source, output, field, input_name)
        if name == "M_RiftArena":
            REPORT.setdefault("arenaGridConnections", []).append({"input": input_name, "connected": bool(connected)})
            if not connected:
                raise RuntimeError("Arena grid shader connection failed: " + input_name)
    connected = unreal.MaterialEditingLibrary.connect_material_property(field, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if name == "M_RiftArena":
        REPORT.setdefault("arenaGridConnections", []).append({"property": "BaseColor", "connected": bool(connected)})
        if not connected:
            raise RuntimeError("Arena grid base color connection failed")
    unreal.MaterialEditingLibrary.connect_material_property(n, "RGB", unreal.MaterialProperty.MP_NORMAL)
    unreal.MaterialEditingLibrary.connect_material_property(o, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    unreal.MaterialEditingLibrary.connect_material_property(o, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.connect_material_property(o, "B", unreal.MaterialProperty.MP_METALLIC)
    if glow:
        mul = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, -300)
        strength = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, -520)
        strength.set_editor_property("parameter_name", "GlowStrength")
        strength.set_editor_property("default_value", 1.4)
        unreal.MaterialEditingLibrary.connect_material_expressions(b, "RGB", mul, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(strength, "", mul, "B")
        unreal.MaterialEditingLibrary.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    elif not glass:
        trim = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionMultiply, 300, -350)
        trim_gain = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, 70, -900)
        trim_gain.set_editor_property("parameter_name", "RiftTeamEmissive")
        trim_gain.set_editor_property("default_value", 0.0)
        trim_color = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionMultiply, 70, -350)
        unreal.MaterialEditingLibrary.connect_material_expressions(team, "", trim_color, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(mk, "R", trim_color, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(trim_color, "", trim, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(trim_gain, "", trim, "B")
        unreal.MaterialEditingLibrary.connect_material_property(trim, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if name.startswith("M_RiftExpansion"):
        dissolve = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 900)
        dissolve.set_editor_property("parameter_name", "DissolveAmount")
        dissolve.set_editor_property("default_value", 0.0)
        cutoff = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionCustom, -100, 900)
        cutoff.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        inputs = []
        for label in ("World", "Amount"):
            item = unreal.CustomInput(); item.set_editor_property("input_name", label); inputs.append(item)
        cutoff.set_editor_property("inputs", inputs)
        cutoff.set_editor_property("code", "float h=frac(sin(dot(World,float3(.17,.13,.19)))*43758.5453); return Amount<=0?1:step(saturate(Amount),h);")
        unreal.MaterialEditingLibrary.connect_material_expressions(world, "", cutoff, "World")
        unreal.MaterialEditingLibrary.connect_material_expressions(dissolve, "", cutoff, "Amount")
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if glass else unreal.BlendMode.BLEND_MASKED)
        if glass and name=='M_RiftExpansionAscensionGlass':
            # Only the Reaper's lower cloth tips use this separate new material.
            # Preserve every accepted legacy/boar glass graph exactly.
            base_alpha=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionScalarParameter,-100,820)
            base_alpha.set_editor_property('parameter_name','RiftBaseOpacity');base_alpha.set_editor_property('default_value',.68)
            fade=unreal.MaterialEditingLibrary.create_material_expression(m,unreal.MaterialExpressionMultiply,100,760)
            for a,inp in ((cutoff,'A'),(base_alpha,'B')):
                if not unreal.MaterialEditingLibrary.connect_material_expressions(a,'',fade,inp):raise RuntimeError('Reaper cloth opacity pin failed: '+inp)
            if not unreal.MaterialEditingLibrary.connect_material_property(fade,'',unreal.MaterialProperty.MP_OPACITY):raise RuntimeError('Reaper cloth opacity output failed')
            REPORT['reaperClothOpacity']={'material':name,'parameter':'RiftBaseOpacity','defaultOpacity':.68,'onlyLowerTips':True,'checkedConnections':3}
        else:
            unreal.MaterialEditingLibrary.connect_material_property(cutoff, "", unreal.MaterialProperty.MP_OPACITY if glass else unreal.MaterialProperty.MP_OPACITY_MASK)
    elif glass:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        alpha = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionConstant, -100, 700)
        alpha.set_editor_property("r", 0.75)
        unreal.MaterialEditingLibrary.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(m)
    return save(m)

def import_expansion_materials(mats, required_names=None):
    imported_textures = {}
    for spec in MANIFEST.get("expansionMaterials") or []:
        if required_names is not None and spec['name'] not in required_names:
            continue
        textures = []
        for key in ("baseColor", "normal", "orm", "teamMask"):
            filename = spec[key]
            if filename not in imported_textures:
                imported_textures[filename] = texture(ROOT / filename, Path(filename).stem, linear=key != "baseColor")
            textures.append(imported_textures[filename])
        mats[spec["name"]] = material(spec["name"], *textures,
                                     glow=spec["name"].endswith("Glow"),
                                     glass=spec["name"].endswith("Glass"))
        REPORT.setdefault("materials", []).append(mats[spec["name"]].get_path_name())

def cloak_material():
    """Sole-purpose owner cloak; never modifies any shared character material."""
    name='M_RiftCloak';path='/Game/Rift/Materials/'+name
    m=LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name,'/Game/Rift/Materials',unreal.Material,unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('two_sided',True)
    unreal.MaterialEditingLibrary.set_base_material_usage(m,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    graph=unreal.MaterialEditingLibrary
    def node(kind,x,y):return graph.create_material_expression(m,kind,x,y)
    color=node(unreal.MaterialExpressionConstant3Vector,-300,0);color.set_editor_property('constant',unreal.LinearColor(.055,.035,.09,1))
    amount=node(unreal.MaterialExpressionScalarParameter,-700,180);amount.set_editor_property('parameter_name','CloakAmount');amount.set_editor_property('default_value',.18)
    world=node(unreal.MaterialExpressionWorldPosition,-700,330)
    pore=node(unreal.MaterialExpressionCustom,-350,240);pore.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs=[]
    for label in ('World','Amount'):
        item=unreal.CustomInput();item.set_editor_property('input_name',label);inputs.append(item)
    pore.set_editor_property('inputs',inputs)
    pore.set_editor_property('code','float3 cell=floor(World/6.0); float h=frac(sin(dot(cell,float3(12.9898,78.233,37.719)))*43758.5453); return step(clamp(Amount,0.0,0.35),h);')
    edge=node(unreal.MaterialExpressionFresnel,-700,500);edge.set_editor_property('exponent',3.);edge.set_editor_property('base_reflect_fraction',0.)
    violet=node(unreal.MaterialExpressionConstant3Vector,-700,650);violet.set_editor_property('constant',unreal.LinearColor(.27,.09,.45,1))
    glow=node(unreal.MaterialExpressionMultiply,-320,560)
    rough=node(unreal.MaterialExpressionConstant,-300,720);rough.set_editor_property('r',.78)
    checks=[]
    def connect(a,out,b,inp):
        succeeded=bool(graph.connect_material_expressions(a,out,b,inp));checks.append({'input':inp,'succeeded':succeeded})
        if not succeeded:raise RuntimeError('Cloak shader expression pin failed: '+inp)
    def output(a,prop):
        succeeded=bool(graph.connect_material_property(a,'',prop));checks.append({'property':str(prop),'succeeded':succeeded})
        if not succeeded:raise RuntimeError('Cloak shader output pin failed: '+str(prop))
    connect(world,'',pore,'World');connect(amount,'',pore,'Amount')
    connect(edge,'',glow,'A');connect(violet,'',glow,'B')
    output(color,unreal.MaterialProperty.MP_BASE_COLOR);output(rough,unreal.MaterialProperty.MP_ROUGHNESS)
    output(pore,unreal.MaterialProperty.MP_OPACITY_MASK);output(glow,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    REPORT['cloakMaterial']={'connections':checks,'baseColor':[.055,.035,.09],
        'defaultCloakAmount':.18,'maximumCloakAmount':.35,'maximumEmissionChannel':.45,
        'clock':'No Time node; deterministic 6cm world cells','sharedMaterialsChanged':False}
    graph.recompile_material(m);return save(m)

def expansion_fx_material():
    """Uniform bounded FX tint; atlas UVs retain authored alpha only.

    This sole-purpose material cannot modify a character's team-mask palette.
    All lifetimes/transforms are native simulation-clock inputs; no Time node.
    """
    name='M_RiftExpansionFX';path='/Game/Rift/Materials/'+name
    m=LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name,'/Game/Rift/Materials',unreal.Material,unreal.MaterialFactoryNew())
    atlas=LIB.load_asset('/Game/Rift/Textures/T_RiftAscensionAtlas_BaseColor')
    if atlas is None:raise RuntimeError('Authored expansion FX opacity atlas is missing')
    graph=unreal.MaterialEditingLibrary;graph.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    m.set_editor_property('two_sided',True)
    graph.set_base_material_usage(m,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    def node(kind,x,y):return graph.create_material_expression(m,kind,x,y)
    color=node(unreal.MaterialExpressionVectorParameter,-750,0)
    color.set_editor_property('parameter_name','EffectColor');color.set_editor_property('default_value',unreal.LinearColor(.37,.08,.66,1))
    uniform=node(unreal.MaterialExpressionCustom,-350,0);uniform.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inp=unreal.CustomInput();inp.set_editor_property('input_name','Color');uniform.set_editor_property('inputs',[inp])
    uniform.set_editor_property('code','return saturate(Color.rgb);')
    texture_sample=node(unreal.MaterialExpressionTextureSample,-750,250)
    texture_sample.set_editor_property('texture',atlas)
    texture_sample.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    checks=[]
    def connect(a,out,b,inp):
        connected=bool(graph.connect_material_expressions(a,out,b,inp));checks.append({'kind':'expression','output':out,'input':inp,'connected':connected})
        if not connected:raise RuntimeError('Expansion FX expression connection failed: '+inp)
    def output(a,prop):
        connected=bool(graph.connect_material_property(a,'',prop));checks.append({'kind':'property','property':str(prop),'connected':connected})
        if not connected:raise RuntimeError('Expansion FX property connection failed: '+str(prop))
    def bounded(name,value,maximum,y):
        parameter=node(unreal.MaterialExpressionScalarParameter,-750,y)
        parameter.set_editor_property('parameter_name',name);parameter.set_editor_property('default_value',value)
        clamp=node(unreal.MaterialExpressionCustom,-350,y);clamp.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        inp=unreal.CustomInput();inp.set_editor_property('input_name','Amount');clamp.set_editor_property('inputs',[inp])
        clamp.set_editor_property('code',f'return clamp(Amount,0.0,{maximum});')
        connect(parameter,'',clamp,'Amount');return clamp
    opacity=bounded('EffectOpacity',.55,.65,430)
    emission=bounded('EffectEmission',.25,.45,660)
    alpha=node(unreal.MaterialExpressionMultiply,0,300);glow=node(unreal.MaterialExpressionMultiply,0,600)
    rough=node(unreal.MaterialExpressionConstant,0,820);rough.set_editor_property('r',.8)
    connect(color,'',uniform,'Color')
    connect(texture_sample,'A',alpha,'A');connect(opacity,'',alpha,'B')
    connect(uniform,'',glow,'A');connect(emission,'',glow,'B')
    output(uniform,unreal.MaterialProperty.MP_BASE_COLOR)
    output(alpha,unreal.MaterialProperty.MP_OPACITY)
    output(glow,unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    output(rough,unreal.MaterialProperty.MP_ROUGHNESS)
    graph.recompile_material(m)
    REPORT['expansionFXMaterial']={'material':name,'uniformColorParameter':'EffectColor',
        'defaultColor':[.37,.08,.66],'opacityParameter':'EffectOpacity','defaultOpacity':.55,'maximumOpacity':.65,
        'emissionParameter':'EffectEmission','defaultEmission':.25,'maximumEmissionPerChannel':.45,
        'opacitySource':source_provenance.pin(ROOT,'Assets/Source/Textures/T_RiftAscensionAtlas_BaseColor.png'),
        'opacitySourceChannel':'A','colorUsesAtlasRGB':False,'timeSource':'Native simulation-clock geometry only; no material Time',
        'connections':checks}
    return save(m)

def assign_materials(mesh, names, mats):
    expansion = bool(names) and all(name.startswith("M_RiftExpansion") for name in names)
    expected_by_index = []
    if isinstance(mesh, unreal.SkeletalMesh):
        slots = mesh.get_editor_property("materials")
        for index, slot in enumerate(slots):
            if expansion:
                # Reimports retain old display-slot names/interfaces. The FBX
                # imported slot is authoritative; only manifest-owned materials
                # may bind an expansion mesh, including its imported LODs.
                imported = str(slot.get_editor_property("imported_material_slot_name"))
                displayed = str(slot.get_editor_property("material_slot_name"))
                name = next((candidate for candidate in (imported, displayed)
                             if candidate in names), None)
                if name is None:
                    name = next(("M_RiftExpansion" + candidate[6:]
                                 for candidate in (imported, displayed)
                                 if candidate.startswith("M_Rift")
                                 and "M_RiftExpansion" + candidate[6:] in names),
                                names[min(index, len(names)-1)])
                if mats.get(name) is None:
                    raise RuntimeError("Missing manifest-owned expansion material: " + name)
                slot.set_editor_property("material_interface", mats[name])
                # Reflected struct-array iteration produces a struct copy.
                # Writing the struct back is required to persist its interface.
                slots[index] = slot
                expected_by_index.append(mats[name].get_path_name())
            else:
                slot.set_editor_property("material_interface", mats.get(str(slot.get_editor_property("material_slot_name")), mats[names[min(index, len(names)-1)]]))
        mesh.set_editor_property("materials", slots)
    else:
        for index, name in enumerate(names):
            mesh.set_material(index, mats[name])
            if expansion:
                expected_by_index.append(mats[name].get_path_name())
    save(mesh)
    if expansion:
        observed = []
        expected = {mats[name].get_path_name() for name in names}
        # UE5.8 Python exposes the BlueprintGetter-backed static_materials
        # property; get_static_materials() is not exported as a Python method.
        # Verified on the actual imported shield fragment by a read-only probe.
        observed_slots = (mesh.get_editor_property("materials") if isinstance(mesh, unreal.SkeletalMesh)
                          else mesh.get_editor_property("static_materials"))
        for index, slot in enumerate(observed_slots):
            interface = slot.get_editor_property("material_interface")
            path = interface.get_path_name() if interface is not None else ""
            observed.append({"index": index,
                             "slot": str(slot.get_editor_property("material_slot_name")),
                             "importedSlot": str(slot.get_editor_property("imported_material_slot_name")),
                             "materialInterface": path,
                             "expectedInterface": expected_by_index[index] if index < len(expected_by_index) else "",
                             "manifestOwned": path in expected,
                             "exactInterface": index < len(expected_by_index) and path == expected_by_index[index]})
        REPORT.setdefault("expansionMaterialBindings", {})[mesh.get_path_name()] = {
            "expectedInterfaces": sorted(expected), "slots": observed,
            "passed": bool(observed) and all(slot["manifestOwned"] and slot["exactInterface"] for slot in observed)
                      and expected <= {slot["materialInterface"] for slot in observed}}
        if not REPORT["expansionMaterialBindings"][mesh.get_path_name()]["passed"]:
            raise RuntimeError("Expansion mesh has an invalid material interface: " + mesh.get_path_name())

def presentation_material(name, domain=None, shader="particle"):
    path = "/Game/Rift/Materials/" + name
    m = LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, "/Game/Rift/Materials", unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    if domain is not None:
        m.set_editor_property("material_domain", domain)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if shader != "water" else unreal.BlendMode.BLEND_OPAQUE)
    if shader != "water":
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT if shader == "shockwave" else unreal.MaterialShadingModel.MSM_UNLIT)
        m.set_editor_property("two_sided", True)
    if shader == "particle":
        unreal.MaterialEditingLibrary.set_base_material_usage(m, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    elif shader == "water":
        unreal.MaterialEditingLibrary.set_base_material_usage(m, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    def node(cls, **props):
        n = unreal.MaterialEditingLibrary.create_material_expression(m, cls)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n
    def custom_input(name):
        value = unreal.CustomInput()
        value.set_editor_property("input_name", name)
        return value
    def wave_connect(source, output, target, input_name):
        connected = unreal.MaterialEditingLibrary.connect_material_expressions(source, output, target, input_name)
        REPORT.setdefault("shaderConnections", {}).setdefault(name, []).append(
            {"kind": "expression", "output": output, "input": input_name, "connected": bool(connected)})
        if not connected:
            raise RuntimeError("Wave material expression connection failed: " + input_name)
    def wave_output(source, output, property_name):
        connected = unreal.MaterialEditingLibrary.connect_material_property(source, output, property_name)
        REPORT.setdefault("shaderConnections", {}).setdefault(name, []).append(
            {"kind": "property", "output": output, "property": str(property_name), "connected": bool(connected)})
        if not connected:
            raise RuntimeError("Wave material property connection failed: " + str(property_name))
    color = node(unreal.MaterialExpressionVectorParameter, parameter_name="Color" if shader == "particle" else "RingColor", default_value=unreal.LinearColor(.12,.65,.9,1))
    uv = node(unreal.MaterialExpressionTextureCoordinate)
    custom = node(unreal.MaterialExpressionCustom, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = [custom_input("UV")]
    if shader == "water":
        color.set_editor_property("default_value", unreal.LinearColor(.025,.19,.23,1))
        custom.set_editor_property("code", "return 0.12 + 0.06 * sin(UV.x * 80 + Time * 1.1) * cos(UV.y * 100 - Time * 0.8);")
        inputs.append(custom_input("Time"))
        custom.set_editor_property("inputs", inputs)
        time = node(unreal.MaterialExpressionTime)
        unreal.MaterialEditingLibrary.connect_material_expressions(time, "", custom, "Time")
        unreal.MaterialEditingLibrary.connect_material_property(custom, "", unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    elif shader == "shockwave":
        # Deferred decals support floor-normal/roughness perturbation, not
        # refractive geometry. UV radius is driven solely by the simulation's
        # decal bounds; no material wall-clock animates pause/replay differently.
        custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
        parameters = []
        for parameter, default in (("FrontWidth", .016), ("TrailWidth", .11),
                                   ("WarpStrength", .035), ("RoughnessDip", .08)):
            value = node(unreal.MaterialExpressionScalarParameter,
                         parameter_name=parameter, default_value=default)
            inputs.append(custom_input(parameter))
            parameters.append((value, parameter))
        custom.set_editor_property("inputs", inputs)
        for value, parameter in parameters:
            wave_connect(value, "", custom, parameter)
        custom.set_editor_property("code", """
float r=length(UV*2.0-1.0);
float fw=clamp(FrontWidth,0.006,0.035);
float tw=clamp(TrailWidth,0.04,0.18);
float inside=1.0-smoothstep(0.998,1.0,r);
float lead=(1.0-smoothstep(0.0,fw,abs(r-(1.0-fw))))*inside;
float trailStart=1.0-2.0*fw;
float u=saturate((trailStart-r)/tw);
float trail=16.0*u*u*(1.0-u)*(1.0-u)*step(r,trailStart);
float alpha=saturate(lead+0.22*trail);
float brightness=1.75*lead+0.32*trail;
float slope=clamp(WarpStrength,0.0,0.08)*trail*sin(u*6.28318530718);
float roughness=0.90-clamp(RoughnessDip,0.0,0.15)*trail;
return float4(alpha,brightness,slope,roughness);
""".strip())
        def channel(component):
            result = node(unreal.MaterialExpressionComponentMask,
                          r=component=="r", g=component=="g",
                          b=component=="b", a=component=="a")
            # UE shortens ComponentMask's displayed Input pin to NAME_None.
            wave_connect(custom, "", result, "")
            return result
        opacity, brightness, slope, roughness = (channel(c) for c in ("r", "g", "b", "a"))
        emissive = node(unreal.MaterialExpressionMultiply)
        wave_connect(color, "", emissive, "A")
        wave_connect(brightness, "", emissive, "B")
        normal = node(unreal.MaterialExpressionCustom,
                      output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                      code="float2 p=UV*2.0-1.0; float r=max(length(p),0.0001); return normalize(float3(p/r*Slope,1.0));")
        normal.set_editor_property("inputs", [custom_input("UV"), custom_input("Slope")])
        wave_connect(uv, "", normal, "UV")
        wave_connect(slope, "", normal, "Slope")
        wave_output(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        wave_output(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        wave_output(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        wave_output(normal, "", unreal.MaterialProperty.MP_NORMAL)
        wave_output(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        REPORT.setdefault("shaderContracts", {})[name] = {
            "revision": "bounded-violet-front-2-checked-pins",
            "timeSource": "simulation-driven decal UV/bounds; no Material Time",
            "surfaceDistortion": "radial deferred-decal normal and roughness modulation; no refraction",
            "leadingWidthNormalized": .032,
            "trailingWidthNormalized": .11,
            "trailingOpacityMaximum": .22,
            "normalSlopeMaximum": .08,
        }
    else:
        shape = node(unreal.MaterialExpressionScalarParameter, parameter_name="Footprint", default_value=0.0 if shader == "ring" else 1.0)
        inputs.append(custom_input("Shape"))
        custom.set_editor_property("inputs", inputs)
        unreal.MaterialEditingLibrary.connect_material_expressions(shape, "", custom, "Shape")
        if shader == "particle":
            code="float2 p=UV*2-1; float r=length(p); return pow(saturate(1-r),2);"
        else:
            code="float2 p=abs(UV*2-1); float r=Shape>.5?max(p.x,p.y):length(p); float edge=saturate((1-r)*80)*saturate((r-.87)*45); return saturate(edge*.75+(r<.95?.12:0));"
        custom.set_editor_property("code", code)
        opacity = custom
        if shader == "particle":
            particle = node(unreal.MaterialExpressionParticleColor)
            opacity = node(unreal.MaterialExpressionMultiply)
            unreal.MaterialEditingLibrary.connect_material_expressions(custom, "", opacity, "A")
            unreal.MaterialEditingLibrary.connect_material_expressions(particle, "A", opacity, "B")
            # Niagara transforms particle centers by the component, but its
            # billboard dimensions stay in world units. Expand actual vertices
            # around each center without editing spawn/update graphs or clocks.
            sprite_scale = node(unreal.MaterialExpressionScalarParameter, parameter_name="RiftSpriteScale", default_value=1.0)
            vertex_world = node(unreal.MaterialExpressionWorldPosition,
                                world_position_shader_offset=unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
            particle_center = node(unreal.MaterialExpressionParticlePositionWS)
            vertex_offset = node(unreal.MaterialExpressionCustom,
                                 output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                                 code="return (World-Center)*(Scale-1.0);")
            vertex_offset.set_editor_property("inputs", [custom_input("World"), custom_input("Center"), custom_input("Scale")])
            unreal.MaterialEditingLibrary.connect_material_expressions(vertex_world, "", vertex_offset, "World")
            unreal.MaterialEditingLibrary.connect_material_expressions(particle_center, "", vertex_offset, "Center")
            unreal.MaterialEditingLibrary.connect_material_expressions(sprite_scale, "", vertex_offset, "Scale")
            unreal.MaterialEditingLibrary.connect_material_property(vertex_offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        unreal.MaterialEditingLibrary.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
        unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if shader == "shockwave":
        wave_connect(uv, "", custom, "UV")
    else:
        unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", custom, "UV")
    unreal.MaterialEditingLibrary.recompile_material(m)
    return save(m)

def card_data(only=None, stats_only=False):
    definitions = json.loads(unreal.RiftAssetLibrary.card_definitions_json())
    REPORT["cardDefinitions"] = definitions
    children=json.loads(unreal.RiftAssetLibrary.entity_definitions_json())
    REPORT['entityDefinitions']=children
    child_ids={definition['id'] for definition in children}
    definitions=definitions+children
    properties = {"cost":"Cost", "count":"Count", "hp":"HP", "damage":"Damage", "attackInterval":"AttackSpeed", "moveSpeed":"MoveSpeed", "range":"AttackRange", "scale":"BaseModelScale", "collisionRadius":"CollisionRadius", "targetMask":"TargetMask", "projectileSpeed":"ProjectileSpeed", "shockwaveSpeed":"ShockwaveSpeed", "splash":"SplashRadius", "lifetime":"Lifetime", "footprint":"Footprint", "towerDamage":"TowerDamage", "spellRadius":"SpellRadius", "castDelay":"CastDelay", "chargeDamage":"ChargeDamage", "slowPct":"SlowPct", "slowDuration":"SlowDuration", "auraDamage":"AuraDamage", "auraRadius":"AuraRadius", "auraInterval":"AuraInterval", "stunDuration":"StunDuration", "dotDamage":"DotDamage", "dotDuration":"DotDuration", "dotInterval":"DotInterval", "rounds":"Rounds", "flying":"Flying", "canHitAir":"GroundAndAir", "structuresOnly":"StructuresOnly", "spell":"Spell", "building":"Building"}
    attacks = {"ironclad":"sword_attack", "ember_archer":"bow_release", "twin_blades":"dual_attack", "boulderback":"heavy_slam", "arc_mage":"arc_cast", "rambeast":"charge", "sky_manta":"manta_cast", "archer_tower":"arrow_flight", "bullet_burst":"bullet_burst", "nova_flask":"nova_impact", "vampire_bats":"bat_bite", "frost_fang":"frost_attack", "storm_raven":"storm_bolt", "meteor_shards":"meteor_impact", "mini_stampede":"heavy_slam", "stampede":"heavy_slam", "pulverizer":"heavy_slam"}
    properties.update({"chargeMoveSeconds":"ChargeMoveSeconds", "chargedMoveSpeed":"ChargedMoveSpeed",
                       "hpDecayPerSecond":"HPDecayPerSecond", "initialAttackCooldown":"InitialAttackCooldown",
                       "shieldHP":"ShieldHP", "deathSpawnCount":"DeathSpawnCount", "deathSpawnId":"DeathSpawnId",
                       "invisibilityDuration":"InvisibilityDuration", "dotStructureDamage":"DotStructureDamage", "healthCloakThreshold":"HealthCloakThreshold",
                       "hardExpiry":"HardExpiry", "shieldOverflowProtected":"ShieldOverflowProtected",
                       "globalFriendlyInvisibility":"GlobalFriendlyInvisibility", "breakExistingLocks":"BreakExistingLocks"})
    attacks.update({"bone_warden":"heavy_slam", "bone_warden_skeleton":"sword_attack",
                    "aether_reaper":"dual_attack", "iron_giant":"heavy_slam", "shadow_surge":"arc_cast", "dart_gun":"dart_fire"})
    effects = ["Deploy","Impact","ArrowFlight","ArcFlight","MantaFlight","StormFlight","TowerFlight","BulletBurst","Nova","Meteor","MeteorTick","Frost","Slow","Stun","Aura","TowerDestroy","CoreAwaken"]
    for c in definitions:
        if only is not None and c["id"] not in only:
            continue
        unreal.log("Building Rift CardData: " + c["id"])
        name = "DA_" + c["id"]
        destination='/Game/Rift/Entities' if c['id'] in child_ids else '/Game/Rift/Cards'
        path = destination + '/' + name
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.RiftCardData)
        data = LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, destination, unreal.RiftCardData, factory)
        data.set_editor_property("CardId", c["id"])
        data.set_editor_property("DisplayName", unreal.Text(c["name"]))
        role = "Spell" if c["spell"] else "Defensive Building" if c["building"] else "Flying" if c["flying"] else "Ground"
        data.set_editor_property("CombatClass", unreal.Text(role))
        data.set_editor_property("Description", unreal.Text(c["name"] + " — " + role))
        for key, prop in properties.items():
            data.set_editor_property(prop, c[key])
        data.set_editor_property("FrontSight", max(8, c["range"]) if c["projectileSpeed"] > 0 else 8)
        data.set_editor_property("RearSight", 5)
        if c["id"] == "dart_gun":
            data.set_editor_property("Description", unreal.Text("An 81-year-old African American WWII veteran. Slow, steady steps; rapid, precise darts."))
        source_card = (MANIFEST.get("characters") or {}).get(c["id"]) or {}
        contract = (source_card.get("modelDesign") or {}).get("animationContract") or {}
        if not contract and source_card.get("animationTiming"):
            contract = {"attack": source_card["animationTiming"],
                        "locomotion": next((clip for clip in source_card.get("animations", [])
                                            if clip["name"] == "Locomotion"), {}),
                        "visual": {"readabilityMultiplier": 0}}  # Retain native model-size defaults.
        for prop, value in animation_contract_properties(contract, c["scale"], (source_card.get("modelDesign") or {}).get("flightBobTiles")).items():
            data.set_editor_property(prop, value)
        if stats_only:
            save(data)
            continue
        if c['id'] not in child_ids:
            data.set_editor_property("Illustration", LIB.load_asset(REPORT["cards"][c["id"]]))
        if c["id"] in REPORT["skeletal"]:
            data.set_editor_property("CharacterMesh", LIB.load_asset(REPORT["skeletal"][c["id"]]))
            data.set_editor_property("Animations", {key:LIB.load_asset(path) for key,path in REPORT["animations"][c["id"]].items()})
        if c["building"] and c["id"] in REPORT["statics"]:
            data.set_editor_property("StructureMesh", LIB.load_asset(REPORT["statics"][c["id"]]))
        data.set_editor_property("Effects", {key:LIB.load_asset("/Game/Rift/VFX/NS_Rift"+key) for key in effects})
        deploy_path = "/Game/Rift/Audio/SFX_spawn_" + c["id"]
        deploy = LIB.load_asset(deploy_path if LIB.does_asset_exist(deploy_path) else "/Game/Rift/Audio/SFX_deploy")
        hit_sound = "dart_hit" if c["id"] == "dart_gun" else "building_hit" if c["id"] == "pulverizer" else "sword_hit"
        sounds = {"Attack": LIB.load_asset("/Game/Rift/Audio/SFX_" + attacks[c["id"]]), "Deploy":deploy, "Hit":LIB.load_asset("/Game/Rift/Audio/SFX_" + hit_sound)}
        if any(v is None for v in sounds.values()):
            raise RuntimeError("Missing sound binding for " + c["id"])
        data.set_editor_property("Sounds", sounds)
        LIB.set_metadata_tag(data, "SourceManifest", MANIFEST.get("illustrations", {}).get(c["id"], {}).get("sourceSceneSHA256", MANIFEST["source"]["sha256"]))
        if c["id"] in MANIFEST.get("illustrations", {}):
            LIB.set_metadata_tag(data,"PortraitSHA256",MANIFEST["illustrations"][c["id"]]["sha256"])
            LIB.set_metadata_tag(data,"PortraitSource", "Production model render" if (ROOT/"Assets/Source/CardArt/model_portraits.json").exists() else "Original illustration")
        save(data)

def main():
    selected_arg = re.search(r"(?:^|\s)-RiftSelectedModels=([a-z0-9_,]+)", unreal.SystemLibrary.get_command_line())
    if selected_arg:
        selected = selected_arg.group(1).split(",")
        if len(set(selected)) != len(selected) or any(card not in MANIFEST["characters"] and card not in MANIFEST["cards"] for card in selected):
            raise RuntimeError("Selected model list contains unknown or duplicate IDs")
        REPORT["mode"] = "SelectedModels"
        REPORT["selectedModels"] = selected
        mats = {name: LIB.load_asset("/Game/Rift/Materials/" + name)
                for name in ("M_RiftSurface", "M_RiftGlow", "M_RiftGlass")}
        model_ids = set(selected).intersection(MANIFEST['characters'])
        effect_ids=set()
        for identity in selected:
            binding = MANIFEST['cards'].get(identity, {})
            model_ids.update(binding[key] for key in ('skeletal', 'skeletalDecoration') if binding.get(key))
            if binding.get('static'): effect_ids.add(binding['static'])
            effect_ids.update(binding.get('effectStatics', []))
        if 'dart_gun' in selected:effect_ids.add('dart_projectile')
        if 'iron_giant' in selected:effect_ids.add('iron_shield_fragment')
        if 'shadow_surge' in selected:effect_ids.update(('shadow_surge_wisp','shadow_surge_ribbon'))
        required={name for card in model_ids for name in MANIFEST['characters'][card]['materials']}
        required.update(name for effect in effect_ids for name in MANIFEST['statics'][effect]['materials'])
        import_expansion_materials(mats,required)
        if set(selected).intersection({'bone_warden','aether_reaper','shadow_surge'}):
            REPORT.setdefault('materials',[]).append(expansion_fx_material().get_path_name())
        if 'shadow_surge' in selected:
            REPORT.setdefault('materials',[]).append(cloak_material().get_path_name())
        for card_id in sorted(model_ids):
            card = MANIFEST["characters"][card_id]
            dest = "/Game/Rift/Characters/" + card_id
            mesh = import_file(ROOT / card["mesh"]["file"], dest, "SK_" + card_id, mesh_options(skeletal=True))
            assign_materials(mesh, card["materials"], mats)
            skeleton = mesh.get_editor_property("skeleton")
            physics = mesh.get_editor_property("physics_asset")
            if skeleton is None or physics is None:
                raise RuntimeError("Selected model has no skeleton/physics: " + card_id)
            save(skeleton); save(physics)
            REPORT["skeletal"][card_id] = mesh.get_path_name()
            skeletal_editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
            for lod in card["lods"]:
                if skeletal_editor.import_lod(mesh, lod["level"], str(ROOT / lod["file"])) < 0:
                    raise RuntimeError("Failed skeletal LOD: " + card_id)
                REPORT["sourceImports"].append({"source":source_provenance.pin(ROOT, lod["file"]),
                    "assets":[mesh.get_path_name()], "method":"SkeletalMeshEditorSubsystem.import_lod",
                    "level":lod["level"], "succeeded":True})
            # Imported LODs can restore cached legacy material interfaces.
            if card["materials"] and all(name.startswith("M_RiftExpansion") for name in card["materials"]):
                assign_materials(mesh, card["materials"], mats)
            clips = {}
            for clip in card["animations"]:
                options = mesh_options(animation=True, skeleton=skeleton, sample_rate=clip.get("fps", 0))
                anim = import_file(ROOT / clip["file"], dest + "/Animations", clip["action"], options)
                clips[clip["name"]] = anim.get_path_name()
            REPORT["animations"][card_id] = clips
            save(mesh)
        for card_id in selected:
            if card_id in MANIFEST['cards']:
                REPORT["cards"][card_id] = texture(ROOT / ("Assets/Source/CardArt/" + card_id + ".png"), "T_Card_" + card_id, ui=True).get_path_name()
        static_editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.get_default_object(unreal.StaticMeshEditorSubsystem)
        for name in sorted(effect_ids):
            prop=MANIFEST['statics'][name]
            mesh=import_file(ROOT/prop['file'],'/Game/Rift/Environment','SM_'+name,mesh_options())
            assign_materials(mesh,prop['materials'],mats)
            for lod in prop['lods']:
                if static_editor.import_lod(mesh,lod['level'],str(ROOT/lod['file']))<0:raise RuntimeError('Failed selected effect LOD: '+name)
                REPORT['sourceImports'].append({'source':source_provenance.pin(ROOT,lod['file']),
                    'assets':[mesh.get_path_name()],'method':'StaticMeshEditorSubsystem.import_lod','level':lod['level'],'succeeded':True})
            assign_materials(mesh,prop['materials'],mats)
            REPORT['statics'][name]=mesh.get_path_name();save(mesh)
        if "pulverizer" in selected:
            REPORT.setdefault("materials", []).append(presentation_material("M_RiftShockwave", unreal.MaterialDomain.MD_DEFERRED_DECAL, "shockwave").get_path_name())
        REPORT["physicsFinalization"] = json.loads(unreal.RiftEditorAssetLibrary.finalize_imported_physics_assets_json())
        if REPORT["physicsFinalization"]["errors"]:
            raise RuntimeError(REPORT["physicsFinalization"]["errors"])
        unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([
            str(ROOT / f"Unreal/RiftCrownArena/Content/Rift/Characters/{card}/SK_{card}_PhysicsAsset.uasset")
            for card in sorted(model_ids)], True)
        # All numerical bindings follow the same compiled canonical definitions;
        # meshes, sound bindings and illustrations of other cards stay untouched.
        card_data(stats_only=True)
        card_data(set(selected))
        REPORT["validation"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
        if REPORT["validation"]["errors"]:
            raise RuntimeError(REPORT["validation"]["errors"])
        return
    if "-RiftArenaOnly" in unreal.SystemLibrary.get_command_line():
        REPORT["mode"] = "ArenaOnly"
        textures = [LIB.load_asset("/Game/Rift/Textures/T_RiftAtlas_" + suffix)
                    for suffix in ("BaseColor", "Normal", "ORM", "TeamMask")]
        if any(value is None for value in textures):
            raise RuntimeError("Existing arena atlas textures are missing")
        REPORT["materials"] = [material("M_RiftArena", *textures).get_path_name()]
        REPORT["arenaGrid"] = {"worldUnitsPerTile": arena_tile_size(),
                              "nativeDefinition": source_provenance.pin(ROOT, "Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation/RiftArenaGeometry.h"),
                              "textures": [value.get_path_name() for value in textures],
                              "parameter": "RiftTileSize", "legacyMaterialsChanged": False}
        return
    if "-RiftPulverizerOnly" in unreal.SystemLibrary.get_command_line():
        # Add one card without importing or resaving any accepted roster asset.
        card_id = "pulverizer"
        card = MANIFEST["characters"][card_id]
        mats = {name: LIB.load_asset("/Game/Rift/Materials/" + name)
                for name in ("M_RiftSurface", "M_RiftGlow", "M_RiftGlass")}
        if any(value is None for value in mats.values()):
            raise RuntimeError("Existing production materials are missing")
        import_expansion_materials(mats)
        dest = "/Game/Rift/Characters/" + card_id
        mesh = import_file(ROOT / card["mesh"]["file"], dest, "SK_" + card_id, mesh_options(skeletal=True))
        assign_materials(mesh, card["materials"], mats)
        skeleton = mesh.get_editor_property("skeleton")
        if skeleton is None or mesh.get_editor_property("physics_asset") is None:
            raise RuntimeError("Pulverizer lacks its skeleton or generated physics")
        save(skeleton)
        save(mesh.get_editor_property("physics_asset"))
        REPORT["skeletal"][card_id] = mesh.get_path_name()
        skeletal_editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
        for lod in card["lods"]:
            if skeletal_editor.import_lod(mesh, lod["level"], str(ROOT / lod["file"])) < 0:
                raise RuntimeError("Failed Pulverizer skeletal LOD")
            REPORT["sourceImports"].append({"source": source_provenance.pin(ROOT, lod["file"]),
                "assets": [mesh.get_path_name()], "method": "SkeletalMeshEditorSubsystem.import_lod",
                "level": lod["level"], "succeeded": True})
        if card["materials"] and all(name.startswith("M_RiftExpansion") for name in card["materials"]):
            assign_materials(mesh, card["materials"], mats)
        clips = {}
        for clip in card["animations"]:
            anim = import_file(ROOT / clip["file"], dest + "/Animations", clip["action"], mesh_options(animation=True, skeleton=skeleton, sample_rate=clip.get("fps", 0)))
            clips[clip["name"]] = anim.get_path_name()
        REPORT["animations"][card_id] = clips
        save(mesh)
        REPORT["cards"][card_id] = texture(ROOT / "Assets/Source/CardArt/pulverizer.png", "T_Card_pulverizer", ui=True).get_path_name()
        REPORT.setdefault("materials", []).append(presentation_material("M_RiftShockwave", unreal.MaterialDomain.MD_DEFERRED_DECAL, "shockwave").get_path_name())
        REPORT["physicsFinalization"] = json.loads(unreal.RiftEditorAssetLibrary.finalize_imported_physics_assets_json())
        if REPORT["physicsFinalization"]["errors"]:
            raise RuntimeError(REPORT["physicsFinalization"]["errors"])
        unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([
            str(ROOT / "Unreal/RiftCrownArena/Content/Rift/Characters/pulverizer/SK_pulverizer_PhysicsAsset.uasset")], True)
        card_data({card_id})
        REPORT["validation"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
        if REPORT["validation"]["errors"]:
            raise RuntimeError(REPORT["validation"]["errors"])
        return
    if "-RiftParticleOnly" in unreal.SystemLibrary.get_command_line():
        REPORT["materials"] = [presentation_material("M_RiftParticle").get_path_name()]
        REPORT["particleSpriteScaling"] = {"parameter": "RiftSpriteScale", "default": 1.0,
                                            "vertexEquation": "(World-Center)*(Scale-1.0)", "simulationGraphsChanged": False}
        return
    if "-RiftGuardOnly" in unreal.SystemLibrary.get_command_line():
        # Split the existing cannon from its fixed foundation without rebuilding
        # unrelated meshes, animation clips, portraits, audio or particle graphs.
        mats = {name: LIB.load_asset("/Game/Rift/Materials/" + name)
                for name in ("M_RiftSurface", "M_RiftGlow", "M_RiftGlass")}
        if any(value is None for value in mats.values()):
            raise RuntimeError("Existing guard materials are missing")
        static_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.get_default_object(unreal.StaticMeshEditorSubsystem)
        for name in ("tower_guard", "guard_cannon"):
            prop = MANIFEST["statics"][name]
            mesh = import_file(ROOT / prop["file"], "/Game/Rift/Environment", "SM_" + name, mesh_options())
            assign_materials(mesh, prop["materials"], mats)
            for lod in prop["lods"]:
                if static_editor.import_lod(mesh, lod["level"], str(ROOT / lod["file"])) < 0:
                    raise RuntimeError(f"Failed guard LOD {name} {lod['level']}")
            REPORT["statics"][name] = mesh.get_path_name()
            save(mesh)
        return
    portraits_only = "-RiftPortraitsOnly" in unreal.SystemLibrary.get_command_line()
    if portraits_only:
        for file in sorted((ROOT / "Assets/Source/CardArt").glob("*.png")):
            REPORT["cards"][file.stem] = texture(file,"T_Card_"+file.stem,ui=True).get_path_name()
    if "-RiftMaterialsOnly" in unreal.SystemLibrary.get_command_line():
        textures = [LIB.load_asset("/Game/Rift/Textures/T_RiftAtlas_" + suffix) for suffix in ("BaseColor","Normal","ORM","TeamMask")]
        if any(t is None for t in textures):
            raise RuntimeError("Authored material atlas textures are missing")
        REPORT["materials"] = [material(name, *textures, name == "M_RiftGlow", name == "M_RiftGlass").get_path_name() for name in ("M_RiftSurface","M_RiftGlow","M_RiftGlass")]
        REPORT["materials"].append(presentation_material("M_RiftParticle").get_path_name())
        REPORT["materials"].append(presentation_material("M_RiftWater", shader="water").get_path_name())
        return
    if "-RiftPhysicsOnly" in unreal.SystemLibrary.get_command_line():
        REPORT["physicsFinalization"] = json.loads(unreal.RiftEditorAssetLibrary.finalize_imported_physics_assets_json())
        if REPORT["physicsFinalization"]["errors"]:
            raise RuntimeError(REPORT["physicsFinalization"]["errors"])
        unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([
            str(ROOT / f"Unreal/RiftCrownArena/Content/Rift/Characters/{card}/SK_{card}_PhysicsAsset.uasset")
            for card in MANIFEST["characters"]], True)
        REPORT["validation"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
        if any(not c.get("physicsRegisteredOnDisk", False) for c in REPORT["validation"]["cards"] if c.get("physics")):
            raise RuntimeError("Generated physics assets did not enter the on-disk registry")
        return
    if "-RiftMaterialUsageOnly" in unreal.SystemLibrary.get_command_line():
        for name in ("M_RiftSurface", "M_RiftGlow", "M_RiftGlass", "M_RiftWater"):
            asset = LIB.load_asset("/Game/Rift/Materials/" + name)
            if asset is None:
                raise RuntimeError("Missing authored material " + name)
            unreal.MaterialEditingLibrary.set_base_material_usage(asset, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
            if name != "M_RiftWater":
                unreal.MaterialEditingLibrary.set_base_material_usage(asset, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
            unreal.MaterialEditingLibrary.recompile_material(asset)
            save(asset)
        return
    if "-RiftImportProbe" in unreal.SystemLibrary.get_command_line():
        mesh = import_file(ROOT/MANIFEST["characters"]["ironclad"]["mesh"]["file"], "/Game/Rift/ImportDiagnostics", "SK_ironclad_clean", mesh_options(skeletal=True))
        asset = LIB.load_asset("/Game/Rift/Cards/DA_ironclad")
        asset.set_editor_property("CharacterMesh", mesh)
        inspected = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
        REPORT["probe"] = {"path":mesh.get_path_name(), "skeleton":mesh.get_editor_property("skeleton").get_path_name(), "physics":mesh.get_editor_property("physics_asset") is not None, "forward_axis":str(mesh.get_forward_axis()), "bounds":next(c for c in inspected["cards"] if c["id"]=="ironclad")}
        return
    if "-RiftPresentationOnly" in unreal.SystemLibrary.get_command_line() or "-RiftCardsOnly" in unreal.SystemLibrary.get_command_line() or portraits_only:
        for card_id, card in MANIFEST["characters"].items():
            dest = "/Game/Rift/Characters/" + card_id
            REPORT["skeletal"][card_id] = dest + "/SK_" + card_id
            REPORT["animations"][card_id] = {clip["name"]:dest+"/Animations/"+clip["action"] for clip in card["animations"]}
        REPORT["statics"] = {name:"/Game/Rift/Environment/SM_"+name for name in MANIFEST["statics"]}
        REPORT["cards"] = {file.stem:"/Game/Rift/CardArt/T_Card_"+file.stem for file in (ROOT/"Assets/Source/CardArt").glob("*.png")}
        if "-RiftCardsOnly" not in unreal.SystemLibrary.get_command_line() and not portraits_only:
            REPORT["presentation"] = json.loads(unreal.RiftEditorAssetLibrary.build_presentation_assets_json())
            if REPORT["presentation"]["errors"]:
                raise RuntimeError(REPORT["presentation"]["errors"])
        card_data()
        REPORT["validation"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
        if REPORT["validation"]["errors"]:
            raise RuntimeError(REPORT["validation"]["errors"])
        return
    texdir = ROOT / "Assets/Source/Textures"
    base = texture(texdir / "T_RiftAtlas_BaseColor.png", "T_RiftAtlas_BaseColor")
    normal = texture(texdir / "T_RiftAtlas_Normal.png", "T_RiftAtlas_Normal", linear=True)
    orm = texture(texdir / "T_RiftAtlas_ORM.png", "T_RiftAtlas_ORM", linear=True)
    mask = texture(texdir / "T_RiftAtlas_TeamMask.png", "T_RiftAtlas_TeamMask", linear=True)
    mats = {name: material(name, base, normal, orm, mask, name == "M_RiftGlow", name == "M_RiftGlass") for name in ("M_RiftSurface", "M_RiftGlow", "M_RiftGlass")}
    material("M_RiftArena", base, normal, orm, mask)
    import_expansion_materials(mats)
    presentation_material("M_RiftWater", shader="water")
    presentation_material("M_RiftParticle")
    presentation_material("M_RiftPlacement", unreal.MaterialDomain.MD_DEFERRED_DECAL, "placement")
    presentation_material("M_RiftGroundRing", unreal.MaterialDomain.MD_DEFERRED_DECAL, "ring")
    presentation_material("M_RiftShockwave", unreal.MaterialDomain.MD_DEFERRED_DECAL, "shockwave")
    cloak_material()
    expansion_fx_material()
    skeletal_editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    static_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.get_default_object(unreal.StaticMeshEditorSubsystem)
    for card_id, card in MANIFEST["characters"].items():
        dest = "/Game/Rift/Characters/" + card_id
        mesh = import_file(ROOT / card["mesh"]["file"], dest, "SK_" + card_id, mesh_options(skeletal=True))
        assign_materials(mesh, card["materials"], mats)
        skeleton = mesh.get_editor_property("skeleton")
        if skeleton is None or mesh.get_editor_property("physics_asset") is None:
            raise RuntimeError(f"Missing skeleton or physics asset for {card_id}")
        # FBX tasks only list the mesh; persist its generated dependencies before
        # any later package load or collection can discard their unsaved data.
        save(skeleton)
        save(mesh.get_editor_property("physics_asset"))
        REPORT["skeletal"][card_id] = mesh.get_path_name()
        for lod in card["lods"]:
            if skeletal_editor.import_lod(mesh, lod["level"], str(ROOT / lod["file"])) < 0:
                raise RuntimeError(f"Failed skeletal LOD {card_id} {lod['level']}")
            REPORT["sourceImports"].append({"source": source_provenance.pin(ROOT, lod["file"]), "assets": [mesh.get_path_name()], "method": "SkeletalMeshEditorSubsystem.import_lod", "level": lod["level"], "succeeded": True})
        if card["materials"] and all(name.startswith("M_RiftExpansion") for name in card["materials"]):
            assign_materials(mesh, card["materials"], mats)
        for socket_name, socket in card["sockets"].items():
            # The dedicated socket bone is imported and usable directly at runtime.
            if socket["bone"] not in card["bones"]:
                raise RuntimeError(f"Missing semantic socket bone {card_id}.{socket_name}")
        clips = {}
        for clip in card["animations"]:
            anim = import_file(ROOT / clip["file"], dest + "/Animations", clip["action"], mesh_options(animation=True, skeleton=skeleton, sample_rate=clip.get("fps", 0)))
            clips[clip["name"]] = anim.get_path_name()
        REPORT["animations"][card_id] = clips
        save(mesh)
    for name, prop in MANIFEST["statics"].items():
        mesh = import_file(ROOT / prop["file"], "/Game/Rift/Environment", "SM_" + name, mesh_options())
        assign_materials(mesh, prop["materials"], mats)
        for lod in prop["lods"]:
            if static_editor.import_lod(mesh, lod["level"], str(ROOT / lod["file"])) < 0:
                raise RuntimeError(f"Failed static LOD {name} {lod['level']}")
            REPORT["sourceImports"].append({"source": source_provenance.pin(ROOT, lod["file"]), "assets": [mesh.get_path_name()], "method": "StaticMeshEditorSubsystem.import_lod", "level": lod["level"], "succeeded": True})
        REPORT["statics"][name] = mesh.get_path_name()
        save(mesh)
    for file in sorted((ROOT / "Assets/Source/CardArt").glob("*.png")):
        art = texture(file, "T_Card_" + file.stem, ui=True)
        REPORT["cards"][file.stem] = art.get_path_name()
    if "-RiftModelsOnly" in unreal.SystemLibrary.get_command_line():
        # Presentation revisions leave the accepted audio and Niagara graphs
        # untouched. Persist the changed models and their existing card bindings.
        if not LIB.save_directory("/Game/Rift", False, True):
            raise RuntimeError("Could not save model presentation imports")
        REPORT["physicsFinalization"] = json.loads(unreal.RiftEditorAssetLibrary.finalize_imported_physics_assets_json())
        if REPORT["physicsFinalization"]["errors"]:
            raise RuntimeError(REPORT["physicsFinalization"]["errors"])
        unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([
            str(ROOT / f"Unreal/RiftCrownArena/Content/Rift/Characters/{card}/SK_{card}_PhysicsAsset.uasset")
            for card in MANIFEST["characters"]], True)
        card_data()
        REPORT["validation"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
        if REPORT["validation"]["errors"]:
            raise RuntimeError(REPORT["validation"]["errors"])
        return
    audio_dir = ROOT / "Assets/Source/Audio"
    audio_manifest = json.loads((audio_dir / "audio_manifest.json").read_text(encoding="utf-8"))
    for wav in sorted(audio_dir.glob("*.wav")):
        sound = import_file(wav, "/Game/Rift/Audio", "SFX_" + wav.stem)
        audio_spec = audio_manifest["sounds"][wav.stem]
        sound.set_editor_property("looping", audio_spec["loop"])
        sound.set_editor_property("compression_quality", 90)
        sound.set_editor_property("priority", 100.0 if audio_spec["loop"] else 75.0 if audio_spec.get("role") == "deployment" else 65.0 if audio_spec.get("role") in ("announcement", "interface") else 40.0)
        sound.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.RETAIN_ON_LOAD if audio_spec["loop"] else unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
        save(sound)
    if not LIB.save_directory("/Game/Rift", False, True):
        raise RuntimeError("Could not save authored imports before presentation binding")
    REPORT["physicsFinalization"] = json.loads(unreal.RiftEditorAssetLibrary.finalize_imported_physics_assets_json())
    if REPORT["physicsFinalization"]["errors"]:
        raise RuntimeError(REPORT["physicsFinalization"]["errors"])
    unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous([
        str(ROOT / f"Unreal/RiftCrownArena/Content/Rift/Characters/{card}/SK_{card}_PhysicsAsset.uasset")
        for card in MANIFEST["characters"]], True)
    # The authored arena is populated by the runtime presentation actor using
    # the same tile geometry and deterministic decoration seed in every build.
    REPORT["presentation"] = json.loads(unreal.RiftEditorAssetLibrary.build_presentation_assets_json())
    if REPORT["presentation"]["errors"]:
        raise RuntimeError(REPORT["presentation"]["errors"])
    card_data()
    REPORT["validation"] = json.loads(unreal.RiftEditorAssetLibrary.inspect_imported_assets_json())
    if REPORT["validation"]["errors"]:
        raise RuntimeError(REPORT["validation"]["errors"])
    LIB.save_directory("/Game/Rift", False, True)

try:
    main()
    source_provenance.verify_frozen(ROOT, REPORT["provenance"], [entry["source"] for entry in REPORT["sourceImports"]])
    REPORT["passed"] = not REPORT["errors"]
except Exception as error:
    REPORT["errors"].append(str(error))
    unreal.log_error(str(error))
    raise
finally:
    (ROOT / "Artifacts/QA").mkdir(parents=True, exist_ok=True)
    report_name = "unreal_asset_particle_material.json" if "-RiftParticleOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_materials.json" if "-RiftMaterialsOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_physics.json" if "-RiftPhysicsOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_material_usage.json" if "-RiftMaterialUsageOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_probe.json" if "-RiftImportProbe" in unreal.SystemLibrary.get_command_line() else "unreal_asset_import.json"
    report_path = Path(os.environ.get("RIFT_IMPORT_REPORT_PATH", str(ROOT / "Artifacts/QA" / report_name)))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(REPORT, indent=2), encoding="utf-8")
