# Rift Crown Arena 1.8.1 — Melee pursuit fix

Melee troops now stop when they reach attack range of a moving enemy. Ironclad, Frost Fang and other melee troops hold that position while attacking, then resume chasing only when their target leaves range.

Friendly yielding, opposing collision, swarm contact spacing and tower approaches retain their existing rules. No card stats, attack ranges, attack intervals, detection ranges, abilities, model scales, visuals or audio assets change. The arena remains 40 tiles long by 22 tiles wide with 125 cm tiles.

All 21 playable cards and the subordinate skeleton retain their 1.8.0 numerical definitions. Navigation revision advances from 10 to 11 so new Meta simulations use the movement fix; historical replays and recorded results retain their original identities. Basic DPS remains damage divided by attack interval.

Balance and mechanics identity: `253831ff25ff676070c0fddf89d2851172c9f25f2baa24dd3f70280877da31cd`.
