# Rift Crown asset source and import contract

`Source/RiftCrown_ProductionAssets.blend` contains the editable authored meshes, rigs, material atlas and named actions. `Export/` contains 214 FBX files: 11 skeletal actors, 113 individual animation clips, three reduced skeletal LODs per actor, and 19 static assets with two reduced LODs each. Ten skeletal actors correspond to the ten troop cards; `tower_archer` is a separate miniature archer for the building. All fourteen card illustration files are mapped by unchanged card ID in `asset_manifest.json`.

The source is generated from original geometry definitions in `Build/generate_assets.py`. It never imports the browser GLB models. Blender 5.2.2 LTS was used. The complete build regenerates the entire scene so the editable source cannot silently lose meshes after a partial rebuild.

Run from the repository root in PowerShell:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python-exit-code 1 --python 'Build\generate_assets.py'
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python-exit-code 1 --python 'Build\validate_assets.py'
& 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe' --background --python-exit-code 1 --python 'Build\render_animation_review.py'
python 'Build\asset_contact_sheet.py'
```

The contact-sheet tool needs Pillow. The forge accepts `-- --no-renders` for a complete interchange/source rebuild without studio renders. It still regenerates all rigs, actions, textures and exports.

## Import and units

FBX **declares meters** with `UnitScaleFactor = 100.0`, meaning 100 centimeters per FBX unit. This was read directly from the binary FBX. Unreal import uses `convert_scene_unit = True`, `convert_scene = True`, `force_front_x_axis = True`, and `import_uniform_scale = 1`. Do not add a second 100× multiplier. Blender uses authored -Y forward/+Z up; Unreal's front-axis conversion gives +X forward/+Z up. The engine import acceptance test must inspect muzzle/head facing and mesh bounds, rather than infer orientation solely from export labels.

Initial character import uses skeletal mesh enabled and animations disabled. Import each `AN_<id>_<action>.fbx` with the imported skeleton selected. Each file contains exactly one animation stack and includes tracks for all bones, including anchors. Import reduced `SK_<id>_LOD1/2/3.fbx` into the same skeletal mesh LOD slots. Static reduced meshes follow `SM_<id>_LOD1/2.fbx`.

Automated UE 5.8 imports must explicitly use the legacy `FbxFactory` with `FbxImportUI` options. The initial Interchange route ignored those legacy option objects in this project, producing centimeter bounds numerically equal to the meter source dimensions and retaining the source axes. Replacing existing generated mesh assets did not repair that result: installed `EditorFactories.cpp` shows skeletal reimport unconditionally replacing task skeletal import data with the existing asset's import data and disabling new PhysicsAsset creation. A clean destination/import is required when changing that initial unit/axis contract. Compare actual engine bounds with the manifest's meter bounds multiplied by 100 before accepting import; changing source export scale to compensate for ignored importer settings would conceal the cause and risk double scaling.

The three shared material definitions are `M_RiftSurface`, `M_RiftGlow` and `M_RiftGlass`. Meshes use at most three slots. The atlas supplies BaseColor, packed occlusion/roughness/metallic, Normal and TeamMask. BaseColor is sRGB; the other channels are linear. TeamMask identifies deliberately small trim/band/crest regions, allowing team tint without repainting the whole character. Use a team parameter in Unreal material instances. Glow is restricted to functional runes/crystals/eyes. Assets have authored UVs; LODs retain them.

The manifest provides relative exported paths, SHA-256 hashes, bounds in meters, triangle and bone counts, materials, clip durations/loop status, contact phase, LOD mappings and anchor coordinates. Troop anchors are actual terminal skeleton bones (`attack_origin`, `hp_anchor`, `status_anchor`, `impact_origin`). Unreal may use bone anchors directly or create sockets attached to those bones. Static tower anchors are manifest coordinates that the importer creates as sockets.

For Archer Tower, the round static tower and `tower_archer` rig are assembled as two components. Place the miniature archer at `[0, 0, 1.94]` meters with uniform scale `0.40`. Card data retains the unchanged gameplay footprint and lifetime. Navigation/combat collision stays authoritative in simulation; optional PhysicsAssets are visual/camera support, never a replacement for deterministic unit radii.

## Animation and simulation synchronization

All troops have Idle, Locomotion, Attack, Hit, Death, Deploy, Status, Turn and Acquire. Special clips cover charge, frost bite/breath, wing cycles, Raven aura charge/pulse/stun, bow draw/release, Mage cast, alternating blades and heavy stone impact. Locomotion is in place. Player/environment movement comes from simulation. Attack clips have wind-up/contact/recovery within one action; the manifest identifies contact at normalized time `0.48`. Time-scale the clip to the locked card hit interval and align the authoritative release event to visible contact. Do not add animation delay to combat or allow an animation notify to own damage. Headless Meta and recorded replay use the same authoritative event.

World-facing is a single convention. Do not restore the browser's imported-model-specific 180° pivot. Flying wing/tail bones animate independently of actor transform. Swarm members use deterministic animation phase offsets. Death lowering is authored in world-relative bone space and leaves the simulation root in place. Particle/audio events use sockets and gameplay event timestamps.

## Verified evidence and remaining review

`asset_qa_report.json` records 1,123 passing source/export checks with zero errors. These check vertex weights, skeleton/anchor completeness, UV validity, bounded material slots, all expected actions, exported skin/animation stacks/bone tracks, ordered LOD reductions, hashes and texture existence. An independent Ironclad FBX reimport confirms source dimensions survive declared-unit conversion.

Actual studio renders and action poses are saved in `Renders/`. Visual review resulted in several concrete corrections: closed layered Ironclad armor, carved human faces and open hoods, fully masked Twin Blades, Mage brim/robe/staff detail, a continuous skinned animal body, feline Frost jaw/paws, tapered curved Manta wings, round Archer Tower masonry, clearer Raven feather layers, corrected bow-arrow attachment, sword contact poses and death lowering.

The numerical audit does **not** certify final production art quality, performance, Unreal material fidelity or packaged animation behavior. The models use a compact stylized sculpture language; portrait card art has more surface detail. Final acceptance requires the engine contact sheet, card/model comparison, both-team facing checks, animation/VFX synchronization, assembled building review, LOD transitions and crowded match profiling in the packaged game. Those engine checks are reported by the main project QA separately.
