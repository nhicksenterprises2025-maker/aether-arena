# Spell impact timing

Meteor Shards and Bullet Burst commit a fixed target area when played. Meteor has a 0.75-second fall; Bullet Burst has a 0.30-second flight. Nova Flask retains its immediate hit. Costs, damage, structure damage, radii and Meteor's five ticks are unchanged.

The portable authoritative simulation owns a pending `SpellCast` with its play ID, team, card, position, cast time and impact deadline. Aether payment and hand cycling happen at the cast. On the first fixed step at the deadline, the simulation removes the pending cast, records one `spell_impact`, and finds eligible enemies using their current positions. Processing follows that step's movement. Meteor's zone is created at impact, with its first tick one second later and its fifth tick five seconds later. Clear Field, the tiebreaker and match completion cancel pending casts.

Presentation reads the same pending cast and simulation clock. Five meteor meshes accelerate downward; seven bullet meshes release in a short volley and reach the ground at the shared deadline. A team-colored circle marks the committed area. Trails, landing effects, debris and sound use the existing authored assets. The impact sound and explosion happen on the authoritative event. Pausing and battle speed affect travel and damage together.

Replay snapshots persist pending casts. `spell_cast` and `spell_impact` reconstruct them between samples. Backward seeking clears future effects even when the rewind is less than ten milliseconds. Older snapshots without the optional cast array and older instant `card_play` events retain their recorded damage and Meteor zone timing.

Card details and hand tooltips expose the impact delay. The two spell DataAssets include the same reflected delay as the native roster. Immutable Meta snapshots and the rules fingerprint include cast timing, so old datasets cannot receive matches under the new rules. Older datasets remain available as historical observations.

Acceptance evidence is recorded in [spell-timing-checks-1.2.1.json](QA/spell-timing-checks-1.2.1.json). It covers the focused spell patch and Windows delivery. The 1.2.0 model/framing review and earlier 10,000-match cohort retain their original versions and do not establish a new balance soak under delayed-spell rules.
