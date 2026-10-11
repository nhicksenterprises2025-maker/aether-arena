# Rift Crown Arena 1.5.0 — Pulverizer

**Pulverizer** joins the collection as a 3-Aether ground defensive building, bringing the roster to 17 cards. It has its own model-rendered card art and animated hammer, crystal and pistons.

| Stat | Value |
| --- | ---: |
| HP | 700 |
| Wave damage per target | 65 |
| Pulse interval / basic DPS per target | 0.9 s / 72.22 |
| Wave radius / speed | 4 tiles / 8 tiles/s |
| Movement slow | 10% for 1.5 s; refreshes on hit |
| Lifetime / footprint | 45 s / 1.65 tiles |

Purple shockwaves hit ground enemy troops as their expanding front reaches them. Each target is hit once per wave. The slow does not stack, and stronger slows keep their own expiry. Flying units, friendly units and structures are unaffected.

All deployable cards, including Stampede, use consistent **8-tile front and 5-tile rear detection**. Their attack ranges and targeting rules stay intact. Mini Stampede's river-edge formation is shifted together into legal placement space, keeping all five hogs close to the selected tile while preserving collisions and lane splitting.

Existing card stats, models, portraits, arena layout, menus, fonts and audio are preserved. Meta Lab keeps new observations under a separate mechanics fingerprint, and replay playback supports the traveling waves and independently expiring slows.

Use **RiftCrownArena-Setup.msi** to install or upgrade. For a direct launch, extract **RiftCrownArena-Windows-x64-1.5.0.zip** and open **RiftCrownArena.exe** inside the extracted folder.

Read the [patch notes](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/PATCH_NOTES_1.5.0.md), [card stats](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/CARD_STATS_1.5.0.tsv) and [ability stats](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/ABILITY_STATS_1.5.0.tsv). The tables paste directly into Google Sheets.
