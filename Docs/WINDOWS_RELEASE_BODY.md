# Rift Crown Arena 1.0.0 — Windows installation

Download **RiftCrownArena-Setup.msi** for the bundled game and launcher. Open the installer, choose the installation folder, and finish setup. A Start menu shortcut is included; a desktop shortcut is optional. Installation is per user and includes the launcher’s .NET runtime and the game’s matching application-local Visual C++ runtime files.

Alternatively, download **RiftCrownLauncher.exe** into a writable folder, open it, choose **Check for Updates**, then **Install**. It retrieves the current game release from this repository. Keep the launcher and its managed `Game` folder together. Launcher Settings can choose another installation location.

For a direct game installation, extract the entire **RiftCrownArena-Windows-x64-1.0.0.zip** archive and start **RiftCrownArena.exe**. Keep every extracted directory and runtime file; the executable alone is insufficient.

## Updates and repair

The launcher’s **Check for Updates** displays the current release and its patch notes. **Update** downloads the complete package, verifies its archive and file hashes, and activates a verified release directory. Close the game before updating or repairing. **Repair** verifies the installed inventory and restores missing or damaged files from the recorded release. **Cancel** stops an active transfer before activation. **Open Logs** exposes local launcher and game diagnostics.

The default manifest endpoint is the repository’s latest release. The manifest identifies a version-pinned game archive, its SHA-256, and every packaged file. **SHA256SUMS.txt** identifies the ZIP, MSI, standalone launcher and manifest from this release.

## Saved data

Native profiles, decks, settings, replays, Meta datasets and logs are stored separately under `%LOCALAPPDATA%\RiftCrownArena`. **Open Saves** in the launcher opens that folder. The original browser `player_save.json` remains intact during migration; native state is saved in `ue_save.json`.

Native replays use compressed `.riftreplay` archives. The game also imports its legacy native `.json` replay format. Imported browser data is preserved through the documented migration path.

The MSI defaults to `%LOCALAPPDATA%\Programs\RiftCrownArena`. Windows Installed Apps supports repair and uninstall. Uninstall removes the launcher, managed game files and shortcuts while preserving the separate save/log folder. A direct ZIP extraction can be removed as a folder without deleting that save location.

## Verification

`windows-release-verification.json` records the exact release hashes and matched Windows packaging, published Play, installed Play and installer lifecycle evidence. The corresponding measured QA reports document the update, native gameplay, replay, Meta, audio, rendering and performance checks performed for this release.

Validation includes 36 deterministic core regressions, 21 complete seeded AI matches, nine Unreal integration tests, a completed 10,000-match native Meta cohort with 488,780 independent checks, 31 launcher update checks, 10 WPF controls, 51 installer lifecycle assertions, 38 packaged audio checks and 80 packaged particle-effect checks. The QA archive contains the selected reports, reproducible synthetic Meta evidence and renderer captures.

Actual 90-second Shipping measurements at 1920×1080 on a Ryzen 7 5700 / RTX 5060 / 32 GB Windows 11 host averaged about 60 FPS. Normal AI battle had a 16.874 ms p99 frame time and no frames over 50 ms. Heavy bridge waves reached 146 entities with Meta correctly paused; the stress runs retained several brief hitches, with a worst measured frame of 117.133 ms. Complete frame, CPU, GPU and memory measurements are included in the QA reports.

The Windows game, launcher and MSI are unsigned.
