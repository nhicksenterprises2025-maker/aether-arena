# Models, card portraits and battle motion in 1.2.0

The fourteen card portraits are studio renders of the production models used by the native game. The source meshes, rigs, materials and animation actions supply both the FBX exports and the portrait subjects. Portraits use an orthographic three-quarter camera, per-card bounds fitting and a slate studio with warm key and cool fill/rim lighting. No diffusion image, generative paint-over or substitute character is used.

## Source and binding provenance

`Build/generate_assets.py` authors and exports the models, rigs, clips, LODs and textures. The production Blender scene is `Assets/Source/RiftCrown_ProductionAssets.blend`; `Assets/asset_manifest.json` identifies its hash and each export, animation, material and card binding.

Run `Build/render_card_portraits.py` in Blender after the final model export. It opens the production scene identified by the manifest and renders 768 × 960 PNGs into `Assets/Source/CardArt`. `--cards <id> ...` selects a subset when intentionally updating individual portraits; a full model revision requires all fourteen. The renderer updates the manifest's `illustrations` entries and writes `model_portraits.json` beside the PNGs. That report records the source scene, referenced FBX exports, action and pose frame, model bounds, orthographic camera scale, and each image's byte length and SHA-256 hash.

`Build/validate_card_portraits.py` independently checks all fourteen canonical IDs, the exact source scene and FBX/image byte hashes, the PNG signature and 768 × 960 IHDR, recognized model/action bindings, and finite pose/framing metadata. It needs only standard-library Python and writes a report with relative paths and source/report/script provenance to `Artifacts/QA/model-card-portraits.json`. Earlier illustration prompts remain as historical source material in `Assets/Source/CardArt/Historical/illustration-prompts.json`; they do not supply the current portraits.

Most troop portraits show an authored idle or wing pose. Twin Blades contains two copies of its production rig; Vampire Bats contains three. Archer Tower combines the actual static tower and animated tower archer. Bullet Burst, Nova Flask and Meteor Shards use their exported static projectile/prop meshes. Those scene arrangements are portrait composition, while each underlying model comes from the production asset source.

`Build/import_unreal_assets.py -RiftModelsOnly` imports the revised meshes, skeletons, clips, materials and portraits, saves/finalizes the skeleton and Physics Asset dependencies, scans their files into the asset registry, and binds the native card DataAssets. It preserves the accepted audio and existing Niagara graphs. The native bindings are:

| Asset | Native path |
| --- | --- |
| Card portrait | `/Game/Rift/CardArt/T_Card_<id>` |
| Character mesh | `/Game/Rift/Characters/<id>/SK_<id>` |
| Character clips | `/Game/Rift/Characters/<id>/Animations` |
| Static tower, prop or projectile | `/Game/Rift/Environment/SM_<name>` |
| Card DataAsset | `/Game/Rift/Cards/DA_<id>` |

The ordered regeneration and import commands are in [BUILD_AND_ARCHITECTURE.md](BUILD_AND_ARCHITECTURE.md). Source hashes and native asset bindings establish provenance; fresh packaged captures are also needed to establish what the player sees.

After the matching models are imported, `-RiftPortraitsOnly` updates only the portrait textures and their existing card bindings, with native binding validation. It leaves models, clips, audio and Niagara graphs in place.

The full asset forge uses `Build/split_guard_cannon.py` to separate the guard tower's seven original cannon parts into `SM_guard_cannon`, with its fixed architecture in `SM_tower_guard`. The helper also supports a narrow revision of the existing production scene and guard exports. `Build/import_unreal_assets.py -RiftGuardOnly` imports those two static meshes and their LODs with the existing materials. This narrow import leaves other models, clips, portraits, audio and Niagara graphs in place.

## Scale and animation

The authored characters gain 10% larger heads, stronger equipment and identity accents, and knee covers. The runtime visual multipliers are applied to each troop's existing canonical card scale:

| Troop | Visual multiplier |
| --- | --- |
| Ironclad | 1.80 |
| Ember Archer, Arc Mage | 1.90 |
| Twin Blades, Vampire Bats | 1.70 |
| Boulderback | 1.65 |
| Rambeast, Frost Fang, Sky Manta | 1.60 |
| Storm Raven | 1.55 |

Guard/core towers use 1.18 times their original visual scale. Archer Tower uses 1.22 times its source height and 1.15 times its normalized visible footprint, with the archer at 0.60 model scale on the adjusted perch. Its portrait uses that same assembled tower: the mesh's source XY bounds are normalized to the canonical 1.65-metre footprint before the visual 1.15 multiplier is applied. The deterministic placement footprint remains 1.65 metres.

`ARiftUnitVisual` applies those factors to the visual replica. Collision radii, movement speed, ranges, placement footprints and canonical card definitions remain in the deterministic simulation. Attack origins follow the enlarged model. Health/status annotations clear its visible top with a 30-centimetre world-space margin rather than retaining the previous model height.

Authored attack actions use eased anticipation, contact and recovery. The existing contact/release point remains at normalized clip position 0.48: an authoritative attack event puts the model at that pose immediately, then displays recovery. Wind-up uses the simulation's cooldown; animation does not postpone damage or projectile creation. Movement cycles advance with distance travelled, pose changes blend at runtime, flyers bank into turns, and death motion continues through its authored follow-through. Animation clocks follow the simulation so pause, slow effects and replay speed hold or advance the same presentation state.

An authoritative attack that occurs during the initial deployment interval takes priority over the deployment pose, preserving its visible release and recovery. Guard towers keep their foundation and crest in the team's fixed orientation; the separate cannon follows the firing direction and recoils after release. Archer Tower similarly aims its skeletal archer while the building remains fixed. Weapon aim and recoil use simulation time, including pause and replay playback, without changing attack timing or tower rules.

## Projectile presentation

`ARiftArenaPresentation` observes the simulation's projectile state and events. Ember Archer and Archer Tower use the arrow mesh; Arc Mage, Sky Manta and Storm Raven have distinct missiles; guard and core towers use visible rounds. Authored projectile bodies aim along the local flight direction, with model-axis correction where needed. Bounded arcs and lateral motion, rotating magical bodies, trails and contact effects add readable flight without changing shot endpoints or authoritative travel time. Source and target positions use the model's presentation anchors.

A newly activated projectile glow can exist before its first world tick, including after seeking a paused replay. After assigning the complete flight transform and effect parameters, the runtime checks that the authored Niagara asset is ready and its system age is still zero, then executes one bounded real graph step of 1/60 second and finalizes that work. It restores the simulation's playback dilation and pause state immediately afterward. An initialized system receives no further warm steps, so pausing does not keep advancing the glow. This initializes presentation particles only; it does not advance match time, create a gameplay projectile or apply a hit.

The particle material expands each billboard's actual vertices about its particle center through the `RiftSpriteScale` parameter: world-position offset is `(World-Center)*(Scale-1)`. The runtime binds a cached material instance with the effect's color and readability scale. `Build/import_unreal_assets.py -RiftParticleOnly` imports that material revision. Niagara spawn/update graphs, particle counts and clocks retain their existing authored values; diagnostics distinguish those raw sprite dimensions from the material-scaled dimensions.

Bullet Burst and Meteor Shards add animated mesh debris at their authoritative impact. Both spells resolve immediately in the simulation, so the visual debris begins at that contact rather than introducing a new gameplay flight. Nova Flask uses its production prop in the card portrait and retains its native Nova burst effect. Visual flight and debris never apply damage; the simulation remains responsible for every hit, area effect and tower result. Projectile bodies, trails and temporary debris are released on completion and cleared when the match or replay state changes.

## Battlefield and HUD fit

The four-card hand uses a centered 640 × 156 bottom dock. Four 116-pixel card buttons occupy one horizontal row with their existing 64 × 80 portraits; a 106-pixel Next panel sits at the row's end and the Aether indicator below. The dock has a 12-pixel bottom gutter. `RiftBattleLayout` shares the hand and clock bounds with the camera fit. The orthographic camera uses a 35° battle angle and reserves 168 + 14 pixels at the bottom, 68 + 14 at the header, and 268 + 14 for each normal sidebar. The Developer panel uses 326 + 14 on the right; its scroll area begins at 202 and leaves a 182-pixel bottom margin to clear the hand. The shallower angle applies to battle framing; the lobby camera retains its existing presentation.

Camera fitting accounts for viewport dimensions, DPI and interface scale and uses the closest safe fitted zoom. Its conservative model envelope spans X ±1750 cm, Y ±2450 cm and Z 0–620 cm, plus a core envelope at X ±350 cm, Y ±2000 cm and Z 0–620 cm. The shared battlefield screen bounds include the bottom reserve for annotation placement. Enlargement affects battlefield presentation; tile coordinates and the hand's portrait size remain unchanged. Final rendered field dimensions must be measured on the current executable, separately from the source fit calculation.

Health-bar widths adapt to the battlefield's projected tile pitch, with compact widths for paired units and swarms. Bar heights and annotation text use physical screen-pixel sizing independently of the global interface scale. The overlay positions labels within the camera's battlefield area and offsets crowded annotations with leader lines back to their model anchors; changing the HUD scale does not simply magnify every world label.

Troops, deployed buildings, guard towers and cores hide both their HP bar and numeric HP until their first positive finite damage. A visual first-damage flag remains set after healing to full health and resets when the visual is configured for a new entity. A partially damaged live snapshot can establish that history; a defensive building is compared with its undamaged lifetime-decay HP baseline so ordinary aging does not count as a combat hit. Charge, stun, slow, dormant-core labels and the Archer Tower lifetime indicator remain independent of HP visibility.

Opening a replay derives a per-entity first-hit timestamp cache from its actual positive damage events. The cache also recognizes recorded Developer crown HP reductions using the initial crown maximum HP, matching live Developer behavior. Each replay synchronization queries that history at its exact double timestamp: backward seeking before the first hit hides the HP annotation, while seeking after a healed full-health sample retains it. Closing a replay clears the cache. These flags and caches belong to presentation; the authoritative entity schema and numerical combat remain unchanged.

Release acceptance must use the final packaged build for roster, close combat, ranged flight, spells, replay, pause and the supported aspect ratios/interface scales. Source provenance, native structural validation and automation are separate from rendered review. Completed checks and any measured limits belong in [RELEASE_QA.md](RELEASE_QA.md) and its associated reports.
