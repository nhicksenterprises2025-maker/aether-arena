"""Preserve historical portrait authorship across a proved animation-only repair."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import importlib.util

ROOT = Path(__file__).resolve().parents[1]
ALLOWED = {'Assets/Source/RiftCrown_ProductionAssets.blend',
           'Assets/Export/Characters/pulverizer/AN_pulverizer_Death.fbx',
           'Assets/asset_manifest.json'}

def raw_pin(root, path):
    path = Path(path)
    path = (path if path.is_absolute() else root / path).resolve()
    relative = path.relative_to(root).as_posix()
    data = path.read_bytes()
    return {'file': relative, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

def resolved_path(root, name, overrides=None):
    path=(Path(root)/(overrides or {}).get(name,name)).resolve()
    path.relative_to(Path(root).resolve())
    return path

def resolved_pin(root, name, overrides=None):
    result=raw_pin(root,resolved_path(root,name,overrides))
    result['file']=name
    return result

def read(root, pin):
    if raw_pin(root, pin['file']) != pin:
        raise ValueError('Animation revision evidence changed: ' + pin['file'])
    return json.loads((root / pin['file']).read_text(encoding='utf-8'))

def validate_audio_append(root, revision, overrides=None):
    proof=read(root,revision['audioAppendEvidence'])
    before=read(root,revision['beforeAudioSnapshot'])
    after=json.loads(resolved_path(root,'Assets/Source/Audio/audio_manifest.json',overrides).read_text(encoding='utf-8'))
    current=resolved_pin(root,'Assets/Source/Audio/audio_manifest.json',overrides)
    old_ids=set(before['sounds']);new_ids=set(after['sounds'])-old_ids
    if not proof.get('passed') or proof.get('schema')!=1 or proof.get('retainedManifest')!=revision['beforeAudioSnapshot']['file'] or \
       proof.get('beforeSHA256')!=revision['beforeAudioSnapshot']['sha256'] or proof.get('afterSHA256')!=current['sha256'] or \
       len(old_ids)!=61 or len(after['sounds'])!=63 or new_ids!={'shield_break','shadow_surge'} or \
       set(proof.get('newSoundIds',[]))!=new_ids or proof.get('beforeSoundCount')!=61 or proof.get('afterSoundCount')!=63:
        raise ValueError('Audio append scope/source bytes differ from exact approved two-sound production')
    if {k:v for k,v in before.items() if k!='sounds'}!={k:v for k,v in after.items() if k!='sounds'} or \
       any(after['sounds'].get(k)!=v for k,v in before['sounds'].items()) or \
       not proof.get('allExistingRecordsUnchanged') or not proof.get('metadataUnchanged'):
        raise ValueError('An existing audio record or metadata changed')
    expected=[{'sound':name,'file':record['file'],'sha256':record['sha256']} for name,record in before['sounds'].items()]
    if proof.get('existingWaveMasters')!=expected:raise ValueError('Audio preservation proof omits existing masters')
    for name,record in after['sounds'].items():
        if resolved_pin(root,record['file'],overrides)['sha256']!=record['sha256']:raise ValueError('Current audio master changed: '+name)
    return current

def validate_animation_revision(root, manifest, portrait_source, overrides=None):
    root = Path(root).resolve()
    def actual_pin(name):
        return resolved_pin(root,name,overrides)
    revisions = manifest.get('sourceRevisions', [])
    if len(revisions) != 1:
        raise ValueError('An explicit single verified animation source revision is required')
    revision = revisions[0]
    if revision.get('schema') != 1 or revision.get('kind') != 'animation-only' or revision.get('changedAnimations') != [{'card':'pulverizer','clip':'Death'}]:
        raise ValueError('Unexpected portrait source revision scope')
    before = read(root, revision['beforeManifestSnapshot'])
    old = before['source']
    if old != portrait_source or revision['fromSource'] != old or revision['toSource'] != manifest['source']:
        raise ValueError('Historical render and current export sources do not match the revision')
    snapshot = raw_pin(root, revision['beforeSourceSnapshot']['file'])
    if snapshot != revision['beforeSourceSnapshot'] or any(snapshot[key] != old[key] for key in ('bytes','sha256')):
        raise ValueError('Preserved original Blender scene does not match historical render source')
    if actual_pin(manifest['source']['file']) != manifest['source']:
        raise ValueError('Current Blender source differs from repaired source pin')
    repair = read(root, revision['repairEvidence'])
    if not repair.get('passed') or repair.get('unexpectedChanges') or set(repair.get('changedFiles',[])) != ALLOWED:
        raise ValueError('Repair did not prove the exact permitted changes')
    signature = repair.get('geometrySignatureBefore')
    if not isinstance(signature,str) or len(signature) != 64 or signature != repair.get('geometrySignatureAfter'):
        raise ValueError('Mesh, UV, weight, material and rest-bone signatures changed')
    if repair['sourceBlend'] != manifest['source']:
        raise ValueError('Repair evidence describes a different production scene')
    expected = copy.deepcopy(before)
    expected['source'] = manifest['source']
    old_clip = next(clip for clip in before['characters']['pulverizer']['animations'] if clip['name']=='Death')
    new_clip = next(clip for clip in manifest['characters']['pulverizer']['animations'] if clip['name']=='Death')
    if new_clip != repair['deathClip'] or new_clip['file'] != old_clip['file']:
        raise ValueError('Repaired Death clip differs from the actual export evidence')
    expected['characters']['pulverizer']['animations'] = [new_clip if clip['name']=='Death' else clip
                                                         for clip in expected['characters']['pulverizer']['animations']]
    expected['sourceRevisions'] = revisions
    if expected != manifest:
        raise ValueError('A model, material, portrait or other animation changed beyond the exact source repair')
    pins = read(root, revision['retainedAssetInventory'])
    retained = {name: pin for name,pin in pins.items() if name not in ALLOWED}
    if len(retained) != repair['unchangedAssetFiles'] or revision['retainedAssetFiles'] != len(retained):
        raise ValueError('Retained asset inventory does not match the real repair count')
    verified = 0
    for name, pin in retained.items():
        # Balance documents can change after a visual repair. All production
        # geometry, material, portrait, scene and render inputs remain pinned.
        if name.startswith(('Assets/Source/','Assets/Export/','Assets/Renders/')):
            if actual_pin(name) != pin:
                raise ValueError('Retained render/model asset changed: ' + name)
            verified += 1
    for label,pin in revision['producerScripts'].items():
        if actual_pin(pin['file']) != pin:
            raise ValueError('Repair producer script changed: ' + label)
    matrices = read(root, revision['matrixSafetyEvidence'])
    current = {(card_id,clip['name']):clip for card_id,card in manifest['characters'].items() for clip in card['animations']}
    actual = {(clip['card'],clip['clip']):clip for clip in matrices.get('clips',[])}
    if not matrices.get('passed') or matrices.get('errors') or matrices.get('sourceBlendSHA256') != manifest['source']['sha256'] or set(actual) != set(current):
        raise ValueError('All-clip quarter-frame safety evidence is incomplete or stale')
    for key,clip in current.items():
        sampled = actual[key]
        if sampled['samples'] != 4*(clip['frames']-1)+1 or sampled['fbx']['sha256'] != clip['sha256'] or sampled['fbx']['unsafeScaleValues'] or sampled['fbx']['nonfiniteElements']:
            raise ValueError('Source/FBX quarter-frame proof is stale or unsafe: ' + str(key))
    original_fbx = matrices.get('regressionSnapshot', {})
    original_snapshot = revision.get('beforeClipSnapshot')
    original_snapshot = raw_pin(root, original_snapshot['file']) if original_snapshot else raw_pin(root, original_fbx['file'])
    if original_fbx.get('sha256') != old_clip['sha256'] or not original_fbx.get('unsafeScaleValues') or original_snapshot['sha256'] != old_clip['sha256'] or original_snapshot['bytes'] != old_clip['bytes'] or (revision.get('beforeClipSnapshot') and original_snapshot != revision['beforeClipSnapshot']):
        raise ValueError('Original failed Death export is missing or does not reproduce singular scaling')
    reproduced = read(root, revision['producerReproducibilityEvidence'])
    if not reproduced.get('passed') or reproduced.get('samples') != 4*(new_clip['frames']-1)+1 or reproduced.get('sourceBlendSHA256') != manifest['source']['sha256'] or reproduced.get('builderSHA256') != revision['producerScripts']['expansion_asset_models']['sha256'] or reproduced.get('maxAbsoluteMatrixDelta',1) > 1e-7:
        raise ValueError('Saved Death poses do not reproduce the current source builder at every quarter frame')
    return {'historicalRenderSource':old,'currentExportSource':manifest['source'],
            'originalSourceSnapshot':snapshot,'retainedAssetFilesAtRevision':len(retained),
            'currentRenderAssetPinsVerified':verified,'allClipQuarterFrameSamples':matrices['sourceSamples'],
            'poseMatricesChecked':matrices['poseMatricesChecked']}

def validate_revision(root,manifest,portrait_source,overrides=None):
    """Validate each historical repair and strictly additive source production.

    Preserved intermediary bytes are used only for their exact historical stage;
    the final stage always checks current raw models, portraits and source scene.
    """
    root=Path(root).resolve();revisions=manifest.get('sourceRevisions',[])
    if len(revisions)==3:
        if overrides:raise ValueError('Final art-polish stage must check current raw assets')
        return validate_art_revision(root,manifest,portrait_source)
    if len(revisions)==1:return validate_animation_revision(root,manifest,portrait_source,overrides)
    if len(revisions)!=2:raise ValueError('Expected the exact animation repair plus approved additive production')
    def actual_pin(name):return resolved_pin(root,name,overrides)
    addition=revisions[1]
    characters={'bone_warden','bone_warden_skeleton','aether_reaper','iron_giant'}
    cards={'bone_warden','aether_reaper','iron_giant','shadow_surge'}
    statics={'iron_shield_fragment','shadow_surge_wisp','shadow_surge_ribbon'}
    if addition.get('schema')!=1 or addition.get('kind')!='additive-model-production' or \
       set(addition.get('addedCharacters',[]))!=characters or set(addition.get('addedPlayableCards',[]))!=cards or \
       set(addition.get('addedStatics',[]))!=statics:
        raise ValueError('Unapproved additive source revision scope')
    before=read(root,addition['beforeManifestSnapshot'])
    if before['source']!=addition['fromSource'] or manifest['source']!=addition['toSource'] or before.get('sourceRevisions')!=revisions[:1]:
        raise ValueError('Additive source chain does not connect the exact accepted scene')
    snapshot=raw_pin(root,addition['beforeSourceSnapshot']['file'])
    if snapshot!=addition['beforeSourceSnapshot'] or any(snapshot[k]!=before['source'][k] for k in ('bytes','sha256')):
        raise ValueError('Preserved accepted pre-addition Blender scene changed')
    old_portraits=read(root,addition['beforePortraitSnapshot'])
    if old_portraits.get('sourceBlend')!=portrait_source:
        raise ValueError('Historical portrait report was relabelled')
    audio=validate_audio_append(root,addition,overrides)
    historical_overrides=dict(overrides or {})
    historical_overrides.update({
        before['source']['file']:snapshot['file'],
        'Assets/Source/CardArt/model_portraits.json':addition['beforePortraitSnapshot']['file'],
        'Assets/Source/Audio/audio_manifest.json':addition['beforeAudioSnapshot']['file']})
    historical=validate_animation_revision(root,before,portrait_source,historical_overrides)
    if actual_pin(manifest['source']['file'])!=manifest['source']:raise ValueError('Current additive source pin changed')
    proof=read(root,addition['productionEvidence'])
    if not proof.get('passed') or proof.get('kind')!='additive-model-production' or \
       proof.get('fromSource')!=before['source'] or proof.get('toSource')!=manifest['source'] or \
       set(proof.get('addedCharacters',[]))!=characters or set(proof.get('addedStatics',[]))!=statics or \
       not proof.get('before',{}).get('objects') or proof.get('before')!=proof.get('after'):
        raise ValueError('Actual old geometry/rest-joint/action preservation proof is incomplete')
    for key in ('producer','modelBuilder','verifier'):
        if actual_pin(proof[key]['file'])!=proof[key]:raise ValueError('Additive producer changed: '+key)
    read(root,proof['retainedOriginalGenerationEvidence'])
    if proof.get('verificationMethod')!='Independent read-only comparison of the exact preserved accepted source and exact final source':
        raise ValueError('Missing real read-only accepted-to-final saved-scene proof')
    expected=copy.deepcopy(before)
    expected['source']=manifest['source'];expected['sourceRevisions']=revisions
    for category,ids in (('characters',characters),('statics',statics),('cards',cards),('illustrations',cards)):
        if set(manifest[category])-set(before[category])!=ids:raise ValueError('Unexpected '+category+' additions')
        for name,record in before[category].items():
            if manifest[category].get(name)!=record:raise ValueError('Existing '+category+' record changed: '+name)
        expected[category].update({name:manifest[category][name] for name in ids})
    expected['entities']={'bone_warden_skeleton':{'skeletal':'bone_warden_skeleton','deckCard':False,'parentCard':'bone_warden'}}
    old_materials=before.get('expansionMaterials',[]);new_materials=manifest.get('expansionMaterials',[])
    if new_materials[:len(old_materials)]!=old_materials or {m['name'] for m in new_materials[len(old_materials):]}!={
        'M_RiftExpansionAscensionSurface','M_RiftExpansionAscensionGlow','M_RiftExpansionAscensionGlass'}:
        raise ValueError('Accepted material palette changed or unapproved new material was added')
    expected['expansionMaterials']=new_materials
    if manifest.get('presentationMaterials'):
        declared=manifest['presentationMaterials']
        if len(declared)!=1 or declared[0].get('name')!='M_RiftExpansionFX':raise ValueError('Unapproved presentation shader additions')
        producer=declared[0]['contractProducer']
        if producer.get('file')!='Build/generate_expansion_fx_contract.py' or actual_pin(producer['file'])!=producer:
            raise ValueError('Dedicated FX contract producer changed')
        spec=importlib.util.spec_from_file_location('approved_fx_contract',root/producer['file'])
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        class HistoricalRoot:
            def __truediv__(self,name):return resolved_path(root,name,overrides)
        definition_root=HistoricalRoot() if overrides else root
        if declared!=[module.definition(definition_root)]:raise ValueError('Dedicated FX shader source/opacity/parameters changed')
        expected['presentationMaterials']=declared
    if expected!=manifest:raise ValueError('Manifest changed outside exact new asset/binding scope')
    inventory=read(root,addition['retainedAssetInventory']);verified=0
    permitted={before['source']['file'],'Assets/Source/CardArt/model_portraits.json','Assets/Source/Audio/audio_manifest.json'}
    for name,record in inventory.items():
        if name in permitted:continue
        if actual_pin(name)!=record:raise ValueError('Accepted source/export/portrait byte changed: '+name)
        verified+=1
    if verified!=addition['retainedAssetFiles']:raise ValueError('Incorrect retained asset count')
    portraits=json.loads(resolved_path(root,'Assets/Source/CardArt/model_portraits.json',overrides).read_text(encoding='utf-8'))
    preserved=copy.deepcopy(portraits);preserved['cards']={name:record for name,record in preserved['cards'].items() if name not in cards}
    if preserved!=old_portraits:raise ValueError('An accepted portrait record or historical producer pin changed')
    for name in cards:
        if portraits['cards'][name].get('sourceSceneSHA256')!=manifest['source']['sha256']:
            raise ValueError('New portrait did not originate from final new source: '+name)
    for name in characters:
        record=manifest['characters'][name]
        for item in [record['mesh'],*record['lods'],*record['animations'],*record.get('identityViews',[])]:
            actual=actual_pin(item['file'])
            if any(actual[k]!=item[k] for k in ('file','bytes','sha256')):raise ValueError('Current new production pin changed: '+item['file'])
        sheet=record['identitySheet']
        if actual_pin(sheet['file'])!=sheet:raise ValueError('Current new identity sheet changed: '+name)
    for name in statics:
        record=manifest['statics'][name]
        for item in [record,*record['lods']]:
            actual=actual_pin(item['file'])
            if any(actual[k]!=item[k] for k in ('file','bytes','sha256')):raise ValueError('Current new effect mesh changed: '+item['file'])
    textures=addition.get('newTextureSources',[])
    expected_textures={'Assets/Source/Textures/T_RiftAscensionAtlas_'+c+'.png' for c in ('BaseColor','Normal','ORM','TeamMask')}
    if {p['file'] for p in textures}!=expected_textures:raise ValueError('Incomplete new material texture inventory')
    for item in textures:
        if actual_pin(item['file'])!=item:raise ValueError('Current new atlas pixels changed: '+item['file'])
    for item in [*manifest['cards']['shadow_surge']['identityViews'],manifest['cards']['shadow_surge']['identitySheet']]:
        actual=actual_pin(item['file'])
        if any(actual[k]!=item[k] for k in ('file','bytes','sha256')):raise ValueError('Current spell identity proof changed')
    contracts=read(root,addition['animationContractEvidence'])
    if not contracts.get('passed') or contracts.get('sourceBlend',{}).get('sha256')!=manifest['source']['sha256'] or \
       contracts.get('modelBuilderSHA256')!=proof['modelBuilder']['sha256'] or \
       set(contracts.get('cards',{}))!=characters or contracts.get('errors') or \
       any(c.get('clipCount')!=9 or c.get('maximumReproductionDelta',1)>1e-7 for c in contracts['cards'].values()):
        raise ValueError('All new saved quarter-frame poses do not reproduce the current source builder')
    matrices=read(root,addition['matrixSafetyEvidence'])
    clips={(name,c['name']):c for name,r in manifest['characters'].items() for c in r['animations']}
    sampled={(c['card'],c['clip']):c for c in matrices.get('clips',[])}
    if not matrices.get('passed') or matrices.get('errors') or matrices.get('sourceBlendSHA256')!=manifest['source']['sha256'] or set(sampled)!=set(clips):
        raise ValueError('Final all-clip matrix/FBX safety proof incomplete')
    for key,c in clips.items():
        sample=sampled[key]
        if sample['samples']!=4*(c['frames']-1)+1 or sample['fbx']['sha256']!=c['sha256'] or sample['fbx']['unsafeScaleValues'] or sample['fbx']['nonfiniteElements']:
            raise ValueError('Final source/export safety pin changed: '+str(key))
    for name in cards:
        record=portraits['cards'][name]
        for item in [record,record['master'],record['editableScene'],record['smallReview'],record['rendererScript']]:
            actual=actual_pin(item['file'])
            if any(actual[k]!=item[k] for k in ('file','bytes','sha256')):raise ValueError('Current new portrait/producer pin changed: '+item['file'])
    return {'historicalAnimationRepair':historical,'historicalRenderSource':portrait_source,
            'preservedAcceptedSource':snapshot,'currentExportSource':manifest['source'],
            'currentRetainedAssetPinsVerified':verified,'addedCharacters':sorted(characters),'addedPlayableCards':sorted(cards),
            'approvedAudioAppend':audio,'allClipQuarterFrameSamples':matrices['sourceSamples']}

def validate_art_revision(root,manifest,portrait_source):
    path=Path(__file__).with_name('finalize_art_polish_provenance.py')
    spec=importlib.util.spec_from_file_location('art_polish_provenance',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module.validate_art_revision(Path(root),manifest,portrait_source)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repair-dir',type=Path,required=True)
    args=parser.parse_args()
    directory=args.repair_dir.resolve()
    directory.relative_to(ROOT/'Artifacts/QA')
    path=ROOT/'Assets/asset_manifest.json'
    manifest=json.loads(path.read_text(encoding='utf-8'))
    before=json.loads((directory/'before/Assets/asset_manifest.json').read_text(encoding='utf-8'))
    repair=json.loads((directory/'repair.json').read_text(encoding='utf-8'))
    history=ROOT/'Assets/Source/Revisions/PulverizerDeath'
    history.mkdir(parents=True,exist_ok=True)
    files={'beforeSourceSnapshot':(directory/'before'/before['source']['file'],'OriginalSource.blend'),
           'beforeManifestSnapshot':(directory/'before/Assets/asset_manifest.json','OriginalManifest.json'),
           'retainedAssetInventory':(directory/'before-pins.json','RetainedAssetInventory.json'),
           'repairEvidence':(directory/'repair.json','Repair.json'),
           'matrixSafetyEvidence':(directory/'matrix-safety.json','MatrixSafety.json'),
           'producerReproducibilityEvidence':(directory/'producer-reproducibility.json','ProducerReproducibility.json')}
    old_clip=next(clip for clip in before['characters']['pulverizer']['animations'] if clip['name']=='Death')
    files['beforeClipSnapshot']=(directory/'before'/old_clip['file'],'OriginalDeath.fbx')
    copied={}
    for key,(source,name) in files.items():
        destination=history/name
        if destination.exists() and destination.read_bytes()!=source.read_bytes():
            raise ValueError('Preserved revision evidence cannot be overwritten: '+name)
        if not destination.exists():shutil.copyfile(source,destination)
        copied[key]=raw_pin(ROOT,destination)
    revision={'schema':1,'kind':'animation-only','changedAnimations':[{'card':'pulverizer','clip':'Death'}],
              'fromSource':before['source'],'toSource':manifest['source'],
              'retainedAssetFiles':repair['unchangedAssetFiles'],
              'producerScripts':{name:raw_pin(ROOT,'Build/'+name+'.py') for name in
                                 ('expansion_asset_models','repair_expansion_animation_scales','validate_animation_matrices')}}
    revision.update(copied)
    manifest['sourceRevisions']=[revision]
    portraits=json.loads((ROOT/'Assets/Source/CardArt/model_portraits.json').read_text(encoding='utf-8'))
    evidence=validate_revision(ROOT,manifest,portraits['sourceBlend'])
    path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps({'passed':True,'verifiedRevision':evidence}))

if __name__=='__main__':main()
