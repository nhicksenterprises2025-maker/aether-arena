"""Refresh only the ordinary Iron source render from the existing producer."""
from pathlib import Path
from types import SimpleNamespace
import ast, hashlib, json
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[3]
path=ROOT/'Assets/asset_manifest.json';M=json.loads(path.read_text());source=ROOT/M['source']['file']
before=hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source));scene=bpy.context.scene
scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1024;scene.render.resolution_y=1024
scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG'
tree=ast.parse((ROOT/'Build/generate_assets.py').read_text())
ns={'bpy':bpy,'Vector':Vector,'ROOT':ROOT,'RENDER':ROOT/'Assets/Renders','scene':scene,'manifest':M,
    'sha':lambda p:hashlib.sha256(p.read_bytes()).hexdigest()}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in
     {'file_info','render_asset'}],type_ignores=[]),'actual-existing-render-helper','exec'),ns)
f=SimpleNamespace(name='iron_giant',mesh=bpy.data.objects['SK_iron_giant'],
                  rig=bpy.data.objects['SKEL_iron_giant'],collection=bpy.data.collections['iron_giant'])
stage=bpy.data.collections['RenderStage'];stage.hide_render=False
ns['render_asset'](f,stage,scene.camera)
if hashlib.sha256(source.read_bytes()).hexdigest()!=before:raise RuntimeError('Source was modified by render-only refresh')
path.write_text(json.dumps(M,indent=2));print('RIFT_IRON_MAIN_RENDER_ONLY',flush=True)
