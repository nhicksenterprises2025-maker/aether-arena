# Rift Crown Arena 2.1.0 — Thunderstorm & Blade Box

## Balance

| Card | Stat | Before | Now | Change |
|---|---|---:|---:|---:|
| Mini Stampede | Aether cost | 2 | 4 | +2 |
| Mini Stampede | HP per hog | 111 | 133.2 | +20% |
| Mini Stampede | Damage per hit | 20 | 24 | +20% |
| Mini Stampede | Basic DPS per hog | 22.22 | 26.67 | +20% |
| Stampede | Aether cost | 7 | 7 | Unchanged |
| Stampede | HP per hog | 111 | 133.2 | +20% |
| Stampede | Damage per hit | 20 | 24 | +20% |
| Stampede | Basic DPS per hog | 20 | 24 | +20% |

Both cards keep their current hog models, unit counts, attack intervals, speed, range and scale. Mini Stampede still deploys five hogs; Stampede deploys fifteen. Fractional HP preserves the exact requested increase. DPS is calculated from damage divided by attack interval.

Shadow Surge now activates after **0.75 seconds** (previously **0.25 seconds**) and grants **2 seconds of invisibility** (previously **1 second**) to friendly troops inside its area at impact. Its purple area aura lasts **0.5 seconds**, and cloaked friendlies remain visible to their owner as purple silhouettes. Cost **3 Aether**, **2-tile radius**, **100 troop damage** and **50 structure damage** stay unchanged. Enemy targeting locks drop on activation; committed attacks, projectiles and area spells still resolve.

## New spell: Thunderstorm

Costs **3 Aether**. A dark cloud and rain mark a fixed **3-tile radius**. Lightning strikes at **0.45, 0.90 and 1.35 seconds** after casting. Each wave hits every eligible enemy currently inside the area, including ground troops, flying troops, buildings and Crown Towers.

Each strike deals **65 troop damage** or **33 structure damage** and stuns survivors for **0.25 seconds**. Later strikes refresh the stun. Full exposure totals **195 troop damage** or **99 structure damage**. Moving into or out of the marked area changes which strikes hit. Invisibility does not protect against area damage.

## New building: Blade Box

Costs **5 Aether**, has **500 HP**, occupies a **1.65-tile footprint**, and expires after **30 seconds** without passive HP decay. It produces **one Twin Blade every 3 seconds**, starting at 3 seconds: up to ten fighters, with the last at 30 seconds if uninterrupted.

Each fighter uses the normal Twin Blades definition: **262 HP, 55 damage, 0.75-second attack interval, 73.33 basic DPS, 3.35 tiles/s movement and 1.2-tile melee range**. Each release creates one fighter, rather than the two-member paid card. Summons cost no extra Aether or deck slot. Destroying the building ends production; stun pauses its production timer while its lifetime continues. A blocked exit waits for safe space instead of overlapping another body or releasing a burst of stored fighters.

## Art and presentation

Archer's hair is fitted to his scalp and hood, with corrected face details; the Archer Tower archer receives the same repair. Boulderback has connected, layered rock armor, fitted crystal roots and smoother body deformation. Updated card portraits show the actual game models.

Blade Box has an animated sliding entrance synchronized with fighter release. Thunderstorm has dark billowing clouds, falling rain and lightning at each damage wave. All stun sources share an animated status effect. New sound cues are short and non-looping; existing audio is preserved.

Collection details, battle card summaries, in-game patch notes, spreadsheet stats, AI evaluations, Meta observations and replays include both new cards. Summoned Twin Blades remain separately attributed to Blade Box so they cannot inflate paid Twin Blades usage or spending. Arena geometry and other existing card balance remain unchanged.

Meta tables now show compact 24-pixel card portraits beside card names, preserving row height, sorting and filters. Narrow matchup headings keep full names in tooltips.
