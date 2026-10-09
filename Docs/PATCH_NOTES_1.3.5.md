# Rift Crown Arena 1.3.5

Every troop and building now occupies collision space. Ground troops collide with both teams' ground troops, towers and buildings. Flying troops collide with other flying troops and can pass over ground units and structures. Spells retain their area effects and have no physical body.

Crowd movement checks the full movement step, steers around nearby units and slides along obstructions. Ground routes account for nearby traffic while preserving each troop's bridge commitment and same-lane Guard-to-Core advance. Troops can leave crowded building pockets through clear gaps between grid points, including routes that briefly move backward to reach an open exit. Attacking and stunned units remain obstacles. Deployments find clear space for every swarm member before spending Aether.

Meta Lab uses the same collision-aware simulation as live battles. A new rules fingerprint keeps these observations separate from historical datasets.

Windows saves recover from brief file-lock conflicts with a bounded retry. Permanent access failures preserve the existing save and backup and report the failing operation.

All fourteen cards, their costs, damage, health, ranges, movement speeds and abilities are retained. The expanded arena, tower positions, model art, battle UI, drag cancellation, damage-only health bars, spell timing, audio controls, training and replay features remain available.
