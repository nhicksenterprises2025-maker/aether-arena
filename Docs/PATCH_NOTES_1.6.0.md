# Rift Crown Arena 1.6.0 — Balance Expansion

Approved balance data for the 1.6.0 update.

The collection expands from 17 to **21 playable cards**, with four new cards: Bone Warden, Aether Reaper, Iron Giant and Shadow Surge. Bone Warden Skeleton is a subordinate unit, not an extra playable card.

The arena grid is **34 tiles long × 24 tiles wide**. Tile side length increases from 100 cm to **125 cm (+25%)**. The Guard Towers remain centered on their bridge lanes and stone paths. Shared enemy detection remains **8 tiles in front and 5 behind**; attack ranges are separate.

## Balance changes

These comparisons use the captured 1.5.0 baseline and approved 1.6.0 definitions. Percentages are relative to the old value. A feature added from zero is marked **ADDED**, without an undefined percentage. Basic DPS is damage divided by attack interval; it excludes charge, aura, DOT and other abilities. A shorter interval means more frequent attacks.

| Card | Stat | 1.5.0 | 1.6.0 | Change |
| --- | --- | ---: | ---: | ---: |
| Ironclad | Attack interval (s) | 1 | 1.1 | +10.00% |
| Ironclad | Attack frequency (attacks/s) | 1 | 0.91 | -9.09% |
| Ironclad | Basic DPS per unit (DPS) | 93 | 84.55 | -9.09% |
| Ember Archer | Attack interval (s) | 1.55 | 1.75 | +12.90% |
| Ember Archer | Attack frequency (attacks/s) | 0.65 | 0.57 | -11.43% |
| Ember Archer | Basic DPS per unit (DPS) | 98.06 | 86.86 | -11.43% |
| Arc Mage | Body HP per unit (HP) | 547 | 512 | -6.40% |
| Arc Mage | Damage per hit per unit (damage) | 120 | 108 | -10.00% |
| Arc Mage | Basic DPS per unit (DPS) | 104.35 | 93.91 | -10.00% |
| Rambeast | Body HP per unit (HP) | 880 | 934 | +6.14% |
| Rambeast | Damage per hit per unit (damage) | 140 | 156 | +11.43% |
| Rambeast | Attack interval (s) | 1.4 | 1.25 | -10.71% |
| Rambeast | Attack frequency (attacks/s) | 0.71 | 0.8 | +12.00% |
| Rambeast | Basic DPS per unit (DPS) | 100 | 124.8 | +24.80% |
| Rambeast | Movement speed (tiles/s) | 2.6 | 2 | -23.08% |
| Rambeast | Charged movement speed (tiles/s) | 2.6 | 3.2 | +23.08% |
| Rambeast | Total charged hit (damage) | 255 | 302 | +18.43% |
| Rambeast | Movement time before charge (s) | 1.65 | 1.5 | -9.09% |
| Storm Raven | Body HP per unit (HP) | 1337 | 1222 | -8.60% |
| Meteor Shards | Structure impact damage (damage) | 0 | 143 | ADDED |
| Meteor Shards | Structure damage per DOT tick (damage) | 0 | 15 | ADDED |
| Meteor Shards | Maximum full structure exposure (damage) | 0 | 218 | ADDED |
| Archer Tower | Attack interval (s) | 1 | 0.9 | -10.00% |
| Archer Tower | Attack frequency (attacks/s) | 1 | 1.11 | +11.11% |
| Archer Tower | Basic DPS per unit (DPS) | 75 | 83.33 | +11.11% |
| Archer Tower | Passive HP decay (HP/s) | 34 | 25 | -26.47% |
| Bullet Burst | Spell radius (tiles) | 2.2 | 2.5 | +13.64% |
| Nova Flask | Spell radius (tiles) | 3.25 | 3 | -7.69% |
| Nova Flask | Impact delay (s) | 0 | 0.85 | ADDED |
| Mini Stampede | Body HP per unit (HP) | 95 | 111 | +16.84% |
| Mini Stampede | Damage per hit per unit (damage) | 18 | 20 | +11.11% |
| Mini Stampede | Basic DPS per unit (DPS) | 20 | 22.22 | +11.11% |
| Mini Stampede | Full group body HP (HP) | 475 | 555 | +16.84% |
| Mini Stampede | Full group basic DPS (DPS) | 100 | 111.11 | +11.11% |
| Mini Stampede | Visual model scale (x) | 0.85 | 1.3 | +52.94% |
| Stampede | Body HP per unit (HP) | 95 | 111 | +16.84% |
| Stampede | Damage per hit per unit (damage) | 18 | 20 | +11.11% |
| Stampede | Basic DPS per unit (DPS) | 18 | 20 | +11.11% |
| Stampede | Full group body HP (HP) | 1425 | 1665 | +16.84% |
| Stampede | Full group basic DPS (DPS) | 270 | 300 | +11.11% |
| Stampede | Attack range (tiles) | 0.9 | 0.8 | -11.11% |
| Stampede | Visual model scale (x) | 1.3 | 0.85 | -34.62% |

Mini Stampede keeps **5 hogs** and Stampede keeps **15 hogs**. Their visual scale changes do not change their gameplay collision radii. Group HP and DPS above assume every member is alive and able to attack.

Rambeast charges after **more than 1.5 seconds of accumulated movement**. Its **302 damage is the total charged hit**, replacing its 156-damage basic hit. Movement speed is 2 tiles/s normally and 3.2 tiles/s while charged.

Archer Tower retains **850 HP, 75 damage, 7-tile range and a 0.35 s initial attack cooldown**. It loses 25 HP/s passively and expires at 25 seconds even if HP remains; its subsequent attacks occur every 0.9 s. Pulverizer keeps its existing 700/45 HP/s decay, 45 s lifetime and no separate hard-expiry rule.

## Four new playable cards

| Card | Cost | Body HP | Hit damage | Interval | Basic DPS | Move | Range | Splash | Basic targets |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Bone Warden | 6 | 1526 | 101 | 1.5 s | 67.33 | 1 tiles/s | 1.75 tiles | 0.5 tiles | Ground troops, structures |
| Aether Reaper | 4 | 445 | 95 | 1.05 s | 90.48 | 1.85 tiles/s | 1.4 tiles | 1.5 tiles | Ground troops, air troops, structures |
| Iron Giant | 3 | 1000 | 85 | 1.3 s | 65.38 | 1.55 tiles/s | 1.35 tiles | 0 tiles | structures |

All three new troops deploy one body per paid cast. Shadow Surge is the fourth new card, a **3-Aether spell** listed below.

**Bone Warden:** On its one death transition, it spawns exactly **4 skeletons** nearby. Each skeleton has **50 HP**, deals **36 damage every 0.75 s (48 DPS)**, moves at **2 tiles/s**, and attacks ground troops and structures within **0.7 tiles**. Together they have 200 body HP and 192 basic DPS. They spend **0 extra Aether**, occupy **0 additional hand or deck slots**, and their contribution belongs to the original Bone Warden cast while remaining separately observable.

**Iron Giant:** Its **1,000 body HP and 310 shield HP are separate**. Damage consumes the shield first. When a damage packet breaks the shield, that packet’s excess is discarded; later packets can damage the body. For example, a 400-damage packet against a fresh shield removes 310 shield HP, discards 90 damage and leaves all 1,000 body HP. The shield does not regenerate.

**Shadow Surge:** At impact, friendly troops inside its 2-tile area gain **1 second of invisibility in simulation time**. Existing enemy target locks drop, including committed Crown Tower locks, and enemies cannot newly acquire those troops while cloaked. Already committed damage, launched projectiles and area spells still resolve. Invisibility does not grant damage immunity and does not cloak troops outside the spell area.

## Spell timing and damage

All four spells can hit ground troops, air troops and structures. Spells resolve against eligible enemies at their positions when the impact occurs. The target area stays where the spell was cast, so players must account for the animation delay.

| Spell | Cost | Troop impact damage | Structure impact damage | Radius | Impact delay |
| --- | ---: | ---: | ---: | ---: | ---: |
| Meteor Shards | 5 | 262 | 143 | 4.5 tiles | 0.75 s |
| Bullet Burst | 2 | 175 | 55 | 2.5 tiles | 0.3 s |
| Nova Flask | 4 | 375 | 185 | 3 tiles | 0.85 s |
| Shadow Surge | 3 | 100 | 50 | 2 tiles | 0.25 s |

Meteor Shards adds **5 DOT ticks**, one per second for 5 seconds after impact: **40 troop damage or 15 structure damage per tick** while a target stays in the area. Maximum full exposure is **262 + 5 × 40 = 462 troop damage**, or **143 + 5 × 15 = 218 structure damage**, before shields, mitigation and remaining-HP limits. Each tick is a separate damage packet.

Bullet Burst’s **7 visible rounds apply the listed damage once per eligible target**, not seven times. Nova Flask has one delayed area hit and no lingering damage zone.

## Reference tables

The tab-separated tables paste directly into Google Sheets: [all card stats](CARD_STATS_1.6.0.tsv), [abilities](ABILITY_STATS_1.6.0.tsv) and [summoned units](SUMMON_STATS_1.6.0.tsv). Values marked N/A are not applicable. The matching [formula-based workbook](../outputs/balance-expansion/Rift_Crown_Arena_Balance_Reference_1.6.0.xlsx) contains all 21 cards, the summoned skeleton, ability details, and automatically calculated DPS.

Historical datasets and replays keep their captured balance identity. New measurements use the current balance identity; archived empirical results are distinguished from current-mechanics scores. Individual child contributions, shield damage, overflow prevention and cloak status retain explicit attribution.

Approved mechanics hash (balanceHash): `e87244ec5b4629688bfdf83e5ef3888f3ad61b96848e9f117e3af12b96b236d5`.
