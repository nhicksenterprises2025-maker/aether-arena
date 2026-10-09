# Windows launcher and release operations

The Windows distribution contains the native Unreal game and a self-contained .NET 8 WPF launcher. The launcher does not require an end-user .NET installation. Its controls are connected to the same verified update library used by the isolated integration suite.

## Player controls

| Control | Actual behavior |
| --- | --- |
| Play | Launches the verified installed native executable, passes launch arguments, and supplies `RIFT_SAVE_ROOT` to the game. |
| Check for updates | Downloads and validates the configured HTTPS release manifest, then displays its version and patch notes. |
| Update / Install | Downloads the full release archive, verifies its SHA-256 and every inventory entry, then activates a complete new release directory. |
| Repair | Rechecks the installed file inventory and stages a verified replacement if files are missing or damaged. |
| Settings | Saves the HTTPS manifest endpoint, install location, launch arguments and startup update-check preference. |
| Open saves / Open logs | Opens the corresponding folders through Windows Explorer. |
| Cancel | Cancels an active transfer before activation and leaves the prior installation usable. |

The default update endpoint is:

`https://github.com/nhicksenterprises2025-maker/aether-arena/releases/latest/download/update-manifest.json`

A missing release endpoint produces a visible recoverable error. Publishing source alone does not create an installable GitHub release; the versioned archive and manifest must both be attached to the release.

## Installation and saved data

The MSI defaults to `%LOCALAPPDATA%\Programs\RiftCrownArena`. Start Menu registration is included; a desktop shortcut is optional in the feature selector. The installation is per user and does not request administrator privileges. The launcher derives this root from its installed `Launcher` directory.

```text
RiftCrownArena/
  Launcher/RiftCrownLauncher.exe
  Game/installed.json
  Game/installed.previous.json           created by later updates
  Game/releases/<version>-<archive-hash>/
    RiftCrownArena.exe
    RiftCrownArena/Binaries/Win64/...
    Engine/...
```

Native profiles, loadouts, settings, replays, Meta datasets, launcher configuration and logs live separately under `%LOCALAPPDATA%\RiftCrownArena`. The launcher refuses intersecting game and save roots. Game archives never extract into the save root. The original browser `player_save.json` remains preserved by native migration.

Uninstall removes the registered launcher, shortcuts and the managed `Game` subtree, including updater-created release directories, interrupted staging and installation metadata. The installer remembers that specific managed cache path and the original installation directory in its per-user registration, restoring the latter before repair/uninstall directory costing. It does not recursively remove the general installation parent or save/log folder. This follows WiX's documented [remembered-path cleanup](https://docs.firegiant.com/wix/schema/util/removefolderex/) mechanism.

The release pipeline stages the compiler's matching x64 release CRT DLLs beside the bootstrap and native game executables. `Stage-AppLocalRuntime.ps1` reads the compiler path from the actual Shipping build log (or an explicit `-ToolchainRoot`), checks every packaged PE's imported CRT symbols against those DLLs' exports, and verifies copied bytes with SHA-256. The initial Unreal stage omitted these DLLs, so the release pipeline performs this step explicitly. Microsoft supports [application-local runtime deployment](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files) and requires a runtime compatible with the [actual build tools](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist). Windows supplies the Universal CRT. The launcher carries its own .NET runtime. Keep the complete packaged Unreal tree together; copying only the game executable omits required assets and runtime files.

## Reproducible build order

Run these scripts from PowerShell 7 with the repository as the working directory:

```powershell
./Build/Build-Launcher.ps1
# Run the repository's Unreal Shipping build/cook/stage procedure first.
./Build/Package-WindowsRelease.ps1 -Version 1.1.0 -GamePackage ./Artifacts/Game/Windows
./Build/Test-LauncherPlay.ps1
./Installer/Build-Installer.ps1 -Version 1.1.0
./Installer/Test-Installer.ps1 -Msi $PreviousSameFamilyMsi -UpgradeMsi ./Artifacts/Installer/RiftCrownArena-Setup.msi
./Build/Finalize-WindowsRelease.ps1 -Version 1.1.0
```

`Build-Launcher.ps1` publishes the real self-contained launcher, runs the isolated update integration suite and launches the published executable's noninteractive WPF smoke check. The smoke check requires explicit isolated install and save paths and renders an actual WPF preview.

For the release lifecycle check, set `$PreviousSameFamilyMsi` to a retained actual previously compiled MSI with the same UpgradeCode and a distinct ProductCode. The current release's measured baseline is `Artifacts/QA/ReleaseCandidates/Verified-20261008-211709/RiftCrownArena-Setup.msi`. A fresh-install-only test can omit these arguments, but the finalizer requires the complete upgrade-inclusive lifecycle evidence.

`Package-WindowsRelease.ps1` refuses to manufacture a release without a real staged `RiftCrownArena.exe`. It preserves any `.pdb` debug symbols separately under `Artifacts/Symbols/<version>/<batch>/`, verifies their unchanged hashes, and keeps all game executables, DLLs and runtime assets in the player package. It then invokes app-local runtime staging before recording every file's size and SHA-256, verifies the actual ZIP entries and copied installation tree against those records, and constructs a fresh `Artifacts/Distribution` with the initial installation pointer. A prior distribution is retained beside it, preventing stale release files from accumulating in the next installer. Packaging does not contact GitHub. Supply `-BuildLog` when the current Shipping log is at another path, or `-ToolchainRoot` when the build's compiler path is known explicitly.

`Build-Installer.ps1` refuses to compile without the actual distribution launcher and `Game/installed.json`. It generates component definitions from real distribution files and compiles a WiX 4 MSI with standard database validation enabled. Per-user components use stable GUIDs and HKCU key paths, following [ICE38](https://learn.microsoft.com/en-us/windows/win32/msi/ice38), and newly created directories have explicit empty-folder removal entries for [ICE64](https://learn.microsoft.com/en-us/windows/win32/msi/ice64). Validation treats warnings as errors with documented exceptions for fixed per-user scope (ICE91), intentional same-version major upgrades (ICE61), and a neutral language declaration for the private LastResort font resource (WiX1101). The private font is not registered as a system font. WiX is restored as project-local NuGet build dependencies; no globally installed compiler is required. An unsigned installer remains unsigned unless a publisher signing identity is explicitly supplied.

`Test-LauncherPlay.ps1` points the freshly published launcher at the actual bundled distribution and supplies a fresh GUID save root. It invokes the visible application's real Play routed event, waits for the packaged bootstrap and native Shipping process, validates an actual 1280×720 Home capture and structured native diagnostics, and proves the launcher's normal `RIFT_SAVE_ROOT` environment forwarding. The original legacy save fixture remains byte-identical. The game's engine configuration uses a separate isolated `-UserDir`.

`Test-Installer.ps1` runs the actual compiled MSI in a fresh workspace QA directory, verifies every bundled file hash, exercises both Start menu and optional desktop shortcuts, and executes the installed WPF launcher smoke check. An optional `-UpgradeMsi` accepts a distinct real product in the same upgrade family for a major-upgrade preservation test. The test then invokes the installed launcher's actual Play command and validates the resulting bootstrap/native process exits, manifest identity, renderer capture and environment save-root forwarding. A separate native launch exposes loaded CRT module paths for verification against packaged files and manifest hashes. The test repairs a deliberately missing registered game executable and tests uninstall cleanup with separate save fixtures. It first refuses existing related MSI products, a matching Start menu folder or an existing matching desktop shortcut, preventing the test from replacing an existing installation. Its measured report is `Artifacts/QA/installer-tests.json`; script existence alone does not establish a passed installer result.

`Finalize-WindowsRelease.ps1` checks that the measured published Play, installer build and complete installed lifecycle reports identify the exact current archive, native executable, launcher and MSI hashes. It requires all 31 update checks, ten published WPF controls and at least 51 installed lifecycle assertions. It then copies the compiled MSI and standalone self-contained launcher into `Artifacts/Release`, writes one `SHA256SUMS.txt` covering the ZIP, MSI, launcher and manifest, and records `windows-release-verification.json`. Stale provisional QA prevents finalization. This script does not publish files or establish separate native gameplay/performance validation results.

## Manifest and transactional activation

The release manifest has `schemaVersion: 1`, `platform: "windows-x64"`, numeric `version`, HTTPS `downloadUrl`, archive `sha256` and `size`, relative `executable`, `minimumLauncherVersion`, `patchNotes` and a complete `files` array of `{ path, size, sha256 }` entries. The initial executable is `RiftCrownArena.exe`.

`Game/installed.json` has `schemaVersion: 1`, `version`, `releasePath` relative to `Game`, `executable`, and the complete validated `manifest`. This pointer is written through a flushed same-directory temporary file and atomic replacement.

Update and repair hold an exclusive installation lock. Downloads, extraction and verification occur in a fresh staging directory. ZIP traversal, links, reserved Windows names, alternate data streams, duplicate case-insensitive paths, unexpected files and missing inventory files are rejected. Only a fully verified directory can become the active pointer. The prior release remains on disk. Updates are refused while an installed game process is running.

An activation journal permits recovery after an interrupted rename or pointer commit. An invalid candidate rolls back to the prior valid pointer. A malformed pointer is retained for diagnosis, and recovery or a verified reinstall does not overwrite saved data. Hash verification detects package/file corruption; the manifest's HTTPS publisher endpoint supplies the release source of trust.

## GitHub release assets

Publish the actual files generated in `Artifacts/Release`, plus the compiled installer and measured QA evidence:

- `RiftCrownArena-Windows-x64-<version>.zip`
- `update-manifest.json`
- `SHA256SUMS.txt`
- The compiled `RiftCrownArena-Setup.msi`
- The self-contained standalone `RiftCrownLauncher.exe`
- Relevant native, launcher and installer QA reports
- `windows-release-verification.json`, which identifies the actual Windows asset hashes and matched lifecycle evidence

The archive's manifest URL is version-pinned to `releases/download/v<version>/...`; the launcher discovers it through the latest release's manifest. Attach the archive named in that manifest to the same version tag. Never upload private player saves, signing credentials, local dependency caches or development build intermediates.

After the final release is public and selected as GitHub's latest release, run `./Build/Test-PublishedUpdate.ps1`. It invokes the actual released launcher's Check for Updates routed event against the default HTTPS endpoint, matches the complete final local manifest, verifies displayed patch notes and Install availability, and confirms unchanged isolated game/save data. It does not download or install the game. Record its actual result from `Artifacts/QA/published-update-check.json` separately from pre-publication build checks.

The MSI installs the launcher and bundled game together. The standalone launcher can install the release through its update endpoint; its default managed `Game` folder sits beside that executable, and Settings can select another location. The game ZIP can also be extracted and started directly with `RiftCrownArena.exe`. Keep its complete directory tree together.
