# Native battlefield presentation acceptance

The runtime implementation is in `Unreal/RiftCrownArena/Source/RiftCrownArena/Public/Presentation` and `Private/Presentation`. `ARiftArenaPresentation` consumes `URiftMatchSubsystem::ViewState()` and `OnEvent`. `ARiftUnitVisual` has no gameplay collision and never applies damage. Existing simulation, card costs, health, damage, targeting, pathing, lifetimes and status durations remain authoritative.

## Connected systems

- Authored instanced ground, paving, river banks, bridge, boundary masonry, foliage, trees, crystal plinths, banners, ruins and floating island terrain. The 28×42 playable tiles, ±7.2 m bridges and 3.3 m river retain the simulation coordinates; additional scenery sits outside the field.
- Imported skeletal troops, round Archer Tower and miniature archer, guard cannon towers, core spire towers and destroyed masonry. Imported forward is +X; simulation `(x,z)` maps to engine `(X,Y)` in centimeters. Friendly and enemy trim use the material atlas's team mask.
- Single-node animation poses driven by simulation time. Attack events seek to normalized contact `0.48` immediately, then recover and anticipate the next cadence. No animation notify changes combat. Locomotion is in place; flyers use independent wing cycles and deterministic swarm phases. Status, charge, acquire, turn, hit, death, bow, cast, frost and Raven aura actions are connected. Corpse animation persists briefly after authoritative removal; decisive-match deaths finish visually after the simulation stops.
- Projected individual health bars, numeric tower health, dormant-core label, building lifetime line, slow/stun/charge labels, exact spell placement circles and square deployment/building footprints. Meteor hazards show their active boundary and time remaining. The overlay is hidden outside Battle/ReplayView and does not receive input.
- Niagara attacks, flight, damage, spell bursts, aura, status, tower collapse and awakening. Projectiles follow the authoritative projectile lifetime and live target; persistent statuses follow anchors and are disposed at status expiry. Finite bursts have an additional bounded cleanup timer. Ground rings use team colors while spell particles retain their card palette.
- Imported original sound effects and streamed original score. Profile master, music, SFX and UI settings apply to the appropriate voice and update active components. At most 32 one-shot voices mix simultaneously. UI calls `URiftBattleAudioSubsystem::PlayUI` with `ui_click`, `ui_error`, `ui_save` or `ui_hover`.
- Paths, front/rear sight, ranges, target arrows, tiles and tower hard-lock lines are all behind the existing default-off developer flags.

## Verified before renderer review

The final presentation sources compiled in the Editor and Shipping targets (`Artifacts/QA/native-editor-build-audit-final.log` and `Artifacts/QA/native-shipping-build.log`). A clean legacy FBX import of Ironclad measured 139.40 cm across, 62.22 cm forward and 214.86 cm tall, with forward axis +X and a generated Physics Asset. This agrees with the authored meter dimensions and requires import scale 1. Existing skeletal reimports can restore stale settings; the importer must perform clean generated imports and explicitly clear cached skeleton/physics pointers before importing each unrelated skeleton.

`Build/Capture-Unreal.ps1` launches the real renderer offscreen, records its log and exit code, rejects stale captures, and checks the PNG's actual width/height against the requested resolution. A captured image is not accepted until it has been visually inspected. Captures use a separate QA save root and leave the player's save untouched.

## Engine review required

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
