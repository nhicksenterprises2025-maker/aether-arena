# Launcher verification record

The actual .NET 8 projects restored, compiled and published successfully on this machine. The published self-contained `RiftCrownLauncher.exe` completed its noninteractive WPF smoke check. The isolated update integration suite passed **31 of 31 checks** with zero failures on 2026-10-08. These numbers describe launcher verification only; they do not claim native game, installer or a 10,000-match native validation result.

## Measured evidence

| Evidence | Result | Generated artifact |
| --- | --- | --- |
| UpdateCore and WPF compilation | Passed; warnings are treated as errors in these projects | `Artifacts/Launcher/RiftCrownLauncher.exe` |
| Real loopback HTTP/filesystem integration | 31 passed, zero failures | `Artifacts/QA/launcher-core-tests.json` |
| Published executable startup/resources | Passed | `Artifacts/QA/launcher-published-smoke.json` |
| Connected WPF commands/resources | 10 controls checked; real settings, invalid-save and cancel routed commands exercised | Same published smoke report |
| Rendered WPF content | Real retained-mode WPF rendering captured | `Artifacts/QA/launcher-preview.png` |

The fixture uses temporary directories and a private loopback HTTP listener. The published smoke check receives fresh GUID-named install and save directories. Neither suite reads or modifies the real player profile.

## Behaviors exercised

The integration tests use actual generated ZIP archives, streamed HTTP responses and on-disk SHA-256 verification. They cover a valid install and complete inventory, byte-identical save preservation, same-version idempotency, downgrade rejection, missing/corrupt file repair, archive size and hash failures, file-inventory hash failure, manifest schema/platform/minimum-launcher errors, malformed JSON and HTTP failure.

Hostile-path checks cover traversal, absolute paths, Windows device names, alternate data streams, case duplicates, file/directory conflicts and linked ZIP entries. Transaction checks cover cancellation, the running-game guard, exclusive locks, intersecting install/save roots, interruption before and after activation, corrupt candidate rollback, a corrupt installed pointer with prior-pointer recovery, and first-install corrupt-pointer preservation followed by a verified reinstall.

The WPF smoke check loads the actual application's compiled resources, constructs its command controls, raises routed settings/save/cancel actions and renders a PNG. It does not substitute a browser screenshot for the native interface.

## Re-run

```powershell
./Build/Build-Launcher.ps1
```

The script fails if compilation, any integration assertion or the published executable's smoke check fails. Its reports are regenerated from measured execution. The installer script requires the actual staged game and is verified separately after Unreal packaging.
