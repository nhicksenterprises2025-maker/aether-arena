# Rift Crown Arena 1.6.0 — Balance Expansion

The balance expansion adds four playable cards, a larger arena grid, revised models and animations, and detailed card and ability references.

The roster grows to **21 playable cards**:

- **Bone Warden — 6 Aether:** 1,526 HP; 101 damage every 1.5 s; 0.5-tile splash against ground troops and structures. On death, releases exactly four skeletons nearby. Each has 50 HP, deals 36 damage every 0.75 s, moves at 2 tiles/s and attacks ground troops and structures within 0.7 tiles. The children cost no extra Aether or deck slot and keep the original cast's attribution.
- **Aether Reaper — 4 Aether:** A flying melee troop with 445 HP, 95 damage every 1.05 s and 1.5-tile splash. Its scythe attacks ground troops, air troops and structures within 1.4 tiles.
- **Iron Giant — 3 Aether:** 1,000 body HP plus a separate 310-HP shield; 85 damage every 1.3 s; structures only. The hit that breaks its shield discards overflow, and the shield does not regenerate.
- **Shadow Surge — 3 Aether:** 100 troop / 50 structure damage in a 2-tile area after 0.25 s; cloaks friendly troops inside that area for 1 simulation second. Enemy locks drop, but committed damage, projectiles and area spells still resolve.

The arena is **34 tiles long × 24 wide**, with **125 cm tiles (+25% side length)** and Guard Towers aligned with their bridges and stone paths. Detection remains 8 tiles ahead and 5 behind, separate from each card’s attack range.

Rambeast now has **934 HP**, **156 basic damage every 1.25 s** and a **302-damage total charged hit** after more than 1.5 s of accumulated movement. Its charged attack uses 302 damage in place of the normal 156-damage hit. Movement speed is 2 tiles/s normally and 3.2 tiles/s while charged.

Archer Tower keeps 850 HP, 75 hit damage and 7-tile range. Its first attack cooldown stays 0.35 s, subsequent attacks occur every 0.9 s, and passive decay falls to **25 HP/s**. It expires at 25 s even if HP remains.

Mini Stampede and Stampede now have **111 HP and 20 damage per hog**, with five and fifteen members respectively. Mini Stampede attacks every 0.9 s; Stampede attacks every 1 s and has a 0.8-tile attack range. Their visual scales become 1.30 and 0.85 respectively, while their existing collision radii remain unchanged. Stampede keeps its compact five-column, three-row formation; members use the bridge on the side where they actually land.

When engaging ground troops, Pulverizer launches **65-damage purple shockwaves every 0.9 s**. The wave front travels at **8 tiles/s** through a **4-tile radius**, hitting each eligible enemy ground troop once per wave. Its **10% slow lasts 1.5 s**, refreshes on another hit and does not stack. Frost Fang's stronger slow retains its separate expiry. Pulverizer keeps 700 HP, its existing 700/45 HP/s decay and 45 s lifetime behavior.

Spells resolve in their fixed target area after the visible cast delay, so timing a moving target matters:

| Spell | Troop / structure impact damage | Radius | Impact delay |
| --- | ---: | ---: | ---: |
| Meteor Shards | 262 / 143 | 4.5 tiles | 0.75 s |
| Bullet Burst | 175 / 55 | 2.5 tiles | 0.3 s |
| Nova Flask | 375 / 185 | 3 tiles | 0.85 s |
| Shadow Surge | 100 / 50 | 2 tiles | 0.25 s |

Meteor Shards adds five separate DOT ticks, one per second after impact: **40 troop damage or 15 structure damage per tick** while the target remains in the area. Full exposure totals **462 troop damage or 218 structure damage**, before shield handling and remaining-HP limits. Bullet Burst's seven visible rounds apply its listed damage **once per eligible target**. Nova Flask has one delayed area hit.

Ironclad, Ember Archer, Arc Mage and Storm Raven also receive balance changes. The full notes show every old/new value, the correct relative percentages and calculated DPS.

Bone Warden has **four recognizable captive skeletons inside its rib cage**, with independent body motion and eight dedicated head and arm joints. Its hammer uses an open finger grip with the shaft clear of the palm and wrist. Aether Reaper has a separated scythe grip and room for its hands and cuffs to move during the swing. These four captive bodies are part of the living Warden model; the four skeletons released on death are separate gameplay units.

The hog models have shaped snouts, seated eyes, fitted guards and tusk collars, carved cloven feet and planted gallop phases. Pulverizer has a segmented compression rim, fitted guide collars and piston rods, recessed basalt panels and a readable windup, impact and recoil. Iron Giant's shield has separate break feedback. The animation sets cover movement, attacks, deployment, reactions and death, with card portraits rendered from the game models. Ground markers leave troop surfaces clear. The established main UI layout remains in place.

The reference files include [full patch notes](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/v1.6.0/Docs/PATCH_NOTES_1.6.0.md), [card stats](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/v1.6.0/Docs/CARD_STATS_1.6.0.tsv), [ability stats](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/v1.6.0/Docs/ABILITY_STATS_1.6.0.tsv) and [summon stats](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/v1.6.0/Docs/SUMMON_STATS_1.6.0.tsv). TSV tables paste directly into Google Sheets. The matching formula-based XLSX contains all 21 cards, the summoned skeleton and ability details; DPS calculates automatically from damage and attack interval.

Windows delivery supports the standalone launcher, MSI installer and direct-launch game ZIP. The ZIP runs through `RiftCrownArena.exe` with its complete folder tree; the MSI installs the launcher and bundled game together.

## Windows packages

- [RiftCrownArena-Windows-x64-1.6.0.zip](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/RiftCrownArena-Windows-x64-1.6.0.zip) — Complete game folder; extract and launch RiftCrownArena.exe.
- [RiftCrownArena-Setup.msi](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/RiftCrownArena-Setup.msi) — Installer with bundled game and launcher.
- [RiftCrownLauncher.exe](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/RiftCrownLauncher.exe) — Standalone launcher and updater.
- [update-manifest.json](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/update-manifest.json) — Windows update manifest.
- [windows-release-verification.json](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/windows-release-verification.json) — Measured local Windows acceptance.
- [RiftCrownArena-Expansion-QAEvidence-1.6.0.zip](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/RiftCrownArena-Expansion-QAEvidence-1.6.0.zip) — Scoped source, native and measurement evidence.
- [Rift_Crown_Arena_Balance_Reference_1.6.0.xlsx](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/Rift_Crown_Arena_Balance_Reference_1.6.0.xlsx) — Six-tab balance reference with calculated DPS.
- [SHA256SUMS.txt](https://github.com/nhicksenterprises2025-maker/aether-arena/releases/download/v1.6.0/SHA256SUMS.txt) — Checksums for the release files.

The actual Windows Shipping build/cook/archive and its 49-file payload passed local acceptance. The launcher passed 31 core checks and 10 connected WPF controls; the MSI passed 51 lifecycle assertions, including real upgrade, installed Play/native rendering, repair, uninstall and byte-identical save preservation.

Performance remains qualified: the extreme paid-swarm Development runs passed functional checks but exceeded frame budgets. A separate 20-second HIGH/1080p normal-battle sample at a 60 FPS cap measured 16.75 ms mean and 17.13 ms p99, with one 97.45 ms spike. This short sample does not establish sustained or uncapped performance. The QA evidence retains these limits.
