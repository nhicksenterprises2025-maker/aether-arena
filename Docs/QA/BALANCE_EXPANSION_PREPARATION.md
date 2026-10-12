# Balance expansion preparation

This is an in-progress preparation report. The user approved all consolidated balance resolutions, all seven candidates (three existing cards updated plus four new playable cards), and use of the numerical tables transcribed in the active brief. The user also confirmed a 34-tile-long × 24-tile-wide arena with 125 cm tiles. The canonical schema 2 manifest now contains 21 playable cards and one subordinate skeleton definition; runtime, new-asset, performance and release acceptance remain in progress.

The approved expansion passed 86 portable scenarios and 21 complete AI matches against its captured 21-card, 24×34 inputs. Actual native automation, asset import/render review, Meta, drag, audio, performance and Shipping acceptance are still being completed. Earlier results below remain historical evidence and do not establish acceptance of the current native build. No current Shipping build or installer is claimed here.

Starting repository commit: `7c25b46e8c6d7dd1326dee60960141da3774bcd1`.

Pre-patch definitions were exported from the actual Unreal runtime before tracked edits. The immutable capture is `Artifacts/QA/ExpansionBaseline/capture-503dbbf1ffec46baa2cfb7b4f7c3701d/`. `Assets/Data/Baselines/1.5.0.json` remains byte-identical. Its schema 2 derivative, `Assets/Data/Baselines/1.5.0-expanded-schema.json`, records actual previous building decay/cooldown and charge defaults for exact comparisons. `Assets/Data/card_manifest.json` and `Assets/Data/source_resolution.json` are approved numerical inputs for version `1.6.0`; approval does not announce completed runtime or release acceptance.

The current inputs use navigation revision 8 and combat revision 3. Crown commitment begins with the first authoritative attack, with the approved Shadow Surge lock-release exception. Mini Stampede and Stampede now use approved visual scales 1.30 and 0.85; their collision radii remain independent and unchanged. The expanded mechanical hash is `e87244ec5b4629688bfdf83e5ef3888f3ad61b96848e9f117e3af12b96b236d5`. Historical datasets retain their captured versions, hashes and accounting policies.

## Prepared implementation

- One canonical manifest generates native definitions and feeds data binding, Meta identity, stat exports, patch-note arithmetic and the balance workbook pipeline. Pending or stale inputs are rejected by final release guards.
- Combat uses explicit target masks and independent collision radii, Crown attack commitment, eligible melee/projectile splash, and a compact deterministic fifteen-member Stampede formation.
- Existing Mini Stampede, Stampede and Pulverizer have editable Blender sources, exported LODs, rigs, authored contact/gait/death timing, model-derived portraits and identity sheets. Historical portrait provenance is retained after the animation-only repair and validates using tracked files from a fresh checkout. Final native material binding, shader and normal-camera acceptance remain open.
- Battle hand hover stat windows are removed; selected-card information and drag/return-to-hand behavior remain.
- Historical Meta filters use captured card identity and roster. Empirical dataset version/fingerprint are separate from current canonical mechanical scores in the UI and exports. New replay archives capture their mechanical context; legacy archives remain readable with explicit reconstruction limits where that context is absent.
- Approved expansion mechanics include shield overflow protection, four subordinate Bone Warden children without extra paid casts, local Shadow Surge cloak/lock release, explicit building decay/expiry and exact charge events. Replay context and telemetry revision 4 retain shield, child and per-entity attribution; their actual native acceptance is pending.
- Native QA fixtures exercise real paid deployments, wave hits, mass defeats and per-entity cleanup deadlines. Renderer/performance instrumentation and isolated LOW/HIGH presets are prepared; final measured counters and rendered appearance require the actual acceptance runs.

## Approved expansion checks

| Check | Observed result | Immutable evidence |
| --- | --- | --- |
| Portable approved combat/navigation | 86 scenarios and 21 complete AI matches passed; zero scenario failures; 21 playable cards plus one subordinate, 24×34 arena and the approved `e87244ec…` mechanical hash; source pins stayed unchanged during the run | `Artifacts/QA/ExpansionCombat/20261011-193133-796bec7c34d446deb9dc9b497853a8f2/verification.json` |
| Approved release-input and patch-note parity | Shared read-only guard passed: approved canonical/generated definitions, exact notes with 15 actual changed/new card panels, version 1.6.0 and the current mechanical hash; pinned inputs stayed unchanged | `Artifacts/QA/ExpansionReleaseCheck/validation-238fff16ed0149f0b7d15e26b4ead337/approved-inputs.json` |
| Workbook development pipeline | Clearly labeled Development1.6.0 fixture passed 6,504 independent saved-XLSX checks with 710 formula cells; long behavior/rule text reviewed after wrapping; no final workbook or Excel UI execution claimed | `Artifacts/QA/BalanceWorkbookFixtures/readability-925f7ce6d4ce413fa83a2370ffbc67c2/independent-validation.json` and `workbook-verification.json` |
| Audio wrapper contract | 13 isolated acceptance fixtures passed, including both exact new-cue checks and the 41 original + 20 variation + 2 expansion master contract; no engine or acoustic acceptance claimed | `Artifacts/QA/ExpansionAudio/exact-cues-fixture-b5309d121a9d4b7b8d3076016f4cca56/verification.json` |
| Historical telemetry audit retention | Preserved older canonical and no-hash 1.5.0 cohorts passed 174,002 and 173,123 checks; 12 synthetic child/shield attribution fixtures passed; this is not a new expansion cohort | `Artifacts/QA/ExpansionTelemetryAudit/cb83071133904a4783280ec8a64ddc44/verification.json` |
| Actual native/GPU/release acceptance | Pending: fresh native automation and Meta 100-game audit, selected asset/audio imports, normal-camera HIGH/LOW review, measured renderer/performance, drag, Shipping, launcher and installer checks | Completed run-specific proof paths will be appended; preparation and historical results cannot substitute for these runs |

## Historical preparation checks

| Check | Observed result | Immutable evidence |
| --- | --- | --- |
| Portable combat/navigation | Fresh 75 scenarios and 21 actual AI matches passed; pinned simulation inputs stayed unchanged | `Artifacts/QA/ExpansionCombat/20261011-130634-0df1cc5dfdcb485f92378113688cadfe/verification.json` |
| Existing seeded AI decks and hands | 56 old/new comparisons matched | `Artifacts/QA/ExpansionCombat/golden-decks-final-verification.json` |
| Saved Blender animation matrices | 561 checks, 20,924 sampled frames and 500,327 matrix/inverse checks passed | `Artifacts/QA/ExpansionAssets/animation-repair-20261011-1644/` |
| Source/export asset contract | 1,417 checks passed after the animation repair; this does not establish final native material/shader acceptance | `Artifacts/QA/ExpansionAssets/animation-repair-20261011-1644/source-export-qa-trackedrevision.json` |
| Portrait and portable provenance | 585 checks passed; fresh tracked checkout also passed | `Artifacts/QA/ExpansionAssets/animation-repair-20261011-1644/` |
| Selected native import | Three existing models imported and canonical card data rebound successfully with unchanged source/DLL pins; predates the stricter material repair below | `Artifacts/QA/ExpansionImport/import-bc0ea74c164d416fa074106695f80afa/run.json` |
| Latest Development Editor build | Gallery/readback compilation passed; native source pins stayed unchanged; this is not rendered material acceptance | `Artifacts/QA/ExpansionNative/build-d32c703669b2450fab8becedaf626751/run.json` |
| Earlier pinned native automation | 24 tests passed under NullRHI: 21 successes and three successes with warnings, zero failures; includes historical replay, Meta filters/exports and paid-fixture regressions | `Build/Automation/20261011-135847-69a552a1dc644b50a47378c75fbf2d51/Report/index.json` and `context.json` |
| Actual native Meta cohort | 100 completed games; 173,999 independent checks, zero failures, zero invalid/economy-invalid matches; maintained auditor matches the native run's source pin | `Artifacts/QA/expansion-foundation100-final/run.json`; `Artifacts/QA/ExpansionMetaAuditFinal/abe90e2b6d9f4ddc824b6c980579e331/` |
| Preserved v1.5.0 Meta cohort | Legacy dataset without a balanceHash passed 173,122 checks; original dataset, engine log and export hashes stayed unchanged | `Artifacts/QA/ExpansionLegacyMetaAudit/c68b4e4f4e5746c684769e1ba0c0e499/` |
| Historical export semantics | Actual production CSV/JSON exports from a clearly synthetic native fixture passed 6,624 independent checks, including 4,063 field-parity checks; 120 captured-roster pairs and 289 current-mechanics matchup rows | `Artifacts/QA/ExpansionMetaSerializationFinal/92756374f5e1472fb2725cd4b08be179/verification.json` |
| Workbook preparation fixtures | Synthetic fixtures passed 1,782 saved-XLSX checks and 228 formula-parity checks; no production balance workbook created and no Microsoft Excel execution | `Artifacts/QA/BalanceWorkbookFixtures/20261011-preparation/fixture-acceptance.json` |
| Release guard fixtures | 13 isolated cases and three routing checks passed; pending release correctly rejected; these fixtures launched neither Unreal nor packaging | `Artifacts/QA/BalanceReleaseGuard/06361c24db584636a855a0eae76e2722/verification.json` |
| Final repaired-tree acceptance | Pending: strict material/shader validation, fresh selected import/build, normal-camera HIGH/LOW review, measured renderer/performance runs, drag verification and required native reruns | Exact completed proof paths will be appended after the repaired inputs are frozen and tested |

These proofs pin their own inputs. Subsequent source or asset edits require fresh acceptance against the repaired tree. NullRHI integration success does not establish GPU shader or camera quality. Meta active-simulation time measures core work rather than renderer frame time; card win rates remain observed associations in generated decks, with the auditor's sampling/model limits retained. Synthetic export and workbook fixtures test semantics and arithmetic, not empirical balance or approved patch values.

## Retained failures and repair history

The first selected Unreal import exited with an FBX infinite-value error. A zero-scale Pulverizer death bone produced singular matrices. The original bad scene/FBX are preserved; the producer and export now use positive hidden scale with sampled matrix/reproduction verification. The failed import remains at `Artifacts/QA/ExpansionImport/import-263b6e1158b2429c80b86cf744718318/`.

A later Editor build exposed private-access calls in a newly added automation fixture. The fixture was corrected to use the public snapshot; the failed build remains at `Artifacts/QA/ExpansionNative/build-6a5caa3d68d044af804be6ca71ac0174/`. A subsequent build exposed an unsupported automation flag and a shared-reference/shared-pointer API mismatch; those were corrected and the failure retained at `Artifacts/QA/ExpansionNative/build-fa475bf986b54955b25d25989279c41e/`. The historical-export test later required conversion of UE5.8 shared JSON keys before prefix checking; that compile failure remains at `Artifacts/QA/ExpansionNative/build-2a70dfdbc3904f9b9af30f08a681c35f/`. The subsequent pinned build and all 24 native tests passed.

The GPU wave capture produced an image but failed acceptance because `M_RiftShockwave` did not compile for `PCD3D_SM6` and would use the default material. Its failure is retained at `Artifacts/QA/VisualRuns/expansion-pulverizer-wave-final-78f6adeb59864ebc90d17e6b2a1b5d51/expansion-pulverizer-wave-final.json`.

The newer strict selected import correctly rejected an invalid material interface on the Mini Stampede mesh. Source and DLL inputs stayed unchanged, and no out-of-scope native asset changes were recorded. That failed run is retained at `Artifacts/QA/ExpansionImport/import-d5338b5ea7d747f98fe0f3042249e5de/run.json`; material binding/shader repairs and their fresh validation remain open.

Final repaired-tree import, integration, renderer, Meta, drag and performance acceptance will be recorded separately. Approved expansion patch notes have been generated and checked; no successful new Shipping build, installer, final balance workbook or publication is claimed.

## Remaining acceptance and release work

The active user brief `c494829f-1fc2-4169-9f28-ae0726453489/Pasted text.txt` requires:

> Source conflicts must be settled before final balance data, release notes, and release builds are committed.

That source requirement is now resolved. The user approved all consolidated alternatives, all seven candidates and the active brief's transcribed tables as the numerical source. The named older file was not located; that fact remains in the provenance history, while the direct approval and resolved choices are recorded in `Assets/Data/source_resolution.json`. No numeric or candidate-scope question remains pending.

The actual baseline drains both existing buildings naturally: Archer Tower at 34 HP/s and Pulverizer at 700/45 HP/s, with no separate hard expiry. The approved patch changes Archer Tower to 25 HP/s plus expiry at 25 seconds. Pulverizer retains its captured decay behavior. The immutable original baseline and expanded schema derivative keep those comparisons explicit.

Complete actual native and rendered acceptance against the revised assets. Package the real Shipping game and test its launcher/installer lifecycle before finalizing and publishing the 1.6.0 release. Every result must identify its actual source/runtime pins; measured frame time and memory must remain distinct from target budgets and headless Meta throughput.

## Subsequent validation and requested art revision

The preceding tables describe retained earlier runs. Later portable validation passed 87 scenarios and 21 complete AI matches at `Artifacts/QA/ExpansionCombat/20261011-204522-bba5411a3cd843e99cd0d84d9edf9bc1/verification.json`. Subsequent native import and automation completed, with 28 eligible main tests and a separate rendered cloak-effects test. Those native runs precede the user-requested model revision and must not certify revised Content.

The six-tab production workbook now exists at `outputs/balance-expansion/Rift_Crown_Arena_Balance_Reference_1.6.0.xlsx` and in Downloads: 78,052 bytes, SHA256 `06ca711dda8beb6fba8367bdd8e9eb137019c1b826e9a5fd8cc5675b9d39b8e4`. Its 710 saved formula cells and 6,504 independent checks passed, and all six tabs were rendered and reviewed. No Microsoft Excel application execution is claimed. The approved numbers remain unchanged during the visual revision; final native evidence binding must be refreshed afterward.

On 2026-10-11 the user rejected the new models and animations as rushed, with overlapping details. Mechanical and render-capture checks did not establish acceptable artistic quality. The seven model rigs, their nine clips each, and matching portraits are being reworked with actual Blender geometry and rendered pose review. The real pre-revision source, assets, portraits and producer files are preserved under `Assets/Source/Revisions/ArtPolishBefore1.6.0/`; the additional provenance stage must verify retained legacy assets and fresh evaluated geometry. Packaging and publication remain pending until the revised models, native camera results and measured performance have been reviewed.

After reviewing the revised images, the user narrowed the remaining art changes to Bone Warden's captive skeletons and the Warden/Reaper weapon grips. Warden needs recognizable skeletons inside its rib cage with independent animation. The actual saved-surface diagnosis confirmed that both weapons passed through solid palms despite accurate hand-joint attachment: the earlier center-inside-palm rule could not establish a proper open grip. The targeted repair separates shaft contact anchors from hand-joint targets and adds evaluated hand/handle surface checks. Other revised models retain their reviewed geometry and animation.

## Actual current acceptance — 2026-10-12

This dated record updates the current acceptance state after the retained history above. The approved numerical manifest remains `1.6.0` / `e87244ec…`, with 21 playable cards, one subordinate, and the 24×34 arena. Earlier rejected runs remain rejected; their completed reports are reused only where the recorded qualification proves identical source, producer and dependency inputs.

| Completed step, in order | Actual result | Evidence |
| --- | --- | --- |
| Final portable mechanics/navigation | 87 scenarios and 21 complete AI matches passed against unchanged current inputs | `Artifacts/QA/ExpansionCombat/20261012-034835-227edf44efb1431292ebb732c431e888/verification.json` |
| Corrected source acceptance and strict finalization | Four complete same-source reports were explicitly qualified and reused: preservation of 48 unrelated objects/113 actions, 704 matrix checks, 1,297 all-seven geometry/pose checks and 312 approved contracts. Seven new stages passed: 398 focused checks, 1,761 whole-asset checks, 17 import-contract fixtures, strict finalization, 776 portrait checks, 48 current fresh-root checks and 32 historical fresh-root checks. The rejected prior run was not relabelled as successful | `Artifacts/QA/ExpansionArtRework/source-resume-eefbcfa926914cdc9825acb21a092aa7/run.json`; its pinned `completedStageQualification` and separate raw reports |
| Final selected model and audio imports | Actual model import followed by audio import passed against finalized source inputs | `Artifacts/QA/ExpansionImport/import-49a95bf4c0ac423490f25d691b3aed27/run.json`; `Artifacts/QA/ExpansionAudio/import-d8774246291e48e6bcb6284934ce8d63/run.json` |
| Current native acceptance | All 28 main tests passed under NullRHI; the separate rendered cloak-effects test passed under D3D12 | `Build/Automation/20261012-003742-d13c2a3c5a584185bd0b602aa1953e5e/context.json`; `Build/Automation/20261012-003953-57f288f68125495bbaa7b7e0a1cd20ef/context.json` |
| Native camera review | All 28 current capture cases were physically reviewed and passed the scoped visual review | `Artifacts/QA/ExpansionNative/captures-6a2534813c934c7889b8e9cd92ac61f9/batch.json`; `Artifacts/QA/ExpansionNative/FinalPhysicalReview-6a2534813c934c7889b8e9cd92ac61f9/review.json` |
| Paid crowd fixture and frame measurements | The 600-second screenshot-enabled visual run and separate 90-second HIGH/LOW runs passed their functional checks. The measured frame times below do **not** pass an FPS budget | The three immutable `Runs/.../performance.json` reports under `Artifacts/QA/Performance/expansion-bound-paid-visual/`, `expansion-bound-high-1080/` and `expansion-bound-low-720/` |
| Fresh current native Meta | Exactly 100 completed matches; 288,775 independent audit checks, zero failures, current mechanical hash and telemetry revision 4, unchanged source/runtime pins. These are sampled associations, not causal card-strength rankings | `Artifacts/QA/expansion-1_6-artfinal100-ccf976e8654f4685bf7bcc760f53e19f/run.json` and `independent-audit.json` |
| Unchanged production workbook rebound to current runtime | 6,504 independent checks, 710 formulas, 21 playable rows plus one subordinate, and 14 preserved reviewed previews passed. The workbook and Downloads copy remain byte-identical: 78,052 bytes / `06ca711d…`. No native Excel application execution is claimed | `Artifacts/QA/BalanceWorkbookRebind/e13c22f90b584ce4858d1057bf0aadc8/final-validation.json` |

Finalized asset manifest SHA256 is `68333849b6484108ce96217859b19b869a169bc0cd647307f4eb4742bd762cf4`; saved Source Blend SHA256 is `b7c215a00681328298919bb147900285a00335729934d08c28d9b7c608296929`. The fixed supersession preserves the unpublished interim stage and the original two accepted historical stages. Only the authorized manifest transition and four new provenance snapshots were written during finalization; geometry, exports, portraits and producers stayed frozen during the acceptance continuation.

The extreme paid-crowd measurements were mean/p99 **283.19/1,739.09 ms** for the 600-second visual run, **140.65/1,119.22 ms** for 90-second HIGH at 1920×1080, and **139.27/864.75 ms** for 90-second LOW at 1280×720. Functional success establishes exercised mechanics, cleanup and accounting; it does not establish acceptable frame rate under these crowds. The screenshot-enabled visual run also includes capture overhead.

A separate short normal-battle HIGH sample at 1920×1080 used a 60 FPS cap: 20 measured seconds after warmup, mean **16.7516 ms**, p99 **17.126 ms**, one **97.4475 ms** frame-time spike, and 28 peak entities. It is a short sample, not a sustained performance guarantee. Its immutable report is `Artifacts/QA/Performance/expansion160-final-normal-af392653dc0e47869955e17d99bca336/Runs/aa164959031543b8a3a305d2a510713e/performance.json`.

Shipping, packaged-game acceptance, launcher/installer lifecycle checks and publication remain pending at this point. This source/native/workbook record does not announce a released build or download.

## Local Windows delivery accepted — 2026-10-12T06:23:00.6972330Z

The isolated actual Shipping build/cook/archive, packaged HIGH/LOW review and Windows delivery are complete. The 49-file payload passed 31 launcher-core checks, 10 connected WPF controls and 51 MSI lifecycle assertions. Actual installed Play launched the verified Shipping executable; install, same-family upgrade, repair and uninstall preserved isolated save bytes. The successful UAT build and its original wrapper-layout rejection remain separately recorded; compilation changed only isolated compiler outputs, with original accepted inputs unchanged.

Evidence: `Artifacts/QA/ExpansionRelease/Windows160-93aef0f6d1f44c38be39e77ac1469d5a/release-state.json`, corrected `ShippingReview-45f81ff9bdd6466d9edb64cd8db2c06b/shipping-review.json`, `Artifacts/QA/installer-0a4c7516a9e94aea84f8c0c4004f3417/report.json`, and `Artifacts/Release/windows-release-verification.json`. The eight actual local assets are sealed at `Artifacts/QA/ExpansionRelease/seal160-fea674cbf663462ea77372bfd0f0af23.json`.

The earlier recorded crowd and short-sample performance limits remain unchanged. Publication, anonymous public downloads and public-latest updater verification are separate checks and were pending at local acceptance.
