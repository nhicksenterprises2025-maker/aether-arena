# Rift Crown Arena 1.5.0 — Pulverizer

Pulverizer is a new 3-Aether defensive building. Its animated hammer sends purple shockwaves outward. Each wave hits ground enemy troops once as the wave reaches them; flying units, friendly units and structures are unaffected.

| Stat | Value |
| --- | ---: |
| Aether / units | 3 / 1 |
| HP | 700 |
| Damage per target per wave | 65 |
| Pulse interval | 0.9 s |
| Basic DPS per target | 72.22 |
| Attack and wave radius | 4 tiles |
| Wave speed | 8 tiles/s |
| Travel time to outer edge | 0.5 s |
| Movement slow | 10% |
| Slow duration | 1.5 s; refreshed on each hit |
| Lifetime | 45 s |
| Building footprint | 1.65 tiles |
| Model scale | 0.95 |

The slow does not stack. Stronger slows retain their own expiration, so Pulverizer cannot prolong Frost Fang's stronger effect. Launched waves finish even if the building is destroyed. The card art uses the same authored model as the battlefield; deployment, idle, compression attack, damage and destruction animations are included.

All deployable cards, including Mini Stampede and Stampede, now use consistent enemy detection: 8 tiles in front and 5 behind. These are detection and pursuit ranges; each card's attack range and target eligibility stay unchanged. Structure-only units still ignore troops and advance through their own lane toward its Guard Tower, or the Core when that Guard has fallen. Engaged Crown attack locks remain.

Mini Stampede's five-member formation stays compact when placed at the river edge. Its formation is shifted together into legal deployment space rather than relocating individual hogs far from the selected tile. Collision and per-member lane splitting remain active.

Existing card balance numbers, models, portraits, arena layout, menus, typography and audio are preserved. New Meta observations have a separate mechanics fingerprint; previous datasets and replays remain readable.
