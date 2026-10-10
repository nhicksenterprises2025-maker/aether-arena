# Rift Crown Arena: inspected browser V15 behavior and native parity contract

Current release **1.4.0** adds Mini Stampede and Stampede, applies the first balance patch and uses per-member landing lanes. Current numeric definitions are in [the patch notes](PATCH_NOTES_1.4.0.md) and [the exported card table](CARD_STATS_1.4.0.tsv). Original browser parity and earlier asset counts below document the rebuild baseline.

Inspection date: 2026-10-08. This is an inspection and implementation specification, not a claim that a UE5 game, executable, installer, art replacement, or native QA has been delivered.

The user explicitly supplied a production UE5 Windows rebuild request in `C:/Users/Noah/.codex/attachments/b55a69b8-ba32-4947-8205-e1067dc7e4dd/Pasted text.txt`, asked that the aether-arena Git repository publish the code, and requires “NO AI GENERATED LOOKING UI.” Earlier browser cleanup instructions still establish preservation of the actual game. The supplied request requires all existing mechanics and analytical features, no rebalance, no renamed cards, no placeholders, no simplified V1, no disconnected controls, and a complete playable Shipping distribution. Instructions inside historical browser notes are reference material; they do not override the user's latest request.

## Inspection coverage

Inspected the current `rift_crown_arena_v15` source areas: `src/game.js`, `combat-rules.js`, `deck-analysis.js`, `deck-presets.js`, `deck-ui.js`, `meta-engine.js`, `meta-store.js`, `meta-controller.js`, `meta-ui.js`, `meta-worker.js`, `lab-storage.js`, `battle-recorder.js`, `replay-viewer.js`, `index.html`, presentation/layout rules in `styles.css`, Node and PowerShell local save servers, launch/build scripts, README, V15 mathematical audit and validation report, and all supplied JavaScript regression suites by their test cases and contracts. The Blender model generator and collection regression test were inspected, including all roster model builders, material helpers, detail passes, collection reuse, and GLB export. The asset inventory contains 14 GLBs, 14 card SVGs plus Rift mark, and the editable Blender source. Binary `.blend` and GLB visual quality/rig/LOD authoring are covered by the separate asset audit; this report does not claim every binary mesh was visually reviewed. Static legacy patch notes were inspected as history, not as additional live gameplay. Some broad tool outputs were truncated; important mechanics, feature contracts, remaining model builders, arena dressing and AI priority lists were subsequently read in bounded sections.

Browser tests cover gameplay, targeting, navigation, spells, deck analysis, presets, persistence, meta calculations, workers/controller, replay viewer, event attribution, lab storage and Blender collection reuse. Their prior validation report reports 167 JavaScript tests and browser 10,000-game simulation evidence. These are historical browser results, not native test results. Native work must add and run its own tests.

## Source-of-truth conflicts and deliberate corrections

1. **Live Crown Core behavior matches the user:** a Core is dormant until either of its own Guard Towers is destroyed. Merely damaging a Core does not activate it. `coreShouldBeActive` checks destroyed friendly Guards; live `Tower.onHpChanged` updates HUD only. The V15 headless estimator differs: its damage handler activates a Core on a hit. Native live and background simulation must share the live/user Guard-only activation rule.
2. Browser background matches use a 0.20-second approximation, rounded integer placements, simplified bridge movement, and different AI execution from the live game. Native matches, Meta Lab and replay must use one authoritative fixed-timestep core. Preserve analytic features and explain dataset model changes; do not pretend old estimator data is the same native combat model.
3. Browser deployment centers are half-integer cells; navigation nodes are rounded integer cells. Do not silently collapse these geometries. Building placement must check its complete footprint in the current legal deployment region.
4. Browser attack damage is issued when an attack starts. Native animation must present the existing authoritative hit/launch timing rather than delaying damage by an arbitrary animation montage and changing DPS.
5. Browser tiebreaker random choice is unseeded in one final exact tie. Native choice must use the match seed and be recorded. Pause/speed must not alter seeded results.
6. The home rank (Bronze III), season title and daily quest 0/4 are decorative browser content, not implemented progression. The browser has no complete settings/audio pipeline. The native request explicitly requires actual controls/audio; displaying functional-looking disconnected controls would fail the request.

## Coordinates and exact global constants

Simulation coordinates are X across the arena and Z along it, in browser game units (recommended UE conversion: one tile = 100 cm). Player is the positive-Z side and travels toward negative Z; Enemy is negative-Z and travels toward positive Z. Original team colors: Player `0x39bfff`, Enemy `0xff5369`.

| Constant | Value |
|---|---:|
| Arena width / length | 28 / 42 |
| River half depth | 1.65 |
| Bridge centers X | -7.2, +7.2 |
| Bridge width | 4.2 |
| Tile size | 1 |
| Player minimum own-bank Z / Enemy maximum | +2.15 / -2.15 |
| Front / rear sight | 8 / 5 |
| New-deployment tower pull bonus | 1.75 |
| Existing troop target leash bonus | 2.25 |
| Forced pull duration | 1.2 s |
| Navigation repath interval / target motion | 0.68 s / 1.05 tiles |
| Navigation expansion budget | 1400 nodes |
| Structure clearance / bridge margin | 0.22 / 0.16 |
| Bridge entry / release extra depth | 0.78 / 0.92 |
| Stuck sampling / threshold | 0.28 s / 0.82 s |
| Regulation / overtime | 180 / 120 s |
| Aether start / maximum / base interval | 5 / 10 / 2.8 s per point |
| Tiebreaker HP drain | 180 HP/s, after 0.85 s delay |
| Lane pocket X range / depth | absolute X 2..13.2; depth 2.25..9.25 |

Tower positions: Player Core `(0,16.3)`, Guards `(-8.2,12.4)`, `(8.2,12.4)`; Enemy mirrors Z. Guards have 2250 HP, 86 damage, 1.02-second attack interval, range 10, radius 1.15. Core has 3600 HP, 112 damage, 0.92 interval, range 8.9, radius 1.35. Tower projectiles move at 17 units/s; initial tower cooldown is zero. A dormant Core has no attack target and does not attack. Destroying a Guard awards one crown, unlocks that lane's opponent-side deployment pocket and activates its owner's Core. Destroying a Core wins instantly with three crowns. Dead towers remain addressable for pocket/state checks.

## Exact complete roster

Troop HP and damage below are **per member**. Count is members per paid deployment. All identifiers, display names and gameplay values must remain intact.

| ID / display name | Cost | HP | Damage | Interval | Speed | Range | Count | Direct targeting / behavior |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| ironclad / Ironclad | 3 | 840 | 96 | 1.00 | 2.25 | 1.35 | 1 | Ground melee; ground troops and structures |
| ember_archer / Ember Archer | 3 | 423 | 152 | 1.55 | 2.35 | 6.00 | 1 | Ground ranged; ground/air/structures; projectile speed 16 |
| twin_blades / Twin Blades | 2 | 262 | 58 | 0.72 | 3.35 | 1.20 | 2 | Ground melee; ground troops and structures |
| boulderback / Boulderback | 5 | 1620 | 118 | 2.00 | 1.30 | 1.50 | 1 | Ground; direct structure targets only |
| arc_mage / Arc Mage | 4 | 547 | 120 | 1.00 | 2.00 | 6.50 | 1 | Ground ranged; ground/air/structures; projectile 17; splash radius 2.5 |
| rambeast / Rambeast | 4 | 880 | 140 | 1.40 | 2.60 | 1.45 | 1 | Ground structures only; charged hit 255 after >1.65 s actual movement |
| sky_manta / Sky Manta | 3 | 480 | 77 | 0.92 | 3.05 | 3.40 | 1 | Flying; ground/air/structures; projectile speed 15 |
| vampire_bats / Vampire Bats | 5 | 174 | 86 | 1.05 | 1.95 | 2.00 | 5 | Flying melee; ground/air/structures |
| frost_fang / Frost Fang | 5 | 1155 | 72 | 0.80 | 2.50 | 1.10 | 1 | Ground melee; ground targets; 30% movement slow for 2 s |
| storm_raven / Storm Raven | 6 | 1337 | 251 | 1.70 | 1.18 | 4.30 | 1 | Flying; direct structures only; projectile 18; troop-only aura below |
| archer_tower / Archer Tower | 4 | 850 | 75 | 1.10 | 0 | 7.00 | 1 | Building; troop-only ground/air; projectile 18; 25 s lifetime; 1.65 footprint |
| bullet_burst / Bullet Burst | 2 | — | 175 | — | — | radius 2.20 | — | Spell; 55 structure damage; 7 visual rounds total, **one** damage application |
| nova_flask / Nova Flask | 4 | — | 375 | — | — | radius 3.25 | — | Spell; 185 structure damage |
| meteor_shards / Meteor Shards | 5 | — | 262 + 40×5 | — | — | radius 4.50 | — | Spell; initial troop hit + 5 one-second troop ticks; zero structure damage |

Collision radius for troops is `0.44 * scale`. Scales in roster order: `1,.95,.82,1.22,.96,1.06,1.05,.68,1.02,1.2`; building scale 1, collision radius `1.65*.52=.858`. Unit old `aggro` fields are not active acquisition rules; front/rear sight is 8/5 for all deployable entities.

Twin formation offsets: `(-.42,.12),(.42,-.12)`. Bat formation: `(-.86,-.18),(0,-.42),(.86,-.18),(-.43,.38),(.43,.38)`. Member X is clamped to ±12.2. Each swarm deployment pays once and cycles once. Kill value is card cost divided by member count, never full card cost per Bat.

## Combat, targeting and specials

HP removal is clamped to actual remaining HP. Dead target, finished match and tiebreaker reject ordinary damage. Keep requested overkill separately from actual damage telemetry. No friendly fire. Cooldowns start at zero for troops, 0.35 s for Archer Tower; a building scans every 0.12 s. Damage/reaction/death/status timers use simulation time, including DEV pause/speed. Troop death cleanup is 0.42 s; building cleanup is 1 s. Effects may continue cosmetically after a result but must not issue further game damage.

Facing is forward local -Z in the browser. Front hemisphere is a nonnegative normalized displacement dot facing; rear is negative. Sight adds `source.radius*.2 + target.radius`. Ground-only attackers cannot directly attack flying units. Structures-only units cannot directly select troops; Storm Raven's troop aura is an explicit exception, not air troop direct DPS.

Ordinary troops keep valid troop targets within sight plus 2.25 leash. Visible valid troops outrank structures while travelling. Candidate troop preference is reachable combat distance with a small forward-direction bias `-.06*forward`. Normal structure selection prefers visible defensive buildings, then their lane Guard, then nearest reachable structure. Structure-only selection chooses nearest reachable building/tower.

**Crown Tower hard lock begins upon entering legal attack range**, before waiting for the first attack cooldown. Nearby deployed defenders/buildings cannot pull an attacker after that lock. The lock ends when that tower or attacker dies. Structure-only units also hard-lock a defensive building upon entering attack range. Ordinary units attacking a building do not get permanent building hard lock.

A fresh defender can pull an ordinary traveller targeting a structure before Crown hard lock when reachable distance is within sight + 1.75 + defender radius + attacker radius*.35. It forces that troop target for 1.2 s, clears charge, and limits cooldown to `max(.08,attackInterval*.32)`. The pull breaks if dead, illegal or outside sight + 2.25 + defender radius. A new building pulls a structure-only traveller when no lock exists and building distance is at most existing target distance +.35; normal traveller building pull uses sight +1.75 and cooldown limited to .35 of attack interval. Bridge routing commitments do not imply combat target locks.

Ranged attacks can cross the river if actual horizontal distance is in range. Melee attack range uses a reachable path/combat distance; it cannot hit across inaccessible river geometry. Standard range includes target radius. Homing projectiles record launch distance and duration `max(.11,3Ddistance/speed)`; browser render motion uses smoothstep and an arc. Cancel a projectile whose target dies. Arc Mage applies full primary damage once and full splash damage to other eligible enemies: troop center within 2.5, defensive building within radius + .35 of its radius, tower within radius + .25 of its radius.

Archer Tower damages only enemy troops within range + target radius +.35 and directional sight; it cannot shoot Crown Towers or buildings. It loses 850/25 =34 HP/s to lifetime decay in addition to combat damage. Decay to zero destroys it normally.

Frost Fang slow affects movement only, refreshes nonstacking 30% duration 2 seconds, and applies only to troop victims. Slow strength is maximum existing/new (generic status clamps to 80%); expiry is maximum existing/new expiry. Stun prevents troop movement, acquisition and attack but cooldown still decreases. Storm Raven aura runs every 3 seconds starting 3 seconds after spawn, radius 2 plus victim collision radius, 82 damage and .4-second stun to enemy ground/flying troops only. Aura can continue while the Raven is stunned because it executes before the direct-attack stun gate. Latest status source attribution owns affected time; avoid double-counting overlapping statuses.

Rambeast charge accumulates time only when actual movement exceeds .002 per update, becomes charged after >1.65 s, and delivers 255 against a structure. In attack range charge timer decays .7×dt but charge flag persists until consumed. A pull resets both. Following a charged attack, reset timer/flag; no permanent charged damage.

Nova Flask troop centers must be inside 3.25; structures use radius + structure.radius*.4. Bullet Burst troop inclusion is 2.2 + victim.radius*.2 and structures 2.2 + structure.radius*.25. Its seven VFX rounds are never seven paid hits. Meteor applies 262 once to troop centers within 4.5 + victim.radius*.2 and five 40-damage scheduled ticks at seconds 1,2,3,4,5 inclusive. Each tick tests current membership; entrants receive later ticks, leavers stop receiving ticks. Final tick is preserved for variable step size; no Crown/building damage. Initial and DOT telemetry share one cast ID.

## Placement and navigation

Mouse/drag positions snap to board tile centers produced by floor from arena minimum: half-integer centers, X ±13.5, Z ±20.5. Ordinary Player deployment requires Z≥2.15; Enemy requires Z≤-2.15. A destroyed opponent Guard unlocks **only its lane pocket** across the river: signed opponent-side depth 2.25..9.25 and absolute X 2..13.2 on that side. No Core-adjacent center strip, backfield, opposite lane or intact Guard pocket is legal. Troops validate deployment center; Archer Tower validates all four footprint corners. Buildings cannot intersect any living Tower or building; clearance is old structure radius + new footprint*.55 + .45 for Towers / .35 for buildings. Dead structures do not block.

Spells can target anywhere inside the board and retain snapped center positions. Invalid placement does not spend Aether or cycle a card. Escape/right click/returning a drag to hand cancels. Drag threshold is 7 pixels. Building highlight intersects the 1.65 footprint with grid cells; it highlights 3×3 cells but collision is not a three-tile building. DEV free spawn bypasses ordinary bank/pocket/footprint rules but still requires an in-board center; it costs zero and never alters hand rotation.

Ground units use 8-neighbor A* on one-tile rounded integer nav cells, octile costs 1/1.41421356, no diagonal corner cutting, bounded 1400 expansions. Edge clearance is `.40 + radius*.72`; structure obstacles include source radius + structure radius +.22, excluding dead/current target and allowing start. Nearest legal goal can be sought in rings 1..5. River cells with |Z|<1.65+.28 require a bridge; bridge applicability extends to |Z|≤1.65+.38. Usable bridge half-width is `max(.34,2.1-.16-radius*.92)`.

Select Guard lane bridge when applicable, otherwise cheapest route through either bridge. Commit to a selected crossing while target remains valid; clearing a combat target does not arbitrarily swap bridges midriver. A same-bank target beyond release depth can clear commitment. Explicit crossing path points are at entry ±(1.65+.78), inner bank ±(1.65+.18), center 0, opposite bank and release ±(1.65+.92), at bridge X plus a stable entity-dependent slot. Browser slot `((entityId%5)-2)*.22` is clamped to 58% of usable half-width. Repath on target change, target movement >1.05, interval .68 or illegal next-four path nodes. Collinear paths may be simplified.

Friendly ground bridge congestion adds per-unit `min(.34,.10+max(.7,radius)*.07)` up to1.35 per bridge near |Z|≤3.2. Movement advances waypoints at distance<.34. Actual speed = moveSpeed×(1-slow). Friendly ground soft separation is radius sum +.12 outside bridge, +.03 near bridge; no lateral push in river corridor, longitudinal push .42 factor and lower .8 versus2.1 separation strength. Structure displacement excludes current target. Physical bank correction must keep units inside committed bridge width; never reset repeatedly to bank. Sample stuck progress every .28 seconds, require .055 distance improvement, recover after .82 accumulated seconds with a small legal probe and repath. Flying units move directly and do not use ground obstacle/separation; flight height is cosmetic.

## Match phases, economy and decks

Regulation lasts180 s. Any nonzero crown score at180 ends the match: leader wins, tied nonzero score draws. **Only 0–0 enters120-second overtime**, where the first tower destruction wins. Core destruction wins immediately in any normal phase. On 0–0 overtime expiry, tiebreaker freezes combat, AI, regeneration and deployment. After .85 s all living Crown Towers drain180 absolute HP/s; first zero loses. Simultaneous loss compares initial minimum tower HP, then initial total tower HP, then seeded coin. Tiebreaker awards final1–0 even if Core is the first zero and does not create false card damage/crown attribution.

Start with5 Aether, cap10. First120 elapsed seconds regenerate1/2.8 per second; elapsed120..240 uses2/2.8;240..300 uses3/2.8. Regulation theoretical budget is90.7142857 per side; complete300 seconds197.8571429. Track capacity leakage separately. Native fixed timestep must split exactly at phase/economy thresholds and deliver identical results for equivalent elapsed-time steps. Browser live dt is clamped .05 and DEV speed0..4 is split into ≤1/60 substeps. Native host presentation should not couple wall frame loss or pause to deterministic simulation time.

The Player's opening hand is the first four saved cards, queue remaining four. Playing replaces that hand slot with queue front and appends played card; no opening shuffle. Decks require exactly8 unique known cards. AI coherent deck generation enumerates legal compositions then independently seeded samples a top-score cohort and shuffles. Browser default deck is Ironclad, Ember Archer, Archer Tower, Boulderback, Arc Mage, Rambeast, Sky Manta, Nova Flask. Mid-battle loadout changes affect the next match only.

Five named preset slots `deck-1`..`deck-5`. Save draft is distinct from active saved deck until SAVE. Names preserve Unicode up to24 characters, collapse whitespace and remove controls and unsafe delimiters. Invalid/duplicate/unknown cards cannot replace a valid saved deck. Legacy deck imports slot1 and initializes other defaults. TEST saves a valid draft and launches training after save hydration/write queue; an active match must finish/abandon before new training state replaces it.

Profile defaults: RIFTBOUND; stable ID `RC-` plus6 base36 characters; gems1250; gold8420. Name input permits18 ASCII alphanumeric/spaces/underscore/hyphen. Wins/losses/draws/matches/crowns are nonnegative integers, corrupt input normalized. Current browser counts completed training matches too; abandon is not a completed result. A result increments profile exactly once. Currency does not gain automatically in current mechanics. Preserve unknown import fields and move native settings to versioned native data rather than discard user data.

## AI contracts

Seven personalities: Beatdown, Aggro, Control, Cycle, Split Lane, Spell Cycle, Counter Push. Regular matches seeded-select a style; training defaults Control. Mid-match DEV style change rebuilds that side's coherent AI deck and resets think/bank/anchor/support state. The AI has its own hand and Aether, visible board and observed Player plays only. It must never read Player hidden hand, queue or exact current bank. Estimate Player bank from initial5, observed paid plays and time regeneration capped10. Keep last20 plays and12 cycle entries; a known card becomes likely ready after four subsequent observed plays. Deck matchup inspector may be omniscient only in Developer Lab, not AI inputs.

| Style | Bank | Desperate | Support cap | Reserve | Defense | Punish | Trade tolerance |
|---|---:|---:|---:|---:|---:|---:|---:|
| beatdown |9.9|7.3|3|2.2|1.00|.58|.34|
| aggro |7.8|6.0|2|1.0|.76|1.00|.74|
| control |9.2|6.8|2|2.8|1.22|.50|.16|
| cycle |6.4|5.2|1|.8|.90|.92|.48|
| split |8.0|6.1|1|1.4|.88|1.08|.60|
| spell_cycle |7.4|5.8|1|1.5|1.02|.62|.38|
| counter |9.0|6.5|3|2.5|1.30|.72|.22|

Initial think delay .35 s; later decisions .22+.20×seeded random. Order: finishing spell cycle, observed-bank opposite-lane punish, defense unless deliberate trade, surviving-troop counterpush, support existing push, split-lane pressure, late-lead hold, banked push, desperation, efficient overflow. Spell-cycle considers the weakest surviving tower below34% HP, respects2.2s spell cooldown, and evaluates structure damage/cost. Punish requires bank≥4, estimated opponent bank≤4.4, nonimminent visible pressure,4.5s punish cooldown and a legal opposite lane. Threat depth and HP/value are visible calculations, never hidden card answers. Attackers at enemy-side depth beyond8 or within5.6 of a Crown attack point count imminent. Serious defense threshold is4.4/defendBias. Do not ground-only-counter flying attacks. Reserve and synergy determine supports; survive HP thresholds are26% counter and38% others. Late lead <42 s can bank until9.92; trailing <38 s or overtime can use desperation threshold. All AI deployments use the same legality and paid card cycle as players.

Coherent baseline deck: ≥1 win condition, ≥1 spell, ≥2 sustained anti-air, ≥3 total anti-air, ≥3 defensive troops, ≥1 ranged troop, ≥2 cheap cards. Most styles allow≤2 spells. Beatdown requires heavy win condition,≥2 ranged and average3.625..4.25; Aggro fast win condition/≥3 fast/≥3 cheap/average≤3.875; Control building, defensive score≥65,average≤4.125; Cycle fast win condition/≥4 cheap/average≤3.375; Split≥2 win conditions/≥2fast/≥2cheap/average≤4.125; Spell Cycle≥2 tower-damaging spells/building/≥3cheap/average≤3.875; Counter building/≥2frontline/≥4defensive troops/average≤4.125.

## Deck workshop and card intelligence

Preserve complete14-card collection, exact card stat details including N/A for nonapplicable spell/building stats,8 draft slots,5 named presets, active/draft state, save/test, cost histogram, average cost, deployment HP/DPS, attack range, targeting/role coverage and live updates. HP/DPS aggregate swarm members; spell cards do not distort persistent-unit HP/DPS/range averages. Raven aura is separate from sustained direct anti-air DPS. Ten overlapping composition archetypes are Beatdown, Control, Cycle, Bridge Pressure, Split Lane, Air Pressure, Siege, Spell Control, Defensive, Hybrid; labels are not an exclusive partition.

Pair synergy is symmetric0–100 for all91 unordered pairs, with reasons. Browser baseline32 plus tank/support19, win-condition/troop11, anti-air9 or4, area9, complementary spell11, slow+DOT12, slow+long-range11, aura+front8, swarm+tank7, cheap5, coverage bonus≤10, and penalties for excess cost, double spell, redundant win conditions and ground-only coverage. Counter score is an analytical mechanic estimate based on legal targeting, health/cost, kill/survival time, range, swarm/area, status, building decay and pull. Spell DOT assumes dwell rather than automatic462 damage. These scores are not observed win probabilities. Every card intelligence panel has role, practical uses, best/weak answers, common partners, best defensive answer and offensive partner. Home shows own composition; hidden-opponent matchup stays in DEV.

## Meta Lab full contract

Native background battles must use the same authoritative core, seeded RNG and card data as real matches, on a bounded worker thread. Keep simulation pause during real battle, manual pause/resume,100/250/500 games/min and measured MAX SAFE. Browser measures rolling40 match durations, reserves28% budget and clamps100..2000 games/min in50 increments; adaptive busy backoff and capped debt prevent UI stalls. Emit progress periodically, not massive snapshots every frame. Preserve actual validation job completion through filter/style/reset changes and reject stale pre-reset results.

Dataset identity must include display version, combat model version, deck policy and canonical fingerprint of all gameplay fields/rules/AI. Exclude display art/name/description fields; include collision scale. Browser model `v15-event-2`, deck policy `v15-deck-1`, FNV-1a8hex canonical JSON fingerprint. Native model must have a new identity rather than append to estimator data. Seed and next seed persist. Exploratory deck sampling is independent of observed win rates; no self-amplifying WR population generation. Count every card appearance in both8-card decks: pick rates sum800%, never100%.

Mirror card appearances contribute picks and factual telemetry but are excluded from card win-rate clean N. Result score win1/draw.5/loss0. Raw WR=score/N; adjusted WR=(score+12)/(N+24), neutral24-game prior. Wilson95% raw confidence interval uses z1.96. N=0 has raw WR unavailable, adjusted50%, interval0..100; do not fabricate evidence. Side rotation/seed update ordering must avoid first-update bias. Record rejected/invalid/nonfinite simulations, actual sim wall time and theoretical/actual Aether, duration, crowns and leakage.

Preserve these40 card metric columns with meaningful denominators: pick rate; adjusted/raw WR; CI; clean N; uses/game; Aether/game; troop/tower/building damage/game; damage taken/game; kills/deaths/game; average lifetime; average placement X/Z; average kill value; damage/Aether; tower damage/Aether; survival rate; opening-hand play rate; first-play rate; OT play rate; connection rate; crown contribution; damage prevented/game; building pulls/game; lifetime utilization; spell targets/cast; spell Aether value/cast; overkill/cast; damage/cast; slow uptime; aura damage/game; units stunned/game; stun uptime; zone occupancy; DOT ticks/cast; initial damage/game; DOT damage/game. Telemetry uses actual removed HP; a card play ID owns all swarm/projectile/aura/DOT effects. Distinguish casts from members and targets. “Damage prevented” is absorbed building HP proxy, not invented counterfactual prevention. Crown contribution must not assign tiebreaker/DEV HP changes to cards. Opening opportunities, uses and appearances have distinct denominators. Signed average positions are observations, not recommended placements.

Matchup matrix has14×14 entries, including14 mirror cells and182 directed nonmirror cells; mechanical edge uses counter(a,b)-counter(b,a), not simulated isolated duel WR. Spell-spell has no direct interaction. Include explicit legal-targeting reasons.

Synergy matrix has91 pairs. Clean pair WR excludes an opponent containing both. Pair baseline is average individual adjusted WR. Pair posterior uses48 prior games centered at that baseline; shrunk delta=(raw pair WR-baseline)×N/(N+48). Show raw delta, shrunk delta, raw Wilson CI and N. No positive WR claim for N0. Association can be confounded by deck/style; do not label it causal. Archetype buckets overlap honestly and style buckets cover all7; simultaneous style/archetype filters use actual intersection data. Best/weak lists require cleanN≥30. Contextual cards and matchup/style slices retain Ns.

Confidence-gated signals: WATCH when N≥100, adjusted≥53 with CI lower>50 or adjusted≤47 with CI upper<50; CONCERN N≥400 and CI lower≥52 or upper≤48; STRONG N≥1000 and lower≥54 or upper≤46. Supporting styles needN≥100 and CI excluding50. Signals never automatically rebalance cards.

Trend checkpoints are actual cumulative dataset observations every100 matches, max2000 checkpoints with older thinning.100/500/1k/5k/10k/All history selects recorded extents; do not claim rolling-window WR. Archived datasets keep their original identity, source card library/rules/styles, timestamps and data. Reset archives the old run before creating a blank current run. V12.8/V12.10/V14 historical sections show unavailable/N0 unless real data exists. Legacy `rift_crown_meta_v1210_balance_math` is uncertain legacy data, never cloned into multiple precisely labeled patches. Comparisons show card WR/pick/damage/usage deltas and sample-supported conditioned archetype changes; unavailable metrics remain null. Model changes are named alongside balance changes.

UI preserves Cards, Matchups, Synergies, Archetypes, AI Styles, Signals, Trends, Patches and hidden Validation; numeric sorts, card type/cost/trait/name filters, AI style, archetype, patch and minimum cleanN filters. Export six subjects (cards, matchups, synergies, archetypes, personalities, patch comparisons) as CSV/JSON with active filters and Ns. F9/Ctrl+Shift+V or DEV opens actual10,000-game validation, not a precomputed success screen.

## Recorder, replay and match review

Browser replay format1 includes version/id/createdAt/metadata/duration/result, ordered time+sequence events and sampled states. Metadata records training, name, decks, AI style and rules. Snapshot interval .25 s plus before/after mutations. Record card plays, deployment/spawn, actual damage, death, tower destruction, crowns, slow/stun sources, Aether leak/grants, hand rotations, AI decisions, phase/OT/TB, DEV tower edits/clear/free spawns and result. Every event carries stable IDs and play attribution where applicable. DEV actions are visibly tagged sandbox, paid costs zero where appropriate. Dead entities and completed states remain replayable.

Snapshot includes time, phase, banks, hands/queues, crowns, entity IDs/kinds/cards/team/position/HP/maxHP/target/count/flying/tower kind/status and active hazards. Native replay adds canonical seed, fixed-step/input sequence and sufficient deterministic state. Event playback/seek must not rerun random AI from a current unrelated seed. Browser interpolation uses adjacent sampled positions and exact event application for mutation; bank interpolation is disabled across spending/grant discontinuities. At duplicate timestamps select final before/after state consistently.

Controls: .25/.5/1/2/4×, pause, seek, ±5s, timeline; bookmark first tower damage, first tower destruction, largest push, biggest spell, overtime and result. Missing bookmarks disabled. Closing destroys playback updates. History export/import/delete and abandoned matches work. Browser bounds100k events,16k snapshots,3600-second duration,8 default replays and~3.8MB local storage; import≤25MB with finite/type/prototype/size validation. Native can raise disk capacity but retains version/size validation and safe error feedback.

Postmatch review preserves both teams' Aether spent/leaked, total and tower damage, kills/deaths, cards played, time-weighted average hand cost, crowns, most/least valuable played card, biggest push, best defensive trade and largest bank advantage; per-card table; bank-difference and total living Crown HP charts (including tiebreaker). Card value is actual damage/paid Aether, tower damage breaks ties; don't assign MVP to an unplayed card. Biggest push sums surviving deployed cost fractions×HP fractions across the river. Defensive trade credits per-member enemy kill value on own half minus that cast's cost. Bank extrema must include recorded before/after events. Live crown contribution denominator respects full towerHP and uncredited DEV/TB HP, rather than inventing attribution. Abandon retains replay without profile win/loss credit.

## Persistence and migration

Browser profile API is `/api/save`, JSON `{profile,deck,deckPresets}`, max262144 UTF8 bytes; atomic temporary write then replacement. File location `%LOCALAPPDATA%/RiftCrownArena/player_save.json`. GET missing returns{}. Invalid/oversized/aborted saves do not overwrite prior valid file. Static servers reject traversal, restrict methods, serve correct MIME and bind loopback. Native uses the same dedicated LOCALAPPDATA root, versioned native profile/settings/replay/meta files, atomic replace and backup/recovery, no dependence on origin/CDN/browser server.

Lab `/api/lab` file `lab_v15.json`, max16MB, wrapper schema1 `{meta,replays,savedAt}`; meta internal schema2 and archive metadata. Import wrapped recorder envelopes and legacy local keys. Browser storage keys are `rift_crown_profile_v1`, `rift_crown_deck_v1`, `rift_crown_deck_presets_v14`, `rift_crown_save_pending_v14`, `rift_crown_lab_v15`, `rift_crown_lab_pending_v15`, `rift_crown_replays_v15`. Queue saves using captured snapshots and monotonic revision markers. Only the latest successful write clears pending data; failed retry/late hydration must not overwrite newer local edits, live hand or open draft. Wait hydration before Battle/Training. Defer heavy lab disk writes during battle; finish/leave flush queues. Native migrates valid browser `player_save.json`, preserves profileID/name/results/crowns/wallet/decks, tracks schema upgrades and never damages previous data on bad import.

## Presentation, assets and complete native delivery requirements

Current visual identity is dark navy/cyan/red/gold original Rift mark, floating fantasy diorama with readable grass/stone/wood/water/crystals, two lanes/river/bridges and subtle tile grid. Edge walls/torches/banners/shrubs and distant islands frame combat; side dioramas fill ultrawide without obstructing board. Browser camera43° at `(0,27.5,27.5)`, ACES exposure.88, subtle emissive bloom and fog. Native camera can upgrade presentation while preserving click readability and full arena visibility. Never copy copyrighted commercial game characters/art.

The supplied Blender source exports static multi-mesh GLBs parented to empty roots, color/metal/rough/emission materials and authored primitive detail. Collection names distinguish `twin_blade`/`vampire_bat` models from plural card IDs. It does not supply the requested professional skeletal animation set, production LODs or full native authored material pipeline. Existing meshes/art are reference/identity evidence, not proof of the requested rebuilt assets. Native request requires complete original stylized PBR models, rigs/skeletons/LODs/collision/shadows/correct facing, ten animation states per troop plus specials, coherent high-detail14-card artwork, finished arena, Niagara projectile/cast/impact/status/tower-death effects, controlled bloom and readable HP. No greybox/placeholder may be described as release art.

Preserve Home/Battle/Training/Profile/Cards/Loadout/Meta/Replays/Patch Notes and full Developer Lab. Battle HUD should remain compact: phase/timer/crowns, four cards, next card and Aether; hidden inspector/debug stays opt-in. Native UMG/CommonUI layouts must work16:9/16:10/21:9, common resolutions and scale settings, with large readable text and low battle obstruction. A professionally authored restrained visual system must follow the user's prohibition on AI-looking UI. Browser oversized decorative statistics are not a mandate to repeat clutter.

Developer controls actually work during a battle: pause/resume/speed, each bank ±1/MAX, any friendly/enemy free spawn, towerHP, clear field, AI enable/style change, optional target/sight/frontrear/path/lock/tile debug. Default debug off; no free card cycling or false paid telemetry. Sound/music/UI/master/SFX volume, graphics window/resolution/VSync/FPS/AA/shadows/effects/textures/post/view presets and controls must be wired and persisted.

Complete requested distribution includes UE5 current stable installed source project and Windows x64 Shipping game (no editor), actual DataAssets/Blueprint presentation/level/art/UI/audio, separate .NET8 self-contained WPF/WinUI launcher, verified version/manifest/download/SHA256/size/patch-note updater and repair with rollback/preserved saves, configurable GitHub Releases, installer with prerequisites/Start menu/optional desktop/uninstall preserving saves, game/launcher/updater logs and crash recovery, versioned save schema and import, complete Meta/replay/DEV systems, build/release instructions, release notes and honest QA report. Do not publish success claims or a download that is merely source/greybox/unbuilt scaffolding.

QA acceptance must include exact14-card stats and legal targeting; dormant Core guard-only activation in both live and background; front/rear sight and pre-first-hit hardlock; legal half-integer placement/pocket/full footprint; both bridge congestion/path recoveries; all specials and final DOT tick; exact phase/economy boundaries; seven no-hidden-info AI styles and coherent decks; all controls and filters/export/archives; profile/lab migration/recovery; deterministic input/state/event replay at every speed/seek; UI across aspect ratios; real packaged EXE/launcher/installer/update/repair/rollback; long AI-v-AI and stress soaks (Boulderbacks both bridges, swarms, buildings, Frost/Raven/Meteor interactions). Publish source through the authorized aether-arena repository with evidence distinguishing completed checks from unavailable verification.
