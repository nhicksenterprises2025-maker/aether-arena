# Rift Crown Arena 1.4.0 — First Balance Patch & Hog Swarms

The first balance patch adds **Mini Stampede** and **Stampede**, bringing the collection to 16 cards. Mini Stampede is a fast group of plain wild hogs; Stampede fields a larger, crowned and armored herd that targets structures.

| Card | Aether | Hogs | HP per hog | Hit damage | Hit interval | DPS per hog | Full swarm DPS | Movement |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Mini Stampede | 2 | 5 | 95 | 18 | 0.90 s | 20 | 100 | 3.5 tiles/s |
| Stampede | 7 | 15 | 95 | 18 | 1.00 s | 18 | 270 | 3 tiles/s |

Ironclad, Ember Archer, Twin Blades, Arc Mage, Frost Fang and Archer Tower receive balance changes. Basic DPS is calculated from **hit damage ÷ attack interval**. Twin Blades now deals 73.33 DPS per member, Arc Mage 104.35 DPS, Frost Fang 96 DPS and Archer Tower 75 DPS. Full swarm DPS assumes every surviving member can keep attacking; travel and ability damage are separate. Existing spells and special ability values remain intact.

Ground troops have rebuilt walking cycles with planted steps and lifted recovery. Movement controls the gait, and pausing preserves the walking pose. Both new hog cards have model-rendered portraits and their own movement and combat animations.

Each swarm member chooses its lane from its **actual landing X position after collision clearance**. Deploying across the center can split a group between both lanes. Ground troops use their side's bridge; flying troops take a direct route. When that lane's Guard Tower is destroyed, troops advance toward the Core while still responding to eligible defenders. The entire formation must fit before the game spends Aether and cycles the card.

Collection details, deck analysis, replay telemetry and Meta Lab use the updated definitions. Meta Lab starts a fresh rules fingerprint for this patch; previous datasets remain historical results. The Stats Guide explains the measured samples and what the Meta statistics mean.

All four Guard Towers are now centered on their bridge and stone path at 7.2 tiles from the arena center. The Core Towers remain centered between the lanes. The 30 × 44 arena and existing tower depths are retained, with collision and routing checked against the aligned positions.

Read the [complete patch notes](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/PATCH_NOTES_1.4.0.md), [all card stats](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/CARD_STATS_1.4.0.tsv) and [all ability stats](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/ABILITY_STATS_1.4.0.tsv). Both tab-separated tables can be pasted directly into Google Sheets.

## Run the game

Download **RiftCrownArena-Setup.msi**, install it and open **RiftCrownArena** from the Start menu. Select **Play** in the launcher.

For a portable installation, extract the entire **RiftCrownArena-Windows-x64-1.4.0.zip** and open **RiftCrownArena.exe**. Keep the extracted folder together. The standalone **RiftCrownLauncher.exe** can also download or update the complete game through **Check for Updates**.

Select a card or press **1–4**, then click a legal tile or drag the card onto it. Return a dragged card to the hand to cancel; **right click** cancels selection. **Escape** opens the battle menu. Close the game before updating or repairing.

## Verification

The complete eighteen-test Editor suite passed, along with all 63 portable scenarios and 21 complete seeded AI matches. A fresh 100-game Meta cohort passed 172,980 independent checks with zero invalid matches or economies. The final Shipping review inspected eleven actual frames at 1080p and 720p, covering both hog cards, center splits, same-lane Core advance, tower/path alignment and the updated UI. Still images establish those observed poses and layouts; native and coordinate-trace checks verify movement and replay timing.

Current source checks passed: 1,336 model/asset checks, 423 portrait provenance checks and 135 walking checks. All 16 current portraits were inspected individually. The 72-frame walking contact sheet covers all nine ground cards. The actual imported asset audit and native animation checks passed against the final Editor runtime.

An isolated 30-second Shipping stress run after eight seconds of warmup at 1920×1080 measured 1,788 frames and 130 peak entities on a Ryzen 7 5700 / RTX 5060. Mean frame time was 16.788 ms (about 59.6 FPS), p95 16.922 ms and p99 17.231 ms. Five mass-deployment spikes exceeded 50 ms; the maximum was 56.735 ms. These are measured results on this host, rather than a frame-rate guarantee for every PC.

The real WPF Play button launched the exact final 49-file game package. All 51 installer lifecycle checks passed from 1.3.5 through 1.4.0 upgrade, installed Play, repair and uninstall, with isolated saves preserved. MSI compilation passed standard ICE validation without warnings or errors.

The [current balance QA record](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/QA/balance-patch-1.4.0.json) and **RiftCrownArena-BalancePatch-QAEvidence-1.4.0.zip** contain the executed acceptance evidence. [Release QA history](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/RELEASE_QA.md), [public download proof](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/QA/public-download-verification.json) and [launcher update proof](https://github.com/nhicksenterprises2025-maker/aether-arena/blob/main/Docs/QA/published-update-check.json) remain separate records. Complete anonymous download and default-latest update checks are recorded separately after publication.
