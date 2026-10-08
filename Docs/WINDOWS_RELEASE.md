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

Uninstall removes the registered launcher, shortcuts and the managed `Game` subtree, including updater-created release directories, interrupted staging and installation metadata. The installer remembers that specific managed cache path in its per-user registration; it does not recursively remove the general installation parent or save/log folder. This follows WiX's documented [remembered-path cleanup](https://docs.firegiant.com/wix/schema/util/removefolderex/) mechanism.

The packaged game uses Unreal's app-local prerequisite DLLs. The launcher carries its own .NET runtime. Keep the complete packaged Unreal tree together; copying only the game executable omits required assets and runtime files.

## Reproducible build order

Run these scripts from PowerShell 7 with the repository as the working directory:

```powershell
./Build/Build-Launcher.ps1
# Run the repository's Unreal Shipping build/cook/stage procedure first.
./Build/Package-WindowsRelease.ps1 -Version 1.0.0 -GamePackage ./Artifacts/Game/Windows
./Installer/Build-Installer.ps1 -Version 1.0.0
./Installer/Test-Installer.ps1
```

`Build-Launcher.ps1` publishes the real self-contained launcher, runs the isolated update integration suite and launches the published executable's noninteractive WPF smoke check. The smoke check requires explicit isolated install and save paths and renders an actual WPF preview.

`Package-WindowsRelease.ps1` refuses to manufacture a release without a real staged `RiftCrownArena.exe`. It records every file's size and SHA-256, creates the versioned archive and release manifest, and constructs `Artifacts/Distribution` with the initial installation pointer. Packaging does not contact GitHub.

`Build-Installer.ps1` refuses to compile without the actual distribution launcher and `Game/installed.json`. It generates component definitions from real distribution files and compiles a WiX 4 MSI. WiX is restored as project-local NuGet build dependencies; no globally installed compiler is required. An unsigned installer remains unsigned unless a publisher signing identity is explicitly supplied.

`Test-Installer.ps1` runs the actual compiled MSI in a fresh workspace QA directory, verifies every bundled file hash, executes the installed WPF launcher smoke check, and tests uninstall cleanup with separate save fixtures. It first refuses existing related MSI products or a matching Start menu folder, preventing the test from replacing an existing installation. Its measured report is `Artifacts/QA/installer-tests.json`; script existence alone does not establish a passed installer result.

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
- Relevant native, launcher and installer QA reports

The archive's manifest URL is version-pinned to `releases/download/v<version>/...`; the launcher discovers it through the latest release's manifest. Attach the archive named in that manifest to the same version tag. Never upload private player saves, signing credentials, local dependency caches or development build intermediates.
