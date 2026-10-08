# Native verification sources

`RiftSimulationTests.cpp` contains the 36 portable regressions and the 21 complete seeded-match soak. `Run-NativeSimulationTests.ps1` locates installed Visual Studio C++ x64 tools with `vswhere`, compiles the exact production simulation sources using C++20, and runs that harness. No Unreal runtime or substituted combat model is used. Binaries, object files and generated command files go under ignored `Build/NativeTests`.

Run from the repository root:

```powershell
& Build/Tests/Run-NativeSimulationTests.ps1
```

`Audit-NativeMeta.py` is a read-only Python 3 program using only the standard library. It accepts the dataset, output report, runtime log, final export and optional comparison dataset through CLI paths. It reconstructs the rule/card fingerprint and seed population, checks every retained aggregate/checkpoint/export metric, and compares gameplay fields. Revision 2 comparisons allow only the documented corrected zone exposure; revision 3 comparisons include those channels. A nonzero exit signals a failed check. `--require-complete` also requires the actual target count, native completion log and validated final export, preventing a partial dataset from being reported complete.

```powershell
python Build/Tests/Audit-NativeMeta.py '<dataset.json>' --output '<audit.json>' --log '<native-runtime.log>' --export '<meta-validation.json>' --target 10000 --initial-seed 151515 --require-complete
```

Add `--compare '<baseline-dataset.json>'` to compare the retained gameplay/economy/status totals. The default seed is the production QA seed; a dataset created with another seed must supply its actual initial seed. Generated private datasets and reports stay outside source control. The independent audit distinguishes dataset counts, active core time and descriptive deck/card associations from renderer performance or causal balance evidence.

`Build/Test-Unreal.ps1 -Filter Rift` runs the nine maintained UE tests in `Private/Tests/RiftIntegrationTests.cpp` and the native Meta subsystem. It isolates both native saves and engine settings, and validates the exported automation result rather than inferring success from process startup.
