# Rift Crown Arena / Aether Arena

Original Windows card battler: sixteen cards, two lanes, deterministic combat,
seven AI personalities, deck workshop, Meta Lab, replays and developer training.

Version 1.4.0 introduces Mini Stampede (five wild hogs, 2 Aether) and Stampede
(fifteen royal hogs, 7 Aether), applies the first balance patch, and adds planted
walking strides. Swarm members choose their lane from their actual landing
position, allowing center formations to split. All four Guard Towers and stone
paths now line up with their bridge centers; Core Towers stay centered between
the lanes. Card details, asset data and Meta Lab use the updated sixteen-card
rules. See [the numeric patch notes](Docs/PATCH_NOTES_1.4.0.md)
and [the spreadsheet-ready stats](Docs/CARD_STATS_1.4.0.tsv).

Version 1.3.5 adds collision bodies to every troop and building, including flying
swarms on their own layer. Troops steer around crowds and queue at narrow
crossings while retaining their lane and bridge choices. Meta Lab simulates the
same collision rules. See [the update notes](Docs/PATCH_NOTES_1.3.5.md).

Version 1.3.4 expands the arena by one tile on each edge, moves all towers one
tile toward their own rear, and keeps ground troops on their current side's
bridge and same-lane route toward the Core after a Guard falls. Meta Lab adds a
Stats Guide and simulates the new rules separately from historical datasets.
See [the update notes](Docs/PATCH_NOTES_1.3.4.md).

Version 1.3.3 fixes ground troops getting stuck near towers. Ground deployments
start in clear space and units route around tower edges. See
[the pathing fix notes](Docs/PATCH_NOTES_1.3.3.md).

Version 1.3.2 stops the continuous river sound in menus and battles while
retaining music, interface sounds and combat effects. See
[the audio fix notes](Docs/PATCH_NOTES_1.3.2.md).

Version 1.3.1 fixes flickering stats when hovering over hand cards. The tooltip
stays open through HUD refreshes and changes when the card in its slot changes.
See [the fix notes](Docs/PATCH_NOTES_1.3.1.md).

The 1.3.0 update revises every card model and its production-rendered portrait,
fits equipment around the character's actual hand and armor layers, and bundles
Barlow Semi Condensed for the interface. Cards support captured mouse dragging:
release in a legal arena position to deploy, or return to the hand to cancel
without spending Aether. The main interface layout is retained. See
[the update notes](Docs/PATCH_NOTES_1.3.0.md) and
[model, typography and input details](Docs/MODEL_INPUT_1.3.0.md).

The 1.2.1 spell update gives Meteor Shards a 0.75-second fall and Bullet Burst
a 0.30-second flight before damage. Both hit enemies at their impact-time
positions in the chosen area. See [spell timing notes](Docs/PATCH_NOTES_1.2.1.md).

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
The sixteen-card balance and gameplay rules remain authoritative in the
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
