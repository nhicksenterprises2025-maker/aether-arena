# Rift Crown Arena / Aether Arena

Original Windows card battler: fourteen cards, two lanes, deterministic combat,
seven AI personalities, deck workshop, Meta Lab, replays and developer training.

`Reference/BrowserV15` preserves the tested browser game and its gameplay
specification. Run its `start_game.bat` or follow its README.

The Unreal Engine 5.8.2 Windows rebuild is being developed under
`Unreal/RiftCrownArena`, with original asset sources in `Assets`, a separate
self-contained .NET 8 launcher in `Launcher`, and packaging tools in `Build`
and `Installer`. The audited mechanics, art and launcher contracts are in `Docs`.

The native release is still in development. A source commit does not certify a
Shipping build or production QA. Packaged artifacts and measured verification
will be documented when generated; no editor installation will be required to
play the delivered Windows package.

Profile data, private saves, generated engine caches and local verification
files are excluded from this repository. Browser and native saves use
`%LOCALAPPDATA%/RiftCrownArena`; release updates must preserve that directory.
