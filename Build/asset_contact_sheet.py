"""Assemble actual Blender renders for visual review; never edits game art."""
from pathlib import Path
import json
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'Assets/Renders'
try: font=ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf',22)
except OSError:font=ImageFont.load_default()
def sheet(names,name,columns=4):
    width=400; height=440; rows=(len(names)+columns-1)//columns
    canvas=Image.new('RGB',(width*columns,height*rows),(9,15,23));d=ImageDraw.Draw(canvas)
    for i,n in enumerate(names):
        path=R/(n+'.png')
        if not path.exists():continue
        im=Image.open(path).convert('RGB');im.thumbnail((width,height-40))
        x=(i%columns)*width;y=(i//columns)*height
        canvas.paste(im,(x+(width-im.width)//2,y))
        d.text((x+18,y+height-34),n.replace('_',' ').title(),font=font,fill=(220,231,241))
    canvas.save(R/(name+'.png'))
sheet(['ironclad','ember_archer','twin_blades','arc_mage','boulderback','rambeast','frost_fang','sky_manta','vampire_bats','storm_raven','archer_tower','tower_archer','tower_guard','tower_core','nova_flask','meteor_shard'],'characters_contact')
sheet(['bridge','floor_tile','lane_paver','bank_segment','boundary_stone','grass_tuft','shrub','tree','crystal_plinth','banner','ruin','distant_island','meteor_shard','bullet_round','tower_rubble'],'environment_contact')
pose_names=[name+'_'+action for name in ('ironclad','ember_archer','twin_blades','frost_fang','storm_raven','sky_manta') for action in ('Idle','Locomotion','Attack','Death')]
if all((R/'Poses'/(n+'.png')).exists() for n in pose_names):
    before=R;R=R/'Poses';sheet(pose_names,'animation_contact',4);R=before
p=ROOT/'Assets/asset_manifest.json';m=json.loads(p.read_text())
m['units'].update({'fbx':'meter','fbxCentimetersPerUnit':100,'unrealImportUniformScale':1})
m['contactSheets']=['Assets/Renders/characters_contact.png','Assets/Renders/environment_contact.png']
p.write_text(json.dumps(m,indent=2),encoding='utf-8')
print('Contact sheets saved.')
