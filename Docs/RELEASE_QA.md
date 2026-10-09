# Native Windows release verification

Verification date: 2026-10-08. The development host is Windows 11 with a Ryzen 7 5700, 32 GB RAM and an NVIDIA GeForce RTX 5060. Engine: Unreal 5.8.2. Results below identify executed checks and their measured limits.

## Version 1.1.0 verification

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
| Published release | [Version 1.1.0](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.1.0) is the latest stable release, published 2026-10-09 at 03:50:51 UTC with draft disabled; initial seven uploaded asset digests matched local bytes |
| Public downloads | Fresh complete anonymous HTTPS downloads of ZIP 448,655,642 bytes / SHA-256 `fc32c6add036ee0c4baaf2656bfb599642fed05028231e17da197db92555c5f2` and MSI 429,428,840 bytes / SHA-256 `ed7244eb6909f8a9529eb1d5da0cb1fc1fe2a37f7175fe9cf74e5446cf663491` matched the tested payloads |
| Published launcher update | Actual released WPF `CheckButton.Click` fetched latest HTTPS manifest 1.1.0 with all 51 files and exact archive identity; patch notes displayed, Install enabled and existing game/save fixtures preserved. This check fetched the manifest without downloading or installing the game |

The final native suite is `Build/Automation/20261008-232104/Report/index.json`, with completed launch context beside it. It includes the exact fractional replay endpoint, a real 240-second triple-Aether transition while overtime stays visible, and win/loss results that survive replay-saving/HUD rebuilding without restarting or recording twice. Final actual playback evidence is `Artifacts/QA/Audio/polish15-{editor,shipping}-audio-final/{audio-smoke,run}.json`. Both runs captured live post-effect normal and overload PCM; waveform/headroom assertions do not establish subjective listening quality. Current source behavior is reviewed in [UI_POLISH_1.1.0.md](UI_POLISH_1.1.0.md), with audio authoring/routing documented in [AUDIO_DESIGN.md](AUDIO_DESIGN.md). Renderer inspection, installer lifecycle and public delivery have their own completed execution evidence below.

The complete final renderer review is `Docs/QA/presentation-review.json`: 29 actual inspected PNGs with each image, guarded successful launch metadata and captured state pinned to its reviewed bytes. It covers all 12 public game pages, battle at 16:9, 16:10 and 21:9, and enlarged UI scale. Battle/replay captures independently report all four legal-field corners within their HUD-safe bounds; requested phase fixtures passed. The QA evidence packager requires the exact final native Shipping executable hashes for selected renderer, audio, VFX and performance reports, then binds the tested installer to the verified release payload.

Final local executable and installer acceptance is recorded in `Artifacts/QA/launcher-play-smoke.json`, `Artifacts/QA/installer-tests.json` and `Artifacts/Release/windows-release-verification.json`, all bound to the exact final Build15 payload. The installer report starts from public 1.0.0 to execute a real upgrade to `installedVersion` 1.1.0; the launcher retains its independent 1.0.0 assembly version. Final Shipping particle evidence is `Artifacts/QA/polish15-shipping-vfx-final-verification.json`. Separate completed public delivery evidence is linked below.

## Version 1.1.0 Shipping performance

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

Raw reports and launch-time executable/report hashes are under `Artifacts/QA/Performance/polish15-{ai-match,stress,stress-meta}-1920x1080/{performance,run}.json`. They are current 1.1.0 measurements; the separate 1.0.0 table below remains historical. Headless 10,000-match cohort throughput is separate from rendered frame timing.

## Version 1.1.0 public release delivery

[Version 1.1.0](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/tag/v1.1.0), release ID 407483225, was published as the latest stable release at 2026-10-09T03:50:51Z with draft disabled. GitHub's server SHA-256 digests matched the six initial release payloads and their checksum asset. Fresh anonymous HTTPS downloads retrieved the entire 448,655,642-byte game ZIP and 429,428,840-byte MSI; both matched the exact locally tested sizes and hashes.

The actual released standalone WPF launcher raised `CheckButton.Click` against the unchanged default HTTPS latest-manifest endpoint. Its returned 1.1.0 manifest matched all 51 game files and the version-pinned archive hash/size. Patch notes displayed, Install was enabled, and existing game/save fixtures remained unchanged. The compatible launcher retains version 1.0.0; this check fetched the manifest and did not download or install the game.

Sanitized observations are [public-download-verification.json](QA/public-download-verification.json) and [published-update-check.json](QA/published-update-check.json). These post-publication checks are separate from the pre-publication QA ZIP. The preserved 1.0.0 public reports use the historical filenames linked below.

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

The earlier 10,000-match revision 2 run is retained as historical evidence and has the documented exposure limitation. Revision 3 is the final rules/telemetry identity. Headless simulation timing is not rendered FPS. Asset counts are not a visual-quality verdict. Automated audio component/waveform checks do not substitute for a listening assessment. Installer verification must use the actual final payload; a provisional successful lifecycle does not certify a later changed installer.

All player-facing native and installer QA uses separate save paths. Current wrappers also isolate Unreal engine configuration with `-UserDir`. Private browser saves are preserved and excluded from Git and release assets.
