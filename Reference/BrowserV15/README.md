# Rift Crown Arena V15 — Meta Lab 2.0

V15 adds a worker-based balance laboratory, versioned analytical history,
live battle replays and post-match analysis. The existing Developer Lab,
seven AI personalities, five deck presets, account saves, art and gameplay
remain available. Every live card stat and match constant is unchanged.

## Play

Extract the entire folder and double-click `start_game.bat` on Windows.
The existing launcher starts PowerShell on an available local port. Alternatively,
run `node server.js` and visit `http://localhost:8080/` (set `PORT` to override).
Use HTTP rather than opening index.html directly. Internet is needed for the
existing pinned Three.js modules; Node and Blender are optional for the launcher.

## Meta Lab

Open META from home. Choose 100, 250 or 500 games/minute, or MAX SAFE for a
measured worker budget that decreases if the interface becomes slow. Background
combat pauses automatically during a live battle. PAUSE SIM and RESUME SIM also
control the worker, including an active validation batch.

The nine views cover card telemetry, explained mechanical matchups, observed
pair synergy, deck archetypes, AI personalities, confidence-gated alerts,
cumulative trends, patch comparisons and Developer-accessible validation.
Click a card or matrix cell for its report. Column groups expose specialized
building/spell/status metrics. Numeric headers sort; filters select type,
cost, air/ground and strategic traits, personality, archetype, version and N.
History charts explicitly describe cumulative estimates rather than rolling WR.
CSV and JSON exports cover all six analytical subjects and respect filters.

Exact patch datasets are kept separate by card/rule/model fingerprint. The
V12.8, V12.10 and V14 comparison choices show unavailable values unless recorded
data exists. A legacy analytical save has uncertain V12.10/V14 provenance and
is displayed separately; it is never relabeled or duplicated as patch evidence.
Reset starts a new run while retaining the previous run as an archive.

Simulations resolve seeded combat events using live card data and shared status,
targeting and economy helpers. Headless bridge routing, tactical policy and
0.20-second scheduling approximate rendered combat. Their results are model
evidence, not live battle statistics. See `V15_META_MATH_AUDIT.txt` for definitions,
confidence formulas, denominator choices and limitations.

## Replays and match analysis

After a battle use MATCH ANALYSIS or WATCH REPLAY. Home REPLAYS opens saved
history, replay JSON import/export and removal controls. Replay playback supports
pause, 0.25/0.5/1/2/4× speed, ±5-second jumps and six event bookmarks.

Recorded events preserve card plays, tile, spending, damage, kills, targets,
tower HP, crowns and AI decisions. Positions interpolate between 250-ms samples;
playback does not rerun AI or randomness. Developer edits and free spawns are
explicitly recorded. Leaving an unfinished match retains an abandoned replay.
Analysis uses actual HP removed, real spending and swarm-aware kill values.

## Saves and validation

Existing profiles/decks remain at `%LOCALAPPDATA%/RiftCrownArena/player_save.json`.
V15 uses a separate `lab_v15.json` for datasets and replay history, so the
launcher's changing port does not lose them. Browser storage provides a fallback.
Queued writes, five-second request timeouts and pending markers protect newer
local data after failed writes. Live battle startup waits for hydration.

Open META and press F9 (or Ctrl+Shift+V), or use Developer Lab → META VALIDATION,
to inspect numeric/economy sanity and
run an actual 10,000-game worker batch. Validation resumes after a live battle
ends and can be paused manually. The packaged `V15_META_VALIDATION.json` contains
an independently measured Node run. See `V15_VALIDATION_REPORT.txt`,
`V15_CHANGELOG.txt`, `V15_PRESERVATION_CHECK.json` and `tests/README.md`.

## Retained V14 documentation

# Rift Crown Arena V14 — Deck Ecosystem

V14 upgrades the existing V13 game with analytical deck building, five named
presets, card strategy reports and decks tailored to all seven AI personalities.
All 14 live cards, art, models, match rules and existing systems are retained.
This update makes no live balance changes.

## Run V14

Extract the whole project and double-click `start_game.bat` on Windows.
The launcher starts the existing PowerShell server on an available local port
and opens the game in your browser. Node.js and Blender are not required for this
launcher. The models are already bundled.

Alternatively, with Node.js installed, run `node server.js` from the project
directory and open `http://localhost:8080/`. Set `PORT` for a different port.
Use a local HTTP server rather than opening `index.html` directly.

Internet access is needed to load the pinned Three.js 0.180.0 modules.
`FIRST_RUN.bat` and `build_models.bat` remain available for optional Blender model
regeneration; you do not need to rebuild the models to play.

## Workshop and training

Open **EDIT LOADOUT** or **LOADOUT**. Select one of five presets, name it and
choose exactly eight unique cards. Click a selected card or slot to remove it.
Inspect cards to compare strategy without changing the draft.

The workshop updates cost, composition, average HP/DPS/range, archetypes,
strengths, weaknesses and all 28 pair scores as the draft changes.
**SAVE LOADOUT** saves the selected preset and makes it your next battle deck.
**TEST DECK** saves it and starts training with Developer Lab available.
Saving during a battle preserves the current hand and rotation.

Card Details provides exact live stats and expandable role, use, counter and
partner notes. The home screen shows your own deck profile. The private **DEV**
panel shows the generated opponent deck, projected composition advantage, lane
plan, threats, counters and AI cycle. Normal battle UI does not reveal it.

All seven AI styles build suitable decks and use their own analysis plus visible
threats, observed plays, cycle memory and estimated player Aether. Developer Lab
and Meta Lab controls, simulations, filters, sorting and CSV export are retained.

## Save compatibility and analytical assumptions

The existing `/api/save` endpoint and Windows save file at
`%LOCALAPPDATA%/RiftCrownArena/player_save.json` remain. Legacy `profile` and
`deck` fields are retained; V14 adds `deckPresets` with five records and active ID.
A valid old deck becomes Deck 1. Browser storage retains a fallback copy. Queued
saves and a pending marker protect newer local edits after a failed disk write.
Battle startup waits for hydration. Existing profiles need no reset.

Scores describe mechanical coverage and compatibility, not measured win rates.
Swarm HP/direct DPS aggregate complete deployments. HP/DPS/range averages include
troops and buildings and exclude spells. Air coverage includes legal spells and
Raven's short aura; its direct structure DPS never counts as troop DPS.
Meteor value estimates time inside the zone. Placement and timing decide trades.

See `V14_CHANGELOG.txt`, `V14_QA_REPORT.txt` and `tests/README.md` for the update,
locked current balance and reproducible tests.

## Historical release notes

The retained text below describes older releases. Its older numbers are historical;
Card Details and the V14 QA report describe the current locked live balance.

### V12.8 — Frost / Storm / Meteor Update

V12.8 builds on V12.7 balance + directional sight with three new cards, new status mechanics, detailed art/models/FX,
Meta Lab support for those mechanics, and removal of persistent death gravestones.

## New cards

### Frost Fang — 5 Aether
- Frost Ground Melee
- 742 HP
- 72 damage
- 0.80s hit speed (90 DPS)
- 2.0 tiles/s
- 1.1-tile range
- Ground targets
- 30% movement slow on melee hit; 2.0s refresh duration; no stacking

### Storm Raven — 6 Aether
- Flying Ranged Win Condition
- 1450 HP
- 282 damage every 1.3s (216.9 direct structure DPS)
- 1.18 tiles/s
- 5-tile range
- Towers / Buildings only
- Electric ring: 2-tile radius, 82 troop damage every 3s, 0.4s stun, Ground + Air

### Meteor Shards — 5 Aether
- Damage Over Time Spell
- 4.5-tile radius
- 151 initial troop damage
- 6 troop damage/second for 5 seconds while units remain in the zone
- Ground + Air troops
- No Crown/building damage

## Death cleanup
Troop death FX remain, but persistent gravestone/corpse markers are not created. Dead unit groups are removed after a
short FX window.

## Meta Lab
The analytical Meta Lab was updated for Frost slow, Storm Raven aura/stun, and Meteor Shards expected DoT dwell time.
Its stored dataset is reset under a new key because the card pool changed from 11 to 14 cards.

See `META_V12_8_VALIDATION.txt` for the math assumptions and sanity checks.

## Running
Use `FIRST_RUN.bat` to regenerate Blender GLBs, including Frost Fang and Storm Raven, then launch the game.


## V12.9 Quick Patch
- Improved Frost Fang, Storm Raven, and Meteor Shards visuals / animations / card art.
- Larger but still clean in-battle HUD sizing pass.


## V13.0 — Battle Intelligence

V13 adds seven live AI personalities, player-Aether estimation, card-cycle awareness, opposite-lane punishment, damage-trade decisions, smarter cheap defense and a private Developer Lab with time controls, spawning, tower HP editing, AI controls and optional path/target/range debugging. Live card balance remains V12.10.
