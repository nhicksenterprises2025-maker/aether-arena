"""Render actual skeletal action poses from the saved source for review."""
from pathlib import Path
import bpy,json,math
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text())
bpy.ops.wm.open_mainfile(filepath=str(ROOT/M['source']['file']))
scene=bpy.context.scene;scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
stage=bpy.data.collections['RenderStage'];camera=scene.camera
R=ROOT/'Assets/Renders/Poses';R.mkdir(parents=True,exist_ok=True)
for name in M['characters']:
    for col in bpy.data.collections:col.hide_render=col!=stage and col.name!=name
    mesh=bpy.data.objects['SK_'+name];rig=bpy.data.objects['SKEL_'+name]
    h=max(1.2,mesh.dimensions.z);w=max(mesh.dimensions.x,mesh.dimensions.y)
    target=Vector((0,0,h*.47));camera.location=target+Vector((4.5,-7,4));camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
    camera.data.ortho_scale=max(h*1.6,w*1.45)
    poses=[('Idle',0),('Locomotion',.25),('Attack',.48),('Death',.70)]
    if M['characters'][name]['rigFamily']=='humanoid':
        poses += [('Attack',.25),('Attack',.80)]
    for action,phase in poses:
        clip=next(x for x in M['characters'][name]['animations'] if x['name']==action)
        rig.animation_data.action=bpy.data.actions[clip['action']];scene.frame_set(1+round((clip['frames']-1)*phase))
        suffix=action if phase in (0,.25) and action!='Attack' or phase in (.48,.70) else action+'_'+str(round(phase*100))
        scene.render.filepath=str(R/(name+'_'+suffix+'.png'));bpy.ops.render.render(write_still=True)
print('RIFT_ANIMATION_REVIEW_RENDERED')
