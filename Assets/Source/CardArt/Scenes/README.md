# Expansion portrait sources

These normal Blender files open directly into the selected card's editable production scene. They retain final skinned model instances, phase-shifted named actions, materials, lights, perspective camera, and the environment used for the 1024 × 1280 masters. They use the authored 100 FPS timeline. Model design and rigging remain in `Assets/Source/RiftCrown_ProductionAssets.blend`.

Selected source production:

```powershell
blender --background --python-exit-code 1 --python Build/generate_assets.py -- --update-models mini_stampede,stampede,pulverizer --no-renders
blender --background --python-exit-code 1 --python Build/render_card_portraits.py -- --cards mini_stampede stampede pulverizer
blender --background --python-exit-code 1 --python Build/finalize_expansion_art_scenes.py
python Build/compose_expansion_identity.py
python Build/validate_card_portraits.py --output Artifacts/QA/ExpansionAssets/portrait-provenance-final.json
blender --background --python-exit-code 1 --python Build/validate_expansion_assets.py
```

Masters live beside these sources in `../Masters`; the canonical 768 × 960 PNGs remain the runtime portrait inputs. `Assets/Renders/Expansion` contains exact 96 × 120 reviews and identity sheets. Native import, facing, game-camera size, visual cleanup and performance checks remain separate runtime acceptance steps.

The extra expansion atlas and materials belong only to revised expansion assets. Existing character exports, architecture, textures and portraits are preserved through selected generation rather than full regeneration.
