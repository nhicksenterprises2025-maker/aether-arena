# Rift Crown Arena 1.4.0 — First Balance Patch & Hog Swarms

Two new hog cards join the collection, bringing the roster to **16 cards**.
The balance changes below are measured against release **1.3.5**. Stats apply
per member unless a full-swarm total is explicitly shown.

## New cards

| Stat | Mini Stampede | Stampede |
| --- | ---: | ---: |
| Aether | 2 | 7 |
| Hogs per deployment | 5 | 15 |
| HP per hog | 95 | 95 |
| Full swarm HP | 475 | 1,425 |
| Damage per hit | 18 | 18 |
| Attack interval | 0.90 s | 1.00 s |
| Basic DPS per hog | 20 | 18 |
| Full swarm basic DPS | 100 | 270 |
| Movement | 3.5 tiles/s | 3 tiles/s |
| Attack range | 1 tile | 0.9 tiles |
| Base model scale | 0.85 | 1.3 |
| Targets | Ground troops and structures | Structures only |

Both are ground melee swarms with solid collision bodies, no projectile and no
splash. Mini Stampede uses smaller wild hogs with restrained equipment; Stampede
uses larger royal hogs with fitted armor and regal details. Their portraits are
rendered from their actual game models. Add either card through the deck workshop;
existing saved decks remain valid.

## Balance changes

| Card | Stat | Previous | Current | Change |
| --- | --- | ---: | ---: | ---: |
| Ironclad | HP | 840 | 822 | −2.14% |
| Ironclad | Hit damage / basic DPS | 96 / 96 | 93 / 93 | −3.13% |
| Ember Archer | HP | 423 | 374 | −11.58% |
| Twin Blades | Hit damage | 58 | 55 | −5.17% |
| Twin Blades | Attack interval | 0.72 s | 0.75 s | +4.17% |
| Twin Blades | Basic DPS per member | 80.56 | 73.33 | −8.97% |
| Arc Mage | Attack interval | 1.00 s | 1.15 s | +15.00% |
| Arc Mage | Basic DPS | 120 | 104.35 | −13.04% |
| Arc Mage | Movement | 2 tiles/s | 1.7 tiles/s | −15.00% |
| Arc Mage | Attack range | 6.5 tiles | 6 tiles | −7.69% |
| Arc Mage | Splash radius | 2.5 tiles | 2.25 tiles | −10.00% |
| Frost Fang | HP | 1,155 | 1,265 | +9.52% |
| Frost Fang | Attack interval | 0.80 s | 0.75 s | −6.25% |
| Frost Fang | Basic DPS | 90 | 96 | +6.67% |
| Archer Tower | Attack interval | 1.10 s | 1.00 s | −9.09% |
| Archer Tower | Basic DPS | 68.18 | 75 | +10.00% |

**DPS = damage per hit ÷ attack interval.** The supplied damage and attack
intervals determine the current values; inconsistent DPS entries in the proposal
have been recalculated. Full-swarm DPS assumes every member continuously attacks
and excludes travel, projectile time and ability damage. Twin Blades now totals
146.67 basic DPS for both members. These are mechanical figures, not observed
win rates or a promise that the whole swarm connects.

All other existing card values and ability parameters remain intact, including
Rambeast's 255-damage charge, Frost Fang's 30% slow for 2 seconds and Storm Raven's
82-damage aura every 3 seconds within 2 tiles with a 0.4-second stun. Meteor
Shards retains its 0.75-second cast, 262 initial troop damage and five 40-damage
ticks. Bullet Burst retains its 0.30-second cast, 175 troop damage, 55 structure
damage and seven visual rounds with one damage application. Nova Flask remains
instant with 375 troop damage and 185 structure damage.

Complete copy-and-paste tables for Google Sheets:
[all card stats](CARD_STATS_1.4.0.tsv) · [all ability stats](ABILITY_STATS_1.4.0.tsv).

## Walking and split lanes

Ground models use planted steps, lifted recovery strides and movement-driven gait
timing. The hogs have their own quadruped locomotion, turns, attacks, deploy, hit,
death and idle poses.

Every swarm member chooses its lane from its **actual landing position after
collision clearance**. A formation placed across the center can split between
left and right. A formation placed wholly on one side stays on that side.
Ground members use that lane's bridge; flying members travel directly. If that
lane's Guard Tower is destroyed, members advance toward the Core while still
responding to nearby eligible defenders and deployed buildings. This applies to
Twin Blades, Vampire Bats and both Stampede cards.

Deployment reserves legal room for the entire formation before spending Aether
or cycling the hand. Fifteen-member formations use radius-aware spacing and
retain the existing ground/air collision, tower clearance and swept movement.

## Tower and path alignment

All four Guard Towers now sit at **X = ±7.2 tiles**, centered on their bridge and
stone lane path. They were at ±8.2 tiles. Both Core Towers remain at X = 0,
centered between the lanes. Guard depth stays 13.4 tiles, Core depth stays 17.3
tiles, and the arena remains 30 × 44 tiles. Collision, deployment clearance and
route planning use the new tower positions.

## Data and Meta Lab

Collection details show current hit intervals, per-member DPS and full-swarm
totals. Card assets validate every balance, targeting and ability field against
the authoritative simulation. The game, background Meta matches, card analysis,
deck workshop, replay snapshots and exported tables use the same definitions.

Meta Lab records a fresh card/rule fingerprint for this patch. Previous datasets
remain historical results and cannot be appended to the new rules. The Stats
Guide explains observed samples and denominators; mechanical card figures remain
separate from measured deck win rates.
