"""Compose the approved identities from actual final Blender renders only."""
from pathlib import Path
import hashlib,json
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
DEST=ROOT/'Assets/Renders/Expansion/Identity'
FONTS=ROOT/'Assets/Fonts/BarlowSemiCondensed'
NAMES={'bone_warden':'Bone Warden','bone_warden_skeleton':'Skeleton / Bone Warden child',
       'aether_reaper':'Aether Reaper','iron_giant':'Iron Giant','shadow_surge':'Shadow Surge'}
DETAILS={
 'bone_warden':'Shaped ivory anatomy inside fitted iron plates and bronze trim. Four captive skull, rib and limb silhouettes sit behind the open rib cage; their heads and reaching arms move independently. One octagonal bone-and-iron maul has two open curled grips around its shaft and releases during Death. Four separate Skeletons in the illustration represent the death summons.',
 'bone_warden_skeleton':'Subordinate thin ivory skeleton with a connected neck, oversized skull, bronze broken cuff and one chipped short sword. A real child rig with nine clips; no deck slot, independent portrait requirement or recursive summon.',
 'aether_reaper':'Airborne hooded specter with an opaque midnight upper cloak, violet lining, narrow cyan seams and silver crescent scythe. Offset palms and curled fingers hold a real cavity around the shaft. Twelve cloth joints bound the lower tips; no legs, wings or simulated cloth.',
 'iron_giant':'One broad furnace-gray automaton with amber eye slits, copper hinges and blunt fists. Six separate shield-only deform bones preserve the underlying torso. The one-Giant illustration uses a source-only small cutaway; a dedicated fragment mesh supplies native shield-break pieces.',
 'shadow_surge':'A spell identity made from one low ring/ribbon and three authored wisps. The card illustration contains three affected allies. These views contain only the actual effect meshes: no skeletal binding or invented spawned creature.',
}
PALETTE=[('Forged iron',(.17,.205,.24)),('Ivory',(.83,.78,.64)),('Midnight cloth',(.09,.13,.21)),
         ('Violet lining',(.19,.145,.24)),('Green soul',(.13,.50,.255)),('Amber eye',(.95,.43,.08))]
def font(size,bold=False):return ImageFont.truetype(str(FONTS/('BarlowSemiCondensed-Bold.ttf' if bold else 'BarlowSemiCondensed-Regular.ttf')),size)
def wrap(draw,text,x,y,width,size=23):
    style=font(size);line=''
    for word in text.split():
        test=(line+' '+word).strip()
        if line and draw.textlength(test,font=style)>width:
            draw.text((x,y),line,font=style,fill=(202,215,218));y+=size+7;line=word
        else:line=test
    if line:draw.text((x,y),line,font=style,fill=(202,215,218));y+=size+7
    return y
report={'schema':1,'sourceBlend':M['source'],'method':'Actual model/effect orthographic review renders; documentation layout, no baked game UI','sheets':{}}
for card,label in NAMES.items():
    sheet=Image.new('RGB',(1600,1660),(17,27,34));draw=ImageDraw.Draw(sheet)
    draw.text((42,25),label.upper(),font=font(43,True),fill=(235,230,215))
    draw.text((44,83),'AUTHORED IDENTITY / BLENDER -Y FORWARD / UNREAL +X IMPORT',font=font(20),fill=(152,175,184))
    for i,(view,title) in enumerate((('front','FRONT'),('rear','REAR'),('side','SIDE'),('threequarter','THREE QUARTER'))):
        x=38+i*391
        image=Image.open(DEST/(card+'_'+view+'.png')).convert('RGB').resize((366,366),Image.Resampling.LANCZOS)
        sheet.paste(image,(x,126));draw.text((x,505),title,font=font(23,True),fill=(224,226,218))
    sil=Image.open(DEST/(card+'_silhouette.png')).convert('RGB').resize((285,285),Image.Resampling.LANCZOS)
    sheet.paste(sil,(40,565));draw.text((40,865),'SOURCE SILHOUETTE',font=font(20,True),fill=(209,219,221))
    sheet.paste(Image.open(ROOT/'Assets/Renders/Expansion/Small'/(card+'_96x120.png')).convert('RGB'),(358,570))
    draw.text((358,702),'96 × 120',font=font(20),fill=(209,219,221))
    art=Image.open(ROOT/'Assets/Source/CardArt/Masters'/(card+'.png')).convert('RGB');art.thumbnail((285,360),Image.Resampling.LANCZOS)
    sheet.paste(art,(1250,565));draw.text((1248,938),'1024 × 1280 MASTER',font=font(20,True),fill=(209,219,221))
    draw.text((495,565),'NEW AUTHORED PALETTE',font=font(26,True),fill=(233,228,216))
    for i,(name,rgb) in enumerate(PALETTE):
        row,col=divmod(i,3);x=500+col*230;y=609+row*75
        draw.rectangle((x,y,x+42,y+42),fill=tuple(round(c*255) for c in rgb));draw.text((x+53,y+7),name,font=font(19),fill=(201,217,219))
    draw.text((495,798),'CONSTRUCTION / ATTACHMENTS',font=font(26,True),fill=(233,228,216))
    wrap(draw,DETAILS[card],495,845,700)
    draw.line((42,1110,1555,1110),fill=(63,86,97),width=2)
    record=M['cards'][card] if card=='shadow_surge' else M['characters'][card]
    if card=='shadow_surge':
        draw.text((42,1133),'EFFECT SOURCE',font=font(26,True),fill=(233,228,216))
        wrap(draw,'SM_shadow_surge_ribbon; SM_shadow_surge_wisp × 3. Static source meshes with two lower LODs. Illustration allies are context only.',42,1178,720)
        draw.text((830,1133),'SIMULATION TIMING / NATIVE PENDING',font=font(26,True),fill=(233,228,216))
        wrap(draw,'Thin 2-tile ring telegraph: 0.25 seconds. Global friendly invisibility: 1 simulation second. Effect expiration: below 0.30 seconds. Native deterministic timers and actual camera acceptance are validated separately.',830,1178,720)
    else:
        design=record['modelDesign'];c=design['animationContract']
        draw.text((42,1133),'JOINTS / SOURCE LODS',font=font(26,True),fill=(233,228,216))
        y=wrap(draw,', '.join(record['bones']),42,1178,730,20)
        wrap(draw,f"{record['boneCount']} authored bones. Triangles: {record['triangles']}, "+', '.join(str(l['triangles']) for l in record['lods'])+'.',42,y+10,730,20)
        draw.text((830,1133),'ANIMATION CONTRACT',font=font(26,True),fill=(233,228,216))
        attack=c['attack'];gait=c['locomotion']
        text=(f"Visual scale {design['baseModelScale']:.2f}. Nine clips at 100 FPS. "
              f"Anticipation {attack['anticipationSeconds']:.2f}s; one contact t=0; recovery +{attack['recoverySeconds']:.2f}s. "
              f"Death cleanup {c['death']['cleanupSeconds']:.2f}s. "
              f"Reference cycle {gait['referenceCycleSeconds']:.2f}s at {gait['referenceSpeedTilesPerSecond']:.2f} tiles/s; stride {gait['strideMeters']:.3f}m before model scale. "
              'In-place roots. Simulation owns collision, attacks, damage and ability timing. Native import and actual camera acceptance are separate.')
        wrap(draw,text,830,1178,720)
    path=DEST/(card+'_IdentitySheet.png');sheet.save(path)
    data=path.read_bytes();pin={'file':path.relative_to(ROOT).as_posix(),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    record['identitySheet']=pin;report['sheets'][card]=pin
(ROOT/'Assets/asset_manifest.json').write_text(json.dumps(M,indent=2),encoding='utf-8')
(DEST/'approved-identity-sheets.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('RIFT_APPROVED_IDENTITY_SHEETS',len(report['sheets']))
