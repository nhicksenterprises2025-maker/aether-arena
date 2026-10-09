# Rift Crown Arena UI typeface

The native interface uses **Barlow Semi Condensed**, designed by Jeremy Tribby
and the Barlow Project Authors. The Regular, Medium, SemiBold and Bold static
TrueType faces are bundled with the game. The existing main menu, collection,
battle dock, button and panel layouts retain their positions and dimensions.
`RiftTypography::Font(weight, size)` selects a named face at the existing font
size for both UMG widgets and Slate battlefield annotations.

The files were downloaded unmodified from the [Barlow upstream repository](https://github.com/jpt/barlow)
at commit `dc2940e2e04ef4ec96c07e23e0f02aefbddd343b`. Source hashes, byte lengths
and exact download URLs are recorded in
`Assets/Fonts/BarlowSemiCondensed/provenance.json`. The family is distributed
under the [SIL Open Font License 1.1](https://github.com/jpt/barlow/blob/dc2940e2e04ef4ec96c07e23e0f02aefbddd343b/OFL.txt).
The complete original copyright notice and license are retained in
`Assets/Fonts/BarlowSemiCondensed/OFL.txt`. An identical text file is staged as
`RiftCrownArena/Content/Rift/Fonts/Barlow-OFL.txt` in Windows packages through
the module's NonUFS runtime dependency.

`Build/import_unreal_fonts.py` verifies all five source files against their
provenance, imports only four font-face assets under `/Game/Rift/Fonts`, then
saves and reads back `/Game/Rift/Fonts/F_RiftUI`. Its faces use **Inline** loading
so the font bytes are part of the cooked assets. A player does not need to
install the fonts or keep any developer TTF path available.

Rebuilding requires an Editor build followed by this narrow Python import with
the Unreal Editor commandlet. Rebuild Shipping after import. The independent
`Rift.Integration.Typography` automation test checks the loaded UFont, named
faces, embedded data lengths, Slate text measurement and font selection on
menu, collection, editing, dropdown and battle widgets. Shipping writes a
`UI_TYPEFACE` diagnostic containing the loaded asset path and face counts for
packaged verification. Font readability and wrapping still require actual
screenshots at supported window sizes before a release is accepted.
