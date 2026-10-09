# Rift Crown Arena 1.2.1

Meteor Shards and Bullet Burst now travel visibly before their damage lands.
Lead moving enemies and time the cast for where they will be at impact.

- **Meteor Shards:** five falling shards land after **0.75 seconds**. The initial hit and the five-second damage zone begin at impact.
- **Bullet Burst:** seven visible rounds arrive after **0.30 seconds**. The troop or structure hit happens at impact.
- The target area stays fixed at the chosen cast position. Enemies can move into or out of it before the spell lands.
- Casts spend Aether and cycle the hand immediately. Pausing, changing battle speed and replay seeking preserve the same simulation timing.
- Card details and battle tooltips show the impact delay. Nova Flask keeps its immediate impact.

Damage, costs, radii, Meteor damage ticks and the fourteen-card roster are retained. The larger battlefield, centered bottom hand, model portraits and first-damage health bars from 1.2.0 remain available.

This update changes spell timing and therefore the rules fingerprint. Historical Meta Lab datasets retain their original identity; new runs use the updated rules. The earlier 10,000-match cohort is historical evidence for its earlier rules.
