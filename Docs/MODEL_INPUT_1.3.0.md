# Model, typography and card input update

The fourteen-card roster keeps its production mesh, rig and card identities.
`Build/generate_assets.py` calls `Build/upgrade_card_models.py` to construct
coherent costume layers and equipment around the existing hand bones. Weapons
share their hand weights, so grips follow the same animation transform rather
than moving as detached accessories. Character-specific faces, fitted armor,
creature anatomy and the four building/spell models replace stacked detail
passes. Gameplay measurements, placement rules and card statistics remain in
the existing deterministic simulation.

`Build/render_card_portraits.py` opens the production Blender scene and renders
the actual meshes with Cycles, denoising and card-specific lighting. Humanoid
portraits frame faces and equipment more closely. The art is rendered from the
same source as the game models. `Build/render_animation_review.py` renders all
eleven skeletal identities and additional humanoid attack poses for equipment
inspection. The manifest, independent asset audit and portrait audit pin the
source scene, raw exports and final images.

The UI uses four bundled Barlow Semi Condensed weights. The unmodified upstream
TrueType files, provenance and OFL license are in `Assets/Fonts/BarlowSemiCondensed`.
Imported font-face assets use inline loading, and `RiftTypography::Font` supplies
the composite font to menus, battle HUD and overlays at their existing sizes.
The packaged game contains the OFL license and requires no installed system font.
See [the font provenance and usage notes](UI_TYPOGRAPHY.md).

`URiftHandButton` retains Slate pointer capture while a card moves. A small
movement threshold distinguishes selection clicks from drags. Once crossed,
returning to the starting point remains a drag cancellation. Release coordinates
come from the mouse event and are checked against the actual safe arena area.
A legal release deploys once; hand, HUD, invalid or outside releases cancel.
Right click, Escape, lost capture or focus, pause and a finished match also cancel.
Cancelling leaves the card and Aether bank intact. Existing click placement,
keyboard shortcuts, confirmation and the Drag Deploy setting remain available.
Captured hand releases own their deployment transaction. Ordinary viewport input
ignores a held hand card, so an earlier queued mouse release cannot deploy a
newer drag. The runtime smoke reproduces that rapid event order through Slate.

`Build/Test-CardDrag.ps1` drives actual Slate mouse and key routing through the
production UMG hand using isolated fixtures. It requires twenty-nine checks,
clean exit, the requested resolution, and unchanged source/runtime hashes.
`Rift.Integration.Typography` verifies embedded font bytes, weights, measured
glyphs and visible text across menus and battle views. The release evidence
packager requires these current results alongside model/portrait audits, reviewed
Shipping captures, audio/VFX/performance and installed launcher verification.

This update retains Meteor Shards' 0.75-second fall and Bullet Burst's 0.30-second
flight. The six authoritative simulation source files are unchanged from 1.2.1;
the earlier 100-match spell cohort keeps its original execution provenance.
The 1.3.0 portable regression and complete native integration suite are executed
against the current sources. No historical instant-spell 10,000-match cohort is
represented as a new model/input audit.
