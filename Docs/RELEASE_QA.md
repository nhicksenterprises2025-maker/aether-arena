# Native Windows release verification

Verification date: 2026-10-08. The development host is Windows 11 with a Ryzen 7 5700, 32 GB RAM and an NVIDIA GeForce RTX 5060. Engine: Unreal 5.8.2. Results below distinguish executed checks from remaining release verification.

| Area | Measured result |
| --- | --- |
| Native deterministic core | 36 regression scenarios and 21 complete seeded AI matches passed; unchanged card balance and rules |
| Unreal integration | Nine tests passed, including connected UI callbacks, complete replay histories, profile recovery, card assets and Meta workers |
| Full-length replay | Peak 80-entity history retained all 23,922 events and 1,256 samples; 88,247,587 JSON bytes in an 8,572,007-byte lossless archive; game-thread save queue 3.735 ms |
| Native Meta telemetry revision 3 | 10,000 actual completed matches, zero invalid matches and zero economy-invalid matches |
| Independent Meta audit | 564,223 checks passed across 82 buckets and 399 checkpoints; all final exported metrics checked |
| Gameplay equivalence after exposure correction | 65,877 retained fields compared without a mismatch; only Meteor exposure/occupancy changed |
| Launcher/update core | 31 integration checks passed, including actual ZIP/inventory verification, rollback, repair, cancellation, locks and save preservation |
| Published WPF launcher | 10 connected controls/resources checked through the actual self-contained application |
| Asset source/export structure | 1,123 checks passed; native scale, skeletons, LODs, physics, illustrations and bindings verified separately in Unreal |
| Original audio masters | All 41 PCM files are non-silent with no clipped samples; both loop endpoints match exactly |
| Windows Shipping packaging | Actual build/cook/stage/archive completed successfully; editor installation is not used by the packaged executable |
| App-local Visual C++ runtime | Matching 14.51 runtime copied and hashed; 362 imported symbols across 17 packaged PE files checked against actual exports |

The fresh final-core Meta cohort, final renderer/AudioDevice review, final Shipping frame measurements and final installer maintenance rerun are still being completed. This document does not certify those pending results.

## Confirmed fixes during release QA

- A Meteor zone created at an AI tick endpoint previously credited exposure before its birth. Revision 3 intersects each tick with the actual lifetime. Damage, DOT ticks, AI decisions and outcomes remain identical; native datasets have distinct fingerprints.
- Generated Unreal Physics Assets needed explicit package saving and a physical-file registry scan. Fresh imports now persist their external dependencies and cook successfully.
- The initial viewport had incorrect orthographic aspect handling, black exposure and a stale UMG root. The actual renderer exposed these issues; camera, material usage, exposure and retained widget ownership were corrected.
- World shutdown originally called into unordered GameInstance subsystem teardown. World EndPlay owns replay completion while dependencies are still alive.
- Whole-history replay validation/serialization blocked the game thread. Finalization now uses an immutable background job, with readiness reflected by the UI and explicit shutdown flushing.
- Replay event application contained a dangling `else` after hazard ticks. Phase, AI and terminal events now have independent branches and are checked between snapshots.
- Unreal's original stage omitted the required Visual C++ runtime. Release staging now supplies the actual compiler's matching runtime and verifies loaded packaged module paths during installer QA.
- UI one-shot audio shares the 32-voice limit with effects. The configured 64-channel device budget reserves capacity for persistent music and river ambience; post-mix loop survival is checked separately.

## Evidence and interpretation

`NativeSimulationQA.md`, `NativeMetaQA.md`, `NativePersistenceReplay.md`, `LAUNCHER_QA.md`, `WINDOWS_RELEASE.md` and `Assets/PRESENTATION_ACCEPTANCE.md` contain the focused contracts and measured evidence. Raw local execution output is retained under ignored `Artifacts/QA` and `Build/Automation`; final reports accompany the published release.

The earlier 10,000-match revision 2 run is retained as historical evidence and has the documented exposure limitation. Revision 3 is the final rules/telemetry identity. Headless simulation timing is not rendered FPS. Asset counts are not a visual-quality verdict. Automated audio component/waveform checks do not substitute for a listening assessment. Installer verification must use the actual final payload; a provisional successful lifecycle does not certify a later changed installer.

All player-facing native and installer QA uses separate save paths. Current wrappers also isolate Unreal engine configuration with `-UserDir`. Private browser saves are preserved and excluded from Git and release assets.
