# V15 validation

Run from the extracted project directory with Node.js:

```powershell
node --test tests/*.test.cjs tests/combat-regressions.cjs
node tests/server-regression.cjs
node tests/v15-meta-validation.cjs
python tests/blender-collections.py
```

The first command includes all retained V14 regressions plus actual headless
combat, worker pause/resume, telemetry, confidence, fingerprints, replay playback,
live event instrumentation and lab disk/local persistence tests. The server test
starts both Node and PowerShell with temporary isolated saves; it verifies the
profile save and separate large lab save endpoint without touching user data.
The validation command performs 10,000 seeded games and writes a measured report.
No test rebuilds or modifies the preserved card art/model assets.

## Retained test details

# V14 regression checks

From the extracted project directory, with Node.js installed:

```powershell
node --test tests/v14-analysis.test.cjs tests/v14-presets.test.cjs tests/v14-persistence.test.cjs tests/v14-gameplay.test.cjs tests/combat-regressions.cjs
node --check src/game.js
node --check src/deck-analysis.js
node --check src/deck-presets.js
node --check src/deck-ui.js
node --check server.js
```

The tests extract and execute the real gameplay definitions without WebGL. They
cover all 14 locked cards, deployment and targeting, spells, status mechanics,
building decay, towers, Aether, overtime/tiebreaker, actual A* navigation and live
movement, Developer Lab controls, Meta Lab, seven AI personalities, deck cycle,
all five presets, old saves, asynchronous hydration, failed saves and training.
AI tests throw if decisions inspect the player's hand, Aether or private matchup
report. Browser checks separately cover rendered UI and the complete user flow.

The original V14 suite contained 112 JavaScript tests, including all 91 unique card
pairs, 350 generated AI decks and all 3,003 legal eight-card compositions.

Optional Blender source checks use Python without running Blender or rebuilding
the preserved art:

```powershell
python tests/blender-collections.py
```

On Windows, `node tests/server-regression.cjs` starts both servers with isolated
temporary saves and checks UTF-8 V14 profile/deck/preset round trips, static
assets, malformed/oversized/aborted requests, traversal and failed-write recovery.

`RIFT_GAME_ROOT` or `RIFT_PROJECT_ROOT` can point the V14 tests at another checkout.
`RIFT_GAME_SOURCE` selects a source file for the cleanup combat regressions.
