"""Exercise release guard failures in synthetic isolated files; never run Unreal."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import uuid

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Build"))
from generate_card_manifest import balance_hash


def digest(path):
    data = path.read_bytes()
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}


def snapshot(directory):
    return {path.relative_to(directory).as_posix(): digest(path)
            for path in sorted(directory.rglob("*")) if path.is_file()}


def save_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qa-directory", type=Path)
    parser.add_argument("--powershell", default=shutil.which("pwsh"))
    args = parser.parse_args()
    if not args.powershell:
        parser.error("PowerShell 7 is required; supply --powershell.")
    qa = (args.qa_directory or ROOT / "Artifacts/QA/BalanceReleaseGuard" / uuid.uuid4().hex).resolve()
    if qa.exists():
        parser.error("Use a fresh QA directory; existing evidence is preserved.")
    qa.mkdir(parents=True)
    template = qa / "Template"
    template.mkdir()
    before = load_json(ROOT / "Assets/Data/Baselines/1.5.0.json")
    before.update(version="9.9.0", status="baseline", source="Synthetic release guard fixture")
    after = copy.deepcopy(before)
    after.update(version="9.9.1", status="approved")
    after["cards"][0]["damage"] += 7
    after["balanceHash"] = balance_hash(after)
    resolution = {"schema": 1, "status": "approved", "unresolved": False,
                  "mechanicNotes": ["Synthetic release guard fixture only."]}
    save_json(template / "before.json", before)
    save_json(template / "after.json", after)
    save_json(template / "resolution.json", resolution)
    (template / "DefaultGame.ini").write_text(
        "[Unrelated.Section]\nProjectVersion=0.0.0\n"
        "[/Script/EngineSettings.GeneralProjectSettings]\nProjectVersion=9.9.1\n", encoding="utf-8")
    generation_commands = [
        [sys.executable, "-B", str(ROOT / "Build/generate_card_manifest.py"),
         "--manifest", str(template / "after.json"), "--output", str(template / "cards.inc"), "--require-approved"],
        [sys.executable, "-B", str(ROOT / "Build/balance_patch_notes.py"),
         "--before", str(template / "before.json"), "--after", str(template / "after.json"),
         "--resolution", str(template / "resolution.json"), "--output", str(template / "notes.json"),
         "--cpp-output", str(template / "notes.inc")],
    ]
    for command in generation_commands:
        result = subprocess.run(command, cwd=ROOT, text=True, encoding="utf-8", errors="replace", capture_output=True)
        if result.returncode:
            raise RuntimeError("Fixture generation failed: " + result.stdout + result.stderr)

    source_paths = [ROOT / path for path in (
        "Build/Tests/Test-BalanceRelease.py", "Build/Validate-BalanceRelease.ps1",
        "Build/generate_card_manifest.py", "Build/balance_patch_notes.py",
        "Build/Build-Unreal.ps1", "Build/Package-WindowsRelease.ps1", "Build/Finalize-WindowsRelease.ps1")]
    source_pins = {path.relative_to(ROOT).as_posix(): digest(path) for path in source_paths}
    cases = []

    def run_case(name, mutation=None, expected_error=None, version="9.9.1"):
        case = qa / name
        shutil.copytree(template, case)
        if mutation:
            mutation(case)
        original = snapshot(case)
        command = [args.powershell, "-NoLogo", "-NoProfile", "-NonInteractive", "-File",
                   str(ROOT / "Build/Validate-BalanceRelease.ps1"), "-Version", version,
                   "-Manifest", str(case / "after.json"), "-Baseline", str(case / "before.json"),
                   "-Resolution", str(case / "resolution.json"), "-GeneratedDefinitions", str(case / "cards.inc"),
                   "-Notes", str(case / "notes.json"), "-CppNotes", str(case / "notes.inc"),
                   "-DefaultGame", str(case / "DefaultGame.ini"), "-PythonExecutable", sys.executable]
        result = subprocess.run(command, cwd=ROOT, text=True, encoding="utf-8", errors="replace", capture_output=True)
        transcript = result.stdout + result.stderr
        inputs_unchanged = snapshot(case) == original
        passed = (result.returncode == 0 if expected_error is None
                  else result.returncode != 0 and expected_error in transcript)
        proof = None
        if result.returncode == 0:
            proof = json.loads(result.stdout)
            passed = passed and proof["passed"] is True and proof["inputsUnchanged"] is True
        (qa / (name + ".log")).write_text(transcript, encoding="utf-8")
        record = {"name": name, "passed": passed and inputs_unchanged,
                  "exitCode": result.returncode, "inputsUnchanged": inputs_unchanged,
                  "expectedError": expected_error, "inputPins": original, "guardProof": proof}
        cases.append(record)
        if not record["passed"]:
            raise AssertionError(name + " failed: " + transcript)

    def change_json(case, filename, mutation):
        path = case / filename
        data = load_json(path)
        mutation(data)
        save_json(path, data)

    run_case("approved-fixture")
    run_case("pending-manifest", lambda case: change_json(case, "after.json", lambda data: data.update(status="pending")), "Release blocked")
    run_case("pending-resolution", lambda case: change_json(case, "resolution.json", lambda data: data.update(status="pending", unresolved=True)), "Release blocked")
    run_case("unresolved-string", lambda case: change_json(case, "resolution.json", lambda data: data.update(unresolved="false")), "Release blocked")
    run_case("stale-canonical-hash", lambda case: change_json(case, "after.json", lambda data: data["cards"][0].update(damage=data["cards"][0]["damage"] + 1)), "Canonical balanceHash is stale")

    def change_generated_value(case):
        path = case / "cards.inc"
        original = path.read_text(encoding="utf-8")
        damage = after["cards"][0]["damage"]
        changed = original.replace(f"card.damage = {damage};", f"card.damage = {damage + 1};", 1)
        if changed == original or "// Balance SHA256: " + after["balanceHash"] not in changed:
            raise AssertionError("Generated-value fixture must retain the matching hash comment.")
        path.write_text(changed, encoding="utf-8")

    run_case("modified-generated-value-matching-comment", change_generated_value, "Native definitions are not synchronized")
    run_case("modified-generated-arena-rule", lambda case: (case / "RiftArenaRules.generated.inc").write_text(
        (case / "RiftArenaRules.generated.inc").read_text(encoding="utf-8").replace("HalfWidth = 15", "HalfWidth = 14", 1), encoding="utf-8"), "Native arena rules are not synchronized")
    run_case("stale-approved-json-notes", lambda case: change_json(case, "notes.json", lambda data: data["panels"][0]["changes"][0].update(new=123456)), "Approved patch notes are missing or stale")
    run_case("stale-approved-cpp-notes", lambda case: (case / "notes.inc").write_text((case / "notes.inc").read_text(encoding="utf-8") + "// stale body\n", encoding="utf-8"), "Approved patch notes are missing or stale")
    run_case("missing-cpp-notes", lambda case: (case / "notes.inc").unlink(), "Approved release definitions/notes are missing")
    run_case("requested-version-mismatch", version="9.9.2", expected_error="Release version mismatch")
    run_case("project-version-mismatch", lambda case: (case / "DefaultGame.ini").write_text("[/Script/EngineSettings.GeneralProjectSettings]\nProjectVersion=9.9.2\n", encoding="utf-8"), "Release version mismatch")
    run_case("stale-baseline-hash", lambda case: change_json(case, "before.json", lambda data: data["cards"][0].update(damage=data["cards"][0]["damage"] + 1)), "Canonical balanceHash is stale")
    run_case("changed-resolution-stale-notes", lambda case: change_json(case, "resolution.json", lambda data: data["mechanicNotes"].append("Changed fixture mechanic note.")), "Approved patch notes are missing or stale")
    wiring = []
    for name, first_mutation in (("Build/Build-Unreal.ps1", "RunUAT.bat"),
                                 ("Build/Package-WindowsRelease.ps1", "Move-Item -LiteralPath $source"),
                                 ("Build/Finalize-WindowsRelease.ps1", "Copy-Item -LiteralPath $launcherSource")):
        source = (ROOT / name).read_text(encoding="utf-8-sig")
        passed = source.index("Validate-BalanceRelease.ps1") < source.index(first_mutation)
        wiring.append({"name": name + " validates before release mutation", "passed": passed})
        if not passed:
            raise AssertionError(name + " release guard must precede release writes/process launch.")
    current_pins = {path.relative_to(ROOT).as_posix(): digest(path) for path in source_paths}
    if current_pins != source_pins:
        raise AssertionError("Release guard source changed during fixture verification.")
    report = {"schema": 1, "passed": True, "fixtureCases": len(cases), "wiringChecks": len(wiring),
              "nativeEngineLaunched": False, "packagingLaunched": False,
              "scope": "Synthetic isolated release guard cases; actual pending balance files were not changed.",
              "sourcePins": source_pins, "sourcesUnchanged": True, "cases": cases, "wiring": wiring}
    save_json(qa / "verification.json", report)
    print(json.dumps({"passed": True, "fixtureCases": len(cases), "wiringChecks": len(wiring), "report": str(qa / "verification.json")}))


if __name__ == "__main__":
    main()
