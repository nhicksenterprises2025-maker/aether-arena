# Rift Crown Arena — Unreal production asset specification

This is an audited design and import contract, not a statement that production assets already exist. The current V15 browser game is the identity and gameplay reference. All card names, balance, placement, footprints and combat timings are preserved. Meshes, illustration, rigging, audio and presentation are rebuilt with original authored designs. No Supercell material is used.

## Inspected reference

- Full UE5 rebuild request: `C:/Users/Noah/.codex/attachments/b55a69b8-ba32-4947-8205-e1067dc7e4dd/Pasted text.txt`.
- `rift_crown_arena_v15/blender/generate_models.py`: complete forge, all material definitions, all fourteen model builders and V3/V4/V8/V9 finish passes.
- `rift_crown_arena_v15/blender/rift_crown_models.blend`: opened read-only in installed Blender 5.2.2 LTS. Fourteen populated model collections, zero armatures, zero actions, zero LOD objects and no authored texture images.
- All fourteen `assets/models/*.glb`: inspected binary glTF headers/accessors/materials/nodes. All have zero skins, animations and textures. Geometry is grouped static parts, with flat authored PBR material colors and emissive accents.
- All fourteen card SVGs and the original Rift crest: inspected source. Card illustrations are 512 × 640 vector drawings; crest is 160 × 160. No raster source illustrations exist.
- V15 `src/game.js`: card definitions; arena geometry/sky/water; imported and procedural model construction; facing normalization; tower/compact troop HP sprites; troop animation; deployment/projectile/hit/death/status/meteor/Nova/Bullet effects. V15 HTML/CSS home, cards, loadout, battle HUD, Meta Lab, replay and Developer Lab presentation was inspected.

| Reference mesh | Triangles | Mesh parts | Materials |
|---|---:|---:|---:|
| Ironclad | 7,224 | 48 | 12 |
| Ember Archer | 7,404 | 44 | 11 |
| Twin Blade | 6,456 | 38 | 9 |
| Boulderback | 6,992 | 38 | 4 |
| Arc Mage | 5,700 | 37 | 10 |
| Rambeast | 7,224 | 40 | 11 |
| Sky Manta | 4,820 | 30 | 4 |
| Vampire Bat | 2,008 | 13 | 6 |
| Frost Fang | 5,156 | 26 | 7 |
| Storm Raven | 4,692 | 22 | 6 |
| Nova Flask | 3,560 | 22 | 4 |
| Archer Tower | 5,052 | 29 | 12 |
| Guard Tower | 9,508 | 42 | 11 |
| Core Tower | 10,540 | 45 | 10 |

Current animation uses whole-object bob, squash and rotation. Bats/Raven rotate wing parts without bone rigs; Manta squashes its body rather than deforming fins. Imported glTF models require a 180° pivot to match fallback facing. These are reference behavior limitations that the production skeleton and import contract must resolve.

## World and material language

The arena is a weathered mountain citadel suspended above a pale blue Rift valley. Moss-green field, cool pale limestone, dark ironstone foundations, reddish brown structural wood, aged brass details and concentrated cyan/violet Rift crystal accents form a single coherent palette. Use stylized PBR with deliberately shaped broad planes, bevels, layered armor and organic volumes; avoid literal primitive stacks or dense noisy photorealism. Silhouette and weapon behavior remain recognizable at the battle camera.

Team identity uses cyan cloth/crest inserts for friendly and coral-red equivalents for enemy, plus a thin ground ring. Team masks affect authored trim areas, never repaint the whole unit and erase its material identity. Frost remains icy; Raven remains indigo; Manta remains teal. Emission is restricted to focal runes and functional status/VFX. Metallic only on real metal. Cloth/fur/stone remain rough and nonmetallic. At least three clearly distinguished material families on each substantial character. UVs support baked normal/roughness/occlusion and an explicit team mask.

Model coordinates: a single UE-facing convention, local +X forward, +Z up; ground pivot at foot plane; aircraft pivot at body flight plane. Mesh import transforms are baked once. No team-dependent ad hoc 180° compensation. Simulation supplies desired facing; presentation turns toward it without changing acquisition direction or movement. The camera generally sees friendly backs and hostile fronts while units advance. Source Blender units use meters; Unreal import uses centimeters. One gameplay tile remains 100 cm, with one authoritative shared conversion.

## Character identity and production rebuild

| Card / asset | Identity retained | Production model and material detail | Rig and required special animation |
|---|---|---|---|
| **Ironclad** | Broad steel knight, angular enclosed visor, cyan runes, short blue cape, left shield and right sword, brass trim/plume | Articulated layered breastplate, asymmetric shield silhouette, real sword fuller and crossguard, plate seams, gloves, shaped boots, cloth folds. Slate iron, silver edges and restrained aged brass. | Humanoid 35–55 deform bones plus shield/sword sockets and cape bones. Shielded walk, deliberate sword wind-up/contact/recovery, shield hit reaction, weighted collapse. |
| **Ember Archer** | Narrow rust-orange hood and tunic, dark leather, wooden bow, quiver, ember arrow/charm | Hood opening and face planes, curved recurved wooden bow with taut string, leather bracer/straps/pouches, visible fletching, gold clasp and ember tip. | Humanoid compatible core skeleton; bow arm/hand and string control. Ready, nock, draw, hold, release, recovery, running carry. Release projectile at authored release phase. |
| **Twin Blades** | Two small masked violet assassins, dark hood, twin silver blades, bright violet eyes/sash | Slim athletic stance, layered wraps, split coat tails, distinct curved dual weapons, leather grips and tiny violet inlays. Same base mesh for both units with phase variation. | Humanoid core plus sash bones and two weapon sockets. Alternating left/right strikes; quick planted changes of direction; distinct independent walk/idle phase. |
| **Boulderback** | Huge low stone quadruped, boulder shell plates, broad craggy face/tusks, purple crystal spines and veins | Continuous carved stone body and joints, interlocking shell slabs, inset emissive cracks, large planted feet, heavy brows and mineral crystal facets. Reads as a living siege creature, not a human rock suit. | Quadruped 25–40 bones plus shell/crystal sockets. Slow weight-shifting gait, heavy forebody slam, grounded hit reaction, fractured stone death. Feet planted via locomotion phase. |
| **Arc Mage** | Indigo/violet robe, pointed hat, gold belt, cyan orb staff and spellbook | Sculpted robe and hat silhouette with fabric folds, angular leather satchel, brass staff mechanism surrounding a cyan crystal, readable hand/face shapes, clasped book. | Humanoid core plus robe/hat secondary chains and staff/book sockets. Gather/wind-up, staff thrust/cast release, settling recovery, robe motion. |
| **Rambeast** | Broad brown horned quadruped, large curled ivory horns, iron saddle/head armor, gold harness and warm gem | Sculpted muzzle and ears, layered fur masses, detailed horn grooves, reinforced harness, chest straps, worn hooves and armor bevels. | Quadruped family 30–45 bones; horns and saddle rigid weighted; tail/ear bones. Trot, charge acceleration, lowered-head sprint, horn impact and recovery. Charge pose represents existing charge state without extra charge damage. |
| **Sky Manta** | Teal floating ray, swept broad fins, long tail, cyan eyes and dorsal Rift markings | Continuous organic ray mesh with curved wing membranes, articulated tapering fin edge and stabilizer fins; soft glossy skin with subtler underside. Cyan markings follow body curvature. | 25–40 bones: spine/tail, three or more segments per wing and fin tips. Traveling fin wave, hovering fin wave, banking turn, ranged pulse discharge, falling death. |
| **Vampire Bats** | Five small plum bats with pointed ears, red-violet eyes, pale fangs, bright chest core | Actual bat membrane with finger struts/scalloped trailing edge, compact furry torso, expressive ears and hooked claws. Deep plum with muted rose membrane; small pale fangs. | 20–30 bones per bat including wing fingers, ears and jaw. Fast independent wing cycles, short bite lunge/recovery, fluttering stun, dive/fall death. Seeded phase offsets keep swarm alive without altering positions. |
| **Frost Fang** | Heavy pale blue sabertooth predator, massive paired white fangs, icy spine crystals, blue steel harness, cyan eyes | Sculpted cat/wolf-like heavy predator anatomy, defined paws and jaw, short pale fur masses, frosted plate armor, translucent-but-readable ice facets. No perpetual blinding frost cloud. | Quadruped family plus jaw/tail/ears. Stalk/run, frost bite and jaw release, breath idle, turn, slowed victim effect at hit contact. No extra damage from ambience. |
| **Storm Raven** | Large indigo raven, broad cyan-accented wings, pale beak, glowing chest core and electric rings | Layered sculpted overlapping primary/secondary feathers, segmented leading wing, recognizable raven head/beak/talons, restrained lightning conduits along feather ends. | 35–55 bones with shoulder/elbow/wrist, primary feather fans, neck/head, tail and talons. Full wing beat, hover, bank, lightning release, aura gather/pulse/recovery, stun discharge. Aura actual radius stays 2 tiles. |
| **Archer Tower** | Round pale stone defensive tower, wood platform, brass cap trim, parapet and small hooded archer | Stone coursing, timber joints, carved team crest and banner, coherent platform silhouette. Archer is skeletal subassembly rather than frozen head/bow. | Static tower with destruction pieces and a skeletal archer using Archer bones. Bow turn/draw/release; construction/deploy reveal; lifetime erosion/effect. Footprint remains 1.65 tiles and gameplay collision is separate. |
| **Nova Flask** | Violet glass potion flask, bright purple core, cork/neck, gold cage bands | Shaped bottle with polished glass, restrained interior energy swirl, worn brass support cage, readable cork/stopper and inscribed rings. Original gameplay remains immediate spell impact; decorative bottle travel cannot delay or add damage. | Static mesh plus spin/presentation curves. Shatter fragments/VFX driven by the authoritative spell event. |
| **Bullet Burst** | Seven warm metallic tracers/compact repeated impacts, steel/gold illustration identity | Original cartridge/shard props with brass casing and steel point; no firearm copied from another game. Seven distinct readable tracers converge into a compact area. | No troop skeleton. Initial damage event is authoritative; tracer visual timing must begin at impact or be previsualized without introducing hidden impact delay. |
| **Meteor Shards** | Orange-gold falling Rift shards, dark scorched ground, persistent ember zone | Shaped fractured molten stone chunks with cooled black rock and thin orange seams; persistent ground decal/rim visually shows exact 4.5-tile radius. | Rotating shard meshes and Niagara streaks/impact ember emitters. Initial impact and five 40-damage ticks remain exact; visuals cannot imply tower damage. |

## Crown architecture

Guard: stepped ironstone plinth, square pale masonry body, buttress corners, deep narrow slits, brass horizontal band, original Rift crest, crenellated parapet and an articulated short cannon barrel. Cannon base/yaw, pitch and barrel recoil are bone or component driven. Core: larger related architecture, distinct high crystal spire, suspended metal halo and crown finials. Dormant core crystal is subdued and its weapon mechanism stays inert; no targeting, muzzle flash, recoil, projectile, attack audio or damage before one allied Guard falls. Activation is a single unmistakable crystal/light/metal-unfolding event. Both have preauthored fractured pieces and persistent low rubble which does not create new path obstacles.

## Arena construction

Maintain the audited V15 playable rectangle: width 28 tiles, length 42 tiles; river half-width 1.65; bridge centers at ±7.2; bridge width 4.2. Terrain dressing must not move collision boundaries. The two bridges are engineered wood spans between pale stone abutments with iron braces and shallow railings. Detailed bank masonry and stones follow the exact river exclusion geometry. Camera-visible lane floors have moss/grain variation, low-profile worn stone path pieces, sparse short grass and readable 1 × 1 grid seams. Decorative crystals, torches, trees, flags and ruins remain outside legal combat/placement areas.

Use separate environment assets for bridge deck, abutment, railing, bank segment, floor segment, boundary stone, grass tuft, shrub, tree, banner, crystal plinth, ruin and distant floating island. Instancing handles repeated decoration. Sky/fog remains at perimeter/horizon. Water has slow flow/foam/depth color and modest reflected light; no river fog over combat lanes. Foliage/cloth world motion is subtle and presentation-only.

Placement preview uses a complete square centered on the legal tile; buildings display every occupied tile, not a circular approximation. Grid is present across the board with restrained contrast and strengthens during placement. Valid friendly preview is cyan; invalid preview coral red. Pocket overlay only illuminates the unlocked lane rectangle from river approach to former Guard approach, never near/behind the Core.

## Skeleton, animation and collision contract

Every troop ships with Idle, locomotion, attack wind-up/release/recovery, hit, death, deploy, status, turn and acquisition clips. Attack phases are time-scaled to locked card hit interval. Gameplay schedules release/contact deterministically, with presentation phase aligned; animation notifies can trigger VFX/audio but cannot own damage authority or cause missing attacks in headless Meta/replay. First attack and stun interruption behavior require parity tests against the V15 simulation. No automatic rebalance through extra animation latency.

Animations are authored in place. Root motion is off for gameplay movement; navigation supplies transforms. Death never restarts AI or blocks the game loop. Flying characters animate body/wing chains rather than entire-object squash. Health widgets anchor to explicit `hp_anchor` socket. Common sockets: `attack_origin`, `impact_origin`, `status_anchor`, `hp_anchor`, `foot_l`, `foot_r`; quadrupeds add four foot sockets; flyers add `wing_l_tip`, `wing_r_tip`; towers add `muzzle` and `crest`.

Physics bodies are compact primitive approximations for visual hit/camera behavior. Deterministic unit radius and structure footprint remain card/simulation values; no complex mesh collision driving combat/pathfinding. Clear material/vertex masks exist for team trim, hit flash, frost/stun and lifetime fade. Maintain independent state per spawned bat/assassin. Preserve exact logical status end times when seeking replays.

## Niagara and presentation event contract

Niagara systems: deploy ground rune; deploy column/embers; sword arc/contact; arrow trail/contact; arc bolt/splash; cannon muzzle/trail/structure chip; Manta pulse/trail; Bat bite; Ram charge dust/impact; frost breath/contact/slow status; Raven bolt/aura charge/pulse/stun; Bullet tracers/impacts; Nova glass/shockwave; Meteor descent/impact/persistent zone/tick; building construction/expiry; Guard/Core destruction; Core awakening.

Every event includes match time, source entity/card/team, target/position, actual damage/status, radius, visual seed. Particle random seed is isolated from gameplay RNG. Pool transient systems, cap emitter counts and distance-cull harmless secondary particles. Telegraph/zone outlines remain visible at every graphics preset; effects quality reduces embellishment rather than functional information. No full-screen fog, large persistent lens flare or excessive bloom. Spell initial impact and projectile contact are visible before damage number/hit reaction.

## Illustration and UI direction

Fourteen replacement illustrations share a 4:5 portrait composition, high-detail stylized fantasy render/painting language, strong directional warm/cool lighting, clear character silhouette and quiet arena context. Card subject retains approximately 70% height in the central crop, with margin at top-left for the separately drawn cost badge. No text baked into illustrations. Do not depict different weapons/armor/anatomy from final meshes. Art approval follows model design approval. Twin Blades image contains exactly two duelists; Vampire Bats depicts the five-unit swarm. Spell illustrations portray the actual flask/tracers/shards and their actual palette. Small-scale tests at battle card size are required, not only large renders.

UI uses original Rift crest, carved dark stone/iron panels with restrained brass edge, crisp ivory type and cyan active focus; Aether remains violet. Layout is deliberate and readable rather than generic glass-gradient dashboard chrome. Home has strong Battle action and direct Profile/Loadout/Cards/Meta/Patch Notes/Training destinations. Battle keeps only timer/crowns/hand/next/Aether with compact HP and selected-card inspection on demand. No emoji icons in final UI. Grid/stat-heavy screens are confined to menus, postmatch analysis and Developer Lab. UMG/CommonUI screens anchor to safe zones for 16:9, 16:10 and 21:9 and apply the saved user UI scale.

## Source and import deliverables

Editable Blender source, rig source, named actions, FBX or supported interchange files, texture source, generated renders, illustration originals, audio WAVs, Niagara assets, material masters/instances and import tooling are versioned. Assets use stable names: `SK_<CardId>`, `SKEL_<RigFamily>`, `AN_<CardId>_<Action>`, `SM_<StructureOrProp>`, `M_RiftCharacter`, `MI_<CardId>`, `T_<CardId>_<Channel>`, `NS_<Event>`, `T_Card_<CardId>`, `SFX_<Event>` under `/Game/Rift/`. A manifest records mesh/rig/material/animation/art mapping by unchanged card id and hashes/triangle counts/bone counts/clip durations. Shipping hard references or explicitly cooked directories retain all assets; no editor-only import code is needed at runtime.

Target budget per principal troop: roughly 12–30k triangles at LOD0, then approximately 50%/25%/12% in three reduced LODs. Bat/assassin budgets are lower for five/two-instance cards. Use 1–3 consolidated material slots where possible and shared atlases for props; LODs must retain ears, horns, fangs, primary wings and weapon silhouette. Character texture sets are 1–2k, large architecture 2–4k, with configurable texture scalability. These are production targets, not substitutes for measured frame-time acceptance.

## Acceptance evidence required

1. Every final mesh has a unique authored design above; source and packaged mesh match; no primitive fallback/graybox final geometry.
2. Skeletal rigs, required actions, LODs, collision and sockets validate automatically for all ten troop meshes plus tower archer.
3. Player/enemy facing test for each card, all travel/attack/turn/death states; no mirrored weapons from negative scaling.
4. Card art compared with final mesh contact sheets; visual differences corrected before release.
5. Attack/contact/VFX/audio synchronized at 0.25×, 1× and 4×; Frost status refresh, Raven aura/stun and Meteor entry/exit shown accurately.
6. Readability contact sheet and full-match screenshots at 1280×720, 1920×1080, 1920×1200, 2560×1440 and 3440×1440, plus UI scales.
7. Worst-case bat swarms, crowded Boulderbacks, buildings near both bridge mouths, all spell zones and debug toggles profiled at representative settings.
8. Every asset loads from packaged Shipping build with no editor dependency; missing/corrupt asset failure is logged and recoverable, and cannot kill simulation.
9. Niagara functional boundaries remain correct at Low quality; dormant Core has no attack presentation or hidden damage.
10. Sound events/volume groups and milestone announcements are checked in actual packaged matches; source/build/release report distinguishes demonstrated checks from untested targets.
