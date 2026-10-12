"""Independently compare preserved 1.6 Source with the actual saved Iron revision."""
from pathlib import Path
import argparse, ast, hashlib, json, sys
import bpy
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]);out=args.output.resolve()
out.relative_to(ROOT/'Assets/Source/Revisions/DesignUpdate1.7.0')
if out.exists():raise ValueError('Saved-scene comparison is immutable')
def pin(p):
    raw=p.read_bytes();return {'file':p.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
tree=ast.parse((ROOT/'Build/generate_assets.py').read_text())
ns={'bpy':bpy,'hashlib':hashlib,'json':json}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in
              {'source_signatures','replacement_material_signatures'}],type_ignores=[]),'pinned-generator-signatures','exec'),ns)
old_path=ROOT/'Assets/Source/Revisions/DesignUpdate1.7.0/BeforeIronGiant/RiftCrown_ProductionAssets.blend'
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text());new_path=ROOT/M['source']['file']
samples=[]
for path in (old_path,new_path):
    bpy.ops.wm.open_mainfile(filepath=str(path))
    excluded={ob.name for ob in bpy.data.collections['iron_giant'].all_objects}
    kept={ob.name for ob in bpy.data.objects}-excluded
    actions={a.name for a in bpy.data.actions if not a.name.startswith('AN_iron_giant_')}
    samples.append({'signatures':ns['source_signatures'](kept,actions,True),
        'materials':ns['replacement_material_signatures'](),'sceneFps':bpy.context.scene.render.fps})
if samples[0]!=samples[1]:raise RuntimeError('Actual saved non-Iron object, action, material, atlas or studio timing changed')
R={'schema':1,'passed':True,'kind':'independent-saved-scene-selected-replacement',
   'scope':'Preserved accepted Source versus actual saved final Source; only Iron Giant is excluded',
   'fromSource':pin(old_path),'toSource':pin(new_path),'unchangedBefore':samples[0],
   'unchangedAfter':samples[1],'keptObjects':len(samples[0]['signatures']['objects']),
   'keptActions':len(samples[0]['signatures']['actions']),'verifier':pin(Path(__file__)),
   'signatureProducer':pin(ROOT/'Build/generate_assets.py')}
out.write_text(json.dumps(R,indent=2));print('RIFT_SAVED_SOURCE_COMPARISON',R['keptObjects'],R['keptActions'],flush=True)
