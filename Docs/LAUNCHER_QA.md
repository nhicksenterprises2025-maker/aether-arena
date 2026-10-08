# Launcher verification record

The actual .NET 8 projects restored, compiled and published successfully on this machine. The published self-contained `RiftCrownLauncher.exe` completed its noninteractive WPF smoke check. The isolated update integration suite passed **31 of 31 checks** with zero failures on 2026-10-08. A separate end-to-end test invoked the real published launcher's Play command against the actual packaged Shipping game; both bootstrap and native child exited successfully. These results describe the measured launcher flows below; they do not claim a 10,000-match native validation result.

## Measured evidence

| Evidence | Result | Generated artifact |
| --- | --- | --- |
| UpdateCore and WPF compilation | Passed; warnings are treated as errors in these projects | `Artifacts/Launcher/RiftCrownLauncher.exe` |
| Real loopback HTTP/filesystem integration | 31 passed, zero failures | `Artifacts/QA/launcher-core-tests.json` |
| Published executable startup/resources | Passed | `Artifacts/QA/launcher-published-smoke.json` |
| Connected WPF commands/resources | 10 controls checked; real settings, invalid-save and cancel routed commands exercised | Same published smoke report |
| Rendered WPF content | Real retained-mode WPF rendering captured | `Artifacts/QA/launcher-preview.png` |
| Published WPF Play → packaged game | Actual `PlayButton.Click`, all 52 packaged files verified, bootstrap and native Shipping process exited 0 | `Artifacts/QA/launcher-play-smoke.json` |
| Real game launched through WPF | 1280×720 Home render, D3D12 Shipping diagnostics, environment save-root forwarding, isolated engine configuration and byte-identical legacy save fixture | `Artifacts/QA/launcher-play-home.png` and the Play report |

The fixture uses temporary directories and a private loopback HTTP listener. The published smoke check receives fresh GUID-named install and save directories. Neither suite reads or modifies the real player profile.

## Behaviors exercised

The integration tests use actual generated ZIP archives, streamed HTTP responses and on-disk SHA-256 verification. They cover a valid install and complete inventory, byte-identical save preservation, same-version idempotency, downgrade rejection, missing/corrupt file repair, archive size and hash failures, file-inventory hash failure, manifest schema/platform/minimum-launcher errors, malformed JSON and HTTP failure.

Hostile-path checks cover traversal, absolute paths, Windows device names, alternate data streams, case duplicates, file/directory conflicts and linked ZIP entries. Transaction checks cover cancellation, the running-game guard, exclusive locks, intersecting install/save roots, interruption before and after activation, corrupt candidate rollback, a corrupt installed pointer with prior-pointer recovery, and first-install corrupt-pointer preservation followed by a verified reinstall.

The WPF smoke check loads the actual application's compiled resources, constructs its command controls, raises routed settings/save/cancel actions and renders a PNG. The separate Play smoke uses the published executable and a verified real package. It raises the application's actual Play routed event, observes the actual UE bootstrap and Shipping child, checks their exit codes, decodes the native capture, and validates the child's structured diagnostics. The game receives `RIFT_SAVE_ROOT` through the launcher's normal environment handling; no command-line save-root override masks that behavior. These QA flags require explicit isolated installation and save roots.

The Play evidence was measured against the first provisional Shipping package. That package's Home render predates the final menu polish. The installer lifecycle test repeats Play using the actual installed launcher after the final package is rebuilt.

## Re-run

```powershell
./Build/Build-Launcher.ps1
./Build/Test-LauncherPlay.ps1
```

The build script fails if compilation, any integration assertion or the published executable's smoke check fails. The Play script requires `Artifacts/Distribution` to contain the actual packaged game, and fails on launch, render, save isolation or clean-exit errors. Reports are regenerated from measured execution. Installer verification is recorded separately after Unreal packaging.
