# Native UI polish 1.1.0: source review

Reviewed 2026-10-08 against the supplied V13 `src/game.js`, the preserved BrowserV15 reference and the current native source. This document describes implemented behavior. Execution, screenshot, audio and distribution evidence belongs in the release QA package.

## Restored battle aids

| Active browser feature | Native implementation |
|---|---|
| Illustrated NEXT card and compact hand stats | NEXT uses the real queue and existing card artwork. Hand tooltips include cost, targeting, damage, health and applicable specials. |
| Drag ghost after seven pixels, legal/invalid preview and return to hand | The controller measures real pointer travel. The illustrated ghost follows the pointer; returning a drag cancels it. Duplicate release callbacks cannot cast twice. |
| Right-click cancellation | Cancels a selected card or armed training spawn. An empty selection leaves the current page open. |
| 2×/3× Aether, overtime and tiebreaker announcements | Brief surge/overtime announcements last 1.75 seconds. Tiebreaker remains visible. Separate phase, clock and crown readouts remain live. |
| Field Manual and developer readouts | Home and the battle menu open the manual. Training shows actual speed, living units/buildings, both banks, AI decisions, estimated opposing bank and the last five publicly observed opposing cards. |
| Developer range, sight, route, target and tile overlays | Projected Slate painting renders these aids in Shipping builds. The prior Unreal debug-drawing calls were compiled away in Shipping. Hard-lock lines remain available. |
| Selected tower's current HP | Changing the tower selector fills that tower's actual health. Live updates preserve text being edited; submitted numeric health is validated. |

The V13 selected-card inspector markup was dormant: selection hid and cleared it. The native always-visible selected-card summary is an addition, rather than a restoration of an active browser panel.

## Added native behavior

The pause menu provides resume, settings, loadout, manual, restart and exit. Closing it restores the exact preceding simulation speed, including a manually paused training match. Settings and loadout retain that paused match; saving a deck applies to the next match. Friendly AI can be toggled independently alongside the existing enemy AI/style controls.

The camera keeps the original angle and arena assets. Its framing accounts for actual UMG scale, header and battle/replay dock heights. Zoom stops before legal rear tiles disappear beneath the HUD and retains outward wheel travel when a larger UI requires a wider view.

Off-board ground is rejected before snapping, preventing decoration clicks from clamping into unintended edge deployments. Hand selection, deployment and drag input close during the pause menu, replay, tiebreaker and finished results. Manually paused training remains editable.

Starting a new live match closes loaded replay playback while retaining its saved archive. Testing a deck from replay analysis starts fresh training at normal speed; Escape from its settings returns to that battle.

Replay seeking, playback and event bookmarks retain the recording's exact double-precision endpoint. The float slider maximum maps to that exact endpoint, while seeking stays silent. Fractional match endings such as 299.05 seconds include their final damage, tower destruction and match result instead of losing those events through a rounded float duration.

Finished results retain the actual winner and crown score while their replay completes saving. Analysis and replay controls become available when the archive is ready; an explicit Play Again action starts the next match.

## Final controls

- **1–4:** select the corresponding hand slot during regulation/overtime.
- **Click or drag:** deploy on a legal tile using the configured input mode. Confirm Deployment requires the same legal tile twice.
- **Right click:** cancel selection or armed sandbox spawn.
- **Escape:** open/close the battle pause menu; return from paused settings/loadout/manual to that battle.
- **F9:** open/close Training Lab while battle input is available.
- **Mouse wheel:** zoom within the current visible-field bounds.

Collection, deck presets and analysis, profile, recorded replays, playback speeds/seeking/bookmarks, match analysis, Meta Lab filters and exports remain accessible. Detailed metrics are available through explicit expanded views; the presentation changes do not replace the underlying datasets. Audio authoring and routing changes are described in [AUDIO_DESIGN.md](AUDIO_DESIGN.md).

## Gameplay preservation and verification hooks

The authoritative simulation files are unchanged in this polish revision: `RiftSimulation.h`, `RiftSimulation.cpp`, `RiftCombat.cpp`, `RiftPathfinding.cpp`, `RiftAI.cpp` and `RiftDeckAnalysis.cpp`. This preserves the complete 14-card roster, costs/specials, eight-card cycle, placement/pockets/footprints, bridge movement, targeting/pulls/hard locks, AI policies, economy, match clocks and recorded telemetry. Arena geometry and existing card illustrations are retained. The underlying native rules are documented in [NativeSimulation.md](NativeSimulation.md).

`Rift.Integration.BattleInputRouting` exercises production key/control bindings, cancellation, exact pause/resume speeds, training health/AI controls, observed-card readouts, all four off-board edges, real overtime/tiebreaker/result transitions, stable Victory/Defeat results through archive completion, explicit Play Again and replay→analysis→loadout→new-training handoff. `Rift.Integration.Presentation` checks the actual overtime-to-triple-Aether announcement. Capture fixtures use isolated saves, real simulated phases and actual recorded matches. Their state reports contain projected legal-field corners and HUD-safe bounds; an out-of-bounds battle projection fails capture QA.

Automated capture, audio and performance processes intercept external Slate input while retaining the real enabled widgets and direct fixture actions. The routing test sends controller/keyboard acceptance presses, repeats and releases through Slate, checking that a finished result remains stable and its deliberate Play Again action still works. Normal gameplay does not install this filter. The 17-effect QA fixture advances actual Niagara graphs to the requested sample age, avoiding wall-clock drift without changing their recipes or lifetimes.
