"""Convert dependency-written portrait libraries into editable startup-ready scenes.

Run in Blender after render_card_portraits.py and before portrait validation.
The image renders remain untouched; only .blend startup state is finalized.
"""
from pathlib import Path
import hashlib
import json
import bpy

ROOT=Path(__file__).resolve().parents[1]
report_path=ROOT/'Assets/Source/CardArt/model_portraits.json'
report=json.loads(report_path.read_text(encoding='utf-8'))
production_path=ROOT/'Assets/Renders/Expansion/portrait-production.json'
production=json.loads(production_path.read_text(encoding='utf-8'))
normalizer={'file':'Build/finalize_expansion_art_scenes.py','bytes':Path(__file__).stat().st_size,
            'sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
for card in ('mini_stampede','stampede','pulverizer'):
    record=report['cards'][card];path=ROOT/record['editableScene']['file']
    bpy.ops.wm.open_mainfile(filepath=str(path))
    target=bpy.data.scenes['Portrait_'+card]
    target.render.fps=100
    bpy.context.window.scene=target
    for scene in list(bpy.data.scenes):
        if scene!=target:bpy.data.scenes.remove(scene)
    for area in bpy.context.screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA'
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(path),compress=True)
    raw=path.read_bytes();record['editableScene']={'file':path.relative_to(ROOT).as_posix(),
                                                 'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
    record['sceneFinalizer']=normalizer
    production['cards'][card]=record
    # Verify this normal .blend now selects the production scene on open.
    bpy.ops.wm.open_mainfile(filepath=str(path))
    if bpy.context.scene.name!='Portrait_'+card:raise RuntimeError('Incorrect portrait startup scene: '+card)
    print('RIFT_EDITABLE_PORTRAIT_READY',card,len(bpy.context.scene.objects),flush=True)
report_path.write_text(json.dumps(report,indent=2),encoding='utf-8')
production_path.write_text(json.dumps(production,indent=2),encoding='utf-8')
