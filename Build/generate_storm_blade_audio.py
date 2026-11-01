"""Add short Thunderstorm/Blade Box cues while preserving every released master."""
import hashlib
import json
import generate_audio as a


def storm(variation=0, impact=False):
    x=a.frames(.38 if impact else .75)
    a.put(x,a.air(.30 if impact else .58,2100+variation,70,1900,decay=10 if impact else 6),gain=.20)
    a.put(x,a.body(.28,72+variation*5,33),at=.018,gain=.20)
    a.layer(x,"Tinysized_wood-twigs-break-01",.12,pitch=.75+variation*.07,limit=.13)
    if not impact:a.put(x,a.glint(.45,[147,294,587+variation*23]),at=.08,gain=.24)
    return x


def blade(variation=0, release=False):
    x=a.frames(.25 if release else .72)
    a.layer(x,"Tinysized_handcuffs-metal-lock-0"+str(1+variation%2),.48,pitch=.82+variation*.08,limit=.18)
    a.layer(x,"Tinysized_metal-knife-scrape-01",.12,at=.025,pitch=1.08+variation*.1,limit=.16)
    if not release:
        a.layer(x,"Tinysized_quiver-leather-squeeze-01",.18,at=.16,pitch=.94,limit=.25)
        a.put(x,a.glint(.34,[294,440+variation*28]),at=.20,gain=.22)
    return x


def main():
    target=a.OUT/'audio_manifest.json'
    manifest=json.loads(target.read_text(encoding='utf-8'))
    names=[f'spawn_{card}{suffix}' for card in ('thunderstorm','blade_box') for suffix in ('','_v2','_v3')]+['thunder_strike','blade_release']
    preserved={name:entry for name,entry in manifest['sounds'].items() if name not in names}
    for card,recipe,identity in [('thunderstorm',storm,'Brief rising thunder and three distinct charged air signatures'),('blade_box',blade,'Fitted metal latches, short blade scrapes and a restrained Aether chime')]:
        takes=[]
        for variation,suffix in enumerate(('','_v2','_v3')):
            name='spawn_'+card+suffix;takes.append(name)
            a.USED_SOURCES.clear();a.source.cache_clear()
            a.write(name,recipe(variation),peak=.58,rms_db=-24,role='deployment',sources=sorted(a.USED_SOURCES))
            a.MANIFEST['sounds'][name]['cardId']=card
        manifest['spawnCueProfiles'][card]={'identity':identity,'takes':takes,'trigger':'one card_play per playId; never entity_spawn'}
    for name,recipe in [('thunder_strike',lambda:storm(0,True)),('blade_release',lambda:blade(0,True))]:
        a.USED_SOURCES.clear();a.source.cache_clear()
        a.write(name,recipe(),peak=.45,rms_db=-26,sources=sorted(a.USED_SOURCES))
    for entry in a.MANIFEST['sounds'].values():entry['productionSource']='Build/generate_storm_blade_audio.py'
    manifest['sounds'].update(a.MANIFEST['sounds'])
    for name,entry in preserved.items():
        if hashlib.sha256((a.ROOT/entry['file']).read_bytes()).hexdigest()!=entry['sha256'] or manifest['sounds'][name]!=entry:
            raise ValueError('Released audio changed: '+name)
    target.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
    report={'passed':True,'preservedMasters':len(preserved),'newMasters':a.MANIFEST['sounds'],'listeningAcceptance':'Not asserted by numerical authoring','loopsAdded':0}
    out=a.ROOT/'Artifacts/QA/Storm210/Audio/production.json';out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'preservedMasters':len(preserved),'newMasters':len(names),'loopsAdded':0}))


if __name__=='__main__':main()
