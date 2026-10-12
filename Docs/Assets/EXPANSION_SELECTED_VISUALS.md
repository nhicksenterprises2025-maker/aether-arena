# Selected expansion visuals: implementation and camera review

Scope: existing Mini Stampede, Stampede, and Pulverizer. This document records
implemented source behavior, actual material/camera acceptance, and the review
procedure. The balance candidates and additional playable-card scope remain
pending; the checks below do not establish a completed expansion release.

## Asset and gameplay boundaries

- The editable production scene, selected exports, portraits, and tracked
  Pulverizer Death revision evidence remain frozen. Native surface effects do
  not change their bytes or relabel historical portrait render provenance.
- The selected source meshes have authored joints, animation actions, sockets,
  and three reduced LODs. Each card has a 1024x1280 portrait master, an optimized
  runtime portrait, a 96x120 review image, and editable portrait scenes.
  Mini/Stampede portraits show five/fifteen identifiable boarlets.
- Source axis: Blender -Y; imported model forward: Unreal +X. UnitVisual follows
  the entity's movement/attack facing. Team-facing static architecture remains
  separate from mobile character facing.
- Model contracts retain the brief's intended Mini 1.30, Stampede 0.85, and
  Pulverizer 0.95 scales. Pending numerical approval leaves current canonical
  gameplay scales at Mini 0.85, Stampede 1.30, and Pulverizer 0.95. Import binding
  normalizes gait stride to the actual canonical scale; this document does not
  imply that the pending balance patch has shipped.
- Presentation effects never write HP, collision, range, speed, deployment
  validity, attack interval, status duration, damage, or simulation events.

## Native surface effects

Implementation: Private/Presentation/RiftUnitVisual.cpp and its public header.
Arena integration: Private/Presentation/RiftArenaPresentation.cpp.
These C++ paths are relative to
Unreal/RiftCrownArena/Source/RiftCrownArena.

Effects reuse the authored SM_projectile_trail mesh as small surface grains,
flecks, and narrow ring segments, with per-batch dynamic instances of
M_RiftExpansionSurface. The existing atlas team-mask region supplies the selected
color. Dust has zero emissive gain and 0.76 dither dissolve, leaving sparse
visible coverage. These are native instanced surface meshes, **not Niagara
particles**. Geometry scale is an effect-size operation, not character animation.
Components have no collision, overlap, navigation influence, shadow casting,
independent tick, or persistent tile marker.

| Effect | Ownership and active bound | Size / placement | Timing |
| --- | --- | --- | --- |
| Mini hoof/deploy dust | At most four active grains per unit; deployment starts with two | Contact grains 5x3x2 cm at an authored hoof contact; deploy grains 6x3x2 cm | 0.22 s |
| Stampede hoof dust | Only entity IDs divisible by five emit contacts; at most four active grains per selected unit | Same authored hoof contacts; sparse trailing motion | 0.22 s |
| Stampede formation dust | One elected living owner per actual playId; at most four active grains for that formation | 12x6x2 cm grains below the current herd center, never fifteen dense clouds | 0.28 s |
| Boar death flecks | Exactly two copper plus one slate-gray instance per dying unit | Initial 4.5x2.5x1.8 cm; short local trajectory, capped at the floor | Starts 0.06 s after death; clears at the imported cleanup deadline |
| Pulverizer slow shackle | One 24-segment narrow ripple per recently hit ground target | About 8 cm above the ground; 2.2 cm segment thickness; radius follows target radius, clamped to 16–75 cm before the small ripple expansion | 0.28 s after the most recent active Pulverizer slow's recorded birth |

The Stampede owner is the lowest living entity ID in the playId group. Arena
computes leader and center in one pass over current entities. Ownership changes
clear the former leader's formation grains; units still move and fight
independently. Visual phase offsets never stagger actual deployment or damage.

Hoof emission follows half-cycle crossings of the existing distance-driven gait.
The current authored contact socket supplies its origin. All aging uses
State.elapsed; repeated renderer ticks during pause cannot age or duplicate
effects. Backward replay seeking resets arena replicas, while the slow shackle
also reconstructs directly from recorded active SlowEffect entries.
The dust CPU record list is separately capped at the newest four hoof and four
formation records. An internal invariant checks the eight-record total even
during repeated developer position changes at a paused timestamp.

Mini/Stampede knees and body use the existing authored collapse. A single
0.08-second warm material flash accompanies death. Their body dissolve and the
three flecks finish at the imported 0.65/0.60-second cleanup times respectively.
Nothing remains on the tile after actor/effect retirement.

Pulverizer's authored crystal cracks, falling piston, and two ring fragments
remain the death geometry. Its per-instance crystal glow brightens during
anticipation/contact and fades inward during the imported 0.85-second collapse.
Its existing pressure-front decal still takes radius from the authoritative
wave's birth, speed, radius, and expiration.

Selected cards no longer create the generic deployment cloud. Pulverizer
damage no longer creates a full Nova hit cloud, and its destruction no longer
creates TowerDestroy smoke. A short building contact sound accompanies its
collapse. The generic above-head Slow effect is suppressed when Pulverizer is
the sole active slow; a different simultaneous slow retains its existing cue.
Other cards retain their existing effect paths.

The sole-purpose M_RiftShockwave producer in Build/import_unreal_assets.py now
builds a lit translucent deferred decal with a thin bright violet front, a softer
purple trail, and radial floor-normal/roughness modulation. This is surface
normal distortion, **not refraction** or displacement of floor geometry:

- The leading support is 0.032 of the current wave radius at default settings
  (up to 12.8 cm at the four-tile radius), with a thin softened outer edge.
- The trailing support is 0.11 of the current radius (up to 44 cm); its opacity
  never exceeds 0.22. The center and everything outside the authoritative radius
  have zero coverage, preventing an opaque disk over the tile grid.
- Default normal slope is 0.035 (about two degrees), clamped to 0.08 maximum.
  The trail modulates roughness from 0.90 toward 0.82 at default settings.
- Inputs are normalized decal UV and bounded material parameters only. There is
  no Material Time, scene sampling, refraction, or independent progression;
  world size still comes from the existing simulation-driven DecalSize.
- Selected imports rebuild this one material only when Pulverizer is selected.
  Full and PulverizerOnly imports call the same producer. Legacy presentation
  material branches retain their existing graph behavior.

Build/validate_shockwave_shader.py executes the actual material producer against
a graph-recording API and evaluates its actual HLSL scalar expressions without
Unreal. The recorder models Unreal's unnamed ComponentMask input and rejects
invalid connections instead of optimistically recording them. The producer
initializes Custom inputs before connecting parameters and requires all native
connection/property calls to return true. It checks output wiring, normal normalization, parameter bounds, center
and outer-radius coverage, trail opacity, and mode routing. Its self-test rejects
an opaque center, opaque trail, missing normal output, and wrong decal shading.
Source checks do not establish shader compilation or rendered appearance.

## Diagnostics and automation

UnitVisual exposes active HoofDustCount, FormationDustCount, DeathFleckCount,
and SlowRippleCount. The Rift.Presentation.SelectedSurfaceEffects native
automation fixture uses actual imported card/model/material bindings. It checks
leader/nonleader output, bounded active counts, paused clocks, simulation-time
expiration, recorded slow reconstruction, three death flecks, and absence of
new authoritative events. The fixture requires RiftAutomationSandbox and
isolated save/user directories. Recorded native execution and final rerun
evidence are listed in Docs/QA/BALANCE_EXPANSION_PREPARATION.md.
It also exercises 128 position changes at one unchanged simulation timestamp,
while records cannot age out, through that CPU invariant and the active bound.

The expansion performance fixture samples native counts every frame before its
one-second record throttle. Under expansionFixture it records:

- maximumObservedHoofDustInstances
- maximumObservedFormationDustInstances
- maximumObservedDeathFleckInstances
- maximumObservedSlowRippleInstances
- maximumObservedSelectedSurfaceInstances
- lastNativeSurfaceCounts with hoofDustInstances, formationDustInstances,
  deathFleckInstances, slowRippleInstances, and selectedSurfaceInstances
- expiredBodyDeathFleckObservations and noDeathFlecksOnExpiredBodies

One-second visualSamples also contain the native counts. Existing Niagara
particle metrics count Niagara only; they are not the total selected-effect
budget. Measured peaks and retirement claims require the actual runtime report.

## Minimum actual-camera review

Keep normal battle framing: Zoom=-1, UIScale=1, no developer overlay. Use Quality=2
for HIGH and Quality=0 for LOW; inspect observedQualityPreset in the returned
state/report. Compare 1920x1080 HIGH with 1280x720 LOW first. Add 2560x1440 and
UIScale=1.4 at 1280x720 if the initial pictures show clipping or crowded cards.
Zoomed inspection can supplement, but cannot replace, the normal battle view.

| Review | Existing source route / parameters | What it proves and what it does not |
| --- | --- | --- |
| Neutral front/rear | Capture-Unreal: Page=Battle, Scenario=roster, HIGH and LOW | Both teams' real models in the normal camera. The fixture freezes the initial deployment pose; it does not prove walking or attack facing. |
| Friendly boar gait | Capture-Unreal: Scenario=swarm_split, SwarmCard=mini_stampede then stampede, SwarmAge=0.6 then 3, SwarmX=0.5, SwarmZ=8.5 | Paid member count, visible rear-facing movement, phase offsets, sparse dust, and independent lane behavior. This fixture is player-only. Repeat SwarmX=-0.5 with SwarmLeftGuardDown for the opposite lane. |
| Pulverizer mechanical pulse | Capture-Unreal: Scenario=pulverizer, PulverizerAge=0.30, 0.60, 0.80; HIGH and LOW | Player Pulverizer, enemy Mini, ground/air distinction, piston, pressure front and recent-hit ripple. First-release timing must be read from actual events; the ages are sample requests, not claimed hit timestamps. |
| Actual hand and both-team combat | Measure-Unreal: ExpansionStress, CaptureScreenshots, Quality=2/0, SimulationSpeed=1, StartSeconds=0, Seed=74021 | Real paid both-lane deployments of selected cards, Boulderback, bats, Archer Tower and real spells. Full eight-card waves cycle the actual initial hand back to Pulverizer/Stampede/Mini/Boulderback. The screenshot hook is pending runtime acceptance. |
| Death and simultaneous effects | Same paid expansion fixture, including its later real Nova casts at an observed enemy herd | Both-team front/rear attack direction, real wave hits, simultaneous defeats, armor flash/flecks and bounded cleanup. Check actual death records and native peaks; do not infer a specific death moment merely from screenshot number. |

The optional paid-fixture screenshot hook requests first-match ages 0.6, 3, 6,
and 8 simulation seconds and records paths and requested state times. Screenshot
requests are asynchronous; request timestamps do not assert the exact state of
every rendered pixel. This route must preserve the ordinary paid fixture rather
than combine a showcase scene with the performance match.
Each request records actual hand IDs, native effect counts, shockwave count,
deaths so far, and selectedVisuals with model ID, entity ID, dead state, current
animation, visual yaw, team, and authoritative facing components. Compare those
fields with the picture; a one-second visualSamples row is not a substitute for
the request's state at 0.6 seconds.

Screenshots enabled for visual review add rendering/readback work. Use separate
CaptureScreenshots-disabled runs for comparable performance measurements.

The standalone pulverizer capture pays its slot-zero card before taking the
picture, cycling Pulverizer out of that hand. It shows Mini and Stampede hand
portraits, **not Pulverizer's hand portrait**. Inspect the paid-fixture opening
hand or use an isolated ordinary legal loadout for the missing hand view.

Generic effects, effects17, and congestion captures do not contain the selected
three-card combat matrix. ShowcaseNiagaraAtAge snapshots cannot substitute for
the real paid fixture, source-facing proof, short-lived native effects, hand
portrait countability, or observed death cleanup.

Review the actual PNGs for recognizable five/fifteen-member species, teal versus
red-orange details, readable silhouettes behind both sides' towers, correct
rear/front view, attached tusks/harness/crystal details, unblocked tile grid,
hidden undamaged health bars, clear normal hand art, and effects that remain
readable at LOW without obscuring other cards.

## Recorded material and camera acceptance

The Development Editor build `build-d32c703669b2450fab8becedaf626751` passed with
unchanged source pins. Selected import
`import-8ccefc1dfe934533a24d8c8d0c4e88ed` passed after the slot-array write-back
repair: 330 unrelated native assets remained byte-identical, all three meshes
read back their exact manifest-owned Expansion interfaces, and all 18 shockwave
graph connections returned true in Unreal. Import proofs are under
Artifacts/QA/ExpansionImport; build proofs are under Artifacts/QA/ExpansionNative.

Six actual GPU captures passed their pinned native checks, and both root and an
independent asset reviewer inspected the PNGs under Artifacts/QA/VisualRuns:

| Capture | Immutable directory suffix | Observed review |
| --- | --- | --- |
| HIGH pulse, age 0.55 | expansion-pulverizer-wave-bound-bd57cc0b45714392a495477c9239a2ae | Thin violet front and transparent trail; tile grid and clear center retained |
| HIGH recent hits, age 0.75 | expansion-pulverizer-hit-bound-7c7d64b231784bb6ad0dc0ff773b7cf9 | Small ankle ripples accompany real recorded slow hits |
| LOW pulse, 1280x720 | expansion-pulverizer-wave-bound-low-9b35eeeb70f94eeb90a1930f0592423d | Purple front/trail remain visible without shader fallback |
| HIGH gallery, 1920x1080 | expansion-gallery-bound-high-3fe7e8be30db4e40954499ac5b114947 | Slate boar hides, copper details, violet Pulverizer rings, both teams readable |
| LOW gallery, 1280x720 | expansion-gallery-bound-low-56b79641bb6b4656875994fcbdefb485 | Body silhouettes remain readable; fine harness and pennant details are small |
| HIGH gallery, 2560x1440 | expansion-gallery-bound-detail-544fa302e4ad4cb1b83d53741c24273d | Same identity and correct facing at the larger window size |

Every gallery read back 42 distinct real paid bodies, six paid plays and 0.35 s
of simulated movement. All 42 component material parents and all 16 rendered
sections across the meshes' four LODs resolved to the correct Expansion
interfaces. No shader compile/default-material fallback appeared in these logs.
Gallery hands have naturally cycled away from the selected cards; opening-hand
art needs the separate paid combat screenshot fixture.

The pulse fixture's Guard Tower partly occludes its center. The radial normal
and roughness modulation is too subtle to distinguish at this camera scale;
the visible ring does not prove that detail by itself. Window size and automatic
UI adaptation differ between HIGH1080 and LOW720, so those pictures are practical
window reviews, not a controlled quality-only comparison.

These captures also display Unreal's video-memory exhaustion warning. The RHI
device received an approximately 1042 MB Windows local-memory budget despite
the adapter's 7895 MB dedicated memory. The warning concerns resources demoted
out of local video memory, not a demonstrated virtual-shadow-map overflow.
Competing GPU clients were present. Performance conclusions require the
separate measured reports and their stated load conditions; these captures
do not establish an uncontended benchmark or a clean Shipping release.
