# Native battlefield presentation acceptance

The runtime implementation is in `Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation` and `Private/Presentation`. `ARiftArenaPresentation` consumes `URiftMatchSubsystem::ViewState()` and `OnEvent`. `ARiftUnitVisual` has no gameplay collision and never applies damage. Existing simulation, card costs, health, damage, targeting, pathing, lifetimes and status durations remain authoritative.

## 1.2.0 source and executed presentation checks

The fourteen portraits are renders of the production Blender meshes/rigs exported and imported into the native game. Enlarged models, pose blending and distance-driven gait, separate guard-cannon aim/recoil, mesh projectiles, particle vertex scaling, the centered bottom hand and 35° battle view are documented in `Docs/MODEL_PRESENTATION_1.2.0.md`. Numerical combat and the authoritative entity schema remain unchanged.

Final Editor Build9 succeeded in 14.84 seconds. The frozen suite at `Build/Automation/20261009-114843/Report/index.json` passed all thirteen scenarios in 75.499664 seconds: eleven clean successes and two successes with warnings, zero failures/unrun tests, clean commandlet/wrapper exits and all 124 source hashes/two Editor DLLs unchanged. The warnings were expected corrupt-profile backup recovery and an engine HTTP connectivity probe timeout. UnitMotion proves first-damage health visibility, full-heal persistence, zero/negative/nonfinite hit rejection, passive building-aging exclusion, visual reuse and actual archived replay seeking.

The fresh registry contains 289 assets: 113 animation clips, 25 StaticMeshes, 61 SoundWaves, fourteen card DataAssets, seventeen Niagara systems and eleven PhysicsAssets, with zero registry/native-load errors. The independent portrait verifier passes 352 source/export/PNG/pose checks across all fourteen cards.

Final Shipping build/cook/archive completed in 127.64 seconds. Native executable: 167,687,168 bytes / SHA-256 `5a209be2b0d17cf7f2aebfc41c9eff9e52e791b1da8b8c1cb1c93d8d9b1a322e`. Actual graph execution passed 78 Editor and 80 Shipping assertions across all seventeen systems, with 156 immediate particles and seven persistent lifecycles. Reports are `Artifacts/QA/model12-{editor,shipping}-vfx-final9-verification.json`.

Final sound-enabled Editor and Shipping playback each passed 49 assertions with all 61 waves loaded, clean exits and zero clipped floats:

| Run under `Artifacts/QA/Audio` | Normal peak / RMS | Overload peak / RMS | Clipped floats |
| --- | --- | --- | --- |
| `model12-editor-audio-final9` | 0.214396551 / 0.048123382 | 0.800236821 / 0.171819371 | 0 in both captures |
| `model12-shipping-audio-final9` | 0.197836846 / 0.050627855 | 0.801364958 / 0.173221058 | 0 in both captures |

Both captures are stereo 48 kHz, measured after the live effect chain before PCM16 encoding. Shipping audio/VFX identify the exact native executable above; mixer measurements are separate from subjective listening. `Docs/QA/presentation-checks-1.2.0.json` retains sanitized executed results with source-report provenance.

Actual launcher Play verified all 48 final game files, launched the native executable through its real routed command, produced a 1280×720 Home capture, preserved the legacy save and passed environment save-root forwarding with clean exits. MSI 1.2.0 compiled with standard ICE validation. All 51 real lifecycle checks passed during the public 1.1.0 → 1.2.0 upgrade, including installed Play, loaded CRT identity, repair, uninstall and separate-save preservation; Windows payload finalization passed against the exact final MSI/ZIP/launcher. All three final serial 90-second isolated performance runs completed with clean exits and native diagnostics free of Error/Fatal records. Normal AI battle had no frame over 50 ms; artificial repeated mass-spawn/restart stress retained ten and thirteen hitches, with maxima 85.929 and 140.815 ms. Meta work stayed paused with zero games during the active-battle measurement. Complete frame/thread/GPU/memory tables and hitch details belong to `Docs/RELEASE_QA.md`. Public delivery remains pending. Earlier Build5, left-tray and first Shipping observations are provisional development history; completed 1.1.0/1.0.0 measurements below remain explicitly historical.

The final packaged renderer review at `Docs/QA/presentation-review.json` accepts 26 actual inspected Shipping PNGs with 78 exact image/run/state pins, twelve public pages, three battle aspect ratios, enlarged UI, replay, placement, effects/projectiles and real phase/result fixtures. At 1280×720/UI scale 1.4, the ground is 507.0625 pixels wide and thirty troop bounds measure minimum/mean/maximum height 33.721/56.733/97.221 pixels. All 38 model/anchor bounds remain safe; the compact 640 × 156 centered bottom hand retains its 64 × 80 portraits, complete card controls, Next and Aether below the player Core. Ground widths are 934.426 pixels at 1920×1080, 672.573 at 1280×800 (16:10) and 934.428 at 2560×1080 (21:9).

All 26 captured states show zero healthy-HP visibility violations; actual damaged live/replay frames show HP while status and lifetime remain independent. Nine real paused projectiles/launches cover seven ranged roles with nine particles and 45 mesh trail instances. Gold/cyan heads remain visibly restrained; the still proves neither continuous motion nor nine broad glow quads. The breath frame contains two Frost puff systems with eight particles each (16 Frost particles), plus 48 Nova particles for 64 world particles. Real double/triple-Aether and overtime banners at delay 1.2 seconds and persistent tiebreaker/victory panels were inspected.

## Historical 1.1.0 presentation evidence

The 1.1.0 presentation update preserves the existing arena, character and tower
meshes, materials, card illustrations, animations, scenery and Niagara assets.
The fourteen-card roster and numerical simulation rules remain authoritative.
It changes the interface and sound bank, with a narrow event-to-impact-sound
mapping correction that does not alter damage or event timing.

The native UMG interface now uses Slate-drawn card frames, crowns, segmented
Aether and selection feedback around the existing illustrations. The lobby,
deck/card pages, profile, settings, replay controls, match reports and Field
Manual use the revised layout. Battle includes separate crown scores, next-card
art, phase announcements, a drag preview and an Escape pause menu. Training
shows current speed, Aether, friendly/enemy AI and tower health. Its projected
paths, front/rear sight, ranges, targets, hard locks and tile coordinates use
Slate painting so they also work in Shipping; these remain default-off tools.
Historical 1.1.0 renderer observations and their limits belong to
`Docs/QA/historical-presentation-review-1.1.0.json`; current 1.2.0 review is recorded above.

The final Shipping Niagara run at
`Artifacts/QA/polish15-shipping-vfx-final-verification.json` passed all 80
checks: all 17 original systems executed their actual graph, the immediate
fixture contained 156 particles, and all seven persistent systems retained
their single particle at the lifecycle sample. The capture fixture advances
the authored graph in small synchronous steps to the requested 0.12 or 1.2
seconds and holds that sample for diagnostics/rendering. This avoids measuring
a startup frame hitch as effect age; it does not change particle recipes,
ordinary effect lifetimes or gameplay behavior.

The new recorded sound bank contains 61 SoundWaves: all 41 original cue IDs and
20 additional combat takes. Recorded impacts, tactile UI feedback, water
ambience, a new original chamber-orchestra arrangement and related stingers
replace the earlier synthesized palette. The runtime reserves six UI and 26
combat voices, aggregates repeated swarm cues, prioritizes decisive events,
ducks music for announcements and routes playback through a registered stereo
limiter. Independent Master, Music, SFX and UI settings are retained.

The final-source sound-enabled Editor and final Shipping runs passed all 49 checks,
including all 61 assets, bound Settings callbacks, independent mutes, voice
reservation, event priority, loop voice survival, live submix registration and
post-effect floating-point capture. Their separate QA profiles left the player
save untouched. The following measurements are from the real stereo 48 kHz
mixer before PCM encoding; the overload deliberately plays two large cues at
12 times their ordinary gain.

| Run under `Artifacts/QA/Audio` | Normal peak / RMS | Overload peak / RMS | Clipped floats |
| --- | --- | --- | --- |
| `polish15-editor-audio-final` (final sources) | 0.206878617 / 0.047645573 | 0.800180495 / 0.172086378 | 0 in both captures |
| `polish15-shipping-audio-final` (final executable) | 0.203053907 / 0.051877354 | 0.801191688 / 0.173328368 | 0 in both captures |

Each run supplies `audio-smoke.json`, `run.json`, `mixed-output.wav` and
`limiter-overload.wav`. `Build/Package-QAEvidence.ps1` independently validates
the raw-float content/headroom assertions and the PCM16 WAV container, sample
layout, quantized peak/RMS and exact SHA256 bytes. The resulting
`mixed-recording-verification.json` preserves those two measurement stages
separately. This establishes limiter/mixer behavior; no human listening
acceptance, perceived balance or subjective sound quality is claimed.

The final-source Editor captures contain 59,392 normal and 116,736 overload
float samples; its actual audio run exited 0 in 22.60 seconds. Final Shipping
captures contain 61,440 normal and 114,688 overload samples. Its actual audio
run exited 0 in 11.16 seconds and pins native
executable SHA256
`0f35c7e9f52bb6009876c5e2eef23be8d4033712d844a91f04a8c28c68aa176b`.
The evidence packager verifies this executable against the final release
manifest. Earlier provisional Shipping audio reports are not used as the final
binary's test.

## Historical 1.0.0 implementation and review record

All sections below retain the original release's implementation notes, failed
captures, fixes and measured review. Their 41-sound/263-asset counts, old UI
screenshots, runtime hashes and acceptance status refer to 1.0.0 and are not
the current 1.2.0 inventory or release certification.

### Connected systems

- Authored instanced ground, paving, river banks, bridge, boundary masonry, foliage, trees, crystal plinths, banners, ruins and floating island terrain. The 28×42 playable tiles, ±7.2 m bridges and 3.3 m river retain the simulation coordinates; additional scenery sits outside the field.
- Imported skeletal troops, round Archer Tower and miniature archer, guard cannon towers, core spire towers and destroyed masonry. Imported forward is +X; simulation `(x,z)` maps to engine `(X,Y)` in centimeters. Friendly and enemy trim use the material atlas's team mask.
- Single-node animation poses driven by simulation time. Attack events seek to normalized contact `0.48` immediately, then recover and anticipate the next cadence. No animation notify changes combat. Locomotion is in place; flyers use independent wing cycles and deterministic swarm phases. Status, charge, acquire, turn, hit, death, bow, cast, frost and Raven aura actions are connected. Corpse animation persists briefly after authoritative removal; decisive-match deaths finish visually after the simulation stops.
- Projected individual health bars, numeric tower health, dormant-core label, building lifetime line, slow/stun/charge labels, exact spell placement circles and square deployment/building footprints. Meteor hazards show their active boundary and time remaining. The overlay is hidden outside Battle/ReplayView and does not receive input.
- Niagara attacks, flight, damage, spell bursts, aura, status, tower collapse and awakening. Projectiles follow the authoritative projectile lifetime and live target; persistent statuses follow anchors and are disposed at status expiry. Finite bursts have an additional bounded cleanup timer. Ground rings use team colors while spell particles retain their card palette.
- Imported original sound effects and streamed original score. Profile master, music, SFX and UI settings apply to the appropriate voice and update active components. At most 32 one-shot voices mix simultaneously. UI calls `URiftBattleAudioSubsystem::PlayUI` with `ui_click`, `ui_error`, `ui_save` or `ui_hover`.
- Paths, front/rear sight, ranges, target arrows, tiles and tower hard-lock lines are all behind the existing default-off developer flags.

### Verified before renderer review

The final presentation sources compiled in the Editor and Shipping targets (`Artifacts/QA/native-editor-build-audit-final.log` and `Artifacts/QA/native-shipping-build.log`). A clean legacy FBX import of Ironclad measured 139.40 cm across, 62.22 cm forward and 214.86 cm tall, with forward axis +X and a generated Physics Asset. This agrees with the authored meter dimensions and requires import scale 1. Existing skeletal reimports can restore stale settings; the importer must perform clean generated imports and explicitly clear cached skeleton/physics pointers before importing each unrelated skeleton.

`Build/Capture-Unreal.ps1` launches the real renderer offscreen, records its log and exit code, rejects stale captures, and checks the PNG's actual width/height against the requested resolution. A captured image is not accepted until it has been visually inspected. Captures use a separate QA save root and leave the player's save untouched.

### Engine review required

The main build pipeline owns the complete import, render and packaged-play results. The source/export audit, clean single-mesh import and successful compilation do not by themselves establish the following visual or performance results:

1. Review all fourteen cards next to their assembled engine models; compare silhouettes, colors, equipment and feature identity with `UE5_ASSET_SPEC.md` and `Renders/characters_contact.png`.
2. Inspect both-team +X facing, cannon and bow origins, animal feet, flyer altitude, guard/core dimensions and Archer Tower archer position/footprint. Verify all models use the intended material slots and normal-map handedness.
3. Record first contact and sustained attacks at normal, quarter and quadruple speeds. Check projectile position and damage flashes against events; no visual delay may change attack intervals. Pause/resume and replay seek must not leave stale actors, status particles or meteor fields.
4. Inspect unit health and statuses amid both Twin Blades and all five Bats; bars must remain readable without covering equipment. Check tower numbers and dormant core before and after Guard destruction.
5. Show square troop/building previews and spell radius previews at legal, illegal, occupied, river and opponent-pocket tiles. The footprint must represent the unchanged placement rule.
6. Change each audio slider during music and effects, and check UI success/invalid cues. Check the mastered score loop and voice limit during swarms and overlapping area damage.
7. Capture the arena at the actual player camera and common display sizes. Verify that the island surface remains below the authored field, bridges meet banks, water shading is coherent, scenery has supporting ground, and manual camera exposure preserves character readability.
8. Profile a crowded match in Shipping and review actual LOD changes. There must be no missing-asset substitution, generic placeholder units, repeated asset errors or accumulating inactive particle/audio components.

Root replay integration must dispatch recorded events during normal playback and store hazard play IDs. Seek should rebuild the visual snapshot without replaying historical audio. These changes belong to the replay subsystem rather than the presentation replica.

Until those engine and packaged reviews are recorded, this document reports connected implementation and its acceptance criteria, not completed release approval.

### First engine capture

`Artifacts/QA/Visual/battle-roster-baseline-1920x1080.png` is the first actual engine capture. Its PNG dimensions were correct, and the projected health overlay drew for both teams. It failed acceptance: the world rendered black, the main Battle widget tree was absent, shared materials lacked skeletal/instanced usage flags, and shutdown crashed in replay recording. Nearby Archer Tower and Guard numeric health labels also overlapped. The associated JSON and engine log retain this failed evidence. Camera exposure, the persistent UMG root, material flags and teardown ownership are being repaired by their owning engineers; the next capture must verify those corrections before visual approval.

Subsequent fixed captures exited cleanly with the arena, models and UI visible. The revised hand and Aether panel fits the viewport. The Home capture shows all eight selected cards, readable costs, navigation and entry actions; longer strategy content uses the scroll panel. The renderer found an additional camera issue: `UCameraComponent::bOverrideAspectRatioAxisConstraint` must be true for the requested horizontal constraint to reach the view. `battle-roster-final-1920x1080.png` verifies the override: the playable field occupies approximately 644 horizontal pixels, and the original silhouettes, equipment, both teams, river and bridges are visible. Its JSON remains `reviewed-needs-refinement`: the bottom hand obscures the friendly core, ground seams are faint and shadows conceal equipment. The models are visibly simpler and smoother than their card illustrations. This is retained evidence rather than full production art approval.

`battle-roster-polish-1920x1080.png` verifies the next camera/material revision: the whole friendly core sits above the hand, the ground boundaries are visible, and the shadow-free cool fill improves equipment and ground readability. The terrain still repeats its small pebble texture, and model detail remains stylized. This frame also exposed an engine warning for two directional lights with equal forward-shading priority. The constructor now explicitly selects the sun at priority 1 and the fill at 0; the next capture must verify that warning disappears. No warning was hidden through screenshot commands.

The full physics diagnostic now verifies all eleven generated assets in the disk registry. Their real body counts range from one to seven, with the expected constraints; package flags are zero and the objects are real public standalone assets. The successful Shipping cook follows generated physics saves and an explicit scan by filename. `Artifacts/QA/unreal_physics_inspection.json` retains the native body/constraint counts and registry checks; `Artifacts/QA/native-shipping-package-final.log` records the cook result. No collision assets were removed to fix package discovery.

### Reproducible event and audio checks

`Build/Capture-Unreal.ps1` supports `roster`, `congestion`, `effects` and `placement` scenarios. The effects scenario deploys original cards through the real sandbox API and lets ordinary combat emit attacks, projectiles, slow, stun, aura and damage events; timers cast the original Meteor Shards, Bullet Burst and Nova Flask. It supports simulation speeds 0.25, 1 and 4. The placement capture passes an original card and snapped tile through the real `CanPlace` rule, then draws the same preview API used by the player controller. Each capture saves an adjacent `.state.json` containing actual event counts, simulation time/speed, live projectiles, hazards, statuses and the preview validity/size. These explicit capture flags are isolated QA behavior and do not alter normal play.

`Build/Test-UnrealAudio.ps1` launches an actual sound-enabled game with a separate QA profile and Engine UserDir. Its native helper invokes the bound Settings slider callbacks, loads all 41 original SoundWave assets, inspects live music, ambience, SFX and UI components and their volume products, checks independent group mutes, looping/streaming configuration and the shared 32 one-shot limit, and restores settings. The final run passed all 38 checks and exited 0 with no authored-asset errors; `Artifacts/QA/Audio/native-audio-postmix64/audio-smoke.json` records the results. The engine log confirms the real WASAPI stereo hardware mixer initialized with 64 physical sources. After 0.1308784 game seconds, all 32 bounded one-shots remained active, and both music and river ambience were playing without virtualization. The device budget therefore accommodates the two loops and the one-shot bound. The earlier 32-source run is retained as evidence of the budget issue that led to the configuration fix.

These checks establish actual Settings callbacks, component mixing, mute behavior and physical-voice survival across mixer ticks. They do not establish acoustic quality, perceived loudness, voice balance or absence of an audible loop seam; no listening review was performed.

### Actual combat and menu evidence

All following images come directly from `FScreenshotRequest` in the running Unreal renderer; none are composites. Each adjacent capture JSON records a clean exit, exact PNG resolution, fresh state metadata and no authored-asset errors. The real completed Meta dataset was copied only into the isolated QA profile; `Artifacts/QA/Visual/meta-capture-provenance.json` records its original and copied SHA256 hashes.

| Capture under `Artifacts/QA/Visual` | Actual state and inspection |
| --- | --- |
| `battle-effects-final-normal-1920x1080` | Speed 1, elapsed 3.1833 s: 27 attacks, 19 projectile launches, 27 damage events, four slows, one aura and one stun. One slowed unit, one stunned unit, two projectiles and one Meteor field remain active. STUN, -30%, CHARGE and the field timer are visible. |
| `battle-effects-final-quarter-1920x1080` | Speed 0.25, elapsed 3.2 s: the same attack/projectile/aura/stun/slow totals and live statuses; two projectiles and one Meteor field. Inspected actual quarter-speed scene. |
| `battle-effects-final-quadruple-1920x1080` | Speed 4, elapsed 3.3 s: 27 attacks, 19 launches, 28 damage events, four slows, one aura and one stun; one projectile and one Meteor field. Inspected actual quadruple-speed scene. |
| `battle-congestion-final-1920x1080` | Elapsed 7.3333 s: 128 spawns, 440 attacks, 450 damage events, 76 deaths, eight slows, eight auras and ten stuns; 58 entities remain alive. This exposed excessive vertical health-bar stacks and is retained as failed readability evidence. |
| `home-final-1920x1080` | Eight selected original illustrations, names and costs, navigation, entry actions and summaries fit the native two-column page. |
| `cards-final-1920x1080` | All 14 original cards and their names/costs/illustrations are visible. |
| `profile-final-1920x1080` | Ivory editable name text is readable on the dark field; statistics and actions are visible. |

The first effects fixture failed to exercise Raven aura because five enemy Bats killed the Raven before its pulse. The corrected QA fixture uses durable adjacent targets; normal combat rules and card statistics were not changed. Original failed capture evidence remains available.

The event totals establish that combat actually occurred. These earlier frames establish status labels and hazard rings, but a subsequent native diagnostic found zero actual Niagara particles. They are historical event/overlay evidence, not successful particle-effect evidence. The concrete graph defect and its repaired runtime proof are recorded below. Single screenshots do not prove continuous animation transitions, exact visible contact synchronization, replay seek cleanup, perceived audio quality or production-level model detail.

### Final camera, placement and UI inspection

The final camera uses a 5350 cm vertical span at default zoom and looks toward `(0,500,0)`. `battle-roster-final-camera-1920x1080.png` shows the entire legal 28×42 field above the hand. The legal friendly back row ends around y=827 while the hand begins at y=876; both cores, tower numbers, upper HUD and the hand remain visible. The square rear-corner preview at tile `(13.5,20.5)` occupies approximately x=1222..1243 and y=800..821, entirely above the hand. The outer decorative grass can extend beneath the HUD without hiding legal placement tiles.

All eight resolutions listed in Settings were actually rendered and visually inspected: 1280×720, 1600×900, 1920×1080, 1920×1200, 2560×1440, 2560×1600, 3440×1440 and 3840×2160. The additional 2560×1080 frame was also inspected. Their stems are `battle-roster-final-camera-<width>x<height>` under `Artifacts/QA/Visual`; all nine captures exited 0, matched their requested PNG dimensions and showed the full legal field, both cores, health labels, Aether/next controls and hand. Terrain tile boundaries are visible in ordinary play, both-team trim and equipment are readable, and the scene has supporting island ground, water and bridge connections. The corrected sun/fill priority no longer produces the earlier competing-directional-light warning in these final captures. Ground pebbles still repeat and character surfaces remain stylized and simpler than the detailed card illustrations; this is functional presentation evidence, not a claim of final production art equivalence.

`battle-congestion-final-2d-1920x1080` verifies the bounded two-dimensional health-label layout during real combat: 58 entities remain alive after 128 spawns, 441 attacks, 450 damage events, eight auras and ten stuns. Each individual bar/status remains present; full tower/status bounds receive priority and leaders are shorter than the earlier vertical stacks. Bodies still naturally overlap within crowded bridge combat, so this frame is not a guarantee that every equipment detail remains unobstructed during congestion.

The six `placement-final-*` frames use actual `CanPlace` results. `rear-corner` accepts Ironclad at `(13.5,20.5)` and shows a complete one-tile cyan square. `building-legal` accepts Archer Tower at `(0.5,7.5)` with its complete 1.65-tile square. `building-river` rejects `(0.5,0.5)`, `building-occupied` rejects the occupied Core area `(0.5,16.5)`, and `opponent-pocket` rejects Ironclad at `(7.5,-5.5)` while the enemy Guard is alive; each shows a red unavailable preview. `spell-radius` accepts Nova Flask at `(0.5,0.5)` and shows its unchanged 3.25-tile radius. Perspective projection makes a ground circle elliptical on screen; the simulation radius is unchanged.

The final menu review includes `home-final-1920x1080`, `cards-final-1920x1080`, `profile-final-1920x1080`, `meta-final-table-1920x1080`, `settings-final-combo-1920x1080`, `patch-notes-final-1920x1080` and `loadout-final-width-1920x1080`. All 14 cards are visible on Cards. Home shows all eight selected cards. The final Loadout image shows all eight complete selected-card names, illustrations and costs, including the full single-line Boulderback name, plus a readable editable deck name. Its below-scroll catalog is not shown and is not certified by this one screenshot. Meta uses aligned table cells, whole counts, two-decimal metrics and both confidence bounds; Settings uses the shared dark/ivory Combo palette and readable sliders/actions. Their connected callbacks are tested separately by native ConnectedUI automation.

### Actual Niagara graph repair and lifecycle proof

`battle-effects-niagara-diagnostic-1920x1080.state.json` recorded compiled-ready systems with zero particles and zero spawned particles. The builder had called `UNiagaraSystem::AddEmitterHandle` directly, leaving the executable system graph incomplete. The repair follows the installed engine factory's `FNiagaraEditorUtilities::AddEmitterToSystem`, which rebuilds emitter nodes and synchronizes the overview graph. Existing packages are loaded fully before mutation; failed saves now appear in the report rather than being hidden by `SAVE_NoError`. `Build/repair_unreal_vfx.py` repairs only the 17 generated VFX systems; no card mechanics, meshes or collision assets are changed. `Artifacts/QA/unreal_vfx_graph_repair.json` records all 17 saves as true with zero errors.

The warmed actual renderer capture `niagara-all17-final-immediate` exercises every original system in one explicit QA scene. Its native CPU-buffer diagnostics report 156 actual particles: Deploy 12, Impact 7, each of the five Flight effects 1, Bullet Burst 7, Nova 24, Meteor 18, Meteor Tick 5, Frost 8, Slow 1, Stun 1, Aura 20, Tower Destroy 32 and Core Awaken 16. Every system is ready, valid, active, visible and incomplete; each reports positive actual spawn counts and finite nonzero sprite dimensions. The warmed PNG has no shader-preparation message and visibly contains small colored sparks/clusters and flight points. It establishes that the zero-particle defect is repaired; the modest sprites do not by themselves establish a complete artistic pass for every spell.

`niagara-all17-final-lifecycle` records all five Flight systems plus Slow and Stun retaining one live particle at actual age 1.217085 seconds, with nonzero sprite dimensions. Finite bursts have finished while these seven persistent effects remain active. Their single-looping template therefore survives its configured short particle lifetime until the authoritative component is destroyed; no gameplay lifetime or status duration was extended to obtain this result. The QA fixture itself has bounded cleanup.

CPU dataset names strip the `Particles.` prefix. Numeric sprite dimensions are read from the actual buffers. Where the diagnostic includes `alphaMin`/`alphaMax`, its current suffix resolver selects `Initial.Color` when both `Color` and `Initial.Color` exist; those values describe initialization alpha and must not be interpreted as current rendered/faded alpha. The seven persistent templates have no Color buffer at all and use the sprite renderer's default color; `colorDataAvailable=false` means unavailable data, not zero opacity. The installed engine sprite renderer initializes its fallback color to white with alpha 1. Current rendered alpha has not been directly measured for every system.

The final real-combat captures also confirm positive particles with actual interactions:

| Final stem under `Artifacts/QA/Visual` | Native state |
| --- | --- |
| `battle-effects-final-particles-1920x1080` | Speed 1, elapsed 3.05 s: 25 attacks, 18 projectile launches, 24 damage events, four slows, one aura and one stun; three projectiles, one hazard, one slowed and one stunned entity; 93 actual Niagara particles. |
| `battle-effects-final-particles-quarter-1920x1080` | Speed 0.25, elapsed 3.1833 s: 27 attacks, 19 launches, 27 damage events, four slows, one aura and one stun; two projectiles, one hazard and both statuses; four actual persistent particles. |
| `battle-effects-final-particles-quadruple-1920x1080` | Speed 4, elapsed 3.3167 s: 27 attacks, 19 launches, 28 damage events, four slows, one aura and one stun; one projectile, one hazard and both statuses; 142 actual Niagara particles. |

All three images were inspected directly and retain visible status labels, hazard boundary/timer and colored effects. Their differing transient counts reflect world-age timing rather than changes to combat rules. The finite-burst and persistent-buffer proof remains separate from continuous animation/contact, replay cleanup and acoustic review.

The final scope review found Frost Fang's authored `Breath` clip was imported but not selected. Its eligible stationary idle branch now selects that loop; deployment, movement, attack/contact/recovery, hit, acquire, turn, stun and death retain priority. A small finite existing Frost puff emits at the mouth at most once per 2.4 simulation seconds while that clip is selected. It creates no simulation event, damage or status, and normal finite-effect cleanup bounds its lifetime. No new assets or card values were introduced.

`frost-breath-final-1920x1080` verifies the actual binding and ambience. The explicit `-RiftBreathSmoke` QA flag briefly resumes the existing roster through ordinary simulation and pauses it again; it does not replace snapshots. At real elapsed 0.883333 seconds, both Frost Fang entities select `Breath`, bound to `/Game/Rift/Characters/frost_fang/Animations/AN_frost_fang_Breath.AN_frost_fang_Breath`, at clip positions 1.100369 and 1.216056 seconds. Two mouth puffs have emitted; both actual Frost systems are active at age 0.166668 seconds with eight live particles each and finite nonzero dimensions. The original 1920×1080 PNG was inspected, the subtle frost does not obstruct the field, and the renderer exited 0 without authored-asset errors. `Artifacts/QA/native-editor-build-frost-breath-final.log` records the successful Editor compile. This capture's runtime DLL SHA256 is `5cc353a2acb11ede59d5d3aef366fe02fee620265d64a21e72b5b07754d81333`; the subsequent complete native tests and Shipping restage must use that source revision. The QA timer only exists when its explicit flag is supplied.

### Native asset evidence at 1.0.0

`Build/inspect_unreal_assets.py` performs a read-only scan and loads existing current assets. `Artifacts/QA/unreal_asset_audit.json` contains 263 actual on-disk registry records: 113 animation sequences, seven materials, 17 Niagara systems, 11 Physics Assets, 14 original Card DataAssets, 11 skeletal meshes, 11 skeletons, 41 SoundWaves, 19 static meshes, 18 textures and the Arena map. All non-map records load successfully, and the native card validation reports all 14 originals with zero errors, intended illustrations/animations/effects/sounds and valid character dimensions/physics. The map's generic asset-load check is explicitly omitted because map loading is a different operation; actual native Arena captures establish that the World loads. Earlier native physics inspection separately records all eleven real body assets. This populated audit has a separate filename and does not overwrite import or materials-only reports.

`Artifacts/QA/presentation_final_review.json` records the inspected final image/state paths, SHA256 evidence hashes, observations and explicit remaining limits. Presentation source and repaired Content are frozen for the final native tests and Shipping restage. Final packaged performance, installed Play and Shipping evidence belong to the release pipeline; this document does not substitute Editor screenshots for those results.
