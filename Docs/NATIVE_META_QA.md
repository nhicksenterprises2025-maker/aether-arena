# Native Meta verification

The Meta Lab runs `rift::Match`, the same authoritative native combat core used by the playable game. It samples the seven canonical AI styles, including `counter`, and builds decks without using observed win rates as a selection bias. Each completed side contributes eight deck appearances; mirrors remain visible as exclusions from clean win-rate samples. A complete match contributes two side observations.

Current datasets capture all sixteen card definitions, rule metadata, combat model, deck policy, telemetry revision and a fingerprint covering card numbers, AI/navigation revisions and gameplay constants. Historical datasets retain their original captured roster. A worker starts a new dataset when the selected archive cannot be resumed by the current model. Missing historical measurements display as unavailable. Patch comparisons require compatible captured metadata.

## Current 1.4.0 verification

[The first balance patch](PATCH_NOTES_1.4.0.md) adds Mini Stampede and Stampede and records navigation revision 6 for per-member landing lanes, radius-aware formations and Guard Towers centered on their bridges at ±7.2 tiles. The accepted aligned 100-match native cohort is [QA/balance-patch-meta-100.json](QA/balance-patch-meta-100.json): 100 actual games and attempts, 200 deck observations, 172,980 independent checks, zero failed checks, invalid matches or invalid economies, and a validated final export. Its captured sixteen-card fingerprint is `a5e2d0e79229e4e484bfc26fe55cd03f`; the snapshot explicitly records `guardBridgeAlignment`. Both new cards have real paid observations: 625 Mini Stampede casts and 31 Stampede casts.

The cohort used the same accepted Editor game DLL as the complete eighteen-test integration run at `Build/Automation/20261009-201513/Report/index.json`: 84.1371307373047 seconds, seventeen clean successes and one intentional profile-recovery warning. [QA/native-integration.json](QA/native-integration.json) records all tests, 134 source pins and two unchanged Editor module pins. The native balance test covers all six actual tower bodies aligned with retained depths/radii, imported definitions, preserved abilities, per-member/swarm DPS, paid hog counts and costs, sixteen-key replay telemetry and actual AI deck selection. The Meta tests cover current sixteen-card capture, all 120 unordered roster pairs and all 256 ordered mechanical matchup cells. The game DLL SHA-256 is `75bfe37d2e865b3a2a6e0570a064ba3cb47f1b9ca82359e5e95ec1df467fd2f4`.

This cohort checks accounting, saved aggregates and export consistency. It is a 100-match sample; the historical 10,000-match cohorts below retain their own rules and do not supply extra observations for this patch. The [pre-clarity](QA/balance-patch-pre-clarity-meta-100.json) and [pre-alignment](QA/balance-patch-pre-alignment-meta-100.json) sixteen-card cohorts also retain their actual prior geometry/fingerprint; they cannot resume under the aligned rules. The current auditor separately re-audited seven archived 100-match cohorts spanning navigation revisions 1–6 with 1,174,646 checks and zero failures, retaining their original fingerprints and input bytes. That compatibility check does not rerun those games. Shipping visual/performance and Windows/public delivery acceptance are still pending.

## Historical recorded execution on 2026-10-08

On 2026-10-08, the full actual Unreal automation suite completed nine tests with zero failures. Eight reported `Success`; the profile backup recovery fixture reported `SuccessWithWarnings` because its expected recovery warning was logged. The report is `Build/Automation/20261008-154026/Report/index.json`, with raw Unreal logs alongside it. After the final table/Combo styling changes, the connected production UI callback test passed again with zero warnings/errors at `Build/Automation/20261008-160858/Report/index.json`.

| Test | Actual checks |
| --- | --- |
| `Rift.Meta.AggregationEconomy` | A seeded complete native match, bank/spend/leak conservation, card-spend agreement, sixteen deck appearances, clean samples plus mirrors, real crowns/duration, immutable definitions, unavailable empty-sample efficiencies, deliberately invalid economy rejection, future-schema/missing-checkpoint/negative-seed resume rejection. |
| `Rift.Meta.WorkerPauseAndRecovery` | Real worker pauses during battle and manual pause, completes four matches then another finite batch, records five valid economies and zero invalid economies, and excludes paused waits from reported active simulation time. |
| `Rift.Integration.CardData` | Native card asset bindings and canonical definitions. |
| `Rift.Integration.ProfilePersistence` | Isolated native persistence, migration and backup recovery. |
| `Rift.Integration.ReplayTimeline` | Actual native replay recording/import/seek behavior. |
| `Rift.Integration.SnapshotRoundtrip` | Snapshot round trips and malformed input handling. |
| `Rift.Integration.PausedResultAndReplayEvents` | Terminal match results while paused, replay event preservation and recorded timelines. |
| `Rift.Integration.ConnectedUI` | Actual UMG action callbacks, loadout/settings persistence and restart behavior. |
| `Rift.Integration.FullLengthReplay` | Complete congested and paid-play matches, asynchronous finalization, compressed archive import, timeline validation and exact JSON reimport. |

The two Meta tests use private in-memory fixtures. Integration tests use isolated save roots. No real player saves are read by this QA. This historical run includes the resume-schema and malformed-checkpoint assertions, telemetry revision 3, and the compiled asynchronous replay-save UI gating. Replay files use the native `.riftreplay` archive while imports remain compatible with legacy native `.json` replays.

The installed-engine-compatible Shipping target also compiled and linked successfully. Compilation is separate evidence from a cooked package, actual renderer performance or installer execution.

## Economy and scheduling

For each side, the worker checks bank in `[0,10]`, nonnegative paid spend/leakage, card telemetry spend equal to total paid spend, and:

```text
spent + bank + leaked <= 5 + AetherGenerated(0, min(elapsed, 300))
```

The terminal combat tick can end before its last economy increment, so the small remaining budget must be below `0.05` Aether. Invalid matches are counted and excluded from statistics. Regulation's theoretical budget is approximately `90.714286`; at 300 seconds it is approximately `197.857143`, including the opening five Aether.

Worker active simulation time measures construction and step execution, excluding pause/rate waits. `MAX SAFE` adapts a bounded delay using menu frame time. JSON harvesting and save writes defer while a live battle is active. Checkpoints retain early history and progressively thin older points while keeping recent samples dense.

## Historical 10,000-match instant-spell validation

The historical authoritative native core completed an isolated 10,000-match run with initial seed `151515`, telemetry revision 3 and fingerprint `695685f9ebb53185d4f45b257e866354`. Its final validity audit is `Artifacts/QA/native-meta10000-final-core/audit-validity-final.json`.

| Measurement | Actual result |
| --- | --- |
| Completed matches / attempts | 10,000 / 10,000 |
| Validity audit | 488,780 checks; zero failures |
| Invalid matches / invalid economies | 0 / 0 |
| Deck observations | 20,000 |
| Aggregate buckets / cumulative checkpoints | 82 / 400 |
| Maximum terminal economy residual | 0.017857143 Aether |
| Active simulation wall time | 1,268.84 seconds, excluding worker pause/rate waits |
| Final exported rows | Validated against the recorded dataset |

This is historical native execution evidence. The audit validates that cohort's economy, aggregates, captured card definitions and export consistency. It does not establish byte-identical aggregate values across separate historical runs, and historical browser reports do not substitute for this native result.
