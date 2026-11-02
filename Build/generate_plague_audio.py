"""Add five finite Plague Queen cues without changing released audio masters."""
import hashlib
import json
import generate_audio as a


def bubbles(variation=0, impact=False):
    x = a.frames(.28 if impact else .62)
    for i in range(3 if impact else 4):
        a.put(x, a.body(.12, 470 + i * 83 + variation * 41, 150 + i * 29),
              at=i * (.032 if impact else .088), gain=.12 if impact else .09)
    if not impact:
        a.put(x, a.glint(.35, [392 + variation * 32, 587, 880], seed=321 + variation),
              at=.10, gain=.45)
        a.put(x, a.air(.30, 712 + variation, 850, 3200, decay=9), at=.17, gain=.025)
    return x


def release():
    x = a.frames(.32)
    a.layer(x, "Tinysized_quiver-leather-squeeze-01", .19, pitch=.9, limit=.13)
    a.layer(x, "Tinysized_wood-twigs-break-02", .35, at=.018, pitch=1.10, limit=.10)
    a.layer(x, "Tinysized_arrow-feathers-01", .25, at=.035, pitch=1.35, limit=.18)
    a.put(x, a.body(.13, 690, 240), at=.025, gain=.05)
    return x


def main():
    target = a.OUT / 'audio_manifest.json'
    manifest = json.loads(target.read_text(encoding='utf-8'))
    names = ['spawn_plague_queen', 'spawn_plague_queen_v2', 'spawn_plague_queen_v3',
             'poison_fire', 'poison_pop']
    preserved = {name: entry for name, entry in manifest['sounds'].items() if name not in names}
    for i, name in enumerate(names):
        a.USED_SOURCES.clear()
        a.source.cache_clear()
        recipe = release() if name == 'poison_fire' else bubbles(i if i < 3 else 0, i == 4)
        a.write(name, recipe, peak=.48, rms_db=-25 if i < 3 else -27,
                role='deployment' if i < 3 else 'combat', sources=sorted(a.USED_SOURCES))
        entry = a.MANIFEST['sounds'][name]
        entry['productionSource'] = 'Build/generate_plague_audio.py'
        if i < 3:
            entry['cardId'] = 'plague_queen'
    manifest['sounds'].update(a.MANIFEST['sounds'])
    manifest['spawnCueProfiles']['plague_queen'] = {
        'identity': 'Three brief royal toxin signatures: rounded bubble pops and a restrained crown chime',
        'takes': names[:3], 'trigger': 'one card_play per playId; never entity_spawn'}
    for name, entry in preserved.items():
        if hashlib.sha256((a.ROOT / entry['file']).read_bytes()).hexdigest() != entry['sha256'] or manifest['sounds'][name] != entry:
            raise ValueError('Released audio changed: ' + name)
    target.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    report = {'passed': True, 'preservedMasters': len(preserved), 'newMasters': a.MANIFEST['sounds'],
              'loopsAdded': 0, 'listeningAcceptance': 'Not asserted by numerical authoring'}
    out = a.ROOT / 'Artifacts/QA/Plague220/Audio/production.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'preservedMasters': len(preserved), 'newMasters': len(names), 'loopsAdded': 0}))


if __name__ == '__main__':
    main()
