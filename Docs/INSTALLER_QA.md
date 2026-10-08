# Actual Windows installer verification

The final native Windows distribution completed an actual per-user MSI install, major upgrade, installed WPF Play, repair and uninstall on 2026-10-08 at 21:33:17 UTC. The latest rerun passed **all 51 assertions**, including real Shipping renderer captures, remembered directory checks, and byte-identical preservation of a separate save fixture. These checks used fresh GUID-named workspace paths and refused existing related products or matching shortcuts. No real player save was opened. The exact package includes the final authored FrostFang animation connection.

## Measured evidence

| Evidence | Actual result | Artifact |
| --- | --- | --- |
| Retained actual upgrade baseline | Compact verified MSI; same family, distinct product identity | `Artifacts/QA/ReleaseCandidates/Verified-20261008-211709/RiftCrownArena-Setup.msi` |
| Final MSI compilation | Passed, zero warnings/errors, 3m02.00s; documented validation exceptions | `Artifacts/QA/installer-build.json` |
| Install/upgrade/installed Play/repair/uninstall assertions | 51 passed, zero failures | `Artifacts/QA/installer-tests.json` |
| Actual installed WPF Play | Real routed Play command, all 51 final game files verified, correct bootstrap/native hash and clean exits | `Artifacts/QA/installer-0779215cbf444d1e90e4ee7a36b2e2c9/installed-launcher-play.json` |
| Actual installed native renderer | 1280×720 Home PNG, clean process exit | Same QA root: `installed-native-home.png` |
| Loaded application-local CRTs | MSVCP140, VCRUNTIME140, VCRUNTIME140_1 loaded from the installed native directory, matching manifest hashes | Same QA root: `native-runtime-modules.json` |
| Runtime symbol compatibility | 650 imported CRT symbols verified against the matching release DLL exports across 37 staged PE files | `Artifacts/QA/app-local-runtime.json` |

Both the retained compact baseline and final upgraded player package contain 51 files: the complete game and 20 verified app-local CRT DLL copies. Matching final 236,097,536-byte development symbols are preserved separately under `Artifacts/Symbols/1.0.0/native-981ff75a5c83` instead of downloaded by players. All installed and upgraded files matched their recorded lengths and SHA-256 hashes. The CRT version 14.51.36247.0 came from the redistributable folder associated with the actual 14.51.36231 compiler toolset. The real Shipping process loaded these packaged DLLs rather than the globally installed Visual C++ runtime.

The installed launcher passed its actual WPF smoke check, found the bundled release and validated ten connected command controls. After the major upgrade, its real `PlayButton.Click` launched the exact installed native executable, verified every manifest file, produced a 1280×720 capture and confirmed `RIFT_SAVE_ROOT` environment forwarding through the bootstrap. The launcher hash in that measured Play report matches the exact installed executable. Both the mandatory Start menu shortcut and selected optional desktop shortcut were created and removed. Both native runs used isolated save and engine-user directories, emitted independent Shipping diagnostics, and recorded no game Error/Fatal entries.

Repair restored a deliberately removed, MSI-registered game bootstrap executable with its exact original hash. The per-user installer records and restores its install directory before maintenance costing, and the actual repair/uninstall logs confirm the original isolated directory. This fixed a defect discovered by a repeated maintenance test after adding the required HKCU component key paths. Standard MSI validation now passes with documented exceptions for fixed per-user scope, same-version major upgrades and the private language-neutral font resource. Uninstall removed registered game files, obsolete update releases, interrupted staging, update journals/locks, the launcher and both shortcuts. The separate `player_save.json` fixture remained byte-identical after installation, launcher execution, major upgrade, native startup, repair and uninstall.

## Re-run against the final package

```powershell
./Installer/Build-Installer.ps1 -Version 1.0.0
./Installer/Test-Installer.ps1 -Msi ./Artifacts/QA/ReleaseCandidates/Verified-20261008-211709/RiftCrownArena-Setup.msi -UpgradeMsi ./Artifacts/Installer/RiftCrownArena-Setup.msi
```

These measurements identify the final MSI SHA-256 `00e5228cfffac7c847b73f13a5dca04014c22ca6d5330d2c8671aea6ad520c75`, archive SHA-256 `954d6ec8268860a8f6f616ff8dd9bc6dd9ea2768342c127e76d2290caaa9a46c`, native executable SHA-256 `981ff75a5c837fa74c2a3f1a00c21794fdc418d39504c12ced1ca3331ce63a86`, and launcher SHA-256 `ca533a10c0c125a8ef3f172aa54ea5afdaec0bd9f03fd8d4ce581306e6624215`. The release finalizer matched these hashes to the actual published Play, installed lifecycle and compilation reports at 21:33:33 UTC. The ZIP, MSI, standalone launcher, manifest, combined checksums and `windows-release-verification.json` are in `Artifacts/Release`. Retained candidate MSI files remain QA evidence.

The full final run installs the retained compact verified MSI, upgrades to the distinct newly compiled final product, and performs 51 assertions. Another retained actual same-family product may replace that baseline. Running only the default fresh-install test omits the five major-upgrade assertions and does not satisfy the release finalizer's complete lifecycle requirement.
