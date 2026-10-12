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
    if len(revisions)==4:
        if overrides:raise ValueError('Final targeted replacement must check current raw assets')
        return validate_targeted_replacement(root,manifest,portrait_source)
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


def validate_targeted_replacement(root,manifest,portrait_source):
    """Qualify unchanged historical artwork across the explicit Iron revision.

    The previous three accepted records retain their original source attribution.
    Their actual accepted report, saved scene and raw assets qualify reuse; only
    the current Iron mesh/poses/portrait receive new production claims.
    """
    root=Path(root).resolve();revisions=manifest['sourceRevisions'];stage=revisions[3]
    if stage.get('schema')!=1 or stage.get('kind')!='targeted-model-replacement' or \
       stage.get('version')!='1.7.0' or stage.get('changedCharacters')!=['iron_giant'] or \
       stage.get('changedPortraits')!=['iron_giant']:
        raise ValueError('Unapproved targeted replacement scope')
    before=read(root,stage['beforeManifestSnapshot']);old_portraits=read(root,stage['beforePortraitSnapshot'])
    if before.get('sourceRevisions')!=revisions[:3] or [r.get('kind') for r in revisions[:3]]!=[
            'animation-only','additive-model-production','art-polish'] or \
       stage['fromSource']!=before['source'] or stage['toSource']!=manifest['source'] or \
       revisions[2]['toSource']!=before['source'] or old_portraits.get('sourceBlend')!=portrait_source:
        raise ValueError('Targeted replacement changed or relabelled the three historical source stages')
    saved=raw_pin(root,stage['beforeSourceSnapshot']['file'])
    if saved!=stage['beforeSourceSnapshot'] or any(saved[k]!=before['source'][k] for k in ('bytes','sha256')):
        raise ValueError('Preserved accepted pre-replacement Source changed')
    accepted=read(root,stage['historicalAcceptance'])
    old_manifest_pin=accepted.get('afterFinalizerManifest',{})
    if accepted.get('passed') is not True or any(old_manifest_pin.get(k)!=stage['beforeManifestSnapshot'][k] for k in ('bytes','sha256')):
        raise ValueError('Historical acceptance does not bind the exact preserved final manifest')
    for old in revisions[:3]:
        def historical_snapshots(value):
            if isinstance(value,dict):
                if {'file','bytes','sha256'}<=set(value) and value['file'].startswith('Assets/Source/Revisions/'):
                    if raw_pin(root,value['file'])!={k:value[k] for k in ('file','bytes','sha256')}:
                        raise ValueError('Historical tracked evidence changed: '+value['file'])
                for child in value.values():historical_snapshots(child)
            elif isinstance(value,list):
                for child in value:historical_snapshots(child)
        historical_snapshots(old)
    comparison=read(root,stage['savedSceneComparison'])
    if comparison.get('passed') is not True or comparison.get('kind')!='independent-saved-scene-selected-replacement' or \
       comparison['fromSource']!=saved or comparison['toSource']!=manifest['source'] or \
       comparison.get('unchangedBefore')!=comparison.get('unchangedAfter') or \
       comparison.get('keptObjects')!=73 or comparison.get('keptActions')!=167 or \
       len(comparison['unchangedBefore']['signatures']['objects'])!=73 or \
       len(comparison['unchangedBefore']['signatures']['actions'])!=167:
        raise ValueError('Actual saved non-Iron geometry/rest-joint/action/material comparison changed')
    for evidence_key,snapshot_key in (('verifier','comparisonVerifierSnapshot'),):
        snapshot=stage[snapshot_key]
        if raw_pin(root,snapshot['file'])!=snapshot or any(snapshot[k]!=comparison[evidence_key][k] for k in ('bytes','sha256')):
            raise ValueError('Saved Source comparison verifier snapshot changed')
    if raw_pin(root,manifest['source']['file'])!=manifest['source']:
        raise ValueError('Current targeted Source raw pin changed')
    for record in stage['producerScripts'].values():
        if raw_pin(root,record['file'])!=record:raise ValueError('Current targeted producer changed: '+record['file'])
    expected=copy.deepcopy(before);expected['source']=manifest['source'];expected['sourceRevisions']=revisions
    expected['characters']['iron_giant']=manifest['characters']['iron_giant']
    expected['illustrations']['iron_giant']=manifest['illustrations']['iron_giant']
    old_renders={item['file']:item for item in before['renders']};new_renders={item['file']:item for item in manifest['renders']}
    selected_render='Assets/Renders/iron_giant.png'
    if len(new_renders)!=len(manifest['renders']) or set(old_renders)!=set(new_renders) or \
       any(old_renders[n]!=new_renders[n] for n in old_renders if n!=selected_render) or \
       [r for r in before['renders'] if r['file']!=selected_render]!=[r for r in manifest['renders'] if r['file']!=selected_render]:
        raise ValueError('A non-Iron general render or render inventory changed')
    expected['renders']=manifest['renders']
    if manifest.get('presentationMaterials')!=before.get('presentationMaterials'):
        validate_import_binding_qualification(root,manifest,stage)
        fx_script=root/'Build/generate_expansion_fx_contract.py'
        if raw_pin(root,fx_script)!=revisions[2]['producerScripts']['FXContract']:
            raise ValueError('The retained dedicated FX contract producer changed')
        spec=importlib.util.spec_from_file_location('targeted_fx_contract',fx_script)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        if manifest['presentationMaterials']!=[module.definition(root)]:
            raise ValueError('Current FX declaration does not equal its actual retained definition')
        expected['presentationMaterials']=manifest['presentationMaterials']
    if expected!=manifest:raise ValueError('A non-Iron source/model/portrait/binding/material record changed')
    portraits=json.loads((root/'Assets/Source/CardArt/model_portraits.json').read_text(encoding='utf-8'))
    expected_portraits=copy.deepcopy(old_portraits);expected_portraits['cards']['iron_giant']=portraits['cards']['iron_giant']
    if portraits!=expected_portraits:raise ValueError('A non-Iron portrait or historical attribution changed')
    if portraits['cards']['iron_giant'].get('sourceSceneSHA256')!=manifest['source']['sha256']:
        raise ValueError('Current Iron portrait does not identify the actual current Source')
    def verify_records(value):
        if isinstance(value,dict):
            if {'file','bytes','sha256'}<=set(value):
                if raw_pin(root,value['file'])!={k:value[k] for k in ('file','bytes','sha256')}:
                    raise ValueError('Current targeted raw file changed: '+value['file'])
            for child in value.values():verify_records(child)
        elif isinstance(value,list):
            for child in value:verify_records(child)
    verify_records(manifest['characters']['iron_giant']);verify_records(portraits['cards']['iron_giant'])
    g=manifest['characters']['iron_giant']
    if g!=read(root,stage['currentModelRecord']) or any(manifest['illustrations']['iron_giant'].get(k)!=
            portraits['cards']['iron_giant'].get(k) for k in ('file','bytes','sha256','sourceSceneSHA256')):
        raise ValueError('Current produced Iron model record or portrait binding changed')
    if g['boneCount']!=34 or len(g['lods'])!=3 or {c['name'] for c in g['animations']}!={
            'Idle','Locomotion','Attack','Hit','Death','Deploy','Status','Turn','Acquire'} or \
       g['modelDesign']['baseModelScale']!=1.65 or any(c['fps']!=100 for c in g['animations']):
        raise ValueError('Current Iron rig, scale, LOD or nine-clip contract changed')
    proof=read(root,stage['generationPreservation'])
    if proof.get('passed') is not True or proof.get('replacedCharacters')!=['iron_giant'] or \
       proof.get('fromSource')!=before['source'] or proof.get('before')!=proof.get('after') or \
       proof.get('materialsBefore')!=proof.get('materialsAfter') or \
       proof.get('untouchedAssetsBefore')!=proof.get('untouchedAssetsAfter'):
        raise ValueError('Original generation preservation proof changed')
    final_generation=read(root,stage['finalGenerationPreservation'])
    used=stage['generationProducerUsedSnapshot']
    recorded=stage['recordedGenerationProducerSnapshot']
    if raw_pin(root,used['file'])!=used or final_generation.get('passed') is not True or \
       final_generation.get('toSource')!=manifest['source'] or final_generation.get('replacedCharacters')!=['iron_giant'] or \
       raw_pin(root,recorded['file'])!=recorded or any(final_generation['producer'][k]!=recorded[k] for k in ('bytes','sha256')) or \
       final_generation['modelBuilder']!=stage['producerScripts']['modelBuilder'] or \
       final_generation.get('before')!=final_generation.get('after'):
        raise ValueError('Actual final generation producer/source/preservation snapshot changed')
    recorded_bytes=(root/used['file']).read_bytes().replace(
        b'bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=bool(opts.replacement_proof))',
        b'if opts.replacement_proof:bpy.ops.wm.save_as_mainfile(filepath=str(blend),compress=True)\nelse:bpy.ops.wm.save_as_mainfile(filepath=str(blend))')
    if (root/recorded['file']).read_bytes()!=recorded_bytes or stage.get('producerPinQualification')!=\
       'The recorded end-of-run producer differs only in the save-call branch; replacement_proof was true, so both branches save with compress=True. Both exact byte versions are retained. Current pose/signature helpers are unchanged.':
        raise ValueError('Generation producer pin qualification changed beyond the exact equivalent save call')
    # The final metadata fix preserves unused Glass declarations. It changes no
    # geometry, pose solver or saved-scene signature implementation; the exact
    # actually used producer remains archived rather than relabelled as fresh.
    import ast
    def helper_asts(path):
        tree=ast.parse(path.read_text(encoding='utf-8'))
        names={'set_world_rotation','aim_leg','aim_arm','source_signatures','replacement_material_signatures'}
        return {n.name:ast.dump(n,include_attributes=False) for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names}
    if helper_asts(root/used['file'])!=helper_asts(root/stage['producerScripts']['generator']['file']):
        raise ValueError('Current producer changed a measured pose/signature helper')
    permitted={manifest['source']['file'],'Assets/Source/CardArt/model_portraits.json',
               'Assets/Renders/Expansion/approved-portrait-production.json',
               'Assets/Renders/Expansion/Identity/approved-identity-sheets.json',selected_render}
    def collect_paths(value):
        if isinstance(value,dict):
            if {'file','bytes','sha256'}<=set(value):permitted.add(value['file'])
            for child in value.values():collect_paths(child)
        elif isinstance(value,list):
            for child in value:collect_paths(child)
    for model in (before['characters']['iron_giant'],g,old_portraits['cards']['iron_giant'],portraits['cards']['iron_giant']):collect_paths(model)
    kept=0
    for name,record in proof['untouchedAssetsBefore'].items():
        # Balance Data is independently versioned by the gameplay update. This
        # stage covers authored Source, exports and rendered artwork only.
        if name in permitted or name.startswith('Assets/Source/Revisions/DesignUpdate1.7.0/') or \
           not name.startswith(('Assets/Source/','Assets/Export/','Assets/Renders/')):continue
        if raw_pin(root,name)!={'file':name,**record}:raise ValueError('A non-Iron authored asset changed: '+name)
        kept+=1
    quality=read(root,stage['qualityEvidence']);exports=read(root,stage['exportEvidence'])
    if quality.get('passed') is not True or quality.get('errors') or quality.get('sourceBlend')!=manifest['source'] or \
       set(quality.get('cards',{}))!={'iron_giant'} or quality['cards']['iron_giant']['clipCount']!=9 or \
       quality['cards']['iron_giant']['maximumReproductionDelta']>1e-7 or \
       exports.get('passed') is not True or exports.get('errors') or exports.get('source')!=manifest['source'] or \
       len(exports.get('exports',[]))!=4:
        raise ValueError('Current Iron source/pose/export evidence failed or binds a different Source')
    if quality['producerPins']['expansion_new_models']!=stage['producerScripts']['modelBuilder'] or \
       any(quality['producerPins']['generate_assets'][k]!=used[k] for k in ('bytes','sha256')) or \
       quality['verifier']!=stage['producerScripts']['qualityValidator'] or \
       any(stage['exportVerifierSnapshot'][k]!=exports['verifier'][k] for k in ('bytes','sha256')) or \
       raw_pin(root,stage['exportVerifierSnapshot']['file'])!=stage['exportVerifierSnapshot']:
        raise ValueError('Current accepted pose/export evidence producer changed')
    for record in exports['exports']:
        actual=raw_pin(root,record['file'])
        if any(actual[k]!=record[k] for k in ('file','bytes','sha256')):raise ValueError('Actual exported Iron LOD changed')
    old_matrix=read(root,revisions[2]['matrixSafetyEvidence']);old_quality=read(root,revisions[2]['artQualityEvidence'])
    if not old_matrix.get('passed') or old_matrix.get('sourceBlendSHA256')!=before['source']['sha256'] or \
       not old_quality.get('passed') or old_quality.get('sourceBlend')!=before['source']:
        raise ValueError('Historical matrix/geometry acceptance binds a different preserved Source')
    for key,current_path,old_pin in [('portrait','Assets/Renders/Expansion/approved-portrait-production.json',stage['beforeRendererSummary']),
                                    ('identity','Assets/Renders/Expansion/Identity/approved-identity-sheets.json',stage['beforeIdentitySummary'])]:
        old=read(root,old_pin);current=json.loads((root/current_path).read_text(encoding='utf-8'));wanted=copy.deepcopy(old)
        field='cards' if key=='portrait' else 'sheets'
        wanted[field]['iron_giant']=current[field]['iron_giant']
        wanted['selectedReplacements']=current.get('selectedReplacements')
        if current!=wanted or len(current.get('selectedReplacements',[]))!=1 or \
           current['selectedReplacements'][0].get('cards')!=['iron_giant'] or \
           current['selectedReplacements'][0].get('sourceBlend')!=manifest['source']:
            raise ValueError('Mixed '+key+' registry relabelled or discarded unchanged historical records')
        if key=='portrait':
            run=read(root,current['selectedReplacements'][0]['run'])
            if run['sourceBlend']!=manifest['source'] or set(run['cards'])!={'iron_giant'} or run['cards']['iron_giant']!=portraits['cards']['iron_giant']:
                raise ValueError('Actual selected Iron portrait run differs from the merged registry')
        else:
            if current['sheets']['iron_giant']!=g['identitySheet']:raise ValueError('Current Iron identity sheet differs from registry')
    return {'kind':'targeted-model-replacement','changedCharacters':['iron_giant'],'historicalStagesPreserved':3,
            'historicalAcceptedManifest':stage['beforeManifestSnapshot'],'historicalRenderSource':portrait_source,
            'currentExportSource':manifest['source'],'actualSavedObjectsUnchanged':73,'actualSavedActionsUnchanged':167,
            'retainedAuthoredAssetsVerified':kept,'currentIronSourceChecks':quality['checkCount'],
            'currentIronExportChecks':exports['checkCount'],'unchangedSourceChecksQualified':True,
            'scope':'Only Iron source/poses/portrait are newly produced; all other original image/source attributions remain exact'}


def validate_import_binding_qualification(root,manifest,stage):
    """Advance whole-file binding pins while proving shader source stayed exact."""
    import ast
    ledger=read(root,stage['importBindingQualification'])
    if ledger.get('schema')!=1 or ledger.get('version')!='1.7.0' or \
       ledger.get('kind')!='health-and-source-stride-import-binding' or ledger.get('passed') is not True or \
       set(ledger.get('producerChanges',{}))!={'importer','validator'}:
        raise ValueError('Unapproved importer binding qualification')
    frozen=read(root,ledger['beforeManifest'])
    expected=copy.deepcopy(frozen)
    expected['presentationMaterials']=manifest['presentationMaterials']
    expected['sourceRevisions'][3]['importBindingQualification']=stage['importBindingQualification']
    expected['sourceRevisions'][3]['producerScripts']['revisionValidator']=stage['producerScripts']['revisionValidator']
    if expected!=manifest:raise ValueError('Binding advance changed Source, model, artwork or another record')
    for label,allowed in (('importer',['animation_contract_properties','card_data']),('validator',['main'])):
        change=ledger['producerChanges'][label];snapshot=change['oldSnapshot'];current=change['currentWholeFile']
        if raw_pin(root,snapshot['file'])!=snapshot or raw_pin(root,current['file'])!=current or \
           any(snapshot[k]!=change['oldWholeFile'][k] for k in ('bytes','sha256')):
            raise ValueError('Binding producer snapshot or current whole-file pin changed: '+label)
        old_text=(root/snapshot['file']).read_text(encoding='utf-8');new_text=(root/current['file']).read_text(encoding='utf-8')
        old={n.name:n for n in ast.parse(old_text).body if isinstance(n,(ast.FunctionDef,ast.ClassDef))}
        new={n.name:n for n in ast.parse(new_text).body if isinstance(n,(ast.FunctionDef,ast.ClassDef))}
        if set(old)!=set(new):raise ValueError('Binding producer definition inventory changed: '+label)
        changed=[name for name in old if ast.dump(old[name],include_attributes=False)!=ast.dump(new[name],include_attributes=False)]
        if changed!=allowed or change.get('changedDefinitions')!=allowed:
            raise ValueError('Binding changes exceed health and authored stride: '+label)
        exact={name:ast.get_source_segment(old_text,old[name])==ast.get_source_segment(new_text,new[name]) for name in old if name not in allowed}
        if not all(exact.values()) or exact!=change.get('unchangedDefinitionsExactSource'):
            raise ValueError('Unchanged shader/helper source text changed: '+label)
        hashes={name:hashlib.sha256(ast.dump(old[name],include_attributes=False).encode()).hexdigest() for name in exact}
        if hashes!=change.get('unchangedDefinitionAstHashes'):raise ValueError('Unchanged shader/helper AST fingerprint changed')
        if label=='validator':
            def prefix(node):
                result=[]
                for statement in node.body:
                    if isinstance(statement,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='M' for t in statement.targets):break
                    result.append(statement)
                return ast.dump(ast.Module(body=result,type_ignores=[]),include_attributes=False)
            if prefix(old['main'])!=prefix(new['main']):raise ValueError('Shader graph fixture assertions changed')
    fx=manifest['presentationMaterials'][0]
    if fx['shaderProducer']!=ledger['producerChanges']['importer']['currentWholeFile'] or \
       fx['contractProducer']!=frozen['presentationMaterials'][0]['contractProducer']:
        raise ValueError('Binding advance replaced the retained FX contract producer or mismatched importer')
    return {'shaderSourceUnchanged':True,'healthAndAuthoredStrideBindingsAdvanced':True,
            'qualification':stage['importBindingQualification']}

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
