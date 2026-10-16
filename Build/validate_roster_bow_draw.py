"""Read saved rig/mesh poses: bow tension, drawing hand, arrow release."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--report',type=Path,required=True)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
output=args.report.resolve();output.relative_to((ROOT/'Artifacts/QA').resolve())
manifest=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
source=ROOT/manifest['source']['file']
report={'schema':1,'scope':'Saved authored mesh/rig draw poses; native imported animation is separate',
        'sourceSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),'checks':[],'cards':{}}
def check(name,passed,detail=None):
    report['checks'].append({'name':name,'passed':bool(passed),'detail':detail})
check('Actual source hash matches manifest',report['sourceSHA256']==manifest['source']['sha256'])
bpy.ops.wm.open_mainfile(filepath=str(source));scene=bpy.context.scene
for card in ('ember_archer','tower_archer'):
    data=manifest['characters'][card];rig=bpy.data.objects['SKEL_'+card];mesh=bpy.data.objects['SK_'+card]
    clip=next(item for item in data['animations'] if item['name']=='DrawRelease')
    rig.animation_data.action=bpy.data.actions[clip['action']]
    frames={}
    for name,phase in (('draw',.37),('released',.49)):
        frame=1+(clip['frames']-1)*phase
        scene.frame_set(int(frame),subframe=frame-int(frame));bpy.context.view_layer.update()
        left=rig.pose.bones['hand_l'];nock=rig.pose.bones['bow_nock']
        transform=left.matrix@left.bone.matrix_local.inverted()
        draw=transform.inverted()@nock.matrix.translation-nock.bone.head_local
        evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
        group=mesh.vertex_groups['hand_r'].index
        points=[evaluated.matrix_world@vertex.co for vertex in evaluated.data.vertices
                if any(item.group==group and item.weight>.99 for item in vertex.groups)]
        gap=min((point-nock.matrix.translation).length for point in points)
        frames[name]={'sourceFrame':frame,'drawMeters':draw.y,'rightHandNearestVertexMeters':gap,
                      'arrowScale':list(rig.pose.bones['nocked_arrow'].scale)}
    check(card+' string pulls visibly before release',frames['draw']['drawMeters']>.12,frames['draw'])
    check(card+' draw hand reaches actual nock',frames['draw']['rightHandNearestVertexMeters']<.045,frames['draw'])
    check(card+' string releases at existing marker',abs(frames['released']['drawMeters'])<.003 and clip['releaseNormalized']==.48,frames['released'])
    check(card+' separate projectile replaces nocked arrow',min(frames['draw']['arrowScale'])>.99 and max(frames['released']['arrowScale'])<.002)
    report['cards'][card]=frames
report['passed']=all(item['passed'] for item in report['checks'])
output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(report,indent=2),encoding='utf-8')
print('RIFT_BOW_DRAW_QA',report['passed'],len(report['checks']))
if not report['passed']:raise RuntimeError('Saved bow draw failed')
