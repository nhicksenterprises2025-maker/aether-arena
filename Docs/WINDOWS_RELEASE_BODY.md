# Rift Crown Arena 1.2.0

All fourteen cards use portraits rendered directly from the production models in the game. Larger deployed troops and towers, clearer equipment and faces, smoother movement and attack poses, and visible ranged projectiles make battlefield interactions easier to follow. The guard cannon aims and recoils while its tower stays fixed.

The four hand cards use a compact centered bottom dock with their existing portrait size. A 35° orthographic camera protects the enlarged models from the hand, clock and open side panels. HP bars and numbers appear after the first damage and remain visible after healing; status labels and building lifetime remain independent. Paused/replayed shots retain their initialized projectile glow.

The fourteen-card roster and numerical gameplay rules remain unchanged. Training, deck presets, the collection, replay controls, match reports and Meta Lab remain available, along with the recorded combat effects and original orchestral music introduced in 1.1.0.

## Run the game

Download **RiftCrownArena-Setup.msi**, install it, and open **RiftCrownArena** from the Start menu. Select **Play** in the launcher.

For a portable installation, extract the entire **RiftCrownArena-Windows-x64-1.2.0.zip** and open **RiftCrownArena.exe**. Keep the complete extracted folder together. Playing requires neither Unreal Editor nor a separate .NET installation.

The standalone **RiftCrownLauncher.exe** uses **Check for Updates → Install** for a new installation or **Check for Updates → Update** for an existing one. Close the game before updating or repairing.

Select a card or press **1–4**, then click a legal tile or drag the card onto it. **Right click** cancels selection. **Escape** opens the battle menu. **DEV** opens the Training tools.

## Saved data and verification

Profiles, deck presets, settings, replays and Meta datasets remain separate under `%LOCALAPPDATA%\RiftCrownArena`. Updates and uninstall preserve that save location. The original browser save remains intact during migration.

Final-source Editor and Shipping builds passed, all thirteen native integration scenarios passed, and all fourteen production-model portraits passed independent provenance checks. Actual Editor/Shipping audio and Niagara lifecycle checks passed on the final build. The launcher's real Play action verified and ran the complete package, and MSI 1.2.0 compiled with standard validation.

All 26 final packaged PNGs were reviewed across twelve public pages, three battle aspect ratios, enlarged UI, projectiles/effects, replay, placement and phase/results. All 51 installed MSI lifecycle checks passed, including upgrade from 1.1.0, installed Play, repair, uninstall and save preservation; Windows payload finalization passed. All three final 90-second performance runs completed: normal AI play had no frame over 50 ms, while artificial mass-spawn stress retained brief hitches up to 140.815 ms.

Fresh complete anonymous public ZIP/MSI downloads matched the exact tested payload. The released launcher's real default-endpoint update check matched version 1.2.0, the complete inventory, archive and displayed notes with Install enabled while preserving isolated data. [Public download proof](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/QA/public-download-verification.json), [actual launcher update proof](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/QA/published-update-check.json) and [full executed checks/limits](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/RELEASE_QA.md) are in the source repository.

Windows packages are unsigned.
