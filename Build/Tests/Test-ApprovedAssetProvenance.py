"""Actual tracked-input fresh-root and isolated provenance rejection tests.

No fixture is written to canonical data. The fresh root contains referenced
tracked files only, with no Artifacts directory, and uses copied source bytes.
"""
from pathlib import Path
import argparse,copy,hashlib,importlib.util,json,shutil,tempfile,sys

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--historical-before-art',action='store_true',help='Validate preserved actual two-stage production with checked archived inputs; no polished acceptance claim')
    args=parser.parse_args();out=args.report.resolve();out.relative_to(ROOT/'Artifacts/QA')
    if out.exists():raise ValueError('Immutable fixture report already exists')
    history=ROOT/'Assets/Source/Revisions/ArtPolishBefore1.6.0'
    M=json.loads((history/'BeforeManifest.json' if args.historical_before_art else ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
    P=json.loads((history/'BeforePortraits.json' if args.historical_before_art else ROOT/'Assets/Source/CardArt/model_portraits.json').read_text(encoding='utf-8'))
    capture=json.loads((history/'BeforeCapture.json').read_text(encoding='utf-8')) if args.historical_before_art or len(M.get('sourceRevisions',[]))==3 else None
    overrides=capture['historicalOverrides'] if args.historical_before_art else None
    paths=set();scanned=set()
    def gather(value):
        if isinstance(value,dict):
            filename=value.get('file')
            if isinstance(filename,str):add(filename)
            snapshot=value.get('snapshot')
            if isinstance(snapshot,str):add(snapshot)
            for archived in value.get('historicalOverrides',{}).values():add(archived)
            for item in value.values():gather(item)
        elif isinstance(value,list):
            for item in value:gather(item)
    def add(filename):
        path=Path(filename)
        if path.is_absolute() or not filename.startswith(('Assets/','Build/')):return
        real=ROOT/path
        if not real.is_file():return
        paths.add(filename)
        if path.suffix=='.json' and filename not in scanned:
            scanned.add(filename)
            try:gather(json.loads(real.read_text(encoding='utf-8')))
            except (UnicodeDecodeError,json.JSONDecodeError):pass
    gather(M);gather(P);add('Assets/Source/CardArt/model_portraits.json');add('Assets/asset_manifest.json')
    add('Assets/Source/Audio/audio_manifest.json');add('Build/portrait_source_revision.py')
    supplemental_summary = history/'HistoricalPortraitProduction.json'
    if supplemental_summary.is_file():
        add('Build/finalize_art_polish_provenance.py')
        add('Assets/Source/Revisions/ArtPolishBefore1.6.0/HistoricalPortraitProduction.json')
        add('Assets/Source/Revisions/ArtPolishBefore1.6.0/PortraitSummaryRecoveryEvidence.json')
        add('Assets/Source/Revisions/ArtPolishBefore1.6.0/HistoricalIdentitySheets.json')
        add('Assets/Source/Revisions/ArtPolishBefore1.6.0/IdentitySummaryRecoveryEvidence.json')
    if capture:
        gather(capture);add('Assets/Source/Revisions/ArtPolishBefore1.6.0/BeforeCapture.json')
    result={'schema':1,'passed':False,'scope':'Exact referenced tracked-input copy without ignored Artifacts; isolated mutations only',
            'historicalBeforeArt':args.historical_before_art,'currentPolishedAcceptance':not args.historical_before_art and len(M.get('sourceRevisions',[]))==3,
            'checks':[],'errors':[],'copiedFiles':len(paths),'copiedBytes':sum((ROOT/p).stat().st_size for p in paths)}
    def check(name,passed):
        result['checks'].append({'name':name,'passed':bool(passed)})
        if not passed:result['errors'].append(name)
    with tempfile.TemporaryDirectory(prefix='RiftApprovedTrackedInputs-') as tmp:
        fresh=Path(tmp).resolve()
        for name in sorted(paths):
            destination=fresh/name;destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/name,destination)
        spec=importlib.util.spec_from_file_location('fresh_provenance',fresh/'Build/portrait_source_revision.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        sys.path.insert(0,str(fresh/'Build'));sys.modules['portrait_source_revision']=module
        recovery_module=None
        if supplemental_summary.is_file():
            recovery_spec=importlib.util.spec_from_file_location('fresh_summary_recovery',fresh/'Build/finalize_art_polish_provenance.py')
            recovery_module=importlib.util.module_from_spec(recovery_spec);recovery_spec.loader.exec_module(recovery_module)
            original_before=json.loads((fresh/'Assets/Source/Revisions/ArtPolishBefore1.6.0/BeforeManifest.json').read_text(encoding='utf-8'))
            original_portraits=json.loads((fresh/'Assets/Source/Revisions/ArtPolishBefore1.6.0/BeforePortraits.json').read_text(encoding='utf-8'))
            original_capture=json.loads((fresh/'Assets/Source/Revisions/ArtPolishBefore1.6.0/BeforeCapture.json').read_text(encoding='utf-8'))
            original_inventory=json.loads((fresh/original_capture['retainedInventory']['file']).read_text(encoding='utf-8'))
            recovered=recovery_module.validated_portrait_summary_recovery(fresh,original_before,original_portraits,original_capture,original_inventory)
            check('Tracked supplemental summary matches exact original retained-input bytes',recovered['original']['sha256']==recovered['snapshot']['sha256'])
            recovered_identity=recovery_module.validated_identity_summary_recovery(fresh,original_before,original_portraits,original_capture,original_inventory)
            check('Tracked supplemental identity index matches exact original retained-sheet bytes',recovered_identity['original']['sha256']==recovered_identity['snapshot']['sha256'])
        if args.historical_before_art and recovery_module:
            recovery=recovery_module.validated_portrait_summary_recovery(fresh,M,P,capture,
                json.loads((fresh/capture['retainedInventory']['file']).read_text(encoding='utf-8')))
            overrides=dict(overrides);overrides[recovery_module.HISTORICAL_SUMMARY]=recovery['snapshot']['file']
            overrides[recovery_module.HISTORICAL_IDENTITY_SUMMARY]=recovered_identity['snapshot']['file']
        check('Fresh referenced-input root has no ignored Artifacts',(fresh/'Assets').is_dir() and not (fresh/'Artifacts').exists())
        baseline=module.validate_revision(fresh,M,P['sourceBlend'],overrides)
        if len(M.get('sourceRevisions',[]))==3:
            check('Actual copied polished source/export/portrait/audio chain passes',
                baseline['currentRetainedAssetPinsVerified']>0 and baseline['currentChangedAssetPinsVerified']==len(M['sourceRevisions'][2]['changedFiles']))
            check('Preserved historical two-stage chain remains readable and exact',
                baseline['historicalAcceptedProduction']['currentRetainedAssetPinsVerified']==632)
        else:
            check('Actual copied source/export/portrait/audio chain passes',baseline['currentRetainedAssetPinsVerified']==632)
        def rejected(name,edit_manifest=None,edit_file=None):
            altered=copy.deepcopy(M);restore=None
            try:
                if edit_manifest:edit_manifest(altered)
                if edit_file:
                    path,transform=edit_file;target=module.resolved_path(fresh,path,overrides);original=target.read_bytes();restore=(target,original)
                    target.write_bytes(transform(original))
                try:module.validate_revision(fresh,altered,P['sourceBlend'],overrides)
                except (ValueError,KeyError,OSError,TypeError):check(name,True)
                else:check(name,False)
            finally:
                if restore:restore[0].write_bytes(restore[1])
        rejected('Reject unapproved extra character',lambda m:m['sourceRevisions'][1]['addedCharacters'].append('fixture_unapproved'))
        rejected('Reject relabelled current source SHA',lambda m:m['source'].update(sha256='0'*64))
        rejected('Reject changed retained old card geometry metadata',lambda m:m['characters']['ironclad'].update(triangles=1))
        rejected('Reject child deck binding',lambda m:m['entities']['bone_warden_skeleton'].update(deckCard=True))
        rejected('Reject altered current new FBX bytes',edit_file=(M['characters']['bone_warden']['mesh']['file'],lambda b:b+b'fixture'))
        rejected('Reject altered retained old FBX bytes',edit_file=(M['characters']['ironclad']['mesh']['file'],lambda b:b+b'fixture'))
        rejected('Reject altered new atlas pixels',edit_file=('Assets/Source/Textures/T_RiftAscensionAtlas_BaseColor.png',lambda b:b+b'fixture'))
        rejected('Reject changed actual old portrait record',edit_file=('Assets/Source/CardArt/model_portraits.json',
            lambda b:json.dumps({**json.loads(b),'method':'fixture_relabelled'}).encode()))
        rejected('Reject changed current portrait pixels',edit_file=('Assets/Source/CardArt/bone_warden.png',lambda b:b+b'fixture'))
        rejected('Reject changed new editable portrait scene',edit_file=('Assets/Source/CardArt/Scenes/shadow_surge_Portrait.blend',lambda b:b+b'fixture'))
        rejected('Reject changed original audio master',edit_file=('Assets/Source/Audio/shield_hit.wav',lambda b:b+b'fixture'))
        rejected('Reject changed approved audio manifest',edit_file=('Assets/Source/Audio/audio_manifest.json',lambda b:b+b' '))
        rejected('Reject changed source builder',edit_file=('Build/expansion_new_models.py',lambda b:b+b'\n# fixture\n'))
        rejected('Reject changed tracked proof bytes',edit_file=(M['sourceRevisions'][1]['productionEvidence']['file'],lambda b:b+b' '))
        if M.get('presentationMaterials'):
            rejected('Reject unbounded FX contract despite current shader pin',lambda m:m['presentationMaterials'][0]['emission'].update(maximumPerChannel=1.))
            rejected('Reject changed actual FX shader producer',edit_file=('Build/import_unreal_assets.py',lambda b:b+b'\n# fixture\n'))
        if len(M.get('sourceRevisions',[]))==3:
            stage=M['sourceRevisions'][2]
            rejected('Reject extra art-polish character scope',lambda m:m['sourceRevisions'][2]['changedCharacters'].append('ironclad'))
            rejected('Reject altered selected geometry declaration',lambda m:m['characters']['bone_warden'].update(triangles=1))
            rejected('Reject missing authored polished clip',lambda m:m['characters']['bone_warden']['animations'].pop())
            rejected('Reject altered archived historical producer',edit_file=('Assets/Source/Revisions/ArtPolishBefore1.6.0/BeforeNewModels.py',lambda b:b+b'\n# fixture\n'))
            rejected('Reject modified original legacy atlas pixels',edit_file=('Assets/Source/Textures/T_RiftAtlas_BaseColor.png',lambda b:b+b'fixture'))
            rejected('Reject modified untouched playable portrait pixels',edit_file=('Assets/Source/CardArt/ironclad.png',lambda b:b+b'fixture'))
            rejected('Reject changed final saved-scene quality evidence',edit_file=(stage['artQualityEvidence']['file'],lambda b:b+b' '))
            rejected('Reject relabelled final quality source metadata',lambda m:m['sourceRevisions'][2]['toSource'].update(sha256='0'*64))
            rejected('Reject omitted changed file from actual art inventory',lambda m:m['sourceRevisions'][2]['changedFiles'].pop())
            rejected('Reject supplemental history resolver beyond the single original summary',
                lambda m:m['sourceRevisions'][2]['historicalSummaryRecovery'].update(additionalOverride='fixture'))
            rejected('Reject supplemental identity resolver beyond the exact original index',
                lambda m:m['sourceRevisions'][2]['historicalIdentityRecovery'].update(additionalOverride='fixture'))
            if stage.get('evidenceDirectory'):
                rejected('Reject partial or mixed final-weight-repair snapshot set',
                    lambda m:m['sourceRevisions'][2]['artQualityEvidence'].update(file='Assets/Source/Revisions/ArtPolishBefore1.6.0/FinalArtQuality.json'))
                rejected('Reject final-weight-repair stage without supersession evidence',
                    lambda m:m['sourceRevisions'][2].pop('supersessionEvidence'))
                rejected('Reject changed supersession evidence raw bytes',
                    edit_file=(stage['supersessionEvidence']['file'],lambda b:b+b' '))
                rejected('Reject altered original two-stage history inside supersession',
                    lambda m:m['sourceRevisions'][0].update(kind='fixture_relabelled_history'))
        if recovery_module:
            def reject_recovery(name, mutate):
                target=fresh/recovery_module.SUMMARY_RECOVERY;original=target.read_bytes()
                try:
                    evidence=json.loads(original);mutate(evidence)
                    target.write_text(json.dumps(evidence,indent=2),encoding='utf-8')
                    try:recovery_module.validated_portrait_summary_recovery(fresh,original_before,original_portraits,original_capture,original_inventory)
                    except (ValueError,KeyError,OSError,TypeError):check(name,True)
                    else:check(name,False)
                finally:target.write_bytes(original)
            reject_recovery('Reject recovered original summary with a changed expected hash',lambda e:e['expectedOriginal'].update(sha256='0'*64))
            reject_recovery('Reject recovered original summary with relabelled source',lambda e:e['sourceBlendPinAsRecorded'].update(sha256='0'*64))
            reject_recovery('Reject recovered original summary with changed serializer',lambda e:e['serialization'].update(newline='LF'))
            reject_recovery('Reject recovered original summary with changed retained input',lambda e:e['retainedPortraits'].update(sha256='0'*64))
            reject_recovery('Reject recovered original summary with a changed recovered raw pin',lambda e:e['recovered'].update(bytes=1))
            target=fresh/recovery_module.SUMMARY_SNAPSHOT;original=target.read_bytes()
            try:
                target.write_bytes(original+b'fixture')
                try:recovery_module.validated_portrait_summary_recovery(fresh,original_before,original_portraits,original_capture,original_inventory)
                except (ValueError,KeyError,OSError,TypeError):check('Reject changed recovered original summary bytes',True)
                else:check('Reject changed recovered original summary bytes',False)
            finally:target.write_bytes(original)
            for name,mutate in (
                    ('changed expected original identity hash',lambda e:e['expectedOriginal'].update(sha256='0'*64)),
                    ('relabelled original identity source',lambda e:e['sourceBlendPinAsRecorded'].update(sha256='0'*64)),
                    ('changed original identity manifest pin',lambda e:e['retainedManifest'].update(sha256='0'*64)),
                    ('changed original identity serializer',lambda e:e['serialization'].update(newline='LF'))):
                target=fresh/recovery_module.IDENTITY_RECOVERY;original=target.read_bytes()
                try:
                    value=json.loads(original);mutate(value);target.write_text(json.dumps(value,indent=2),encoding='utf-8')
                    try:recovery_module.validated_identity_summary_recovery(fresh,original_before,original_portraits,original_capture,original_inventory)
                    except (ValueError,KeyError,OSError,TypeError):check('Reject '+name,True)
                    else:check('Reject '+name,False)
                finally:target.write_bytes(original)
            target=fresh/recovery_module.IDENTITY_SNAPSHOT;original=target.read_bytes()
            try:
                target.write_bytes(original+b'fixture')
                try:recovery_module.validated_identity_summary_recovery(fresh,original_before,original_portraits,original_capture,original_inventory)
                except (ValueError,KeyError,OSError,TypeError):check('Reject changed recovered original identity bytes',True)
                else:check('Reject changed recovered original identity bytes',False)
            finally:target.write_bytes(original)
        check('Isolated fixture writes leave actual source pins unchanged',
              all((ROOT/name).read_bytes()==(fresh/name).read_bytes() for name in paths))
    result['passed']=not result['errors'];result['checkCount']=len(result['checks'])
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({'passed':result['passed'],'checks':result['checkCount'],'copiedFiles':result['copiedFiles'],'errors':result['errors']}))
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
