"""Pin approved additive source evidence without relabelling any old portrait."""
from pathlib import Path
import argparse,json,shutil
from portrait_source_revision import raw_pin,validate_revision

ROOT=Path(__file__).resolve().parents[1]
HISTORY=ROOT/'Assets/Source/Revisions/ApprovedExpansion'
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--contracts',type=Path,required=True)
    parser.add_argument('--matrices',type=Path,required=True)
    args=parser.parse_args()
    path=ROOT/'Assets/asset_manifest.json';m=json.loads(path.read_text(encoding='utf-8'))
    old=json.loads((HISTORY/'PreviousManifest.json').read_text(encoding='utf-8'))
    if m.get('sourceRevisions')!=old['sourceRevisions']:raise ValueError('Additive revision already present or old history changed')
    copied={}
    for label,source,name in (('animationContractEvidence',args.contracts,'AnimationContracts.json'),
                              ('matrixSafetyEvidence',args.matrices,'MatrixSafety.json')):
        source=source.resolve();source.relative_to(ROOT/'Artifacts/QA');target=HISTORY/name
        if target.exists() and target.read_bytes()!=source.read_bytes():raise ValueError('Immutable tracked evidence already exists')
        if not target.exists():shutil.copyfile(source,target)
        copied[label]=raw_pin(ROOT,target)
    inventory=json.loads((HISTORY/'RetainedAssetInventory.json').read_text(encoding='utf-8'))
    allowed={old['source']['file'],'Assets/Source/CardArt/model_portraits.json','Assets/Source/Audio/audio_manifest.json'}
    revision={'schema':1,'kind':'additive-model-production','fromSource':old['source'],'toSource':m['source'],
              'addedCharacters':['bone_warden','bone_warden_skeleton','aether_reaper','iron_giant'],
              'addedPlayableCards':['bone_warden','aether_reaper','iron_giant','shadow_surge'],
              'addedStatics':['iron_shield_fragment','shadow_surge_wisp','shadow_surge_ribbon'],
              'retainedAssetFiles':len(set(inventory)-allowed),
              'newTextureSources':[raw_pin(ROOT,'Assets/Source/Textures/T_RiftAscensionAtlas_'+c+'.png')
                                   for c in ('BaseColor','Normal','ORM','TeamMask')]}
    for label,name in {'beforeManifestSnapshot':'PreviousManifest.json','beforeSourceSnapshot':'PreviousSource.blend',
                       'beforePortraitSnapshot':'PreviousPortraits.json','retainedAssetInventory':'RetainedAssetInventory.json',
                       'productionEvidence':'VerifiedModelAdditions.json','audioAppendEvidence':'AudioAppend.json',
                       'beforeAudioSnapshot':'PreExpansionAudioManifest.json'}.items():
        revision[label]=raw_pin(ROOT,HISTORY/name)
    revision.update(copied);m['sourceRevisions'].append(revision)
    portraits=json.loads((ROOT/'Assets/Source/CardArt/model_portraits.json').read_text(encoding='utf-8'))
    result=validate_revision(ROOT,m,portraits['sourceBlend'])
    path.write_text(json.dumps(m,indent=2),encoding='utf-8')
    print(json.dumps({'passed':True,'revision':result}))
if __name__=='__main__':main()
