"""Render actual skeletal action poses from the saved source for review."""
from pathlib import Path
import bpy,json,math,argparse,sys,hashlib
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--models',nargs='*');parser.add_argument('--walk-only',action='store_true')
opts=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/M['source']['file']))
scene=bpy.context.scene;scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
stage=bpy.data.collections['RenderStage'];camera=scene.camera
R=ROOT/'Assets/Renders'/('Walking' if opts.walk_only else 'Poses');R.mkdir(parents=True,exist_ok=True)
report={'schema':1,'version':M['build'],'sourceBlend':M['source'],'renders':[]}
for name in opts.models or M['characters']:
    for col in bpy.data.collections:col.hide_render=col!=stage and col.name!=name
    mesh=bpy.data.objects['SK_'+name];rig=bpy.data.objects['SKEL_'+name]
    h=max(1.2,mesh.dimensions.z);w=max(mesh.dimensions.x,mesh.dimensions.y)
    target=Vector((0,0,h*.47));camera.location=target+Vector((4.5,-7,4));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=max(h*1.6,w*1.45)
    poses=[('Idle',0),('Locomotion',.25),('Attack',.48),('Death',.70)]
    if opts.walk_only:poses=[('Locomotion',phase/8) for phase in range(8)]
    elif M['characters'][name]['rigFamily']=='humanoid':
        poses += [('Attack',.25),('Attack',.80)]
    for action,phase in poses:
        clip=next(x for x in M['characters'][name]['animations'] if x['name']==action)
        rig.animation_data.action=bpy.data.actions[clip['action']];scene.frame_set(1+round((clip['frames']-1)*phase))
        suffix=action if phase in (0,.25) and action!='Attack' or phase in (.48,.70) else action+'_'+str(round(phase*100))
        if opts.walk_only:suffix='Locomotion_'+f'{round(phase*1000):03d}'
        path=R/(name+'_'+suffix+'.png');scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)
        report['renders'].append({'model':name,'action':action,'phase':phase,'file':path.relative_to(ROOT).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
(R/'render-review.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('RIFT_ANIMATION_REVIEW_RENDERED')
