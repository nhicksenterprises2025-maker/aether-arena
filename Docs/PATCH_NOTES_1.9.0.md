# Rift Crown Arena 1.9.0 — Dart Gun

Dart Gun joins the roster: an 81-year-old African American WWII veteran with a weathered field jacket, gray hair and mustache, service medals and a compact dart weapon. His authored 3D model supplies the card portrait. Slow walking, idle, deployment, firing, hit and death animations accompany visible darts and dedicated audio.

| Stat | Value |
|---|---:|
| Aether cost | 3 |
| Units | 1 |
| HP | 420 |
| Damage per dart | 50 |
| Attack interval | 0.4 s |
| Basic DPS | 125 |
| Attack range | 8.5 tiles |
| Movement speed | 1.5 tiles/s |
| Projectile speed | 22 tiles/s |
| Base model scale | 1.05 |
| Collision radius | 0.42 tiles |

Dart Gun is a ground troop targeting ground troops, flying troops and structures. His forward detection covers his 8.5-tile attack range; rear detection remains 5 tiles. Darts deal damage on impact, so travel time still matters. No poison, stun or splash is added. HP, movement speed, projectile speed and model dimensions are implementation defaults for the unspecified stats.

Friendly swarm members make room when their attack formation blocks a larger ally. Movement retains swept collision checks, opposing troops remain solid, and melee attackers hold their position while targets remain in reach. Existing card stats, abilities and arena geometry retain their 1.8.1 values.

The roster now contains 22 playable cards and one subordinate skeleton definition. Navigation revision 12 gives new Meta simulations a distinct mechanics identity; existing recorded results and replays retain their original definitions.
