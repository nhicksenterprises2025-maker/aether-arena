# Native Windows release verification

Verification date: 2026-10-08. The development host is Windows 11 with a Ryzen 7 5700, 32 GB RAM and an NVIDIA GeForce RTX 5060. Engine: Unreal 5.8.2. Results below identify executed checks and their measured limits.

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

## Actual Shipping performance

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

## Public release delivery

Version 1.0.0 is published as the repository's latest stable release. GitHub's server SHA-256 digests matched all six immutable uploaded payloads. Fresh anonymous downloads retrieved the entire 447,292,600-byte game ZIP and 428,171,368-byte MSI; both matched their exact tested local hashes.

The released standalone launcher raised its actual `CheckButton.Click` against the unchanged default HTTPS latest-manifest endpoint. Its returned 1.0.0 manifest matched every one of the 51 game files and the version-pinned archive hash/size. Patch notes displayed, Install was enabled, and the isolated game/save fixtures remained unchanged. This public check fetched the manifest; its report does not claim an additional game installation.

Sanitized reports are `Docs/QA/public-download-verification.json` and `Docs/QA/published-update-check.json`. Both accompany the public release separately from the pre-publication QA ZIP, and their hashes are included in the final `SHA256SUMS.txt`.

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
