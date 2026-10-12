# Approved expansion source assets

The four new playable illustrations, four new skeletal families (including the
Skeleton child), three effect meshes, and five identity sheets are authored
production source. This document records source validation. Native import,
shader compilation, battle-camera appearance, and runtime abilities require
the subsequent pinned Unreal runs.

## Geometry and joints

| Asset | Authored bones | LOD0 / LOD1 / LOD2 / LOD3 triangles | Supplied model scale |
| --- | ---: | --- | ---: |
| Bone Warden | 39 | 45884 / 22942 / 11456 / 5482 | 1.50 |
| Bone Warden Skeleton | 27 | 14696 / 7348 / 3674 / 1760 | 0.65 |
| Aether Reaper | 28 | 17343 / 8671 / 4335 / 2079 | 1.10 |
| Iron Giant | 34 | 22436 / 11218 / 5608 / 2690 | 1.00 |

The standard FBX import can add an Armature root; actual imported counts must
therefore be read rather than confused with the authored counts above. Source
is meters, Blender −Y forward/Z up; the existing converter requests Unreal +X
forward and unit conversion with import scale 1. Logical tiles now render at
125 cm. These assets use the same normalized tile convention, independent of
simulation collision shapes.

Bone Warden has connected ivory anatomy, fitted black iron plates, bronze
trim, and four small skeleton bodies recessed behind its ribs. The retained
`soul_seal_0..3` roots now own those bodies; `captive_head_0..3` and
`captive_arm_0..3` give each head and arm independent, phase-offset living
motion. The maul follows `maul_socket`, with separate palm targets, curled
finger and thumb surfaces, and clear wrists around its shaft. The child is
the thin ivory sword-bearing skeleton
`bone_warden_skeleton`; it is not a deck card and has no recursive summon.

Aether Reaper has no legs or wings. Its silver crescent follows
`scythe_socket`; separate palm targets and lateral forearm approaches keep
its open grips and cuffs clear of the handle during the swing. The opaque
upper cloak and the lower tips are separate material
assignments. Six cloth roots and six tips articulate the lower cloak without
cloth physics. The dedicated lower-tip material defaults to opacity 0.68;
the body keeps its opaque material. Its declared native bob limit is 0.08 tiles.

Iron Giant has the underlying chest mesh and six shield-only deform bones,
all parented to `chest`. Editable source panel objects are physically attached
to those bones and excluded from the runtime skeletal export. Hiding a shield
bone can hide its weighted panel without hiding the torso.

| Bone | Source center, meters |
| --- | --- |
| `shield_panel_0` | −0.31, −0.22, 0.86 |
| `shield_panel_1` | 0.31, −0.22, 0.86 |
| `shield_panel_2` | −0.50, −0.05, 1.07 |
| `shield_panel_3` | 0.50, −0.05, 1.07 |
| `shield_panel_4` | −0.28, 0.22, 0.92 |
| `shield_panel_5` | 0.28, 0.22, 0.92 |

The dedicated fragment path is `/Game/Rift/Environment/SM_iron_shield_fragment`.
Shadow Surge exports `/Game/Rift/Environment/SM_shadow_surge_wisp` and
`SM_shadow_surge_ribbon`, each with two lower static LODs. Shadow is a spell;
the manifest has no Shadow creature mesh, rig, or skeletal binding.

## Animation contracts

Every new skeletal family has Idle, Locomotion, Attack, Hit, Death, Deploy,
Status, Turn and Acquire at 100 FPS. Roots remain in place. An attack's single
contact marker sits after its anticipation; damage and cooldown are owned by
the simulation. Death cleanup contracts are source metadata consumed by native
presentation rather than additional damage or spawn logic in an animation.

| Asset | Anticipation | Recovery after contact | Attack clip | Locomotion cycle | Death cleanup |
| --- | ---: | ---: | ---: | ---: | ---: |
| Bone Warden | 0.34 s | 0.48 s | 0.82 s | 1.10 s | 0.90 s |
| Skeleton | 0.12 s | 0.20 s | 0.32 s | 0.55 s | 0.45 s |
| Aether Reaper | 0.22 s | 0.38 s | 0.60 s | 1.20 s | 0.75 s |
| Iron Giant | 0.22 s | 0.38 s | 0.60 s | 0.90 s | 0.80 s |

Warden and Reaper use constrained weapon translation and two-bone arm IK.
The source gate checks actual evaluated hand, finger, thumb and wrist
surfaces against both the handle and weapon head throughout held half-frame
poses. It also checks contact gaps, reach, containment and finite quarter-frame
matrices, rather than treating a palm-center match as proof of a clear grip.
The Warden
releases the maul during Death deliberately; that clip is excluded from the
living two-handed attachment rule. The corpse and maul have no source physics.

The art revision improves the existing Mini Stampede/Stampede geometry and
planted gallop, while preserving their approved gameplay values and collision
shapes. Importer stride binding normalizes source stride by the
approved canonical scale, now Mini 1.30 and Stampede 0.85, so their reference
gait remains distance driven at the supplied pace.

## Materials, portraits, and editable sources

Only the new models use the separate `T_RiftAscensionAtlas` texture family and
`M_RiftExpansionAscensionSurface`, `Glow`, and `Glass`. The older atlas families
are preserved. New pixels include black iron, ivory, midnight cloth, violet
lining, green soul details and amber eyes; team color occupies the ordinary
team-mask palette slot.

`M_RiftCloak` is a separate masked Default Lit material, with violet-gray base,
Fresnel edge emission capped at 0.45 per channel, and deterministic 6 cm world
pores adjustable by `CloakAmount` (default 0.18; clamped 0..0.35). It has no
Material Time. Native code controls owner visibility and restores original
material instances; enemy hiding is not simulated by adding an opaque cloud.
The importer checks all eight real cloak connections. The three added
lower-tip opacity connections also require successful UE return values.

`M_RiftExpansionFX` is the sole-purpose ribbon/wisp material. Native batches
set `EffectColor` uniformly, so green Warden threads, cyan Reaper contact and
violet Shadow effects cannot be overridden by a non-team atlas cell. RGB is
clamped 0..1; atlas RGB is excluded. Authored atlas alpha alone multiplies
`EffectOpacity` (default 0.55; clamp 0..0.65). The actual palette atlas alpha is
255 throughout, so the authored geometry supplies the ring/wisp silhouette;
this is not claimed as a painted opacity gradient. `EffectEmission` defaults
to 0.25 and clamps 0..0.45 per channel. The material is translucent, Default
Lit, two-sided, roughness 0.8, and supports instanced static meshes. It has no
Time node, extra lifetime, collision or damage. All eleven connection return
values are checked; CPU tests reject each false return and a missing atlas.

The manifest's one `presentationMaterials` record pins the actual importer,
contract producer and opacity texture. The strict source-chain validator
checks that exact record against the generator's bounded declaration. Adding
this shader does not rewrite Blender source, meshes, animations or portraits.
`Build/generate_expansion_fx_contract.py --write` updates this approved record;
without `--write` it checks the current declaration without mutations.

The four deck portraits have 1024×1280 masters, 768×960 runtime PNGs, and
96×120 reviews. The Warden portrait contains one Warden and exactly four
subordinate Skeletons; Reaper and Giant each contain one model. Giant has an
illustration-only panel cutaway, preserving one character. Shadow contains
three affected allies, three source wisps and a low source ring. No text or
game UI is baked into the art.

The optional child identity portrait lives in `CardArt/Children`, outside the
21-card deck portrait inventory. Each new identity sheet contains actual front,
rear, side, three-quarter and monochrome source views, palette, joints and
timing data. Shadow's identity views contain only the real effect meshes.
New `.blend` portrait scenes are startup-ready and pack their material images.

## Preservation and reproducible evidence

`Assets/Source/Revisions/ApprovedExpansion` retains the exact accepted
pre-addition scene, manifest, portrait report and pre-append audio manifest.
The original portrait-render source SHA remains unchanged. The first two
historical source stages connect the earlier Pulverizer Death repair and the
approved additive production. The third stage records the selected art polish
with exact model, atlas, portrait, scene, identity and producer pins. Historical
renders retain their original attribution.

`VerifiedModelAdditions.json` independently rereads both actual saved scenes
and compares every retained object and action signature. This covers mesh
vertices, topology, UVs, weights, material slots, rest bones, transforms and
animation curves. The retained original generation report had a dictionary
alias in its source fields; its raw bytes are preserved. The producer now
deep-copies the prior manifest, and the independent read-only proof uses exact
pre-addition and final source pins. No scene or export was rewritten to fix
that report.

The current saved source is
`b7c215a00681328298919bb147900285a00335729934d08c28d9b7c608296929`.
Its complete seven-rig geometry and animation check passed 1,297 checks with
zero errors. All 63 selected clips reproduce their saved poses with zero
matrix delta. The separate matrix check passed 704 checks across all 176
clips, sampling 30,572 quarter frames and 812,079 pose matrices. The saved-scene
comparison preserves all 48 mesh/rig objects and 113 actions outside the
selected collections. These are source checks; runtime appearance and imported
animation bindings have separate native evidence.

The final saved-source repair assigns the nine previously unweighted Reaper
scythe-tip vertices to their existing weapon bone. It preserves vertex
positions, topology, UVs, material slots, existing positive weights, rest bones
and all 176 actions. The guarded repair and original rejected source-export
check remain retained evidence. The complete source checks above were rerun
against this repaired scene; they are not substituted from the earlier scene.

`ArtPolishBefore1.6.0` preserves the actual before scene, capture and inventory.
The two small historical portrait and identity indexes were recovered from
their retained records using their original serialization. Both recovered
files match the independently recorded original byte lengths and SHA-256
hashes. The original capture and inventory remain exact, and the resolver
permits only those two named metadata files. Current renderer summaries and
the child identity portrait are checked against the actual selected source,
meshes, portrait records and sheet pixels.

The audio proof checks the exact unchanged 61 records, metadata and WAV master
SHAs, and permits only the two approved `shield_break` and `shadow_surge`
additions. It checks all 63 current WAV pins. No audio preservation check is
silently skipped.

Historical additive-source QA retained under `Artifacts/QA/ExpansionAssets`
before the later art revision:

- `approved-source-contracts-20261011-04.json`: 192 checks; all 36 new clips
  reproduce every saved quarter-frame pose with zero matrix delta.
- `approved-all-matrices-20261011-04.json`: 705 checks across all 176 current
  clips; 30,572 quarter-frame samples and 790,951 pose matrices, with no
  nonfinite or singular source/FBX transforms. The old failed Death FBX remains
  tracked and reproduces its unsafe scaling.
- `approved-source-export-20261011-04.json`: 1,761 checks, including real source
  and FBX structure/units/rigs/LODs and the independent unit roundtrip.
- `approved-portrait-pins-20261011-05.json`: 21 playable cards, 776 checks.
- `approved-fresh-tracked-provenance-20261011-01.json`: 784 exact referenced
  files copied into a temporary root without `Artifacts`; baseline and 14
  isolated rejection cases passed. Fixtures never enter canonical data.
- `approved-import-contract-20261011-01.json`: 17 actual producer fixtures for
  new reflected material slots, opacity returns and approved gait/bob values.
- Fresh cloak, existing material binding, shockwave and 125 cm grid producer
  checks also passed. These are source/API-contract tests, not GPU claims.
- `approved-expansion-fx-shader-20261011-01.json`: 4,027 checks on the actual
  uniform FX shader, authored alpha, eleven return contracts and bounded
  opacity/emission samples. Fresh portrait/import/cloak/binding/shockwave/grid
  checks under filenames containing `-fx-` cover the final shader producer.

Historical pre-art source SHA:
`46fbbcf748d5b023987a33d2bdba08c19d4cc488dd591b0aacec313cc0c58914`.
The current source revision requires its own saved-scene, export, portrait
and native acceptance; the historical checks above do not certify revised
geometry. Inspect all six paid
troop plays from both sides at the actual battle camera, HIGH and LOW, then
shield damage/break, cloak owner/enemy/expiry, Warden death summons and all
effect cleanup using the production paid-match fixtures. Read back materials,
bone names, axis, clip sample rates and stride values; screenshots alone do not
prove those bindings.
