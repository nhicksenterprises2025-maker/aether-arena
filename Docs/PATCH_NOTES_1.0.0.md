# Rift Crown Arena 1.0.0

Rift Crown Arena now runs as a native Windows x64 game built with Unreal Engine 5.8.2. The fourteen-card roster, numerical balance, two-lane arena, tower rules, tile placement, paid card cycle, seven AI personalities and match phases are preserved. The complete browser implementation remains available in the repository as a reference.

- Original models, skeletal animation, card illustrations, materials, Niagara effects, arena scenery and audio replace the browser presentation. Both teams face their actual direction of travel.
- The deck workshop saves five presets with eight distinct cards, complete card intelligence, average cost, coverage, archetypes, strengths, weaknesses and synergy.
- Battle, Developer Lab, Meta Lab, profile, replay, analysis and settings are connected to the same native simulation and persistent profile. Debug overlays start off.
- Replays retain complete snapshots and events in lossless compressed archives. Saving runs in the background; playback supports pause, speed changes, seeking and JSON import/export.
- Meta Lab uses background workers and pauses simulation during a live battle. Its versioned datasets retain actual paid economy, mirror exclusion, Bayesian adjustment, confidence intervals, matchups, synergy, archetypes, styles and trends.
- Native profile migration preserves the original browser save. Compatible extension fields, saved decks and settings survive atomic save/recovery operations.
- The self-contained Windows launcher verifies complete file inventories, downloads, repairs, rollback and update hashes. The per-user installer includes the required runtime files, shortcuts, repair and save-preserving uninstall.

Release QA corrected a floating-point payment residue, Meteor exposure attribution, replay event branching and finalization stalls, physics package discovery, camera framing, material usage, menu contrast and installer maintenance paths. These fixes preserve card values and gameplay rules. Measured native, renderer, performance, launcher and installer results are recorded in `RELEASE_QA.md` and the accompanying release evidence.

Controls: choose a card with the hand or keys 1–4, then select a legal tile center. Right click cancels. Training provides pause, speed, bank, spawning, tower HP, AI and debug controls. Profile, audio, graphics, UI scale and deployment settings are available from the menu.
