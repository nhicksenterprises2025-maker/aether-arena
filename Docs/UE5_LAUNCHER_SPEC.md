# Rift Crown Arena Windows launcher and release specification

Inspected reference: `rift_crown_arena_v15`, the supplied Unreal rebuild request, and the installed Windows tooling. This document records the implementation contract; it is not a claim that an Unreal Shipping build, launcher, or installer has already been produced.

## Verified toolchain

| Component | Observed on this machine | Consequence |
| --- | --- | --- |
| Unreal Engine | `C:/Program Files/Epic Games/UE_5.8`, Build.version 5.8.2, promoted `++UE5+Release-5.8` | Use this release installation; no preview engine needed. |
| .NET SDK | 8.0.424 | WPF `net8.0-windows` is buildable with the installed SDK. |
| Windows Desktop runtime | 8.0.18 and 8.0.30 | Available for development; the delivered launcher must carry its own runtime. |
| Windows SDK | 10.0.26100.0 x64; MakeAppx, MakeCat and SignTool present | Packaging/signing tools exist; no signing identity has been supplied. |
| Unreal prerequisites | `Engine/Extras/Redist/en-us/vc_redist.x64.exe`, GameInputRedist.msi | Prerequisite payloads can be incorporated if the packaged game requires them. |
| Inno/WiX | No `iscc`, `wix`, `candle` or `light` command found; no standard Inno/WiX install directory found | An installer compiler still needs to be selected and restored locally. Do not call an uncompiled script an installer. |
| Git | Available | Publish source through the user-authorized `aether-arena` repository. |

Recommended launcher publish command: `dotnet publish -c Release -r win-x64 --self-contained true`. Set `UseWPF=true`, `TargetFramework=net8.0-windows`, `PublishSingleFile=true`, `IncludeNativeLibrariesForSelfExtract=true`, and `PublishTrimmed=false`. WPF requires preservation of reflection/XAML-loaded types. Embed the original Rift branding/resources instead of relying on browser assets at runtime. Self-contained distribution removes a separate end-user .NET installation requirement. Single-file deployment and native-library extraction are documented by [Microsoft](https://learn.microsoft.com/en-us/dotnet/core/deploying/single-file/overview).

## Source and build layout

Keep the browser specification alongside the new native source. Suggested repository layout:

```text
Reference/BrowserV15/            preserved current project and tests
Unreal/RiftCrownArena/           uproject, Config, Source, Content, source assets
Launcher/RiftCrown.Launcher/     WPF presentation, launch/settings commands
Launcher/RiftCrown.UpdateCore/   manifests, downloads, hashes, transactions, logs
Launcher/RiftCrown.Tests/        isolated filesystem and HTTP integration checks
Installer/                     compiler source and packaging script
Build/                         pinned build/release orchestration
Docs/                          migration, release, technical and QA reports
```

Generated Unreal Intermediate/Binaries/Saved/DerivedDataCache and .NET bin/obj are excluded from Git. Packaged release outputs belong in GitHub Releases rather than ordinary Git commits. Do not upload personal saves, machine credentials, caches, verification saves, or crash dumps containing user data. Art source and Unreal Content need repository-appropriate binary handling; establish this before uploading large assets.

## Install and save locations

Default per-user installation: `%LOCALAPPDATA%/Programs/RiftCrownArena`. A configurable install root must be separate from `%LOCALAPPDATA%/RiftCrownArena`.

```text
Programs/RiftCrownArena/
  Launcher/RiftCrownLauncher.exe
  Game/releases/<version>-<package-hash-prefix>/...
  Game/staging/<transaction-id>/...
  Game/installed.json
  Game/update-journal.json
RiftCrownArena/
  player_save.json              original browser profile/decks; preserved
  lab_v15.json                  original browser meta/replays; preserved
  ue_save.json                  native versioned profile/settings/references
  Meta/                        native datasets, separate by model fingerprint
  Replays/                     native event recordings and imported archives
  Backups/                     save migration backups
  Launcher/settings.json
  Logs/Launcher/
  Logs/Updater/
  Logs/Game/
```

Every write/update/uninstall operation resolves an absolute path and verifies it remains under the intended installation subtree. Saves are never a package extraction destination and never a cleanup target. The game receives or derives the same save root; Unreal's default project Saved location must not accidentally become a second profile authority.

## Actual browser save compatibility

`server.js` and `local_server.ps1` persist `player_save.json` under `%LOCALAPPDATA%/RiftCrownArena`, with an atomic temporary-file rename. `/api/save` accepts at most 262,144 UTF-8 bytes. The save has no top-level schema number:

```json
{
  "profile": {
    "username": "RIFTBOUND", "playerId": "RC-EXAMPLE",
    "wins": 0, "losses": 0, "draws": 0, "matches": 0,
    "crowns": 0, "gems": 1250, "gold": 8420
  },
  "deck": ["ironclad", "ember_archer", "archer_tower", "boulderback", "arc_mage", "rambeast", "sky_manta", "nova_flask"],
  "deckPresets": {
    "activePresetId": "deck-1",
    "presets": [
      {"id": "deck-1", "name": "Deck 1", "cards": ["ironclad", "ember_archer", "archer_tower", "boulderback", "arc_mage", "rambeast", "sky_manta", "nova_flask"]}
    ]
  }
}
```

The sample abbreviates presets: the actual normalized store has **five** IDs `deck-1` through `deck-5`. Every deck has exactly eight distinct IDs drawn from the existing fourteen-card roster. Preset names are sanitized and capped at 24 Unicode characters. Profile names are capped at 18; editable names permit letters, digits, space, underscore and hyphen. Profile numeric values normalize to nonnegative whole numbers; preserve supplied valid values and existing `playerId`. Current browser training results also pass through its profile match accounting; changing that behavior would need an explicit gameplay decision.

V13 saves can have only `profile` and `deck`. Migrate a valid old deck into Deck 1 and create the other four default presets. V14/V15 `deckPresets` takes precedence when valid. Invalid records recover individually, retaining other valid presets. Do not discard the profile because one deck is malformed. New native profiles use a generated unique local identifier; imported identifiers remain stable.

The separate `/api/lab` save accepts up to 16,777,216 bytes. `lab_v15.json` has top-level `schema:1`, `savedAt`, `meta` with `schema:2`, and an array of replays. The Meta store contains `activeId` and datasets with original card library, rules, styles, version, model, fingerprint, aggregates and checkpoints. Browser replays have `formatVersion:1`, metadata, result, ordered `{time,seq,type,data}` events and sampled states. Preserve original archives as browser-model evidence. Native Unreal simulation results must get their own model identity even when card values match; never relabel V15 aggregates as native results or invent missing historical datasets.

Migration must run once into a new schema-versioned `ue_save.json`, record import provenance and source checksum, and keep the original files untouched. Parse and normalize before committing. Write a same-directory temporary file, flush, then atomically replace the native file while retaining the last valid backup. On corrupt/future-schema input, report the issue and preserve it; prefer the last valid native backup before creating defaults. Migrations are ordered, repeat-safe transformations with fixtures, not broad exceptions that erase the profile. Large lab import runs outside live gameplay and must not block the game thread.

## Launcher behavior

The UI uses a custom Rift mark, restrained stone/dark-metal framing, legible type, team blue accents, and one dominant Play action. Avoid generic dashboard cards, excessive gradients, emoji branding and decoration that obscures status. Support keyboard focus, ordinary Windows scale factors and resizing. Display actual states; no disconnected controls.

| Control | Required behavior |
| --- | --- |
| PLAY | Resolve the committed installed version and validated packaged executable. Launch the Unreal Shipping game with its working directory, without Unreal Editor or an HTTP server. Show running state, avoid duplicate launches, and retain actionable startup error details. |
| VERSION | Show installed game version and launcher version independently. Never derive a native version from an old browser label. |
| CHECK FOR UPDATES | Fetch configured manifest with bounded timeout/cancellation; validate schema; compare parsed release versions; show available/current/offline/error accurately. |
| UPDATE | Enabled only for a validated downloadable package. Download with bytes/total and phase progress, verify size/hash, stage, verify contents, commit transaction, then show the installed version. |
| REPAIR FILES | Validate file-level size/hash manifest for the installed release. Fetch that release's verified package when damaged files exist, stage a clean copy and commit it. Do not merely rehash the archive or reset saves. |
| SETTINGS | Change endpoint, optional startup update check, supported install root, and launch options. Validate/save before applying. Endpoint change requires a new check before Update is enabled. |
| OPEN SAVE FOLDER | Open the actual shared `%LOCALAPPDATA%/RiftCrownArena` folder. |
| OPEN LOGS | Open the actual log root; create it if absent. |
| PATCH NOTES | Show plain text supplied by the validated manifest or bundled installed release, with source/version. Never execute remote HTML. |

A launcher instance mutex prevents simultaneous installs. File repair/update cannot mutate a running game version; stage safely and wait for game exit before commit. Cancellation before commit leaves the current version playable. Recoverable errors return the UI to a useful state.

## Manifest and GitHub publication

Default configurable endpoint for the explicitly named repository:
`https://github.com/nhicksenterprises2025-maker/aether-arena/releases/latest/download/update-manifest.json`.
GitHub documents this stable asset link format in its [release link documentation](https://docs.github.com/en/repositories/releasing-projects-on-github/linking-to-releases).

```json
{
  "schemaVersion": 1,
  "version": "1.0.0",
  "platform": "windows-x64",
  "downloadUrl": "https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.0.0/RiftCrownArena-Windows-x64.zip",
  "sha256": "GENERATED_FROM_THE_ACTUAL_RELEASE_ARCHIVE",
  "size": 0,
  "executable": "RiftCrownArena.exe",
  "patchNotes": "Release notes generated for this exact artifact.",
  "minimumLauncherVersion": "1.0.0",
  "files": [
    {"path": "RiftCrownArena.exe", "size": 0, "sha256": "GENERATED_FROM_THE_ACTUAL_FILE"}
  ]
}
```

This is a **template**, not a valid published manifest. Release generation replaces every illustrative hash, size and path with values computed from the actual packaged tree. File inventory includes Unreal Pak/IoStore, executable, DLLs, config and required release resources. Manifest validation rejects missing fields, malformed hashes, wrong platform, invalid versions, duplicate paths, traversal/absolute paths and unreasonable sizes. HTTPS is the default remote transport; isolated tests may use a loopback endpoint explicitly provided by the test harness. No GitHub token is bundled in the launcher. If hosting changes, settings allow another compatible endpoint.

Publish only after source checks and artifact smoke checks. Tag the exact source commit; upload game archive, self-contained launcher, installer, manifest, checksums, patch notes and QA report to the matching GitHub Release. Source publication is authorized by the user's repository instruction. Artifact publication must truthfully identify measured verification and any unresolved release gate; do not title an incomplete native build as final production.

## Transactional update and recovery

1. Load and validate manifest, reject incompatible launcher requirements, and retain the immutable release metadata for the transaction.
2. Download into a task-specific `.part` file with streaming progress and cancellation. Bound network retries; an interrupted download never becomes an installed package.
3. Verify exact byte count and SHA-256 before opening the archive. A mismatched hash stops installation and keeps the previous version.
4. Extract to a fresh staging directory on the same installation volume. Reject traversal, drive-qualified names, alternate streams, duplicate normalized paths and reparse-point/symlink escapes. Enforce declared file count and uncompressed-size limits.
5. Check every staged file's actual length/hash against the inventory and confirm the executable is present. Reject a package that is internally inconsistent despite a valid archive checksum.
6. Write an update journal identifying previous committed version and staged candidate. Flush metadata. Rename the verified staging directory to a unique release directory.
7. Atomically replace `installed.json` with the new release pointer; this is the only activation point. The current release remains intact until activation succeeds.
8. On startup, recover any journal: use the last committed pointer, discard only identified incomplete staging within the installation subtree, and preserve the previous verified release for rollback. Do not infer success from directory existence.
9. Clean up obsolete release directories only after successful commit, game exit, and a verified absolute-path boundary check. Never follow a directory link outside the installation tree.

A failed game launch can restore the prior committed pointer and expose error/log details. A launcher executable update must use an independent helper after the launcher exits, or a new installer; a running WPF executable must never try to overwrite itself. Keeping game updates and launcher minimum-version enforcement explicit avoids falsely reporting the launcher updated when only game files changed.

## Installer contract

Use a compiled Inno Setup installer or WiX Windows Installer/Burn package after the compiler is available. Prefer per-user install without elevation for normal launcher/game files, stable product/upgrade identity, branded setup pages, Start Menu shortcut, optional Desktop shortcut, repair/uninstall registration, and explicit prereq handling. Determine which Unreal prerequisites the Shipping package actually needs; do not install unrelated engine/editor components. Respect prerequisite restart/exit codes and avoid launching the game before a required restart.

Uninstall removes registered installed binaries, updater staging and shortcuts, and preserves the save/log root. If removal of user data is offered, it is a separate explicit user choice, unchecked by default. The installer must not place legacy saves in a tracked component that Windows Installer removes on uninstall. Test upgrade/uninstall with isolated save root and record hashes before/after.

## Logs and smoke-testability

Use structured timestamped local logs for launcher, updater and game. Log phase/version, actual paths, byte counts/hash result, process exit/start failures and transaction recovery. Never log credentials. Rotate bounded logs and surface a direct Open Logs action after failure. Game crash context should include release, match phase, card/entity IDs and subsystem without leaking private hidden-state information into normal UI. Isolate noncritical presentation errors from simulation progression.

Keep update core independent of WPF. Tests use a temporary filesystem, configurable save/install roots, deterministic fixture payloads and a loopback HTTP server; never touch real `%LOCALAPPDATA%/RiftCrownArena`. Test valid update, no update, downgrade rejection, malformed/unsupported manifest, timeout, cancellation, wrong size/hash, missing/corrupt file repair, archive traversal, locked executable, low disk space, failed extraction, each interrupted commit phase, restart recovery, duplicate launcher instance, and exact save preservation.

WPF startup exposes a noninteractive `--self-test` that validates embedded resources/config and exercises command bindings. A separate `--self-test-play` invokes the real Play routed command against a verified actual packaged game, observes the bootstrap and native Shipping process, and validates native capture/diagnostics with isolated environment saves. Both require explicit isolated install/save roots. Update core integration tests verify disk state, not button-label changes. Run the published self-contained executable with system .NET discovery disabled for a smoke check. Installer verification includes an actual install, installed WPF Play, upgrade, repair and uninstall in an isolated destination. Native game verification requires a packaged Shipping run without Editor, all retained gameplay regressions and the specifically requested bridge/status/zone soak scenarios. Common-resolution UI checks and frame-time measurements remain separate from source compilation.

## Release gates and honest evidence

A complete deliverable requires actual Unreal source/assets, successful Shipping packaging, playable native executable, published self-contained launcher, compiled installer, usable verified update manifest, documented migrations, and recorded QA. Browser V15's existing 167-test report is useful reference evidence but does not prove native correctness. Every source/card/mechanic regression must execute against the native implementation; no placeholder executable, disconnected control, generated empty DataAsset or untested installer script satisfies this contract.
