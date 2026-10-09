# Rift Crown Arena 1.1.0

The battle interface, lobby, deck workshop, collection, replay viewer and match reports have been rebuilt. The arena graphics, fourteen cards and numerical gameplay rules are preserved.

The hand now shows larger illustrated cards, Aether costs, selection and affordability feedback, a drag preview and an illustrated NEXT card. A ten-segment Aether meter, separate crown scores and phase announcements make the match easier to read. Escape opens the pause menu. The Field Manual explains the controls and card interactions, and Training overlays now work in the packaged game.

Recorded combat sounds, quieter tactile UI sounds, three variations for common impacts and a new original orchestral score replace the earlier audio palette. Voice priorities, repeated-sound grouping and a stereo limiter keep crowded battles clear. Master, Music, SFX and UI controls remain independent.

## Run the game

Download **RiftCrownArena-Setup.msi**, open it and finish setup. Start **Rift Crown Arena** from the Start menu, then select **Play** in the launcher. Installation is per user and includes the game and its required runtimes.

For a portable installation, extract the entire **RiftCrownArena-Windows-x64-1.1.0.zip** and open **RiftCrownArena.exe**. Keep the complete extracted folder together. Playing requires neither Unreal Editor nor a separate .NET installation.

The standalone **RiftCrownLauncher.exe** can install the current game through **Check for Updates → Install**. Existing installations use **Check for Updates → Update**. Close the game before updating or repairing.

Select a card or press **1–4**, then click a legal tile or drag the card onto it. **Right click** cancels selection. **Escape** opens the battle menu. **DEV** opens the Training tools.

## Saved data and verification

Profiles, deck presets, settings, replays and Meta datasets remain separate under `%LOCALAPPDATA%\RiftCrownArena`. Updates and uninstall preserve that save location. The original browser save remains intact during migration.

The release includes SHA-256 checksums, the versioned update manifest, exact Windows package verification and a QA archive. Validation covers the deterministic simulation, native UI/input/replays, real audio mixing, rendered pages, performance and the actual Windows installer lifecycle. The unchanged simulation core retains its completed 10,000-match audit; current presentation checks use the new Shipping executable. Detailed results and limits are in the source repository's `Docs/RELEASE_QA.md`.

Windows packages are unsigned.