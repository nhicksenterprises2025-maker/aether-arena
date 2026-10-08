# Actual Windows installer verification

The provisional native Windows distribution completed an actual per-user MSI install, major upgrade, repair and uninstall on 2026-10-08. The latest rerun passed **all 45 assertions**, including a real installed Shipping renderer capture, remembered directory checks, and byte-identical preservation of a separate save fixture. These checks used fresh GUID-named workspace paths and refused existing related products or matching shortcuts. No real player save was opened.

## Measured evidence

| Evidence | Actual result | Artifact |
| --- | --- | --- |
| Baseline MSI compilation | Passed, zero warnings/errors, 4m49s | `Artifacts/QA/InstallerPackages/RiftCrownArena-Provisional-Baseline.msi` |
| Runtime-complete MSI compilation | Passed, zero warnings/errors, 3m04s | `Artifacts/QA/InstallerPackages/RiftCrownArena-Provisional-Runtime.msi` |
| Standard-validation MSI compilation | Passed, zero warnings/errors, 2m52s; documented validation exceptions | `Artifacts/QA/installer-build.json` |
| Install/upgrade/repair/uninstall assertions | 45 passed, zero failures | `Artifacts/QA/installer-tests.json` |
| Actual installed native renderer | 1280×720 Home PNG, clean process exit | `Artifacts/QA/installer-e7df281b9b13442ba76e235eaadba079/installed-native-home.png` |
| Loaded application-local CRTs | MSVCP140, VCRUNTIME140, VCRUNTIME140_1 loaded from the installed native directory, matching manifest hashes | Same QA root: `native-runtime-modules.json` |
| Runtime symbol compatibility | 650 imported CRT symbols verified against the matching release DLL exports across 37 staged PE files | `Artifacts/QA/app-local-runtime.json` |

The baseline contained 32 real native-package files. The upgrade added 20 verified DLL copies: ten release CRT DLLs beside each executable, producing a 52-file game inventory. All installed and upgraded files matched their recorded lengths and SHA-256 hashes. The CRT version 14.51.36247.0 came from the redistributable folder associated with the actual 14.51.36231 compiler toolset. The real Shipping process loaded these packaged DLLs rather than the globally installed Visual C++ runtime.

The installed launcher passed its actual WPF smoke check, found the bundled release and validated ten connected command controls. Both the mandatory Start menu shortcut and selected optional desktop shortcut were created and removed. The installed game rendered with isolated save and engine-user directories, emitted independent Shipping diagnostics, and recorded no game Error/Fatal entries.

Repair restored a deliberately removed, MSI-registered game bootstrap executable with its exact original hash. The per-user installer records and restores its install directory before maintenance costing, and the actual repair/uninstall logs confirm the original isolated directory. This fixed a defect discovered by a repeated maintenance test after adding the required HKCU component key paths. Standard MSI validation now passes with documented exceptions for fixed per-user scope, same-version major upgrades and the private language-neutral font resource. Uninstall removed registered game files, obsolete update releases, interrupted staging, update journals/locks, the launcher and both shortcuts. The separate `player_save.json` fixture remained byte-identical after installation, launcher execution, major upgrade, native startup, repair and uninstall.

## Re-run against the final package

```powershell
./Installer/Build-Installer.ps1 -Version 1.0.0
./Installer/Test-Installer.ps1
# A distinct actual MSI in the same upgrade family may be supplied with -UpgradeMsi.
```

The measurements above apply to provisional package identity and installer behavior. The final camera/UI/restaged game must regenerate the release inventory/archive and MSI, then rerun the actual installer checks. The current test also invokes the installed launcher's actual `PlayButton.Click` against the installed native package, validates bootstrap/child exit codes and the native capture, and proves `RIFT_SAVE_ROOT` environment forwarding with isolated engine configuration. That additional installed Play check is pending the final rebuilt installer; the separately published launcher already passed it against the provisional game. The provisional MSI files are QA evidence and are not release assets.
