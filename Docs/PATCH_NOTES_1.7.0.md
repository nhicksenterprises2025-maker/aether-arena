# Rift Crown Arena 1.7.0 — Arena and Card Design

The arena is **40 tiles long × 22 tiles wide**. Each end gains three rows, each side loses one column, and tile size stays **125 cm**. All six towers move three tiles toward their own rear. The Guard Towers, bridges and stone paths move two tiles outward on each lane; the Core Towers remain centered. This opens more space between the Core and Guard Towers.

| Arena measurement | 1.6.0 | 1.7.0 | Change |
| --- | ---: | ---: | ---: |
| Arena length (tiles) | 34 | 40 | +6 |
| Width (tiles) | 24 | 22 | −2 |
| Total tile centers | 816 | 880 | +64 |
| Floor tile centers | 720 | 792 | +72 |
| River tile centers | 96 | 88 | −8 |
| Guard/bridge distance from center axis (tiles) | 5.76 | 7.76 | +2 per lane |
| Guard distance from river center (tiles) | 10.72 | 13.72 | +3 per team |
| Core distance from river center (tiles) | 13.84 | 16.84 | +3 per team |

## Card size and models

These are base model scales. Scale changes affect the rendered model; collision changes are listed separately. Pulverizer retains its 1.65-tile building footprint.

| Card | 1.6.0 scale | 1.7.0 scale | Change |
| --- | ---: | ---: | ---: |
| Pulverizer | 0.95 | 1.05 | +10.53% |
| Bone Warden | 1.50 | 1.65 | +10.00% |
| Iron Giant | 1.00 | 1.65 | +65.00% |
| Aether Reaper | 1.10 | 1.35 | +22.73% |
| Stampede | 0.85 | 0.90 | +5.88% |

Iron Giant gains fitted armor, a recessed visor, clearer joints and furnace details on both sides of the body. Its six shield pieces remain distinct from the body underneath. Its collision radius increases from **0.44 to 0.70 tiles** to fit the measured neutral body at the new 1.65 scale. Stampede's radius decreases from **0.572 to 0.42 tiles**, using the hog's torso rather than decorative equipment. Troop movement and placement use these updated bodies.

## Troop movement and collisions

Ground troops retain valid routes and reserve separate attack positions so swarms spread around targets while keeping body collision. Each ground unit uses the bridge on its current side when starting a crossing and keeps that bridge through the crossing. A physically blocked bridge waits for space to open.

Direct troop melee now measures reach between the bodies' edges. Listed attack ranges, damage and attack intervals stay unchanged; ranged attacks retain their existing reach.

In **24 paid Mini Stampede/Stampede tests** against Guard Towers, Core Towers and Archer Towers, every member joined the attack together: **240 troops total**. The longest final approach was **4.68 seconds** after entering within four tiles of melee reach. Under real tower fire, nearby troops filled clear openings within **0.35 seconds**.

## Aether Reaper

A purple aura animates around the Reaper while idle and moving. Its open strands leave the hood, scythe and hands readable. A real melee attack that damages multiple enemies creates a visible purple cleave; the visual effect does not add hit packets.

The Reaper triggers a **one-time, 1-second self cloak** when a damage packet first leaves it alive at **50% HP or less**: **222.5 HP out of 445**. The cloak drops enemy target locks and prevents new acquisition during that second. Committed hits, existing projectiles and area damage still resolve, and a lethal packet does not trigger the cloak. Damage remains **95 per hit every 1.05 seconds**, or **90.48 basic DPS**; attack range remains **1.4 tiles**.

## Pulverizer

The real shockwave release now has an elevated purple pulse. A visible front travels outward at **8 tiles/s**, reaching its **4-tile radius in 0.5 seconds**. Damage occurs when that front reaches a ground troop: **65 damage**, with a non-stacking **10% movement slow for 1.5 seconds**, refreshed by another hit. Its **0.9-second attack interval** and other combat stats are unchanged.

## Battle controls

- Placement previews refresh on each captured pointer move and release. The crosshair projects onto the same ground plane used for deployment.
- Returning a dragged card to the hand cancels without spending Aether. Releases over the HUD, outside the arena or on an illegal tile also cancel.
- Removed the visible **1, 2, 3, 4** badges from hand cards. Aether costs remain visible, and the keyboard bindings remain available.
- Removed the **DORMANT** label above Core Towers. Core activation and first-damage health-bar rules are preserved.

The roster remains **21 playable cards plus Bone Warden's subordinate skeleton**. Detection remains **8 tiles in front and 5 behind**, separate from attack range. Existing decks, saves, Meta Lab, replays and developer tools remain available. New matches and Meta datasets record the current mechanics schema **3** and navigation revision **9**; older datasets retain their captured rules.

Approved mechanics hash: `7d23c5e1d5bb848cb887fec350fd814753210a9a81bfcf6a561d5a42d3fa15f4`.
