# Native Windows release verification

Verification date: 2026-10-09. The development host is Windows 11 with a Ryzen 7 5700, 32 GB RAM and an NVIDIA GeForce RTX 5060. Engine: Unreal 5.8.2. Results below identify executed checks and their measured limits.

## Version 1.3.2 continuous water audio fix

The arena world previously started `river_ambience` as a nonspatial infinite loop before showing Home. Menu and battle share that world, so the same water bed played continuously. The hotfix removes its automatic component creation, volume updates and obsolete component field. All 61 authored sound assets remain; music, interface sounds, combat effects and their volume callbacks are retained. [PATCH_NOTES_1.3.2.md](PATCH_NOTES_1.3.2.md) describes the change.

Actual enabled Editor and final Shipping audio each pass all 61 assertions. A zero-gain live river component proves the inspection sees a real playing voice; it is immediately stopped and destroyed. Eight subsequent observations find zero river voices across Home, active Battle, repeated world binding, return to Home, Settings, deferred combat burst, limiter overload and a quiet scene. Muting only music through the actual Settings slider leaves Master/SFX/UI positive. After at least 0.8 seconds for existing tails, both live MasterMix quiet captures contain 245,760 float samples at eight channels/48 kHz, with peak 0, RMS 0 and zero clipped samples. Normal/12× overload Shipping peaks are 0.197657645/0.801207542; those captures also contain zero clipped samples. These measurements establish actual playback and mixer output rather than subjective listening quality.

The fresh complete fifteen-test native suite passes in 53.153130 seconds: fourteen clean successes and one intentional profile-backup-recovery warning, zero failed/unrun tests and clean exit. [QA/native-integration.json](QA/native-integration.json) identifies the current run and unchanged source/Editor-module pins; [QA/historical-native-integration-1.3.1.json](QA/historical-native-integration-1.3.1.json) preserves the previous version. The first 1.3.2 attempt at `Build/Automation/20261009-160432` remains failed development history: Typography recorded a failed atomic save replacement, with no font assertion failure. Its primary and backup remained valid and subsequent saves succeeded. All launch-time source/module hashes still match. The OS failure reason was not logged, so a sharing lock is plausible but unproven; the fresh same-byte suite above is the accepted run.

The final Shipping executable is 167,781,888 bytes / SHA-256 `63f928171ef5d5ba0263efc2d35af02d4596f65cd33090c9432b8b819f9d4e1f`. The 49-file ZIP is 397,990,465 bytes / SHA-256 `b134013ec747bbee3703dbbe1ccea73481d26d1b79f2b1d64912088b8995c88d`; MSI is 383,737,856 bytes / SHA-256 `ba1743a53d6d4311331934ff6f8c82d7c9ca936032521ba23fe3861c83e2cfeb`. Actual WPF Play launches this exact game. All 51 installer lifecycle checks pass from the preserved public 1.3.1 MSI through 1.3.2 upgrade, installed Play, repair and uninstall with isolated saves preserved. MSI compiled with standard ICE validation and zero warnings/errors in 144.725042 seconds; Windows finalization matches all release identities.

[QA/audio-hotfix-1.3.2.json](QA/audio-hotfix-1.3.2.json) binds current live audio, native and Windows evidence. The immutable [audio QA packet](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.3.2/RiftCrownArena-Audio-QAEvidence-1.3.2.zip) contains 100 independently reopened entries including all six actual mixer WAVs, is 1,149,165 bytes / SHA-256 `839bdfcd72ed7270c7376158308eac60ce9f6d57e6cda5a1e5d09997f9a083d0`. Original source/report hashes are distinct from sanitized entry hashes. Its prepublication pending marker is retained; public download and actual launcher update verification follow separately.

## Historical version 1.3.1 card hover verification

The HUD previously called `SetToolTipText` on every refresh. UE 5.8 creates a new Slate tooltip object for each call; its hover manager then closes the previous object and restarts the summon/fade state. The hotfix initializes each hand tooltip once and replaces it only when that slot's card identity changes. Its name and stats still refresh after deployment cycles the hand. Layout, models, art, fonts and combat rules are retained.

The actual Editor smoke passes all 32 assertions at 1920×1080; final Shipping passes all 32 at both 1920×1080 and 1280×720. The original 29 drag/cancel routes remain, plus three actual hover checks. Public Slate APIs use an application-local faux cursor, routed mouse events, production `SObjectWidget::Tick` and the real tooltip manager without touching the OS mouse. A fully visible tooltip retains its object/window and opacity 1.0 through twelve HUD refreshes. A real legal drop cycles Ironclad to Arc Mage, refreshes its tooltip text and identity, and retains that new hover through six more refreshes. Each accepted run checks fresh native diagnostics, exact executable/source identities, clean exit and zero errors.

The complete fifteen-scenario native suite passes in 63.373077 seconds, with thirteen clean successes and two successes with warnings: intentional profile-backup recovery and an engine HTTP connectivity probe timeout. There are zero failed/unrun tests; source and both Editor modules remain unchanged during execution. Integration is preserved as [QA/historical-native-integration-1.3.1.json](QA/historical-native-integration-1.3.1.json); [QA/historical-native-integration-1.3.0.json](QA/historical-native-integration-1.3.0.json) preserves the prior run. [QA/hover-stats-1.3.1.json](QA/hover-stats-1.3.1.json) binds the hotfix's actual hover, native and Windows evidence. The preceding model/portrait and performance reviews remain explicitly historical 1.3.0 evidence, with unchanged authored and imported asset hashes rather than a new physical review or performance claim.

The final native executable is 167,775,232 bytes / SHA-256 `995b53588d2368b18eb6f2f3367ee83c78df6c7364da496f72e6b9759c19d518`. The 49-file ZIP is 397,986,577 bytes / SHA-256 `1812ead60c240ba11513bc982ccddb0cbb6634d9f0374469e5f1257ca0af24fe`; MSI is 383,729,664 bytes / SHA-256 `224a18cd9ace730549f954df8bf4dcdb9507a77a7d9f43e8ea745a85d2fd654e`. Actual standalone WPF Play and all 51 installed lifecycle checks passed from the public 1.3.0 MSI through 1.3.1 upgrade, installed Play, repair and uninstall with isolated saves preserved. MSI compiled with standard ICE validation and zero warnings/errors in 138.991433 seconds. Windows finalization matched the package, installer, launcher and manifest identities.

The immutable [hover QA packet](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.3.1/RiftCrownArena-Hover-QAEvidence-1.3.1.zip) contains 49 independently reopened entries, is 319,494 bytes / SHA-256 `5286890ba727638ada86851c103381d7e2a3d031e4f41aa203b51bbdf0c23b82`. It includes current native and hover reports/logs, changed source and Windows delivery evidence. Original source hashes are separate from sanitized archive-entry hashes. The prior large model/portrait archive remains untouched; 259 authored-asset/source pins and all 204 imported model/card/font asset pins still match its 1.3.0 snapshot. The hotfix's frozen acceptance summary retains its prepublication public-pending marker; separate public-download and launcher-update reports follow publication.

[Version 1.3.1](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.3.1) public delivery passed. [QA/historical-public-download-verification-1.3.1.json](QA/historical-public-download-verification-1.3.1.json) records fresh complete anonymous HTTPS downloads of the 397,986,577-byte ZIP and 383,729,664-byte MSI, matching the exact tested hashes above. [QA/historical-published-update-check-1.3.1.json](QA/historical-published-update-check-1.3.1.json) records the released WPF launcher's actual `CheckButton.Click` against the default latest HTTPS endpoint: version 1.3.1, all 49 files, archive identity and displayed notes matched in 0.607602 seconds, with Install enabled. The check preserved isolated game and legacy-save fixtures. Complete game downloads were verified separately. All nine public asset sizes and server SHA-256 digests match the final local pins. These separate postpublication reports supersede the pending marker while retaining the immutable 49-entry hover packet.

## Historical version 1.3.0 model, typography and card input verification

All fourteen card identities have revised models and portraits rendered from the actual production scene. Eleven rigs retain their exact bone hierarchy, with fitted equipment following the hand bones; fifty-four source action poses and all fourteen portraits were physically inspected. Four embedded Barlow Semi Condensed weights replace the default UI font while retaining the main layout. Captured-pointer dragging deploys once on a legal release and cancels freely when returned to the hand or released over UI, invalid ground or outside the field.

| Area | Executed 1.3.0 evidence |
| --- | --- |
| Source models and portraits | 1,176 independent asset checks and 355 portrait checks passed; fourteen portraits, fifty-four action poses and five contact sheets physically reviewed |
| Actual import provenance | 250 raw source imports, 167 persisted primary-asset MD5 fingerprints and fourteen card bindings passed; fresh import/audit commandlets exited 0 with zero errors |
| Native assets and font | 294 imported assets, including one composite Font and four inline FontFace assets; upstream TTF/OFL provenance and packaged license match |
| Portable core | 41 regression scenarios and 21 complete seeded AI matches passed against current sources |
| Complete native integration | All fifteen scenarios passed in 58.188854 seconds, fourteen clean successes and one expected profile-recovery warning, zero failures/unrun tests; 130 source hashes and both Editor DLLs unchanged |
| Actual hand input | 29/29 Shipping checks at both 1920×1080 and 1280×720, including legal deployment, return cancellation, invalid/UI release, capture/focus loss and queued-release ordering |
| Final Shipping renderer | Eighteen actual frames physically reviewed, covering retained pages, details, roster, projectiles, pause, training, replay, pre-impact spells, 720p enlarged UI, 16:10 and ultrawide |
| Actual Shipping audio | 49 assertions passed with all 61 waves; normal/12× overload float peaks 0.224404/0.801212, zero clipped float samples and exit 0 |
| Actual Shipping Niagara | 80 assertions passed across seventeen systems, 156 immediate particles and seven persistent lifecycles |
| Actual launcher and MSI | Standalone WPF Play launched the exact final game; all 51 installed lifecycle checks passed from public 1.2.1 through upgrade, installed Play, repair and uninstall with isolated saves preserved |
| Windows finalization | 49 game files, matching app-local CRT, 31 launcher checks and ten WPF controls; final package/installer/manifest identities match |

The exact final native executable is 167,768,064 bytes / SHA-256 `42c76768e584c3e0f26c17ec7c84b9d2b2a49be07784e0441b9a088a92f8387d`. ZIP is 397,984,983 bytes / SHA-256 `ca1963682e325bc0af4fa72f8119171bd5fbc42ef22dc921a31cc0e55e1d4b66`; MSI is 383,709,184 bytes / SHA-256 `1c1f1f1ac92aaa616c1896e7f6243f5ac60d96b75cff61c4151cf2e29ef183c3`. [QA/model-input-checks-1.3.0.json](QA/model-input-checks-1.3.0.json) binds current source, asset, import, native-log, visual and Windows evidence. [MODEL_INPUT_1.3.0.md](MODEL_INPUT_1.3.0.md) describes the implementation and [PATCH_NOTES_1.3.0.md](PATCH_NOTES_1.3.0.md) describes the player-facing behavior.

One serial isolated D3D12 normal AI battle run at 1920×1080 measured 90 seconds after eight seconds of warmup on that exact executable. It recorded 5,401 frames at mean/p95/p99/maximum 16.667726/16.803298/16.933598/17.227303 ms, no frames over 50 ms and peak thirteen entities. Mean game/render/GPU times were 1.429591/2.431894/3.893711 ms; peak process memory was 1,149,263,872 bytes. Requested Meta simulation stayed paused during battle with zero completed games. This is a normal-play measurement, not a new stress soak.

Shipping uses its native structured FRiftDiagnostics logs. Each current runtime check verifies fresh timestamps, PID, game version, Shipping build and isolated save/config roots, copies only native logs outside the save fixture, and requires zero Error/Fatal entries. One earlier Replay capture attempt logged a failed atomic profile commit during Meta initialization; its retained primary/backup were valid and later writes succeeded. The fresh same-binary retry passed the full log gate. An external sharing lock is plausible but its source is not proven. The failed attempt remains development history rather than accepted evidence.

Still images establish captured state and readability; native animation/replay tests separately exercise timing. Automated audio establishes runtime routing and measured output, not subjective listening quality. The six authoritative simulation files are unchanged from 1.2.1, so its 100-match delayed-spell cohort retains its original provenance. The older instant-spell 10,000-match cohort remains historical. Enlarged framing, the centered bottom hand, first-damage HP and Meteor/Bullet delays remain in the tested game. Public delivery is checked separately after publication; the frozen acceptance snapshot retains its original pending marker.

The frozen primary [1.3.0 QA evidence archive](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.3.0/RiftCrownArena-QAEvidence-Windows-x64-1.3.0.zip) contains 644 independently reopened entries, is 162,442,157 bytes, and has SHA-256 `12c65630445adc8bf064a15d611a6fe238301e3f6f6ac47372ab643b4791dd4e`. Its actual Slate drag packet identifies the 1080p run. The separately frozen [720p packet](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.3.0/RiftCrownArena-CardDrag720-QAEvidence-1.3.0.zip) supplies the second-resolution evidence without changing the primary archive: 24 independently reopened entries, 150,163 bytes / SHA-256 `20557ae701e55d7462b205a3b1496835a27ff8b858f1535c74df4f6cfb66370c`. [QA/card-drag-720-1.3.0.json](QA/card-drag-720-1.3.0.json) pins all twenty-nine native checks and 73 fresh diagnostic records.

[Version 1.3.0](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.3.0) public delivery passed after publication. [QA/historical-public-download-verification-1.3.0.json](QA/historical-public-download-verification-1.3.0.json) records fresh complete anonymous HTTPS downloads of the 397,984,983-byte ZIP and 383,709,184-byte MSI, matching the exact tested hashes above. [QA/historical-published-update-check-1.3.0.json](QA/historical-published-update-check-1.3.0.json) records actual released WPF `CheckButton.Click` against the default latest HTTPS manifest: version 1.3.0, all 49 files, archive identity and displayed notes matched in 0.5798398 seconds, with Install enabled. The update check preserved isolated game and legacy-save fixtures and did not install or download the game; the complete downloads are separate evidence. These postpublication reports supersede the frozen acceptance snapshot's pending marker without changing either QA archive. All ten published asset sizes and server SHA-256 digests match the finalized local files.

## Historical version 1.2.1 spell timing verification

Only Meteor Shards and Bullet Burst gain a delay: 0.75 and 0.30 seconds respectively. Their marked target areas stay fixed while the models travel; damage resolves against current enemy positions at impact. Meteor's five-second damage zone begins at that impact. Bullet's seven visible rounds retain one damage application per eligible target. Nova Flask remains instant, and the existing fourteen-card costs, damage, radius and targeting rules are retained.

| Area | Executed 1.2.1 evidence |
| --- | --- |
| Portable authoritative core | 41 regression scenarios and 21 complete seeded AI matches passed; `Artifacts/QA/spell121-portable-tests.log` |
| Final-source native integration | Fourteen scenarios passed in 61.562084 seconds: thirteen clean successes and one expected profile-recovery warning, zero failures/unrun tests and commandlet/wrapper exit 0; `Build/Automation/20261009-130849/Report/index.json` |
| Launch provenance | Schema-2 [QA/historical-native-integration-1.2.1.json](QA/historical-native-integration-1.2.1.json) pins all 124 source hashes and both Editor DLLs unchanged through completion |
| Replay and presentation timing | SpellCastReplay covers snapshots, event reconstruction, pause, impact/zone timing, cancellation, backward seeks and legacy instant-spell archives; actual presentation playback restores rings/meshes and removes future impact debris/cues after eight-millisecond rewinds across both deadlines |
| Player-facing timing | ConnectedUI verifies 0.75/0.30 seconds and lead advice only for the two delayed spells; CardData matches authoritative timing for all fourteen cards |
| Current-rule Meta batch | 100 completed matches, 165,725 independent checks, zero failed checks, invalid matches or invalid economies; validated final export, 79 buckets and four checkpoints |
| Final Shipping build | Build/cook/stage/archive completed successfully in 107 seconds; native executable 167,720,448 bytes / SHA-256 `30b7989948b690296a44c53f1f12d82162c7397016b7e58338e7d59545c59ae9` |
| Package and matching CRT | 48 game files, twenty matching runtime files, 650 CRT imports checked across 37 PE files; ZIP 396,393,616 bytes / SHA-256 `1c89f74925c197364a7acbcfe8ec260bd02b32491f85d7ab307c46dcd5d51357` |
| Final Shipping renderer | Nine actual PNGs physically reviewed against the final native hash: five spell flight/impact states, 720p large UI, both spell-detail pages and patch notes; 35 state/runtime checks passed |
| Actual Shipping audio | 49 assertions passed with 41 base sounds and twenty combat variations; normal/12× overload peaks 0.213759/0.801210, zero clipped float samples and exit 0 |
| Actual Shipping Niagara | 80 assertions passed across seventeen systems, 156 immediate particles and seven persistent lifecycles |
| Actual standalone launcher Play | WPF `PlayButton.Click` verifies all 48 files and the exact final executable; bootstrap/native exit 0, real 1280×720 Home capture, isolated save forwarding and preserved legacy save |
| MSI compilation | 1.2.1 MSI compiled in 148.332558 seconds with standard ICE validation; 382,246,912 bytes / SHA-256 `2fb2eca115656fff1a6d3a36b4f27738e06e8fe2e81bf2487bd97da55327a85a` |
| Installed lifecycle | All 51 actual checks passed from public 1.2.0 to installed 1.2.1, including installed WPF Play, packaged CRT identity, repair, uninstall and preserved separate saves |
| Windows finalization | Passed with 31 launcher checks, ten WPF controls, the 51-check lifecycle and matched ZIP/MSI/launcher/manifest identities |
| Public delivery | Fresh complete anonymous ZIP/MSI downloads match the tested sizes/hashes; actual released WPF `CheckButton.Click` matches default latest version 1.2.1, all 48 files and archive identity, displays notes and enables Install |

The earlier complete native suite in `Artifacts/QA/spell121-first-historical-native-integration-1.2.1.json` precedes final presentation refinements and remains development history. Current Shipping and Windows acceptance above identifies the final 1.2.1 payload. Public delivery passed through the separate anonymous-download and actual launcher-update checks below.

The current-rule audit is [QA/spell-timing-meta-100.json](QA/spell-timing-meta-100.json), fingerprint `b3968027993fd0ece72577b77c48d76e`. Delayed spell impacts intentionally change gameplay outcomes, so the earlier 10,000-match instant-spell cohort remains historical. Old datasets stay available for viewing; the game starts a separate dataset when the rules fingerprint differs. Existing replay archives retain their recorded instant-spell timing.

The nine-frame review is `Artifacts/QA/spell121-final-visual-review.json`; [QA/spell-timing-visual-1.2.1.json](QA/spell-timing-visual-1.2.1.json) pins the spell-state checks. Actual sound-enabled playback reports are `Artifacts/QA/Audio/spell121-shipping-audio/{audio-smoke,run}.json`, and actual graph/lifecycle execution is `Artifacts/QA/spell121-shipping-vfx-verification.json`. Runtime audio checks do not establish subjective listening quality; still frames establish captured states and readability, while the replay integration test exercises motion timing.

`Artifacts/QA/Release121History/windows-release-verification.json` binds the final package to the actual launcher and installer lifecycle. Source reports are `Artifacts/QA/Release121History/launcher-play-smoke.json`, `Artifacts/QA/Release121History/installer-build.json` and `Artifacts/QA/Release121History/installer-tests.json`. The compatible launcher retains its independent 1.0.0 assembly version; installed game and manifest are 1.2.1. Public delivery is established by the separate postpublication evidence below.

One serial isolated 90-second D3D12 run at 1920×1080, after eight seconds of warmup, exercised normal AI battle with Meta requested on the final executable. `Artifacts/QA/Performance/spell121-final-normal-meta/{performance,run}.json` records 5,401 frames at mean/p95/p99/maximum 16.666949/16.736697/16.828999/17.317697 ms, no frames over 50 ms, peak thirteen entities and zero Meta games during active battle. Mean game/render/GPU times were 1.345848/2.292829/4.003551 ms; peak process memory was 1,165,185,024 bytes. The process exited 0. The historical stress measurements below belong to 1.2.0 and earlier releases; this is not a new 1.2.1 stress soak.

[Version 1.2.1](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.2.1) public delivery passed. [QA/historical-public-download-verification-1.2.1.json](QA/historical-public-download-verification-1.2.1.json) records complete anonymous HTTPS downloads of the 396,393,616-byte ZIP and 382,246,912-byte MSI, matching the exact tested archive and installer hashes above. [QA/historical-published-update-check-1.2.1.json](QA/historical-published-update-check-1.2.1.json) records actual released WPF `CheckButton.Click` against the default latest HTTPS manifest: version 1.2.1, all 48 inventory files, archive identity and displayed notes matched in 0.6146819 seconds, with Install enabled. It fetched the manifest without downloading/installing the game and preserved the isolated game and legacy-save fixtures; the full package downloads are separate evidence.

The published [1.2.1 QA evidence archive](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.2.1/RiftCrownArena-QAEvidence-Windows-x64-1.2.1.zip) contains 184 verified entries, is 26,529,928 bytes, and has SHA-256 `106a3151dbe952582b102959dddd495608ccb32df3361c3ee4a4b975a11e0e56`. Its frozen prepublication [QA/spell-timing-checks-1.2.1.json](QA/spell-timing-checks-1.2.1.json) keeps the original public-pending marker. The separate postpublication reports supersede that marker without changing the archived acceptance snapshot. Historical 1.2.0 reports remain preserved under their explicitly versioned filenames.

## Historical version 1.2.0 verification

The final revision changes production models, card portraits, animation presentation, mesh projectiles, guard weapon aim/recoil, particle readability, battlefield/HUD framing and first-damage health visibility. The fourteen-card numerical rules and six authoritative simulation sources remain unchanged. A compact centered bottom hand and a 35° orthographic battle angle protect enlarged models from the controls. Source/import responsibilities are in [MODEL_PRESENTATION_1.2.0.md](MODEL_PRESENTATION_1.2.0.md).

| Area | Executed final-source 1.2.0 result |
| --- | --- |
| Editor build | Build9 succeeded in 14.84 seconds; log `Artifacts/model12-editor-build9.log` |
| Native integration | All 13 scenarios passed in 75.499664 seconds: 11 clean successes and two successes with warnings, zero failures/unrun tests and commandlet/wrapper exit 0 |
| Launch provenance | Schema-2 context pins all 124 source hashes and both Editor DLLs unchanged through completion; curated evidence identifies 1.2.0 |
| Native asset registry | 289 on-disk assets: 113 animation clips, 25 StaticMeshes, 61 SoundWaves, 14 RiftCardData assets, 17 Niagara systems and 11 PhysicsAssets; zero registry/native-load errors |
| Portrait provenance | Fourteen production-model portraits pass 352 independent source/export/PNG/pose checks, pinning raw source, model, report and script bytes |
| Health and replay | UnitMotion passes first-damage, full-heal persistence, zero/negative/nonfinite hit rejection, building lifetime baseline, actor reuse and actual archived first-hit/backward/forward-seek assertions |
| Windows Shipping build | Final UE 5.8 build/cook/stage/archive succeeded in 127.64 seconds; native executable is 167,687,168 bytes / SHA-256 `5a209be2b0d17cf7f2aebfc41c9eff9e52e791b1da8b8c1cb1c93d8d9b1a322e` |
| Local package and matching CRT | 48 game files; 650 imported CRT symbols verified across 37 PE files, with 20 runtime files staged. ZIP is 396,373,442 bytes / SHA-256 `63d02e6f3de1a535e9563691724c1e0ccf0dfacc6eaf86472b569c2326431e0b` |
| Actual Editor audio | All 49 assertions passed with the complete 61-wave bank; normal/12× overload float peaks 0.214397/0.800237, zero clipped floats, exit 0; wrapper duration 26.93 seconds |
| Actual Shipping audio | All 49 assertions passed on the final executable; normal/12× overload float peaks 0.197837/0.801365, zero clipped floats, complete 61-wave bank and exit 0; wrapper duration 12.23 seconds |
| Actual Niagara lifecycle | Editor passed 78 assertions and final Shipping passed 80 across all 17 graphs, with 156 immediate particles and seven persistent lifecycles |
| Final Shipping renderer | All 26 real PNGs physically reviewed, with 78 image/run/state pins and the exact final native/model-document hashes; twelve public pages, three battle aspect ratios, enlarged UI, projectiles/effects, placement, replay and phase/result fixtures covered |
| Published launcher Play | Actual `PlayButton.Click` verified all 48 game files and launched the final native executable; bootstrap/native exited 0, real 1280×720 Home capture and environment save-root forwarding passed, legacy save preserved |
| MSI compilation | Actual 1.2.0 MSI compiled in 168.965 seconds with standard ICE validation; 382,181,376 bytes / SHA-256 `77efc44db0ca00ce9ad874437baabb833fdfeb4be815c707d60f7b2099f8e768` |

The complete native suite is `Build/Automation/20261009-114843/Report/index.json`, with its frozen launch context beside it. [QA/historical-native-integration-1.2.0.json](QA/historical-native-integration-1.2.0.json) records the accepted final source and modules. The two warnings are the intentional corrupt-profile backup recovery and an engine HTTP connectivity probe timing out; both tests completed successfully, and neither warning was hidden. UnitMotion and ProjectilePresentation use the actual imported production assets.

The accepted [historical 1.2.0 Shipping renderer review](QA/historical-presentation-review-1.2.0.json) measures the arena ground at 507.0625 pixels wide at 1280×720/UI scale 1.4, with 13.34375-pixel tile pitch. Thirty troop bounds measure 33.721/56.733/97.221 pixels in minimum/mean/maximum height; all 38 model and annotation-anchor bounds fit safely. Ground widths are 934.426 pixels at 1920×1080, 672.573 at 1280×800 (16:10) and 934.428 at 2560×1080 (21:9). The 640 × 156 centered bottom dock retains 64 × 80 portraits, keys, costs, names, Next and Aether and clears the player Core.

Healthy HP bars remain hidden, while damage in actual live and replay frames shows HP; all 26 captured states have zero healthy-HP visibility violations. Status labels and the building lifetime indicator remain independent. The paused ranged fixture contains nine actual bodies/launches, nine particles, seven ranged roles and 45 trail instances. Gold/cyan projectile heads are visible and restrained; a still does not establish continuous animation timing or nine broad glow quads. The breath fixture contains 16 Frost particles in two actual puff systems plus 48 Nova particles, for 64 total world particles. Real double/triple-Aether and overtime banners were inspected at delay 1.2 seconds, together with persistent tiebreaker and victory panels.

The frozen prepublication [QA/presentation-checks-1.2.0.json](QA/presentation-checks-1.2.0.json) preserves sanitized executed local checks and exact source-report hashes. Its original pending-public-delivery marker is superseded by the separate postpublication proof below, leaving the archived QA evidence unchanged. Actual sound-enabled playback reports are `Artifacts/QA/Audio/model12-{editor,shipping}-audio-final9/{audio-smoke,run}.json`; actual graph/lifecycle reports are `Artifacts/QA/model12-{editor,shipping}-vfx-final9-verification.json`. Shipping reports identify the executable above. Mixer measurements precede PCM16 encoding and do not establish subjective listening acceptance.

All 51 real installed MSI lifecycle checks passed from published 1.1.0 to game 1.2.0, covering install, upgrade, installed WPF Play, loaded CRT identity, repair, uninstall and byte-preserved saves. `Artifacts/QA/installer-tests.json` identifies the exact final MSI/ZIP and 48 installed game files. `Artifacts/Release/windows-release-verification.json` passed finalization with 31 launcher update checks, ten WPF controls and the matched lifecycle; it pins the final MSI, ZIP, launcher and manifest. All three final performance runs completed on the exact native executable with clean exits and independent native JSONL diagnostics; measured limits follow below. Public download/update delivery passed after actual publication; the executed results and proof links follow below. Earlier Build5/left-tray and first-Shipping results are provisional development history and do not identify the final executable. The 1.1.0 and 1.0.0 sections below retain their executed historical measurements.

## Historical version 1.2.0 public delivery

[Version 1.2.0](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.2.0) became the latest stable release at 2026-10-09 16:23:03 UTC, after all seven GitHub asset digests matched the tested files. Release ID: 408102831. Source PR #4 merged at commit `9eb08839f448fa979850e57ab0196c11bb263eb1`, with tag `v1.2.0` identifying the game payload.

[QA/historical-public-download-verification-1.2.0.json](QA/historical-public-download-verification-1.2.0.json) records fresh complete anonymous HTTPS downloads of the 396,373,442-byte game ZIP and 382,181,376-byte MSI. Their SHA-256 hashes match the exact tested `63d02e6f…` archive and `77efc44d…` installer recorded above. [QA/historical-published-update-check-1.2.0.json](QA/historical-published-update-check-1.2.0.json) records the released WPF launcher's actual `CheckButton.Click` against its default latest HTTPS endpoint: version 1.2.0, all 48 inventory files, archive identity and displayed patch notes matched in 0.609123 seconds, with Install enabled. The update check fetched the manifest without downloading or installing the game and preserved the isolated game and legacy-save fixtures; complete package downloads are separate evidence.

The published [QA evidence archive](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.2.0/RiftCrownArena-QAEvidence-Windows-x64-1.2.0.zip) contains 311 verified entries, is 137,277,782 bytes, and has SHA-256 `9a6c309837a35855bf9238002905148dcd18943b98042987ae1c47bfe65a5776`. Its prepublication local acceptance snapshot stays unchanged; these postpublication reports establish delivery independently. Historical 1.1.0 public proof remains byte-identical in the explicitly named historical files.

## Historical version 1.2.0 Shipping performance

Three serial isolated runs used the final D3D12 Shipping executable at 1920×1080, eight seconds of warmup and 90 measured seconds each, on the host above with the default 60 FPS limit. Actual reports are `Artifacts/QA/Performance/model12-final9-{normal,stress,stress-meta}/{performance,run}.json`. Launch wrappers pin native SHA-256 `5a209be2b0d17cf7f2aebfc41c9eff9e52e791b1da8b8c1cb1c93d8d9b1a322e` and each measured report's exact hash. Wrapper durations, including startup/shutdown, were 102.32, 103.42 and 102.29 seconds, separately from the measured intervals.

| Scenario | Measured frames | Mean ms | p95 ms | p99 ms | Maximum ms | Frames over 50 ms | Peak entities |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Normal AI battle | 5,401 | 16.667 | 16.736 | 16.848 | 17.196 | 0 | 15 |
| Repeated heavy bridge waves | 5,363 | 16.785 | 16.918 | 17.181 | 85.929 | 10 | 134 |
| Heavy waves with Meta requested | 5,355 | 16.810 | 16.933 | 17.207 | 140.815 | 13 | 154 |

| Scenario | Mean game thread ms | Mean render thread ms | Mean GPU ms | Peak process memory GiB |
| --- | --- | --- | --- | --- |
| Normal AI battle | 1.261 | 2.255 | 3.953 | 1.067 |
| Repeated heavy bridge waves | 3.942 | 2.718 | 4.216 | 1.447 |
| Heavy waves with Meta requested | 3.926 | 2.760 | 4.246 | 1.486 |

The stress fixture deliberately spawns heavy waves through production Developer commands every five seconds and restarts completed matches. Those artificial mass-spawn boundaries recorded ten and thirteen frames over 50 ms. Ordinary stress's worst frame was 85.929 ms at wall time 45.167 s / simulation time 45.100 s with 134 entities. Requested-Meta stress's worst was 140.815 ms at wall time 60.600 s, during the match restart at simulation time 0.016667 s with 50 entities; peak population elsewhere was 154. Wall time spans fixture restarts while simulation time resets. These hitches remain an explicit limit; the measurements do not promise smooth performance during every stress burst. Normal AI play recorded no frame over 50 ms.

Meta advanced by zero games during each active-battle measurement and reported its paused state. The requested-Meta worker resumed after that measurement and completed four valid matches before shutdown; those are separate from active-battle work. All three native processes exited 0. Each isolated `UserData/Logs/RiftGame*.log` contains one valid 1.2.0 Shipping/D3D12 context, complete JSONL records and zero Error/Fatal entries. [QA/presentation-checks-1.2.0.json](QA/presentation-checks-1.2.0.json) pins the exact performance/run/native-diagnostic reports and preserves all hitch records, statistics and native source/module hashes.

## Historical version 1.1.0 verification

This revision changes native UI, battle input, camera framing and audio. The six authoritative simulation sources remain byte-identical to the completed final-core 10,000-match cohort. Existing arena geometry, card illustrations, all 14 cards, five deck presets, detailed collection/deck intelligence, replay controls, recorded metrics and Meta Lab exports remain available.

Replay timing preserves recorded doubles and maps the float slider maximum to the exact end. The passing real 299.05-second regression checks that the preceding slider position cannot show terminal events early, forward playback emits the final tower/result once, endpoint seeking stays silent, and the actual tower-fall bookmark restores the finished result. Final Build15 and Shipping evidence below includes this correction and the widened overtime announcement panel verified in actual Editor renders. Earlier Build14/`623c53e7…` results remain provisional history for that changed presentation.

| Area | Observed 1.1.0 result |
| --- | --- |
| Native Editor build | Final Build15 completed successfully with corrected replay dates and fractional endpoints, readable result reasons, triple-Aether announcement, widened overtime panel and complete selected-card inspector |
| Portable deterministic core | Fresh production-core run passed 36 regression scenarios and 21 complete seeded AI matches |
| Unreal integration | Final Build15 passed all 11 scenarios in 51.673996 seconds: ten clean successes and the expected corrupt-profile recovery warning; zero failures, errors or unrun tests; commandlet and wrapper exited 0 |
| Launch provenance | Final schema-2 context verified all 121 source hashes and both Editor DLLs unchanged from launch through completion; its curated report matches game version 1.1.0 |
| Actual Editor audio | Final Build15 playback passed 49 assertions in 22.6 seconds with all 41 base sounds and 20 combat variations loaded; normal/12× overload float peaks 0.206879/0.800180, zero clipped float samples, clean exit 0 |
| Actual Shipping audio | The final native executable passed 49 assertions in 11.16 seconds; normal/12× overload float peaks 0.203054/0.801192, zero clipped float samples, complete 61-wave bank and clean exit 0 |
| Actual Shipping Niagara | All 80 assertions passed across 17 systems; 156 immediate particles and all seven persistent effect lifecycles executed on the final native executable |
| Native asset registry | 283 on-disk assets, including 61 SoundWaves, 14 RiftCardData assets, 17 Niagara systems and 11 PhysicsAssets; zero registry/native-load errors |
| Launcher compatibility | Fresh launcher regression passed 31 checks; ten actual self-contained WPF control/resource checks also passed. The compatible launcher binary retains its separate 1.0.0 version while the game advances to 1.1.0 |
| Actual launcher Play | The existing launcher verified all 51 final game files and routed Play to native SHA-256 `0f35c7e9f52bb6009876c5e2eef23be8d4033712d844a91f04a8c28c68aa176b`; bootstrap and native processes exited 0 |
| Installer lifecycle | All 51 actual lifecycle checks passed, including upgrade from public 1.0.0 to installed game 1.1.0; MSI SHA-256 `ed7244eb6909f8a9529eb1d5da0cb1fc1fe2a37f7175fe9cf74e5446cf663491`. MSI built in 217.9309124 seconds with zero warnings/errors |
| Gameplay cohort | The existing 10,000-match final-core validity evidence remains applicable to the unchanged six simulation sources; it is historical execution evidence, not a new rendered 1.1.0 soak |
| Windows Shipping build | Final build/cook/stage/archive succeeded in 59.24 seconds; native executable SHA-256 `0f35c7e9f52bb6009876c5e2eef23be8d4033712d844a91f04a8c28c68aa176b` |
| Local release payload | Finalization verified all 51 game files, actual launcher execution and the tested MSI against ZIP SHA-256 `fc32c6add036ee0c4baaf2656bfb599642fed05028231e17da197db92555c5f2`; ZIP 448,655,642 bytes and MSI 429,428,840 bytes |
| Final Shipping renderer | All 29 fresh PNGs were physically reviewed and the complete aggregate accepted against the final executable: all 12 public pages, three battle aspect ratios, enlarged UI, training/placement/combat/frost and actual phase/result fixtures |
| Final Shipping performance | Three serial 90-second runs completed on the exact final executable, with clean exits and independent native diagnostics. Normal battle had no frame over 50 ms; stress runs had eight and nine, with worst frame 59.212 ms |
| Published release | [Version 1.1.0](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.1.0) was selected as the latest stable release when published 2026-10-09 at 03:50:51 UTC with draft disabled; initial seven uploaded asset digests matched local bytes |
| Public downloads | Fresh complete anonymous HTTPS downloads of ZIP 448,655,642 bytes / SHA-256 `fc32c6add036ee0c4baaf2656bfb599642fed05028231e17da197db92555c5f2` and MSI 429,428,840 bytes / SHA-256 `ed7244eb6909f8a9529eb1d5da0cb1fc1fe2a37f7175fe9cf74e5446cf663491` matched the tested payloads |
| Published launcher update | Actual released WPF `CheckButton.Click` fetched latest HTTPS manifest 1.1.0 with all 51 files and exact archive identity; patch notes displayed, Install enabled and existing game/save fixtures preserved. This check fetched the manifest without downloading or installing the game |

The final native suite is `Build/Automation/20261008-232104/Report/index.json`, with completed launch context beside it. It includes the exact fractional replay endpoint, a real 240-second triple-Aether transition while overtime stays visible, and win/loss results that survive replay-saving/HUD rebuilding without restarting or recording twice. Final actual playback evidence is `Artifacts/QA/Audio/polish15-{editor,shipping}-audio-final/{audio-smoke,run}.json`. Both runs captured live post-effect normal and overload PCM; waveform/headroom assertions do not establish subjective listening quality. Current source behavior is reviewed in [UI_POLISH_1.1.0.md](UI_POLISH_1.1.0.md), with audio authoring/routing documented in [AUDIO_DESIGN.md](AUDIO_DESIGN.md). Renderer inspection, installer lifecycle and public delivery have their own completed execution evidence below.

The historical complete renderer review is [QA/historical-presentation-review-1.1.0.json](QA/historical-presentation-review-1.1.0.json): 29 actual inspected PNGs with each image, guarded successful launch metadata and captured state pinned to its reviewed bytes. It covers all 12 public game pages, battle at 16:9, 16:10 and 21:9, and enlarged UI scale. Battle/replay captures independently report all four legal-field corners within their HUD-safe bounds; requested phase fixtures passed. The QA evidence packager requires the exact final native Shipping executable hashes for selected renderer, audio, VFX and performance reports, then binds the tested installer to the verified release payload.

Final local executable and installer acceptance is recorded in `Artifacts/QA/launcher-play-smoke.json`, `Artifacts/QA/installer-tests.json` and `Artifacts/Release/windows-release-verification.json`, all bound to the exact final Build15 payload. The installer report starts from public 1.0.0 to execute a real upgrade to `installedVersion` 1.1.0; the launcher retains its independent 1.0.0 assembly version. Final Shipping particle evidence is `Artifacts/QA/polish15-shipping-vfx-final-verification.json`. Separate completed public delivery evidence is linked below.

## Historical version 1.1.0 Shipping performance

Three sequential runs used the actual final D3D12 Shipping executable at 1920×1080, eight seconds of warmup and 90 measured seconds each, on the host above at the default 60 FPS limit. Launch reports pin native SHA-256 `0f35c7e9f52bb6009876c5e2eef23be8d4033712d844a91f04a8c28c68aa176b` and each completed report's exact hash. Wrapper durations, including startup and shutdown, were 102.31, 103.37 and 102.25 seconds; these are distinct from the measured intervals.

| Scenario | Measured frames | Mean ms | p95 ms | p99 ms | Maximum ms | Frames over 50 ms | Peak entities |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Normal AI battle | 5,401 | 16.668 | 16.803 | 16.948 | 17.818 | 0 | 15 |
| Repeated heavy bridge waves | 5,368 | 16.769 | 16.947 | 17.246 | 56.962 | 8 | 142 |
| Heavy waves with Meta requested | 5,362 | 16.789 | 16.973 | 17.376 | 59.212 | 9 | 154 |

| Scenario | Mean game thread ms | Mean render thread ms | Mean GPU ms | Peak process memory GiB |
| --- | --- | --- | --- | --- |
| Normal AI battle | 1.349 | 2.299 | 3.844 | 1.07 |
| Repeated heavy bridge waves | 3.983 | 2.749 | 4.383 | 1.43 |
| Heavy waves with Meta requested | 4.140 | 2.842 | 4.344 | 1.49 |

The stress fixture creates large waves every five seconds through production spawning and restarts completed matches. Recorded hitches cluster around these wave boundaries: the largest ordinary stress frame was 56.962 ms at wall time 45.126 seconds / simulation time 45.083 seconds with 142 entities; the largest Meta-requested stress frame was 59.212 ms at wall time 60.155 seconds / simulation time 60.117 seconds with 154 entities. Wall time continues across fixture restarts while simulation time resets. These brief hitches remain recorded; normal AI play had none over 50 ms. Meta advanced by zero matches during every active battle, including the run requesting Meta work. All three processes exited 0 and their independent `RiftGame*.log` diagnostics contained no Error/Fatal records.

Raw reports and launch-time executable/report hashes are under `Artifacts/QA/Performance/polish15-{ai-match,stress,stress-meta}-1920x1080/{performance,run}.json`. They are historical 1.1.0 measurements; the separate 1.0.0 table below remains historical. Headless 10,000-match cohort throughput is separate from rendered frame timing.

## Historical version 1.1.0 public release delivery

[Version 1.1.0](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.1.0), release ID 407483225, was published as the latest stable release at 2026-10-09T03:50:51Z with draft disabled. GitHub's server SHA-256 digests matched the six initial release payloads and their checksum asset. Fresh anonymous HTTPS downloads retrieved the entire 448,655,642-byte game ZIP and 429,428,840-byte MSI; both matched the exact locally tested sizes and hashes.

The actual released standalone WPF launcher raised `CheckButton.Click` against the unchanged default HTTPS latest-manifest endpoint. Its returned 1.1.0 manifest matched all 51 game files and the version-pinned archive hash/size. Patch notes displayed, Install was enabled, and existing game/save fixtures remained unchanged. The compatible launcher retains version 1.0.0; this check fetched the manifest and did not download or install the game.

Sanitized observations are preserved byte-for-byte in [historical-public-download-verification-1.1.0.json](QA/historical-public-download-verification-1.1.0.json) and [historical-published-update-check-1.1.0.json](QA/historical-published-update-check-1.1.0.json). These post-publication checks are separate from the pre-publication QA ZIP. The preserved 1.0.0 public reports use the historical filenames linked below.

## Historical version 1.0.0 acceptance

The following results and executable identity describe the previously published 1.0.0 release. They remain historical evidence and do not certify the changed 1.1.0 presentation, sound bank or installer.

| Area | Measured result |
| --- | --- |
| Native deterministic core | 36 regression scenarios and 21 complete seeded AI matches passed; unchanged card balance and rules |
| Unreal integration | Nine tests passed, including connected UI callbacks, complete replay histories, profile recovery, card assets and Meta workers |
| Full-length replay | Peak 80-entity history retained all 23,922 events and 1,256 samples; 88,247,587 JSON bytes in an 8,572,005-byte lossless archive; game-thread save queue 2.027 ms |
| Native Meta telemetry revision 3 | 10,000 actual completed matches, zero invalid matches and zero economy-invalid matches |
| Independent final-core Meta audit | 488,780 checks passed across 82 buckets and 400 checkpoints; actual completion and all final exported metrics checked |
| Historical bank-fix comparison | 67,915 retained fields compared; 11,171 overlapping aggregate checks differ. A reproducible 25-seed witness isolates the numerical cause; card values and deck/style populations remain unchanged |
| Launcher/update core | 31 integration checks passed, including actual ZIP/inventory verification, rollback, repair, cancellation, locks and save preservation |
| Published WPF launcher | 10 connected controls/resources checked through the actual self-contained application |
| Final installer lifecycle | 51 assertions passed against the exact final MSI: install, real same-family upgrade, installed WPF Play/native rendering, matching loaded CRTs, repair, shortcuts, uninstall and byte-identical save preservation |
| Asset source/export structure | 1,123 checks passed; native scale, skeletons, LODs, physics, illustrations and bindings verified separately in Unreal |
| Original audio masters | All 41 PCM files are non-silent with no clipped samples; both loop endpoints match exactly |
| Packaged native audio | 38 checks passed with the real sound-enabled AudioDevice, including persistent loop survival after the 32-voice one-shot budget |
| Niagara renderer | 78 Editor and 80 Shipping assertions passed across all 17 actual systems; 156 immediate particles and all seven persistent effects survived beyond one second |
| Packaged combat rendering | Actual 1920×1080 Shipping capture reached 4.0 simulation seconds with attack, projectile, slow, aura and stun events, an active Meteor zone and 35 live particles |
| Frost Fang idle presentation | Both teams select the real authored Breath asset and emit bounded mouth frost puffs; combat animation priorities and timing are preserved |
| Windows Shipping packaging | Actual build/cook/stage/archive completed successfully; editor installation is not used by the packaged executable |
| App-local Visual C++ runtime | Matching 14.51 runtime copied and hashed; 650 imported symbols across 37 packaged PE files checked against actual exports |

The final source, Shipping package, launcher and installer passed the checks above. `windows-release-verification.json` binds the final ZIP, launcher, manifest and MSI to their actual executed lifecycle reports. Every packaged audio, VFX, combat and performance run records the final native executable SHA-256 `981ff75a5c837fa74c2a3f1a00c21794fdc418d39504c12ced1ca3331ce63a86` at launch.

## Historical 1.0.0 Shipping performance

Three sequential runs used the actual packaged D3D12 executable at 1920×1080, eight seconds of warmup and 90 measured seconds each. Other builds, compression and native QA processes were stopped during measurement. These are frame times on the host above, at the game's default 60 FPS limit.

| Scenario | Mean ms | p95 ms | p99 ms | Maximum ms | Frames over 50 ms | Peak entities |
| --- | --- | --- | --- | --- | --- | --- |
| Normal AI battle | 16.667 | 16.762 | 16.874 | 17.097 | 0 | 13 |
| Repeated heavy bridge waves | 16.782 | 16.944 | 17.268 | 117.133 | 4 | 138 |
| Heavy waves with Meta requested | 16.777 | 16.951 | 17.246 | 66.451 | 7 | 146 |

| Scenario | Mean game thread ms | Mean render thread ms | Mean GPU ms | Peak process memory GiB |
| --- | --- | --- | --- | --- |
| Normal AI battle | 1.056 | 2.187 | 3.846 | 1.07 |
| Repeated heavy bridge waves | 3.794 | 2.643 | 4.061 | 1.47 |
| Heavy waves with Meta requested | 3.901 | 2.737 | 4.096 | 1.53 |

Meta advanced by zero matches throughout the requested active-battle run. The stress fixture creates repeated large waves through production Developer spawning and restarts completed matches; most recorded hitches coincide with those wave boundaries. Its brief hitches remain documented, with the largest at 117.133 ms. The earlier multi-second replay-finalization stall did not recur. Normal AI play recorded no frame over 50 ms. All three processes exited 0, and their independent Shipping diagnostics contained no Error/Fatal entries.

Raw reports and launch-time executable/report hashes are under `Artifacts/QA/Performance/release-{ai-match,stress,stress-meta}-1920x1080`. The release QA archive includes those selected reports and sanitized native diagnostics. Headless 10,000-match cohort throughput is reported separately.

## Historical 1.0.0 public release delivery

Version 1.0.0 was published as the repository's latest stable release at the time of its verification. GitHub's server SHA-256 digests matched all six immutable uploaded payloads. Fresh anonymous downloads retrieved the entire 447,292,600-byte game ZIP and 428,171,368-byte MSI; both matched their exact tested local hashes.

The released standalone launcher raised its actual `CheckButton.Click` against the unchanged default HTTPS latest-manifest endpoint. Its returned 1.0.0 manifest matched every one of the 51 game files and the version-pinned archive hash/size. Patch notes displayed, Install was enabled, and the isolated game/save fixtures remained unchanged. This public check fetched the manifest; its report does not claim an additional game installation.

Preserved sanitized reports are [historical-public-download-verification-1.0.0.json](QA/historical-public-download-verification-1.0.0.json) and [historical-published-update-check-1.0.0.json](QA/historical-published-update-check-1.0.0.json). Both accompanied the 1.0.0 public release separately from its pre-publication QA ZIP, with their hashes included in that release's final `SHA256SUMS.txt`.

## Confirmed fixes during release QA

- A Meteor zone created at an AI tick endpoint previously credited exposure before its birth. Revision 3 intersects each tick with the actual lifetime. Damage, DOT ticks, AI decisions and outcomes remain identical; native datasets have distinct fingerprints.
- Generated Unreal Physics Assets needed explicit package saving and a physical-file registry scan. Fresh imports now persist their external dependencies and cook successfully.
- The initial viewport had incorrect orthographic aspect handling, black exposure and a stale UMG root. The actual renderer exposed these issues; camera, material usage, exposure and retained widget ownership were corrected.
- World shutdown originally called into unordered GameInstance subsystem teardown. World EndPlay owns replay completion while dependencies are still alive.
- Whole-history replay validation/serialization blocked the game thread. Finalization now uses an immutable background job, with readiness reflected by the UI and explicit shutdown flushing.
- Replay event application contained a dangling `else` after hazard ticks. Phase, AI and terminal events now have independent branches and are checked between snapshots.
- Unreal's original stage omitted the required Visual C++ runtime. Release staging now supplies the actual compiler's matching runtime and verifies loaded packaged module paths during installer QA.
- UI one-shot audio shares the 32-voice limit with effects. The configured 64-channel device budget reserves capacity for persistent music and river ambience; post-mix loop survival is checked separately.
- Floating-point subtraction could leave a paid Aether bank slightly below zero. The existing affordability tolerance remains, and the payment result is normalized to the intended zero lower bound. This can change later AI threshold decisions; the final cohort is audited separately and is not reported as historically identical.
- Unreal's viewport DPI calculation canceled a global Slate scale setting. UI scale now applies through the actual game viewport interface settings and passes callback, persistence and four-resolution DPI checks.
- Meta tables now use actual aligned cells, display both confidence bounds, and retain precise exports, sorting and filtering. Dark controls and a minimum card width keep names readable.
- A bounded two-dimensional health-bar layout keeps tower labels clear and preserves each unit's health/status information during dense bridge combat. The camera and placement captures verify the full legal field above the hand.
- The first generated Niagara systems contained emitter handles without executable system-graph connections, producing zero particles despite valid assets. Generation now follows the engine's editor graph utility, fully loads existing packages before saving, reports save failures and tunes the templates' actual parameter names.

## Evidence and interpretation

`NativeSimulationQA.md`, `NativeMetaQA.md`, `NativePersistenceReplay.md`, `LAUNCHER_QA.md`, `WINDOWS_RELEASE.md` and `Assets/PRESENTATION_ACCEPTANCE.md` contain the focused contracts and measured evidence. Raw local execution output is retained under ignored `Artifacts/QA` and `Build/Automation`; final reports accompany the published release.

The earlier 10,000-match revision 2 run is retained as historical evidence and has the documented exposure limitation. Telemetry revision 3 identifies exposure accounting; the additional 1.2.1 cast-delay fields and rules fingerprint identify the changed spell timing. Headless simulation timing is not rendered FPS. Asset counts are not a visual-quality verdict. Automated audio component/waveform checks do not substitute for a listening assessment. Installer verification must use the actual final payload; a provisional successful lifecycle does not certify a later changed installer.

All player-facing native and installer QA uses separate save paths. Current wrappers also isolate Unreal engine configuration with `-UserDir`. Private browser saves are preserved and excluded from Git and release assets.
