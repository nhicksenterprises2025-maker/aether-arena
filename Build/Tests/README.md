# Native verification sources

`RiftSimulationTests.cpp` contains the 41 portable regressions, including delayed spell impacts, and the 21 complete seeded-match soak. `Run-NativeSimulationTests.ps1` locates installed Visual Studio C++ x64 tools with `vswhere`, compiles the exact production simulation sources using C++20, and runs that harness. No Unreal runtime or substituted combat model is used. Binaries, object files and generated command files go under ignored `Build/NativeTests`.

Run from the repository root:

```powershell
& Build/Tests/Run-NativeSimulationTests.ps1
```

`Audit-NativeMeta.py` is a read-only Python 3 program using only the standard library. It accepts the dataset, output report, runtime log, final export and optional comparison dataset through CLI paths. It reconstructs the rule/card fingerprint and seed population, checks every retained aggregate/checkpoint/export metric, and compares gameplay fields. Revision 2 comparisons allow only the documented corrected zone exposure; revision 3 comparisons include those channels. A nonzero exit signals a failed check. `--require-complete` also requires the actual target count, native completion log and validated final export, preventing a partial dataset from being reported complete.

```powershell
python Build/Tests/Audit-NativeMeta.py '<dataset.json>' --output '<audit.json>' --log '<native-runtime.log>' --export '<meta-validation.json>' --target 10000 --initial-seed 151515 --require-complete
```

Add `--compare '<baseline-dataset.json>'` to compare the retained gameplay/economy/status totals. The default seed is the production QA seed; a dataset created with another seed must supply its actual initial seed. Generated private datasets and reports stay outside source control. The independent audit distinguishes dataset counts, active core time and descriptive deck/card associations from renderer performance or causal balance evidence.

`Build/Test-Unreal.ps1 -Filter Rift` runs the complete fifteen-test UE suite, including UI, typography, input routing, unit motion, projectiles, delayed-spell replay and native Meta checks. It isolates both native saves and engine settings, and validates the exported automation result rather than inferring success from process startup. `Build/Test-CardDrag.ps1` separately exercises real Slate pointer capture and cancellation against Editor or Shipping builds. Version 1.3.1 requires 32 assertions: the original 29 drag routes plus initial hover, sustained tooltip identity/window/opacity and updated tooltip content after a card cycles. Explicit 1.3.0 runs retain their original 29-check expectation.

`Compare-BankNormalization.ps1` compiles `RiftBankNormalizationWitness.cpp` twice using the same production core: once with the final post-payment zero clamp and once with a generated historical subtraction copy. It never edits production files. The default 25 seeds (one-based cohort ordinals 626–650) demonstrate a rare downstream AI/action/outcome change caused by the numerical fix. Generated source copies, object files, binaries and CSV traces remain under ignored `Build/NativeTests/BankWitness-*`. This is historical comparison evidence, separate from the 36 final-core regressions; differing historical results are expected and must not be reported as identical gameplay.

```powershell
& Build/Tests/Compare-BankNormalization.ps1 -FirstOrdinal 626 -LastOrdinal 650
```

Run aggregate validity without `--compare` to get a standalone validity result. Run a separate strict comparison with `--compare` when evaluating historical cohorts; its nonzero exit reports genuine differences. The final numerical correction cohort passes validity while differing from its historical baseline. Public, path-sanitized reports and that distinction are preserved in `Docs/QA`.
