"""Read both saved scenes independently and prove old objects/actions unchanged.

This proof does not repair or resave either scene and does not relabel the
retained generation report. It resolves the producer's old dictionary alias by
using actual preserved pre-addition bytes and actual final scene bytes.
"""
import ast,hashlib,json
from pathlib import Path
import bpy

ROOT=Path(__file__).resolve().parents[1]
H=ROOT/'Assets/Source/Revisions/ApprovedExpansion'
out=H/'VerifiedModelAdditions.json'
if out.exists():raise ValueError('Immutable source comparison already exists')
def pin(path):
    b=path.read_bytes();return {'file':path.relative_to(ROOT).as_posix(),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
old=json.loads((H/'PreviousManifest.json').read_text(encoding='utf-8'))
old_actual=pin(H/'PreviousSource.blend')
if any(old_actual[k]!=old['source'][k] for k in ('bytes','sha256')):raise ValueError('Accepted old source snapshot changed')
if pin(ROOT/M['source']['file'])!=M['source']:raise ValueError('Current source pin changed')
tree=ast.parse((ROOT/'Build/generate_assets.py').read_text(encoding='utf-8'))
fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='source_signatures')
env={'bpy':bpy,'json':json,'hashlib':hashlib}
exec(compile(ast.Module(body=[fn],type_ignores=[]),'actual-source-signatures','exec'),env)
bpy.ops.wm.open_mainfile(filepath=str(H/'PreviousSource.blend'));before=env['source_signatures']()
bpy.ops.wm.open_mainfile(filepath=str(ROOT/M['source']['file']))
after=env['source_signatures'](before['objects'],before['actions'])
if before!=after:raise ValueError('Existing geometry/UV/weights/material/rest-bone/action changed')
proof={'schema':1,'passed':True,'kind':'additive-model-production',
       'verificationMethod':'Independent read-only comparison of the exact preserved accepted source and exact final source',
       'fromSource':old['source'],'toSource':M['source'],
       'addedCharacters':['aether_reaper','bone_warden','bone_warden_skeleton','iron_giant'],
       'addedStatics':['iron_shield_fragment','shadow_surge_ribbon','shadow_surge_wisp'],
       'before':before,'after':after,'producer':pin(ROOT/'Build/generate_assets.py'),
       'modelBuilder':pin(ROOT/'Build/expansion_new_models.py'),'verifier':pin(Path(__file__)),
       'retainedOriginalGenerationEvidence':pin(H/'ProductionModelAdditions.json')}
out.write_text(json.dumps(proof,indent=2),encoding='utf-8')
print('RIFT_SAVED_SOURCE_PRESERVATION',len(before['objects']),'objects',len(before['actions']),'actions')
