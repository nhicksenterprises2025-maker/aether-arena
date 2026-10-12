"""Append/check the approved visual-only FX material contract without art writes."""
from pathlib import Path
import argparse,hashlib,json

ROOT=Path(__file__).resolve().parents[1]
def pin(root,path):
    p=root/path;b=p.read_bytes();return {'file':path,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def definition(root):
    return {'name':'M_RiftExpansionFX','scope':'Approved expansion ribbon/wisp geometry only',
        'blendMode':'Translucent','shadingModel':'DefaultLit','twoSided':True,
        'color':{'parameter':'EffectColor','default':[.37,.08,.66],'minimum':0.,'maximum':1.,'usesAtlasRGB':False},
        'opacity':{'parameter':'EffectOpacity','default':.55,'minimum':0.,'maximum':.65,'atlasChannel':'A',
                   'atlasSource':pin(root,'Assets/Source/Textures/T_RiftAscensionAtlas_BaseColor.png')},
        'emission':{'parameter':'EffectEmission','default':.25,'minimum':0.,'maximumPerChannel':.45},
        'timeSource':'Native simulation-clock geometry only; no material Time',
        'shaderProducer':pin(root,'Build/import_unreal_assets.py'),
        'contractProducer':pin(root,'Build/generate_expansion_fx_contract.py')}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--write',action='store_true');args=parser.parse_args()
    p=ROOT/'Assets/asset_manifest.json';m=json.loads(p.read_text(encoding='utf-8'));expected=[definition(ROOT)]
    if args.write:
        if m.get('presentationMaterials') and [e['name'] for e in m['presentationMaterials']]!=['M_RiftExpansionFX']:
            raise ValueError('Cannot replace another presentation material contract')
        m['presentationMaterials']=expected;p.write_text(json.dumps(m,indent=2),encoding='utf-8')
    elif m.get('presentationMaterials')!=expected:raise ValueError('Dedicated expansion FX contract is missing or stale')
    print('RIFT_EXPANSION_FX_CONTRACT', 'WRITTEN' if args.write else 'PASS')
if __name__=='__main__':main()
