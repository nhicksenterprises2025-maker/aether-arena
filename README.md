# Rift Crown Arena / Aether Arena

Original Windows card battler: fourteen cards, two lanes, deterministic combat,
seven AI personalities, deck workshop, Meta Lab, replays and developer training.

The 1.2.0 presentation update replaces all fourteen card illustrations with
portraits rendered from the game's production models. Larger battlefield units,
clearer model details, smoother movement and attack poses, and visible mesh
projectiles make battles easier to follow. The camera fits the battlefield and
model height around the centered bottom hand and open HUD panels. HP bars appear
after the first damage and stay visible after healing. Numerical combat rules remain unchanged. See
[the update notes](Docs/PATCH_NOTES_1.2.0.md) and
[the model and portrait pipeline](Docs/MODEL_PRESENTATION_1.2.0.md).

The interface, Training tools, replay controls, recorded combat sounds and
original orchestral score from [1.1.0](Docs/PATCH_NOTES_1.1.0.md) remain available.

`Reference/BrowserV15` preserves the tested browser game and its gameplay
specification. Run its `start_game.bat` or follow its README.

The native Windows implementation uses Unreal Engine 5.8.2 under
`Unreal/RiftCrownArena`, original asset sources in `Assets`, a self-contained
.NET 8 WPF launcher in `Launcher`, and packaging tools in `Build` and `Installer`.
The fourteen-card balance and gameplay rules remain authoritative in the
deterministic C++ simulation. The browser reference remains intact.

Download the Windows installer, portable game ZIP, standalone launcher and
verification reports from [GitHub Releases](https://github.com/nhicksenterprises2025-maker/aether-arena/releases).
The MSI installs the complete game and launcher per user. The portable ZIP
runs through `RiftCrownArena.exe`; keep the complete extracted tree together.
Playing the packaged game requires neither Unreal Editor nor a separate .NET
installation. Windows packages are unsigned.

Build and architecture instructions are in [Docs/BUILD_AND_ARCHITECTURE.md](Docs/BUILD_AND_ARCHITECTURE.md).
Installation, updates, repair and release procedures are in
[Docs/WINDOWS_RELEASE.md](Docs/WINDOWS_RELEASE.md). Executed checks and their
limits are recorded in [Docs/RELEASE_QA.md](Docs/RELEASE_QA.md), with curated
evidence in [Docs/QA](Docs/QA). Release asset hashes identify the exact tested
game, launcher and installer; source alone does not establish packaged QA.

Profile data, private saves, generated engine caches and local verification
files are excluded from this repository. Browser and native saves use
`%LOCALAPPDATA%/RiftCrownArena`; release updates must preserve that directory.
