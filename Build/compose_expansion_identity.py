"""Compose production identity documents from actual Blender review renders."""
from pathlib import Path
import hashlib
import json
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
M=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
DEST=ROOT/'Assets/Renders/Expansion/Identity'
FONT=ROOT/'Assets/Fonts/BarlowSemiCondensed'
names={'mini_stampede':'Mini Stampede','stampede':'Stampede','pulverizer':'Pulverizer'}
report={'schema':1,'method':'Production documentation layout with actual final Blender renders',
        'sourceBlend':M['source'],'sheets':{}}

def font(size,bold=False):
    return ImageFont.truetype(str(FONT/('BarlowSemiCondensed-Bold.ttf' if bold else 'BarlowSemiCondensed-Regular.ttf')),size)

def wrap(draw,text,x,y,width,style,fill):
    words=text.split();line='';height=style.size+6
    for word in words:
        trial=line+' '+word if line else word
        if draw.textlength(trial,font=style)>width and line:
            draw.text((x,y),line,font=style,fill=fill);y+=height;line=word
        else:line=trial
    if line:draw.text((x,y),line,font=style,fill=fill);y+=height
    return y

for card,name in names.items():
    sheet=Image.new('RGB',(1600,1480),(17,27,34));draw=ImageDraw.Draw(sheet)
    draw.text((42,25),name.upper(),font=font(44,True),fill=(235,230,215))
    draw.text((44,83),'FINAL MODEL IDENTITY / BLENDER -Y FORWARD TO UNREAL +X',font=font(20),fill=(152,175,184))
    for i,(view,label) in enumerate((('front','FRONT'),('rear','REAR'),('side','SIDE'),('threequarter','THREE QUARTER'))):
        x=38+i*391
        image=Image.open(DEST/(card+'_'+view+'.png')).convert('RGB').resize((366,366),Image.Resampling.LANCZOS)
        sheet.paste(image,(x,126));draw.text((x,505),label,font=font(23,True),fill=(224,226,218))
    sil=Image.open(DEST/(card+'_silhouette.png')).convert('RGB').resize((290,290),Image.Resampling.LANCZOS)
    sheet.paste(sil,(40,565));draw.text((40,860),'SILHOUETTE / SAME ANATOMY',font=font(20,True),fill=(209,219,221))
    preview=Image.open(ROOT/'Assets/Renders/Expansion/Small'/(card+'_96x120.png')).convert('RGB')
    sheet.paste(preview,(358,570));draw.rectangle((357,569,454,690),outline=(95,127,141))
    draw.text((358,702),'96 × 120',font=font(20),fill=(209,219,221))
    art=Image.open(ROOT/'Assets/Source/CardArt/Masters'/(card+'.png')).convert('RGB')
    art.thumbnail((285,360),Image.Resampling.LANCZOS);sheet.paste(art,(1250,565))
    draw.text((1248,938),'1024 × 1280 MASTER',font=font(20,True),fill=(209,219,221))
    metadata=M['characters'][card]['modelDesign'];contract=metadata['animationContract']
    palette=[('Slate hide',(91,105,111)),('Copper guard',(165,85,40)),('Amber bristles',(182,115,40)),
             ('Teal harness',(40,111,115)),('Ivory tusks',(216,224,216)),('Dark hooves',(20,29,35))]
    if card=='pulverizer':palette=[('Basalt',(42,52,53)),('Steel',(79,99,111)),('Bronze',(174,120,61)),('Violet crystal',(125,48,195))]
    elif card=='stampede':palette.append(('Pennant knots',(182,50,22)))
    draw.text((495,565),'MATERIAL PALETTE',font=font(26,True),fill=(233,228,216))
    for i,(label,color) in enumerate(palette):
        row,col=divmod(i,3);x=500+col*230;y=609+row*75
        draw.rounded_rectangle((x,y,x+42,y+42),radius=4,fill=color)
        draw.text((x+53,y+7),label,font=font(20),fill=(201,217,219))
    detail=('Flat copper snout plate: two recessed nostril slots and four rivets. Fixed paired tusks. Two fitted shoulder ropes, rear knot and independently jointed ears. '
            'No rider, curved ram horns, crown, charge damage, knockback or shared group hitbox.') if card!='pulverizer' else (
            'Broad basalt base with four bronze braces. Four compressing suspension struts connect to a low piston head; the annular shoe physically reaches the recessed plate. '
            'Independent counter-rotating ring and suspended violet crystal. No muzzle, barrel, air target or death damage.')
    if card=='stampede':detail+=' Same species as Mini; red-orange pennant knots distinguish the fifteen-unit formation.'
    draw.text((495,852),'CONSTRUCTION / ATTACHMENTS',font=font(26,True),fill=(233,228,216))
    wrap(draw,detail,495,900,700,font(23),(202,215,218))
    draw.line((42,1110,1555,1110),fill=(63,86,97),width=2)
    draw.text((42,1133),'JOINTS & SOCKETS',font=font(26,True),fill=(233,228,216))
    y=wrap(draw,', '.join(M['characters'][card]['bones']),42,1178,735,font(21),(198,214,219))
    draw.text((830,1133),'ANIMATION CONTRACT',font=font(26,True),fill=(233,228,216))
    attack=contract['attack'];locomotion=contract['locomotion']
    lines=[f"Supplied visual scale: {metadata['baseModelScale']:.2f}; collision remains independent.",
           f"Anticipation {attack['anticipationSeconds']:.2f}s; one contact at t=0.",
           f"Recoil {attack['recoilSeconds']:.2f}s; ready by +{attack['recoverySeconds']:.2f}s.",
           f"Death cleanup: {contract['death']['cleanupSeconds']:.2f}s."]
    if locomotion['referenceCycleSeconds']:
        lines.append(f"Reference gallop {locomotion['referenceCycleSeconds']:.2f}s at {locomotion['referenceSpeedTilesPerSecond']:.2f} tiles/s; distance driven.")
    else:lines.append('Stationary foundation; piston and ring animate through their own bones.')
    lines.append('Simulation owns damage, spawn timing and cooldown. Visual poses cannot add damage or delay the first attack.')
    yy=1178
    for line in lines:yy=wrap(draw,line,830,yy,720,font(22),(198,214,219))+7
    path=DEST/(card+'_IdentitySheet.png');sheet.save(path)
    raw=path.read_bytes();report['sheets'][card]={'file':path.relative_to(ROOT).as_posix(),'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
    M['characters'][card]['identitySheet']=report['sheets'][card]
(ROOT/'Assets/asset_manifest.json').write_text(json.dumps(M,indent=2),encoding='utf-8')
(DEST/'identity-sheets.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('RIFT_EXPANSION_IDENTITY_SHEETS',len(report['sheets']))
