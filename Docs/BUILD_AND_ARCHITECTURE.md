# Building Rift Crown Arena

The maintained Windows implementation is `Unreal/RiftCrownArena`. The complete browser reference is preserved in `Reference/BrowserV15`. The simulation's fixed rules are documented in `UE5_PARITY_SPEC.md`; card definitions and paid Aether accounting are authoritative in the native simulation.

## Development tools

The checked development machine uses Unreal Engine **5.8.2**, Visual Studio 18 Build Tools with the **14.51.36231 x64 C++ toolset**, Windows SDK **10.0.26100.0**, PowerShell 7, .NET SDK **8.0.424**, and Blender **5.2.2 LTS**. Player installations use the packaged game, the matching app-local Visual C++ runtime, and the launcher's self-contained .NET runtime.

Open `Unreal/RiftCrownArena/RiftCrownArena.uproject` for development. The checked-in Content contains native meshes, skeletons, Physics Assets, animations, textures, materials, card DataAssets, Niagara systems, audio, and the Arena map.

From the repository root:

```powershell
# Native Editor target and isolated production integration tests.
./Build/Build-Unreal.ps1
./Build/Test-Unreal.ps1
./Build/Tests/Run-NativeSimulationTests.ps1

# Actual Windows build, cook, stage and archive.
./Build/Build-Unreal.ps1 -Configuration Shipping -Package

# Self-contained launcher, update tests, and actual WPF smoke test.
./Build/Build-Launcher.ps1

# Matching CRTs, inventory, verified ZIP, manifest and installer.
./Build/Package-WindowsRelease.ps1 -Version 1.0.0
./Installer/Build-Installer.ps1 -Version 1.0.0
./Installer/Test-Installer.ps1
```

Pass `-EngineRoot` to Unreal scripts for a different installation path. Pass the actual final Shipping build log to `Package-WindowsRelease.ps1 -BuildLog` when it is outside the default QA log path. A matching compiler's app-local runtime is selected from that log and its imported symbols are checked before packaging. Installer upgrade QA accepts a distinct real MSI in the same upgrade family through `-UpgradeMsi`.

## Runtime responsibilities

| Component | Responsibility |
| --- | --- |
| `Private/Simulation` | Portable C++20 fixed-step match state, roster, deterministic targeting/navigation, AI intelligence, deck analysis, events and accounting |
| `URiftMatchSubsystem` | Per-world authoritative simulation, frame accumulator, result finalization and live/replay presentation events |
| `URiftCardData` and card catalog | Native DataAssets bound to canonical rules and original presentation assets |
| `URiftProfileSubsystem` | Local identity/history, five deck presets, settings, atomic saves, migrations, backups and recovery |
| `URiftReplaySubsystem` | Full snapshots/events, validation, import/export, playback speed, seek, bookmarks and match analysis |
| `URiftMetaSimulationSubsystem` | Versioned background datasets, actual economy, adjusted/mirror-excluded statistics, matchups, synergy, trends and patch comparisons |
| Developer, combat, AI and pathfinding interfaces | Connected training controls and views of the same authoritative match |
| `Private/Presentation` | Authored arena, skeletal poses, health/status overlays, Niagara and bounded audio voices |
| `URiftUIWidget` | Native UMG menus, deck workshop, Cards, Settings, Battle, Meta, replay and Developer controls |
| Launcher/UpdateCore | WPF interface, verified downloads, full file inventory, transactional activation, repair and rollback |

Presentation never applies damage or changes card timing. Mesh collision does not replace the deterministic radius-aware navigation. Default-off QA command-line helpers run only with explicit isolated paths.

## Regenerating original assets

`Build/generate_assets.py` is the Blender source pipeline for models, rigs, clips, LODs and material atlases; `Build/generate_audio.py` produces the original sound library. Source artwork and its generation provenance are under `Assets/Source/CardArt`. `Build/validate_assets.py` audits the exported asset inventory. The native import pipeline is `Build/import_unreal_assets.py`, with inspection in `Build/inspect_unreal_physics.py`.

Run Blender scripts with the installed Blender executable in background mode. Import scripts run through Unreal's Python editor facility with the repository as their source root. Use clean generated FBX imports when changing scale, forward axes or skeletons: Unreal skeletal reimport can retain stale cached options. The importer saves each mesh's external Skeleton and Physics Asset explicitly, finalizes them through the editor module, and forces a physical-file registry scan before card binding and cook. `Assets/PRESENTATION_ACCEPTANCE.md` explains the visual checks in addition to structural validation.

## Verification and isolation

```powershell
# Actual renderer capture; validates fresh PNG dimensions and process exit.
./Build/Capture-Unreal.ps1 -Page Battle -Scenario roster -Width 1920 -Height 1080

# Actual packaged Shipping frame measurements.
./Build/Measure-Unreal.ps1 -Stress -Seconds 90
./Build/Measure-Unreal.ps1 -Stress -WithMeta -Seconds 90

# Actual sound-enabled mixer and connected audio settings.
./Build/Test-UnrealAudio.ps1 -Executable ./Artifacts/Game/Windows/RiftCrownArena.exe

# A fresh 10,000-match cohort, using a frozen built runtime and isolated saves.
./Build/Validate-NativeMeta.ps1 -Games 10000
```

Audit the completed Meta dataset and its final export with the standard-library Python program `Build/Tests/Audit-NativeMeta.py --require-complete`. Its path arguments and optional baseline comparison are documented in `Build/Tests/README.md`. A partial cohort cannot pass the completion gate. The portable regression runner compiles the production simulation directly; its binaries and generated command file remain outside source control.

Native automation, capture, audio and performance runs must supply both a separate `-RiftSaveRoot` and a separate engine `-UserDir`; profile isolation alone does not isolate Unreal graphics configuration. The provided QA wrappers do this. Generated local results are under ignored `Artifacts/QA` or `Build/Automation`; curated release evidence is documented in `Docs`. Headless simulation wall times do not establish rendered game FPS.

Normal player data is `%LOCALAPPDATA%/RiftCrownArena`. Keep that directory separate from installation and update files. Never commit private saves, local QA fixtures, credentials, engine caches or generated installation trees.

Publishing procedures, manifest fields, rollback and save-preserving installation are documented in `WINDOWS_RELEASE.md`.
