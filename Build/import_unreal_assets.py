"""Import authored source assets with the installed UE editor Python runtime.

Run with UnrealEditor-Cmd RiftCrownArena.uproject -run=pythonscript
-script=<absolute file> -unattended -NullRHI. RIFT_REPO_ROOT may override root.
"""
import os
import json
from pathlib import Path
import unreal

ROOT = Path(os.environ.get("RIFT_REPO_ROOT", Path(__file__).resolve().parents[1]))
MANIFEST = json.loads((ROOT / "Assets/asset_manifest.json").read_text(encoding="utf-8"))
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary
REPORT = {"imported": [], "errors": [], "skeletal": {}, "animations": {}, "statics": {}, "cards": {}}

def save(asset):
    if not LIB.save_loaded_asset(asset, False):
        raise RuntimeError(f"Could not save imported asset {asset.get_path_name()}")
    return asset

def import_file(source, destination, name, options=None):
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
    REPORT["imported"].extend(paths)
    for path in paths:
        asset = LIB.load_asset(path)
        if asset is not None and asset.get_name() == name:
            return asset
    return LIB.load_asset(paths[0])

def mesh_options(skeletal=False, animation=False, skeleton=None):
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
        data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
        data.set_editor_property("use_default_sample_rate", True)
    if not skeletal and not animation:
        data.set_editor_property("combine_meshes", True)
        data.set_editor_property("auto_generate_collision", True)
    return options

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
    highlight = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -150, -180)
    highlight.set_editor_property("constant", unreal.LinearColor(1,.82,.56,1))
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
    field.set_editor_property("code", "float2 d=abs(frac(World.xy/100.0+0.5)-0.5)*100.0; float aa=max(length(fwidth(World.xy)),0.25); float seam=1.0-smoothstep(1.0,2.5+aa,min(d.x,d.y)); return Base*Gain*(1.0-0.58*saturate(Grid)*seam);")
    for source, output, input_name in ((flash_lerp,"","Base"),(world,"","World"),(grid,"","Grid"),(gain,"","Gain")):
        unreal.MaterialEditingLibrary.connect_material_expressions(source, output, field, input_name)
    unreal.MaterialEditingLibrary.connect_material_property(field, "", unreal.MaterialProperty.MP_BASE_COLOR)
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
    if glass:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        alpha = unreal.MaterialEditingLibrary.create_material_expression(m, unreal.MaterialExpressionConstant, -100, 700)
        alpha.set_editor_property("r", 0.75)
        unreal.MaterialEditingLibrary.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    unreal.MaterialEditingLibrary.recompile_material(m)
    return save(m)

def assign_materials(mesh, names, mats):
    if isinstance(mesh, unreal.SkeletalMesh):
        slots = mesh.get_editor_property("materials")
        for index, slot in enumerate(slots):
            slot.set_editor_property("material_interface", mats.get(str(slot.get_editor_property("material_slot_name")), mats[names[min(index, len(names)-1)]]))
        mesh.set_editor_property("materials", slots)
    else:
        for index, name in enumerate(names):
            mesh.set_material(index, mats[name])
    save(mesh)

def presentation_material(name, domain=None, shader="particle"):
    path = "/Game/Rift/Materials/" + name
    m = LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, "/Game/Rift/Materials", unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(m)
    if domain is not None:
        m.set_editor_property("material_domain", domain)
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT if shader != "water" else unreal.BlendMode.BLEND_OPAQUE)
    if shader != "water":
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
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
    unreal.MaterialEditingLibrary.connect_material_expressions(uv, "", custom, "UV")
    unreal.MaterialEditingLibrary.recompile_material(m)
    return save(m)

def card_data():
    definitions = json.loads(unreal.RiftAssetLibrary.card_definitions_json())
    properties = {"cost":"Cost", "count":"Count", "hp":"HP", "damage":"Damage", "attackInterval":"AttackSpeed", "moveSpeed":"MoveSpeed", "range":"AttackRange", "projectileSpeed":"ProjectileSpeed", "splash":"SplashRadius", "lifetime":"Lifetime", "footprint":"Footprint", "towerDamage":"TowerDamage", "spellRadius":"SpellRadius", "chargeDamage":"ChargeDamage", "slowPct":"SlowPct", "slowDuration":"SlowDuration", "auraDamage":"AuraDamage", "auraRadius":"AuraRadius", "auraInterval":"AuraInterval", "stunDuration":"StunDuration", "dotDamage":"DotDamage", "dotDuration":"DotDuration", "dotInterval":"DotInterval", "rounds":"Rounds", "flying":"Flying", "canHitAir":"GroundAndAir", "structuresOnly":"StructuresOnly", "spell":"Spell", "building":"Building"}
    attacks = {"ironclad":"sword_attack", "ember_archer":"bow_release", "twin_blades":"dual_attack", "boulderback":"heavy_slam", "arc_mage":"arc_cast", "rambeast":"charge", "sky_manta":"manta_cast", "archer_tower":"arrow_flight", "bullet_burst":"bullet_burst", "nova_flask":"nova_impact", "vampire_bats":"bat_bite", "frost_fang":"frost_attack", "storm_raven":"storm_bolt", "meteor_shards":"meteor_impact"}
    effects = ["Deploy","Impact","ArrowFlight","ArcFlight","MantaFlight","StormFlight","TowerFlight","BulletBurst","Nova","Meteor","MeteorTick","Frost","Slow","Stun","Aura","TowerDestroy","CoreAwaken"]
    for c in definitions:
        unreal.log("Building Rift CardData: " + c["id"])
        name = "DA_" + c["id"]
        path = "/Game/Rift/Cards/" + name
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.RiftCardData)
        data = LIB.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, "/Game/Rift/Cards", unreal.RiftCardData, factory)
        data.set_editor_property("CardId", c["id"])
        data.set_editor_property("DisplayName", unreal.Text(c["name"]))
        role = "Spell" if c["spell"] else "Defensive Building" if c["building"] else "Flying" if c["flying"] else "Ground"
        data.set_editor_property("CombatClass", unreal.Text(role))
        data.set_editor_property("Description", unreal.Text(c["name"] + " — " + role))
        for key, prop in properties.items():
            data.set_editor_property(prop, c[key])
        data.set_editor_property("Illustration", LIB.load_asset(REPORT["cards"][c["id"]]))
        if c["id"] in REPORT["skeletal"]:
            data.set_editor_property("CharacterMesh", LIB.load_asset(REPORT["skeletal"][c["id"]]))
            data.set_editor_property("Animations", {key:LIB.load_asset(path) for key,path in REPORT["animations"][c["id"]].items()})
        if c["building"]:
            data.set_editor_property("StructureMesh", LIB.load_asset(REPORT["statics"][c["id"]]))
        data.set_editor_property("Effects", {key:LIB.load_asset("/Game/Rift/VFX/NS_Rift"+key) for key in effects})
        sounds = {"Attack": LIB.load_asset("/Game/Rift/Audio/SFX_" + attacks[c["id"]]), "Deploy":LIB.load_asset("/Game/Rift/Audio/SFX_deploy"), "Hit":LIB.load_asset("/Game/Rift/Audio/SFX_sword_hit")}
        if any(v is None for v in sounds.values()):
            raise RuntimeError("Missing sound binding for " + c["id"])
        data.set_editor_property("Sounds", sounds)
        LIB.set_metadata_tag(data, "SourceManifest", MANIFEST["source"]["sha256"])
        if c["id"] in MANIFEST.get("illustrations", {}):
            LIB.set_metadata_tag(data,"PortraitSHA256",MANIFEST["illustrations"][c["id"]]["sha256"])
            LIB.set_metadata_tag(data,"PortraitSource", "Production model render" if (ROOT/"Assets/Source/CardArt/model_portraits.json").exists() else "Original illustration")
        save(data)

def main():
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
    presentation_material("M_RiftWater", shader="water")
    presentation_material("M_RiftParticle")
    presentation_material("M_RiftPlacement", unreal.MaterialDomain.MD_DEFERRED_DECAL, "placement")
    presentation_material("M_RiftGroundRing", unreal.MaterialDomain.MD_DEFERRED_DECAL, "ring")
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
        for socket_name, socket in card["sockets"].items():
            # The dedicated socket bone is imported and usable directly at runtime.
            if socket["bone"] not in card["bones"]:
                raise RuntimeError(f"Missing semantic socket bone {card_id}.{socket_name}")
        clips = {}
        for clip in card["animations"]:
            anim = import_file(ROOT / clip["file"], dest + "/Animations", clip["action"], mesh_options(animation=True, skeleton=skeleton))
            clips[clip["name"]] = anim.get_path_name()
        REPORT["animations"][card_id] = clips
        save(mesh)
    for name, prop in MANIFEST["statics"].items():
        mesh = import_file(ROOT / prop["file"], "/Game/Rift/Environment", "SM_" + name, mesh_options())
        assign_materials(mesh, prop["materials"], mats)
        for lod in prop["lods"]:
            if static_editor.import_lod(mesh, lod["level"], str(ROOT / lod["file"])) < 0:
                raise RuntimeError(f"Failed static LOD {name} {lod['level']}")
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
        sound.set_editor_property("priority", 100.0 if audio_spec["loop"] else 65.0 if audio_spec.get("role") in ("announcement", "interface") else 40.0)
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
except Exception as error:
    REPORT["errors"].append(str(error))
    unreal.log_error(str(error))
    raise
finally:
    (ROOT / "Artifacts/QA").mkdir(parents=True, exist_ok=True)
    report_name = "unreal_asset_particle_material.json" if "-RiftParticleOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_materials.json" if "-RiftMaterialsOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_physics.json" if "-RiftPhysicsOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_material_usage.json" if "-RiftMaterialUsageOnly" in unreal.SystemLibrary.get_command_line() else "unreal_asset_probe.json" if "-RiftImportProbe" in unreal.SystemLibrary.get_command_line() else "unreal_asset_import.json"
    (ROOT / "Artifacts/QA" / report_name).write_text(json.dumps(REPORT, indent=2), encoding="utf-8")
