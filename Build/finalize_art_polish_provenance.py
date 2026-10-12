"""Preserve and independently prove the selected 1.6 art revision.

Capture uses real pre-revision files, never a reconstructed baseline. The saved
scene comparison runs inside Blender and does not repair or resave either scene.
Finalization requires completed production, pose, matrix and quality evidence.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
HISTORY = ROOT / 'Assets/Source/Revisions/ArtPolishBefore1.6.0'
WEIGHT_REPAIR_DIRECTORY = 'Assets/Source/Revisions/ArtPolishBefore1.6.0/FinalWeightRepair1.6.0'
EVIDENCE_FILENAMES = {'preservationEvidence': 'SavedScenePreservation.json',
                      'matrixSafetyEvidence': 'FinalMatrixSafety.json',
                      'artQualityEvidence': 'FinalArtQuality.json',
                      'productionManifestSnapshot': 'AfterArtManifestBeforeProvenance.json'}
CHARACTERS = {'mini_stampede', 'stampede', 'pulverizer', 'bone_warden',
              'bone_warden_skeleton', 'aether_reaper', 'iron_giant'}
STATICS = {'iron_shield_fragment', 'shadow_surge_wisp', 'shadow_surge_ribbon'}
PORTRAITS = (CHARACTERS - {'bone_warden_skeleton'}) | {'shadow_surge'}
TEXTURES = {'Assets/Source/Textures/' + prefix + '_' + channel + '.png'
            for prefix in ('T_RiftExpansionAtlas', 'T_RiftAscensionAtlas')
            for channel in ('BaseColor', 'Normal', 'ORM', 'TeamMask')}
HISTORICAL_SUMMARY = 'Assets/Renders/Expansion/portrait-production.json'
SUMMARY_SNAPSHOT = 'Assets/Source/Revisions/ArtPolishBefore1.6.0/HistoricalPortraitProduction.json'
SUMMARY_RECOVERY = 'Assets/Source/Revisions/ArtPolishBefore1.6.0/PortraitSummaryRecoveryEvidence.json'
HISTORICAL_IDENTITY_SUMMARY = 'Assets/Renders/Expansion/Identity/identity-sheets.json'
IDENTITY_SNAPSHOT = 'Assets/Source/Revisions/ArtPolishBefore1.6.0/HistoricalIdentitySheets.json'
IDENTITY_RECOVERY = 'Assets/Source/Revisions/ArtPolishBefore1.6.0/IdentitySummaryRecoveryEvidence.json'
RENDERER_SUMMARIES = {HISTORICAL_SUMMARY,
                      'Assets/Renders/Expansion/approved-portrait-production.json'}
IDENTITY_SUMMARIES = {HISTORICAL_IDENTITY_SUMMARY,
                      'Assets/Renders/Expansion/Identity/approved-identity-sheets.json'}
CHILD_PORTRAIT_FILES = {'Assets/Source/CardArt/Children/bone_warden_skeleton.png',
                        'Assets/Source/CardArt/Masters/bone_warden_skeleton.png',
                        'Assets/Source/CardArt/Scenes/bone_warden_skeleton_Portrait.blend',
                        'Assets/Renders/Expansion/Small/bone_warden_skeleton_96x120.png'}


def pin(path, root=ROOT):
    path = Path(path).resolve()
    raw = path.read_bytes()
    return {'file': path.relative_to(root).as_posix(), 'bytes': len(raw),
            'sha256': hashlib.sha256(raw).hexdigest()}


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def immutable_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if destination.read_bytes() != source.read_bytes():
            raise ValueError('Preserved evidence cannot be overwritten: ' + str(destination))
    else:
        shutil.copyfile(source, destination)


def immutable_json(path, value):
    raw = json.dumps(value, indent=2).encode('utf-8')
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() != raw:
        raise ValueError('Immutable proof already exists with different bytes: ' + str(path))
    if not path.exists():
        path.write_bytes(raw)


def referenced_files(value):
    if isinstance(value, dict):
        if isinstance(value.get('file'), str):
            yield value['file']
        for item in value.values():
            yield from referenced_files(item)
    elif isinstance(value, list):
        for item in value:
            yield from referenced_files(item)


def capture_before():
    manifest = load(HISTORY / 'BeforeManifest.json')
    portraits = load(HISTORY / 'BeforePortraits.json')
    preserved = load(HISTORY / 'PreservedBeforeArt.json')
    overrides = {}
    for item in preserved['pins']:
        actual = pin(ROOT / item['snapshot'])
        if any(actual[key] != item[key] for key in ('bytes', 'sha256')):
            raise ValueError('Preserved original bytes changed: ' + item['source'])
        overrides[item['source']] = item['snapshot']
    # Producers may already be edited; the explicitly preserved producer files
    # above remain the historical inputs. Shared production must still be exact.
    for name in ('Assets/asset_manifest.json', 'Assets/Source/CardArt/model_portraits.json',
                 manifest['source']['file']):
        historical = pin(ROOT / overrides[name])
        current = pin(ROOT / name)
        if any(current[key] != historical[key] for key in ('bytes', 'sha256')):
            raise ValueError('Capture must precede production rewrite: ' + name)
    files = set(TEXTURES)
    for name in CHARACTERS:
        files.update(referenced_files(manifest['characters'][name]))
    for name in STATICS:
        files.update(referenced_files(manifest['statics'][name]))
    for name in PORTRAITS:
        files.update(referenced_files(portraits['cards'][name]))
        files.update(referenced_files(manifest['illustrations'].get(name, {})))
        files.update(referenced_files(manifest['cards'][name]))
    for item in manifest.get('renders', []):
        if Path(item['file']).stem in CHARACTERS | STATICS | {'shadow_surge'}:
            files.add(item['file'])
    # Renderer/finalizer source is captured even if it remains unchanged.
    for name in ('Build/render_card_portraits.py', 'Build/render_expansion_portraits.py',
                 'Build/render_new_card_portraits.py', 'Build/finalize_expansion_art_scenes.py',
                 'Build/render_expansion_identity.py'):
        if (ROOT / name).is_file():
            files.add(name)
    archived = {}
    for name in sorted(files):
        source = (ROOT / name).resolve()
        source.relative_to(ROOT)
        if not source.is_file():
            raise ValueError('A referenced before asset is missing: ' + name)
        if name in overrides:
            archived[name] = pin(ROOT / overrides[name])
        else:
            destination = HISTORY / 'OldFiles' / name
            immutable_copy(source, destination)
            archived[name] = pin(destination)
            overrides[name] = archived[name]['file']
    inventory = {}
    for directory in ('Assets/Source', 'Assets/Export', 'Assets/Renders', 'Assets/Data'):
        for path in sorted((ROOT / directory).rglob('*')):
            if path.is_file() and not path.is_relative_to(ROOT / 'Assets/Source/Revisions'):
                inventory[path.relative_to(ROOT).as_posix()] = pin(path)
    immutable_json(HISTORY / 'RetainedProductionInventory.json', inventory)
    capture = {'schema': 1, 'kind': 'art-polish-before-capture',
               'source': manifest['source'], 'beforeManifest': pin(HISTORY / 'BeforeManifest.json'),
               'beforePortraits': pin(HISTORY / 'BeforePortraits.json'),
               'preservedBeforeArt': pin(HISTORY / 'PreservedBeforeArt.json'),
               'archivedFiles': archived, 'historicalOverrides': overrides,
               'retainedInventory': pin(HISTORY / 'RetainedProductionInventory.json'),
               'inventoryFiles': len(inventory), 'archivedFileCount': len(archived),
               'archivedBytes': sum(item['bytes'] for item in archived.values())}
    immutable_json(HISTORY / 'BeforeCapture.json', capture)
    print(json.dumps({'passed': True, 'capture': pin(HISTORY / 'BeforeCapture.json'),
                      'inventoryFiles': len(inventory), 'archivedFileCount': len(archived),
                      'archivedBytes': capture['archivedBytes']}))


def checked_json(root, record):
    actual = pin(root / record['file'], root)
    if actual != record:
        raise ValueError('Tracked revision evidence changed: ' + record['file'])
    return load(root / record['file'])


def validated_supersession(root, record, original_before):
    """Certify the exact unpublished rejected stage; never replace its proofs."""
    root = Path(root).resolve()
    expected_file = WEIGHT_REPAIR_DIRECTORY + '/Supersession.json'
    if record.get('file') != expected_file:
        raise ValueError('Supersession evidence must use the single approved fixed path')
    evidence = checked_json(root, record)
    if evidence.get('schema') != 1 or evidence.get('kind') != 'unaccepted-art-polish-supersession' or evidence.get('accepted') is not False:
        raise ValueError('Superseded interim art must remain explicitly unaccepted')
    fixed = {'interimManifest': ('InterimManifest.json', '5bb773c0bd9868d799151d3591e6162208b19e7f8a11466a548dccfb9526d185', 363425),
             'interimSource': ('InterimSource.blend', '6e5d91490a984b4601ad9c12ee92f13ce567ae6ac06e09fb877c6b08f7099902', 22081077),
             'interimModelBuilder': ('InterimNewModels.py', 'd510e80fa36f56192d12e98c469e0571f45b1e29be5dec02d30e3ae5c8943107', 65333),
             'interimFinalizer': ('InterimFinalizer.py', '3f08407b050ba6b6a76b132b90629c1c5dde4a8d24d9b9836460b3dd1a05328f', 42682),
             'failedAssetValidation': ('InterimAllAssetsFailure.json', 'e9a5b307afdcf5c0dd5141b48cf181cc96cf36c4bb7fab80a8f0504e8be89746', 233783)}
    for label, (filename, digest, size) in fixed.items():
        expected = {'file': WEIGHT_REPAIR_DIRECTORY + '/' + filename, 'sha256': digest, 'bytes': size}
        if evidence.get(label) != expected or pin(root / expected['file'], root) != expected:
            raise ValueError('Superseded original raw input changed: ' + label)
    interim = checked_json(root, evidence['interimManifest'])
    revisions = interim.get('sourceRevisions', [])
    if len(revisions) != 3 or revisions[:2] != original_before['sourceRevisions'] or revisions[2].get('kind') != 'art-polish' or \
       any(interim['source'][key] != evidence['interimSource'][key] for key in ('bytes', 'sha256')) or \
       any(revisions[2]['producerScripts']['newModelBuilder'][key] != evidence['interimModelBuilder'][key] for key in ('bytes', 'sha256')) or \
       any(revisions[2]['producerScripts']['finalizer'][key] != evidence['interimFinalizer'][key] for key in ('bytes', 'sha256')):
        raise ValueError('Superseded stage changed the original two historical stages or authorship')
    old = revisions[2]
    metadata = {'Assets/asset_manifest.json': evidence['interimManifest']}
    for label in EVIDENCE_FILENAMES:
        metadata[old[label]['file']] = old[label]
    for label in ('historicalSummaryRecovery', 'historicalIdentityRecovery'):
        for key in ('snapshot', 'evidence'):
            metadata[old[label][key]['file']] = old[label][key]
    recorded = evidence.get('preservedInterimMetadata', [])
    if len(recorded) != 9 or len({item.get('originalFile') for item in recorded}) != 9 or \
       {item['originalFile']: item['snapshot'] for item in recorded} != metadata:
        raise ValueError('Supersession must preserve all exact nine interim metadata files')
    for original, snapshot in metadata.items():
        if pin(root / snapshot['file'], root) != snapshot:
            raise ValueError('Superseded proof was overwritten: ' + original)
    failed = checked_json(root, evidence['failedAssetValidation'])
    bad = [item for item in failed.get('checks', []) if item.get('passed') is not True]
    if failed.get('passed') is not False or len(failed.get('checks', [])) != 1761 or len(failed.get('errors', [])) != 1 or \
       len(bad) != 1 or bad[0].get('name') != 'aether_reaper every vertex skinned' or \
       bad[0].get('detail') != {'vertices': 9043, 'unweighted': 9} or \
       failed.get('provenance', {}).get('assetManifest', {}).get('sha256') != evidence['interimManifest']['sha256'] or \
       failed.get('provenance', {}).get('sourceBlend', {}).get('sha256') != evidence['interimSource']['sha256']:
        raise ValueError('Supersession must retain the actual rejected nine-vertex source proof')
    if evidence.get('authorizedRepair') != {'card': 'aether_reaper', 'bone': 'scythe_socket',
            'vertexIndices': [7984, 7998, 8000, 8001, 8004, 8348, 8356, 8358, 8361], 'weightBefore': 0, 'weightAfter': 1}:
        raise ValueError('Supersession repair differs from the authorized nine rigid tip weights')
    return evidence


def validated_evidence_directory(root, stage, original_before, preflight=False):
    baseline = 'Assets/Source/Revisions/ArtPolishBefore1.6.0'
    directory = stage.get('evidenceDirectory', baseline)
    if directory not in (baseline, WEIGHT_REPAIR_DIRECTORY):
        raise ValueError('Art evidence directory is outside the two exact supported sets')
    if directory == WEIGHT_REPAIR_DIRECTORY:
        if 'supersessionEvidence' not in stage:
            raise ValueError('Fresh weight-repair snapshots require exact supersession evidence')
        validated_supersession(root, stage['supersessionEvidence'], original_before)
    elif 'supersessionEvidence' in stage:
        raise ValueError('Original art snapshot set cannot claim a supersession')
    if not preflight:
        for label, filename in EVIDENCE_FILENAMES.items():
            if stage.get(label, {}).get('file') != directory + '/' + filename:
                raise ValueError('Art evidence snapshot set is partial, mixed or relabelled: ' + label)
    return directory


def production_inventory(root):
    result = {}
    for directory in ('Assets/Source', 'Assets/Export', 'Assets/Renders', 'Assets/Data'):
        for path in sorted((root / directory).rglob('*')):
            if path.is_file() and not path.is_relative_to(root / 'Assets/Source/Revisions'):
                result[path.relative_to(root).as_posix()] = pin(path, root)
    return result


def validated_portrait_summary_recovery(root, before, old_portraits, capture, inventory):
    """Resolve one omitted summary only after proving its original exact bytes.

    The before-capture and original inventory remain untouched. The independently
    retained card records and renderer produce the already recorded SHA, rather
    than a replacement baseline or a general historical resolver exception.
    """
    root = Path(root).resolve()
    evidence = load(root / SUMMARY_RECOVERY)
    snapshot = pin(root / SUMMARY_SNAPSHOT, root)
    expected = inventory[HISTORICAL_SUMMARY]
    renderer = capture['archivedFiles']['Build/render_expansion_portraits.py']
    if pin(root / renderer['file'], root) != renderer:
        raise ValueError('Recovered summary original renderer bytes changed')
    renderer_pin = {**renderer, 'file': 'Build/render_expansion_portraits.py'}
    serialization = {'outerKeyOrder': ['schema', 'sourceBlend', 'renderer', 'cards'],
                     'cardOrder': ['mini_stampede', 'stampede', 'pulverizer'],
                     'indent': 2, 'encoding': 'utf-8', 'newline': 'CRLF',
                     'trailingNewline': False}
    if evidence.get('schema') != 1 or evidence.get('passed') is not True or \
       evidence.get('expectedOriginal') != expected or \
       evidence.get('retainedPortraits') != capture['beforePortraits'] or \
       evidence.get('retainedInventory') != capture['retainedInventory'] or \
       evidence.get('retainedRenderer') != renderer or \
       evidence.get('sourceBlendPinAsRecorded') != old_portraits['sourceBlend'] or \
       evidence.get('rendererPinAsProduced') != renderer_pin or \
       evidence.get('serialization') != serialization or evidence.get('trackedFilesMutated') is not False:
        raise ValueError('Exact original portrait-summary recovery provenance changed')
    for key in ('retainedPortraits', 'retainedInventory', 'retainedRenderer'):
        if pin(root / evidence[key]['file'], root) != evidence[key]:
            raise ValueError('Recovered summary original input changed: ' + key)
    recovered = evidence.get('recovered', {})
    if not str(recovered.get('file', '')).startswith('Artifacts/QA/ExpansionArtRework/PortraitSummaryRecovery-') or \
       any(recovered.get(key) != expected[key] or snapshot[key] != expected[key]
           for key in ('bytes', 'sha256')):
        raise ValueError('Recovered summary differs from the immutable original raw pin')
    candidate = {'schema': 1, 'sourceBlend': old_portraits['sourceBlend'],
                 'renderer': {'file': renderer_pin['file'], 'sha256': renderer_pin['sha256'],
                              'bytes': renderer_pin['bytes']},
                 'cards': {name: old_portraits['cards'][name] for name in serialization['cardOrder']}}
    original_bytes = json.dumps(candidate, indent=2).replace('\n', '\r\n').encode('utf-8')
    if (root / SUMMARY_SNAPSHOT).read_bytes() != original_bytes:
        raise ValueError('Recovered summary is not the exact retained original records/serializer')
    if before['sourceRevisions'][0]['kind'] != 'animation-only':
        raise ValueError('Recovered summary must belong to the retained original animation stage')
    return {'original': expected, 'snapshot': snapshot,
            'evidence': pin(root / SUMMARY_RECOVERY, root)}


def validated_identity_summary_recovery(root, before, old_portraits, capture, inventory):
    """Recover the sole original three-sheet index from exact retained records."""
    root = Path(root).resolve()
    evidence = load(root / IDENTITY_RECOVERY)
    expected = inventory[HISTORICAL_IDENTITY_SUMMARY]
    snapshot = pin(root / IDENTITY_SNAPSHOT, root)
    serialization = {'outerKeyOrder': ['schema', 'method', 'sourceBlend', 'sheets'],
                     'sheetOrder': ['mini_stampede', 'stampede', 'pulverizer'],
                     'indent': 2, 'encoding': 'utf-8', 'newline': 'CRLF', 'trailingNewline': False}
    if evidence.get('schema') != 1 or evidence.get('passed') is not True or \
       evidence.get('expectedOriginal') != expected or \
       evidence.get('retainedManifest') != capture['beforeManifest'] or \
       evidence.get('retainedPortraits') != capture['beforePortraits'] or \
       evidence.get('retainedInventory') != capture['retainedInventory'] or \
       evidence.get('sourceBlendPinAsRecorded') != old_portraits['sourceBlend'] or \
       evidence.get('serialization') != serialization or evidence.get('trackedFilesMutated') is not False:
        raise ValueError('Exact original identity-summary recovery provenance changed')
    for key in ('retainedManifest', 'retainedPortraits', 'retainedInventory'):
        if pin(root / evidence[key]['file'], root) != evidence[key]:
            raise ValueError('Recovered identity summary original input changed: ' + key)
    recovered = evidence.get('recovered', {})
    if not str(recovered.get('file', '')).startswith('Artifacts/QA/ExpansionArtRework/IdentitySummaryRecovery-') or \
       any(recovered.get(key) != expected[key] or snapshot[key] != expected[key]
           for key in ('bytes', 'sha256')):
        raise ValueError('Recovered identity summary differs from immutable original raw pin')
    candidate = {'schema': 1, 'method': 'Production documentation layout with actual final Blender renders',
                 'sourceBlend': old_portraits['sourceBlend'],
                 'sheets': {name: before['characters'][name]['identitySheet'] for name in serialization['sheetOrder']}}
    original_bytes = json.dumps(candidate, indent=2).replace('\n', '\r\n').encode('utf-8')
    if (root / IDENTITY_SNAPSHOT).read_bytes() != original_bytes:
        raise ValueError('Recovered identity summary differs from exact retained original sheet records')
    return {'original': expected, 'snapshot': snapshot,
            'evidence': pin(root / IDENTITY_RECOVERY, root)}


def selected_asset_files(manifest, portraits):
    result = set(TEXTURES) | {manifest['source']['file'], 'Assets/asset_manifest.json',
                            'Assets/Source/CardArt/model_portraits.json'} | CHILD_PORTRAIT_FILES
    for category, names in (('characters', CHARACTERS), ('statics', STATICS),
                            ('illustrations', PORTRAITS)):
        for name in names:
            result.update(referenced_files(manifest[category][name]))
    for name in PORTRAITS:
        result.update(referenced_files(portraits['cards'][name]))
    result.update(referenced_files(manifest['cards']['shadow_surge']))
    for item in manifest.get('renders', []):
        if Path(item['file']).stem in CHARACTERS | STATICS | {'shadow_surge'}:
            result.add(item['file'])
    # These selected renderer summaries are derived art evidence, not gameplay.
    for name in RENDERER_SUMMARIES | IDENTITY_SUMMARIES:
        if name in production_inventory_cache:
            result.add(name)
    return {name for name in result if name.startswith('Assets/')}


production_inventory_cache = set()


def validate_art_revision(root, manifest, portrait_source, _finalization_preflight=False):
    """Strict third stage; historical bytes never stand in for final assets."""
    from portrait_source_revision import validate_revision, raw_pin
    root = Path(root).resolve()
    revisions = manifest.get('sourceRevisions', [])
    if len(revisions) != 3 or revisions[2].get('schema') != 1 or revisions[2].get('kind') != 'art-polish':
        raise ValueError('Expected exact two historical stages and one art-polish stage')
    stage = revisions[2]
    if set(stage.get('changedCharacters', [])) != CHARACTERS or set(stage.get('changedStatics', [])) != STATICS or \
       set(stage.get('changedPortraits', [])) != PORTRAITS or set(stage.get('changedTextureFiles', [])) != TEXTURES:
        raise ValueError('Art revision exceeds the seven-character, three-effect, seven-portrait, eight-texture scope')
    before = checked_json(root, stage['beforeManifestSnapshot'])
    validated_evidence_directory(root, stage, before, _finalization_preflight)
    old_portraits = checked_json(root, stage['beforePortraitSnapshot'])
    capture = checked_json(root, stage['beforeCapture'])
    if before.get('sourceRevisions') != revisions[:2] or stage['fromSource'] != before['source'] or stage['toSource'] != manifest['source']:
        raise ValueError('Art source chain does not connect the exact preserved accepted scene')
    if old_portraits['sourceBlend'] != portrait_source or capture['source'] != before['source']:
        raise ValueError('Historical portrait/source identity was relabelled')
    overrides = capture['historicalOverrides']
    if overrides.get(before['source']['file']) != stage['beforeSourceSnapshot']['file']:
        raise ValueError('Historical source override is not the preserved before scene')
    archived = capture['archivedFiles']
    preserved = checked_json(root, capture['preservedBeforeArt'])
    snapshot_pins = {item['source']: {'file': item['snapshot'], 'bytes': item['bytes'], 'sha256': item['sha256']}
                     for item in preserved['pins']}
    snapshot_pins.update(archived)
    if set(overrides) != set(snapshot_pins):
        raise ValueError('Historical override inventory is incomplete or extended')
    for name, record in snapshot_pins.items():
        path = (root / record['file']).resolve()
        path.relative_to(root / 'Assets/Source/Revisions/ArtPolishBefore1.6.0')
        if raw_pin(root, path) != record or overrides[name] != record['file']:
            raise ValueError('Preserved original asset/producer changed: ' + name)
    inventory = checked_json(root, stage['retainedAssetInventory'])
    recovery = validated_portrait_summary_recovery(root, before, old_portraits, capture, inventory)
    if stage.get('historicalSummaryRecovery') != recovery:
        raise ValueError('Art revision must pin only the exact supplemental historical summary')
    overrides = dict(overrides)
    if HISTORICAL_SUMMARY in overrides:
        raise ValueError('Supplemental historical summary must not replace a captured override')
    overrides[HISTORICAL_SUMMARY] = recovery['snapshot']['file']
    identity_recovery = validated_identity_summary_recovery(root, before, old_portraits, capture, inventory)
    if stage.get('historicalIdentityRecovery') != identity_recovery or HISTORICAL_IDENTITY_SUMMARY in overrides:
        raise ValueError('Art revision must pin only the exact supplemental historical identity index')
    overrides[HISTORICAL_IDENTITY_SUMMARY] = identity_recovery['snapshot']['file']
    historical = validate_revision(root, before, portrait_source, overrides)
    if raw_pin(root, manifest['source']['file']) != manifest['source']:
        raise ValueError('Current polished source bytes differ from final source pin')
    proof = checked_json(root, stage['preservationEvidence'])
    if proof.get('passed') is not True or proof.get('kind') != 'art-polish-saved-scene-comparison' or \
       proof.get('fromSource') != before['source'] or proof.get('toSource') != manifest['source'] or \
       not proof.get('unchangedBefore', {}).get('objects') or proof['unchangedBefore'] != proof.get('unchangedAfter') or \
       set(proof.get('changedCharacters', [])) != CHARACTERS or set(proof.get('changedStatics', [])) != STATICS:
        raise ValueError('Read-only saved-scene geometry/rest-bone/action preservation proof incomplete')
    for record in stage['producerScripts'].values():
        if raw_pin(root, record['file']) != record:
            raise ValueError('Current polished producer changed: ' + record['file'])
    if proof.get('verifier') != stage['producerScripts']['finalizer']:
        raise ValueError('Preservation report was not produced by the pinned current verifier')
    expected = copy.deepcopy(before)
    expected['source'] = manifest['source']
    expected['sourceRevisions'] = revisions
    for category, names in (('characters', CHARACTERS), ('statics', STATICS), ('illustrations', PORTRAITS)):
        if set(manifest[category]) != set(before[category]):
            raise ValueError('Art polish added or removed ' + category)
        for name in names:
            expected[category][name] = manifest[category][name]
    # Existing deck/entity bindings and the dedicated FX contract remain exact.
    shadow = copy.deepcopy(before['cards']['shadow_surge'])
    for key in ('identityViews', 'identitySheet'):
        shadow[key] = manifest['cards']['shadow_surge'][key]
    expected['cards']['shadow_surge'] = shadow
    old_render_list = before.get('renders', [])
    new_render_list = manifest.get('renders', [])
    old_renders = {item['file']: item for item in old_render_list}
    new_renders = {item['file']: item for item in new_render_list}
    selected_render_files = {'Assets/Renders/' + name + '.png' for name in CHARACTERS | STATICS}
    if len(old_renders) != len(old_render_list) or len(new_renders) != len(new_render_list) or \
       set(old_renders) - set(new_renders) or (set(new_renders) - set(old_renders)) - selected_render_files:
        raise ValueError('Art polish removed general renders or added entries outside the selected models/effects')
    def retained_render(item):
        return item['file'] not in selected_render_files
    if [item for item in old_render_list if retained_render(item)] != [item for item in new_render_list if retained_render(item)]:
        raise ValueError('Art polish changed or reordered unrelated general render records')
    # The selective generator removes and appends selected render entries. Their
    # order may move; unrelated entries and their order remain exact.
    expected['renders'] = new_render_list
    # Only the existing six private expansion material records may change.
    materials = manifest.get('expansionMaterials', [])
    if [item['name'] for item in materials] != [item['name'] for item in before.get('expansionMaterials', [])]:
        raise ValueError('Art polish added, removed or reordered material declarations')
    expected['expansionMaterials'] = materials
    fx=manifest.get('presentationMaterials',[])
    old_fx=copy.deepcopy(before.get('presentationMaterials',[]))
    if len(fx)!=1 or len(old_fx)!=1:
        raise ValueError('Art polish must retain the single existing dedicated FX contract')
    old_fx[0]['opacity']['atlasSource']=fx[0]['opacity']['atlasSource']
    if fx!=old_fx:
        raise ValueError('Art polish changed FX rendering parameters beyond the private atlas source pin')
    fx_producer=stage['producerScripts']['FXContract']
    spec=importlib.util.spec_from_file_location('current_polished_fx_contract',root/fx_producer['file'])
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    if fx!=[module.definition(root)]:
        raise ValueError('Final FX contract does not match actual private atlas/shader/producer bytes')
    expected['presentationMaterials']=fx
    if expected != manifest:
        raise ValueError('Manifest changed outside exact art-polish record scope')
    portraits = load(root / 'Assets/Source/CardArt/model_portraits.json')
    expected_portraits = copy.deepcopy(old_portraits)
    if set(portraits['cards']) != set(old_portraits['cards']):
        raise ValueError('Art polish added or removed playable portraits')
    for name in PORTRAITS:
        expected_portraits['cards'][name] = portraits['cards'][name]
        if portraits['cards'][name].get('sourceSceneSHA256') != manifest['source']['sha256']:
            raise ValueError('Selected portrait lacks actual polished-source identity: ' + name)
    if expected_portraits != portraits:
        raise ValueError('An unrelated portrait record or global historical identity changed')
    inventory = checked_json(root, stage['retainedAssetInventory'])
    if inventory != checked_json(root, capture['retainedInventory']):
        raise ValueError('Retained inventory differs from pre-production capture')
    current = production_inventory(root)
    global production_inventory_cache
    production_inventory_cache = set(current) | set(inventory)
    permitted = selected_asset_files(before, old_portraits) | selected_asset_files(manifest, portraits)
    removed = sorted(set(inventory) - set(current))
    if removed:
        raise ValueError('Art polish deleted production files: ' + ', '.join(removed))
    changed = sorted(name for name, record in current.items() if name not in inventory or inventory[name] != record)
    unexpected = sorted(set(changed) - permitted)
    if unexpected:
        raise ValueError('Art polish changed unrelated production/data bytes: ' + ', '.join(unexpected))
    if set(stage['changedFiles']) != set(changed):
        raise ValueError('Art changed-file report is stale or omits actual mutations')
    def verify_records(value):
        if isinstance(value, dict):
            if {'file', 'bytes', 'sha256'}.issubset(value):
                actual = raw_pin(root, value['file'])
                if any(actual[key] != value[key] for key in ('file', 'bytes', 'sha256')):
                    raise ValueError('Final art raw file pin changed: ' + value['file'])
            for item in value.values():
                verify_records(item)
        elif isinstance(value, list):
            for item in value:
                verify_records(item)
    for name in CHARACTERS:
        verify_records(manifest['characters'][name])
        if {item['name'] for item in manifest['characters'][name]['animations']} != {
                'Idle', 'Locomotion', 'Attack', 'Hit', 'Death', 'Deploy', 'Status', 'Turn', 'Acquire'}:
            raise ValueError('Art character does not retain all nine authored clips: ' + name)
    for name in STATICS:
        verify_records(manifest['statics'][name])
    for name in PORTRAITS:
        verify_records(portraits['cards'][name])
        verify_records(manifest['illustrations'][name])
    if set(stage.get('identitySummaries', {})) != IDENTITY_SUMMARIES:
        raise ValueError('Missing exact current selected identity sheet summaries')
    for name, ids, method in (
            (HISTORICAL_IDENTITY_SUMMARY, {'mini_stampede', 'stampede', 'pulverizer'},
             'Production documentation layout with actual final Blender renders'),
            ('Assets/Renders/Expansion/Identity/approved-identity-sheets.json',
             {'bone_warden', 'aether_reaper', 'iron_giant', 'shadow_surge', 'bone_warden_skeleton'},
             'Actual model/effect orthographic review renders; documentation layout, no baked game UI')):
        summary = checked_json(root, stage['identitySummaries'][name])
        if summary.get('schema') != 1 or summary.get('sourceBlend') != manifest['source'] or \
           summary.get('method') != method or set(summary.get('sheets', {})) != ids:
            raise ValueError('Current identity index source/method/scope changed: ' + name)
        for card, record in summary['sheets'].items():
            expected_sheet = manifest['cards'][card]['identitySheet'] if card == 'shadow_surge' else manifest['characters'][card]['identitySheet']
            if record != expected_sheet:
                raise ValueError('Current identity index differs from actual selected model/effect sheet: ' + card)
            verify_records(record)
    if set(stage.get('rendererSummaries', {})) != RENDERER_SUMMARIES:
        raise ValueError('Missing exact current selected renderer summaries')
    summaries = {name: checked_json(root, record) for name, record in stage['rendererSummaries'].items()}
    for name, ids, renderer in (
            (HISTORICAL_SUMMARY, {'mini_stampede', 'stampede', 'pulverizer'}, 'selectedPortraitRenderer'),
            ('Assets/Renders/Expansion/approved-portrait-production.json',
             {'bone_warden', 'aether_reaper', 'iron_giant', 'shadow_surge', 'bone_warden_skeleton'},
             'approvedPortraitRenderer')):
        summary = summaries[name]
        if summary.get('schema') != 1 or summary.get('sourceBlend') != manifest['source'] or \
           summary.get('renderer') != stage['producerScripts'][renderer] or set(summary.get('cards', {})) != ids:
            raise ValueError('Current selected renderer summary source/producer/scope changed: ' + name)
        for card, record in summary['cards'].items():
            if record.get('sourceSceneSHA256') != manifest['source']['sha256']:
                raise ValueError('Current rendered identity lacks actual polished-source attribution: ' + card)
            verify_records(record)
            if card != 'bone_warden_skeleton' and record != portraits['cards'][card]:
                raise ValueError('Current renderer summary differs from actual portrait record: ' + card)
            if card == 'bone_warden_skeleton':
                actual_files = {record['file'], record['master']['file'], record['editableScene']['file'], record['smallReview']['file']}
                if actual_files != CHILD_PORTRAIT_FILES or record.get('subjectCount') != 1 or \
                   record.get('rendererScript') != stage['producerScripts']['approvedPortraitRenderer'] or \
                   len(record.get('sources', [])) != 1 or record['sources'][0].get('model') != manifest['characters'][card]['mesh']:
                    raise ValueError('Child identity exceeds the exact one-body non-deck portrait scope')
    verify_records(manifest['cards']['shadow_surge'])
    for record in stage['textureSources']:
        if raw_pin(root, record['file']) != record:
            raise ValueError('Final private atlas pixels changed: ' + record['file'])
    if {item['file'] for item in stage['textureSources']} != TEXTURES:
        raise ValueError('Missing current eight-texture private atlas inventory')
    matrices = checked_json(root, stage['matrixSafetyEvidence'])
    clips = {(name, item['name']): item for name, record in manifest['characters'].items() for item in record['animations']}
    sampled = {(item['card'], item['clip']): item for item in matrices.get('clips', [])}
    if not matrices.get('passed') or matrices.get('errors') or matrices.get('sourceBlendSHA256') != manifest['source']['sha256'] or set(clips) != set(sampled):
        raise ValueError('Current all-character quarter-frame source/FBX matrix proof incomplete')
    for key, item in clips.items():
        actual = sampled[key]
        if actual['samples'] != 4 * (item['frames'] - 1) + 1 or actual['fbx']['sha256'] != item['sha256'] or \
           actual['fbx']['unsafeScaleValues'] or actual['fbx']['nonfiniteElements']:
            raise ValueError('Current quarter-frame matrix/export evidence stale or unsafe: ' + str(key))
    quality = checked_json(root, stage['artQualityEvidence'])
    if quality.get('passed') is not True or quality.get('preview') or quality.get('productionExportPinsChecked') is not True or quality.get('sourceBlend') != manifest['source'] or \
       set(quality.get('cards', {})) != CHARACTERS or quality.get('errors') or \
       quality.get('verifier') != stage['producerScripts']['qualityValidator']:
        raise ValueError('Current evaluated art-quality proof incomplete or stale')
    produced_manifest=checked_json(root,stage['productionManifestSnapshot'])
    expected_produced=copy.deepcopy(manifest);expected_produced['sourceRevisions']=revisions[:2]
    if produced_manifest!=expected_produced or any(quality['manifest'][key]!=stage['productionManifestSnapshot'][key] for key in ('bytes','sha256')):
        raise ValueError('Quality report did not audit the exact final pre-provenance production manifest')
    expected_producer_pins={'generate_assets':stage['producerScripts']['producer'],
        'expansion_new_models':stage['producerScripts']['newModelBuilder'],
        'expansion_asset_models':stage['producerScripts']['oldModelBuilder']}
    if quality.get('producerPins')!=expected_producer_pins:
        raise ValueError('Quality report producer inputs differ from current generation')
    if any(item.get('clipCount') != 9 or item.get('maximumReproductionDelta', 1) > 1e-7
           for item in quality['cards'].values()):
        raise ValueError('Actual nine-clip saved-pose reproduction proof incomplete')
    for name, item in quality['cards'].items():
        actual = manifest['characters'][name]
        if item.get('sourceTriangles') != actual['triangles'] or item.get('measuredTriangles') != actual['triangles'] or \
           item.get('sourceBoundsMeters') != actual['boundsMeters'] or \
           len(item.get('measuredBoundsMeters', [])) != 3 or \
           max(abs(a-b) for a,b in zip(item['measuredBoundsMeters'],actual['boundsMeters'])) > 1e-5 or \
           set(item.get('clips', {})) != {'Idle', 'Locomotion', 'Attack', 'Hit', 'Death', 'Deploy', 'Status', 'Turn', 'Acquire'}:
            raise ValueError('Evaluated geometry/clip evidence differs from declared polished asset: ' + name)
    return {'historicalAcceptedProduction': historical, 'historicalRenderSource': portrait_source,
            'currentExportSource': manifest['source'], 'changedCharacters': sorted(CHARACTERS),
            'changedPortraits': sorted(PORTRAITS), 'currentRetainedAssetPinsVerified': len(current) - len(changed),
            'currentChangedAssetPinsVerified': len(changed), 'allClipQuarterFrameSamples': matrices['sourceSamples'],
            'visualAcceptance': 'Evaluated source gates only; human animation/native-camera review remains separate'}


def verify_scenes(output):
    import bpy
    before = load(HISTORY / 'BeforeManifest.json')
    manifest = load(ROOT / 'Assets/asset_manifest.json')
    if any(pin(HISTORY / 'BeforeSource.blend')[key] != before['source'][key] for key in ('bytes', 'sha256')) or \
       pin(ROOT / manifest['source']['file']) != manifest['source']:
        raise ValueError('Actual before/final scene bytes do not match their source identity')
    def signatures():
        excluded = {obj.name for name in CHARACTERS | STATICS for obj in bpy.data.collections[name].all_objects}
        objects = {}
        for obj in bpy.data.objects:
            if obj.name in excluded or obj.type not in ('MESH', 'ARMATURE'):
                continue
            payload = {'type': obj.type, 'transform': [list(row) for row in obj.matrix_world]}
            if obj.type == 'MESH':
                payload.update(vertices=[list(v.co) for v in obj.data.vertices],
                    faces=[list(p.vertices) for p in obj.data.polygons],
                    uv=[[list(v.uv) for v in layer.data] for layer in obj.data.uv_layers],
                    groups=[g.name for g in obj.vertex_groups],
                    weights=[[(g.group, g.weight) for g in v.groups] for v in obj.data.vertices],
                    materials=[material.name if material else None for material in obj.data.materials])
            else:
                payload['bones'] = {bone.name: {'parent': bone.parent.name if bone.parent else None,
                    'matrix': [list(row) for row in bone.matrix_local], 'deform': bone.use_deform} for bone in obj.data.bones}
            objects[obj.name] = hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        actions = {}
        for action in bpy.data.actions:
            if any(action.name.startswith('AN_' + name + '_') for name in CHARACTERS):
                continue
            curves = list(action.fcurves) if hasattr(action, 'fcurves') else [curve for layer in action.layers for strip in layer.strips for bag in strip.channelbags for curve in bag.fcurves]
            payload = [{'path': curve.data_path, 'index': curve.array_index,
                'keys': [[list(k.co), k.interpolation, list(k.handle_left), list(k.handle_right)] for k in curve.keyframe_points]} for curve in curves]
            actions[action.name] = hashlib.sha256(json.dumps(payload, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        return {'objects': objects, 'actions': actions}
    bpy.ops.wm.open_mainfile(filepath=str(HISTORY / 'BeforeSource.blend'))
    old = signatures()
    bpy.ops.wm.open_mainfile(filepath=str(ROOT / manifest['source']['file']))
    current = signatures()
    proof = {'schema': 1, 'kind': 'art-polish-saved-scene-comparison', 'passed': old == current,
             'fromSource': before['source'], 'toSource': manifest['source'],
             'changedCharacters': sorted(CHARACTERS), 'changedStatics': sorted(STATICS),
             'unchangedBefore': old, 'unchangedAfter': current, 'verifier': pin(Path(__file__)),
             'scope': 'Independent read-only saved mesh, UV, weights, materials, rest-bone and full action comparison outside selected collections'}
    immutable_json(output, proof)
    print(json.dumps({'passed': proof['passed'], 'unchangedObjects': len(old['objects']), 'unchangedActions': len(old['actions']), 'report': pin(output)}))
    if not proof['passed']:
        raise ValueError('An unrelated saved mesh/rest-joint/action changed')


def finalize(args):
    from portrait_source_revision import validate_revision
    manifest_path = ROOT / 'Assets/asset_manifest.json'
    manifest = load(manifest_path)
    before = load(HISTORY / 'BeforeManifest.json')
    if manifest.get('sourceRevisions') != before['sourceRevisions']:
        raise ValueError('Art revision already present or historical stages changed')
    output_directory = (args.output_revision_directory or HISTORY).resolve()
    if output_directory not in (HISTORY.resolve(), (ROOT / WEIGHT_REPAIR_DIRECTORY).resolve()):
        raise ValueError('Only the exact original or FinalWeightRepair evidence directory is supported')
    evidence_sources = {}
    for label, source, filename in (
            ('preservationEvidence', args.preservation, 'SavedScenePreservation.json'),
            ('matrixSafetyEvidence', args.matrices, 'FinalMatrixSafety.json'),
            ('artQualityEvidence', args.quality, 'FinalArtQuality.json')):
        source = source.resolve()
        source.relative_to(ROOT / 'Artifacts/QA')
        evidence_sources[label] = (source, output_directory / filename)
    inventory = load(HISTORY / 'RetainedProductionInventory.json')
    current = production_inventory(ROOT)
    changed = sorted(name for name, record in current.items() if inventory.get(name) != record)
    producers = {'producer': 'Build/generate_assets.py', 'oldModelBuilder': 'Build/expansion_asset_models.py',
                 'newModelBuilder': 'Build/expansion_new_models.py', 'finalizer': 'Build/finalize_art_polish_provenance.py',
                 'qualityValidator': 'Build/validate_expansion_art_quality.py',
                 'matrixValidator': 'Build/validate_animation_matrices.py',
                 'savedContractValidator':'Build/validate_approved_expansion.py',
                 'portraitRenderer': 'Build/render_card_portraits.py',
                 'selectedPortraitRenderer': 'Build/render_expansion_portraits.py',
                 'newPortraitRenderer': 'Build/render_new_card_portraits.py'}
    producers.update({'approvedPortraitRenderer':'Build/render_approved_expansion.py',
        'approvedIdentityComposer':'Build/compose_approved_identity.py',
        'expansionIdentityComposer':'Build/compose_expansion_identity.py',
        'portraitSceneFinalizer':'Build/finalize_expansion_art_scenes.py'})
    producers['FXContract']='Build/generate_expansion_fx_contract.py'
    stage = {'schema': 1, 'kind': 'art-polish', 'fromSource': before['source'], 'toSource': manifest['source'],
             'changedCharacters': sorted(CHARACTERS), 'changedStatics': sorted(STATICS),
             'changedPortraits': sorted(PORTRAITS), 'changedTextureFiles': sorted(TEXTURES),
             'changedFiles': changed, 'textureSources': [pin(ROOT / name) for name in sorted(TEXTURES)],
             'producerScripts': {key: pin(ROOT / name) for key, name in producers.items() if (ROOT / name).is_file()}}
    if output_directory == (ROOT / WEIGHT_REPAIR_DIRECTORY).resolve():
        stage['evidenceDirectory'] = WEIGHT_REPAIR_DIRECTORY
        stage['supersessionEvidence'] = pin(output_directory / 'Supersession.json')
        validated_supersession(ROOT, stage['supersessionEvidence'], before)
    for label, name in {'beforeManifestSnapshot': 'BeforeManifest.json', 'beforePortraitSnapshot': 'BeforePortraits.json',
                        'beforeSourceSnapshot': 'BeforeSource.blend', 'beforeCapture': 'BeforeCapture.json',
                        'retainedAssetInventory': 'RetainedProductionInventory.json'}.items():
        stage[label] = pin(HISTORY / name)
    stage['historicalSummaryRecovery'] = validated_portrait_summary_recovery(
        ROOT, before, load(HISTORY / 'BeforePortraits.json'), load(HISTORY / 'BeforeCapture.json'), inventory)
    stage['historicalIdentityRecovery'] = validated_identity_summary_recovery(
        ROOT, before, load(HISTORY / 'BeforePortraits.json'), load(HISTORY / 'BeforeCapture.json'), inventory)
    stage['rendererSummaries'] = {name: pin(ROOT / name) for name in sorted(RENDERER_SUMMARIES)}
    stage['identitySummaries'] = {name: pin(ROOT / name) for name in sorted(IDENTITY_SUMMARIES)}
    # Preflight against the original immutable QA inputs before creating any
    # permanent revision snapshots. A rejected proof must not poison the fixed
    # historical filenames and prevent a later corrected, real production run.
    stage.update({label: pin(source) for label, (source, _) in evidence_sources.items()})
    stage['productionManifestSnapshot'] = pin(manifest_path)
    manifest['sourceRevisions'].append(stage)
    portraits = load(ROOT / 'Assets/Source/CardArt/model_portraits.json')
    # Only this internal preflight uses the original immutable QA file locations.
    # Public saved-manifest validation always requires the full fixed snapshot set.
    validate_art_revision(ROOT, manifest, portraits['sourceBlend'], _finalization_preflight=True)
    for label, (source, destination) in evidence_sources.items():
        immutable_copy(source, destination)
        stage[label] = pin(destination)
    snapshot=output_directory/'AfterArtManifestBeforeProvenance.json'
    immutable_copy(manifest_path,snapshot)
    stage['productionManifestSnapshot']=pin(snapshot)
    result = validate_revision(ROOT, manifest, portraits['sourceBlend'])
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'revision': result, 'manifest': pin(manifest_path)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture-before', action='store_true')
    parser.add_argument('--verify-scenes', type=Path)
    parser.add_argument('--finalize', action='store_true')
    parser.add_argument('--preservation', type=Path)
    parser.add_argument('--matrices', type=Path)
    parser.add_argument('--quality', type=Path)
    parser.add_argument('--output-revision-directory', type=Path,
                        help='Exact original history directory or its fixed FinalWeightRepair1.6.0 child')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:])
    if args.capture_before:
        capture_before()
    elif args.verify_scenes:
        output = args.verify_scenes.resolve()
        output.relative_to(ROOT / 'Artifacts/QA')
        verify_scenes(output)
    elif args.finalize:
        if not all((args.preservation, args.matrices, args.quality)):
            parser.error('--finalize requires --preservation, --matrices and --quality')
        finalize(args)
    else:
        parser.error('Select --capture-before; final production modes require completed evidence')


if __name__ == '__main__':
    main()
