"""Pin authored import inputs and read their persisted UE source fingerprints.

This helper does not import, modify metadata, or save an Unreal asset.
"""
import hashlib
import json
from pathlib import Path


def pin(root, path):
    root = Path(root).resolve()
    path = Path(path)
    path = (path if path.is_absolute() else root / path).resolve()
    relative = path.relative_to(root).as_posix()
    data = path.read_bytes()
    return {"file": relative, "sha256": hashlib.sha256(data).hexdigest(),
            "bytes": len(data), "md5": hashlib.md5(data).hexdigest()}


def production_sources(root, manifest):
    """Every FBX including LODs, fourteen portraits, and four atlas textures."""
    paths = {}
    def add(record):
        actual = pin(root, record["file"])
        if actual["sha256"] != record["sha256"] or actual["bytes"] != record["bytes"]:
            raise RuntimeError("Authored source differs from manifest: " + record["file"])
        paths[actual["file"]] = actual
    for character in manifest["characters"].values():
        for record in [character["mesh"], *character["lods"], *character["animations"]]:
            add(record)
    for prop in manifest["statics"].values():
        for record in [prop, *prop["lods"]]:
            add(record)
    for record in manifest["illustrations"].values():
        add(record)
    for suffix in ("BaseColor", "Normal", "ORM", "TeamMask"):
        record = pin(root, "Assets/Source/Textures/T_RiftAtlas_" + suffix + ".png")
        paths[record["file"]] = record
    return [paths[path] for path in sorted(paths)]


def freeze(root, manifest, script):
    source = pin(root, manifest["source"]["file"])
    if source["sha256"] != manifest["source"]["sha256"] or source["bytes"] != manifest["source"]["bytes"]:
        raise RuntimeError("Authored Blender source differs from manifest")
    return {"schema": 1, "build": manifest["build"],
            "assetManifest": pin(root, "Assets/asset_manifest.json"),
            "sourceBlend": source, "script": pin(root, script),
            "helperScript": pin(root, __file__),
            "rawSources": production_sources(root, manifest),
            "sourcesUnchanged": False}


def verify_frozen(root, provenance, extra_sources=()):
    records = [provenance[name] for name in ("assetManifest", "sourceBlend", "script", "helperScript")]
    records += provenance["rawSources"] + list(extra_sources)
    for record in records:
        if pin(root, record["file"]) != record:
            raise RuntimeError("Import/audit source changed during execution: " + record["file"])
    provenance["sourcesUnchanged"] = True


def expected_assets(manifest):
    assets = {}
    def add(package, record):
        assets[package + "." + package.rsplit("/", 1)[1]] = record["file"]
    for name, character in manifest["characters"].items():
        destination = "/Game/Rift/Characters/" + name
        add(destination + "/SK_" + name, character["mesh"])
        for clip in character["animations"]:
            add(destination + "/Animations/" + clip["action"], clip)
    for name, prop in manifest["statics"].items():
        add("/Game/Rift/Environment/SM_" + name, prop)
    for name, record in manifest["illustrations"].items():
        add("/Game/Rift/CardArt/T_Card_" + name, record)
    for suffix in ("BaseColor", "Normal", "ORM", "TeamMask"):
        add("/Game/Rift/Textures/T_RiftAtlas_" + suffix,
            {"file": "Assets/Source/Textures/T_RiftAtlas_" + suffix + ".png"})
    return assets


def persisted_source(unreal, root, obj, registry_record, expected_file):
    """Read UE's saved FileMD5, rather than merely hashing today's source file."""
    entry = {"asset": obj.get_path_name(), "expectedSource": pin(root, expected_file),
             "assetImportDataAvailable": False, "importedSources": [], "matches": False}
    import_data = obj.get_editor_property("asset_import_data")
    if import_data is None:
        raise RuntimeError("AssetImportData is unavailable: " + entry["asset"])
    entry["assetImportDataAvailable"] = True
    filenames = list(import_data.extract_filenames())
    # UObject::SourceFileTagName() is "AssetImportData" in UE 5.8. The
    # SourceFile name is not an AssetRegistry tag and returns None.
    tag = unreal.AssetRegistryHelpers.get_tag_value(registry_record, "AssetImportData")
    # UE Python versions can expose bool/out parameters as a tuple or the value.
    if isinstance(tag, tuple):
        tag = next((value for value in tag if isinstance(value, str)), "")
    if not isinstance(tag, str) or not tag.strip():
        raise RuntimeError("Saved AssetImportData fingerprint tag is unavailable: " + entry["asset"])
    source_files = json.loads(str(tag))
    if len(source_files) != len(filenames) or not filenames:
        raise RuntimeError("Saved source filenames/fingerprints are incomplete: " + entry["asset"])
    for filename, stored in zip(filenames, source_files):
        actual = pin(root, filename)
        md5 = str(stored.get("FileMD5", "")).lower()
        entry["importedSources"].append({**actual, "storedMD5": md5,
                                         "matches": md5 == actual["md5"]})
    entry["matches"] = (all(source["matches"] for source in entry["importedSources"])
                        and any(source["file"] == expected_file for source in entry["importedSources"]))
    return entry


def readback(unreal, root, manifest, records):
    records_by_path = {str(record.package_name) + "." + str(record.asset_name): record for record in records}
    result = {"schema": 1, "assets": [], "cards": [], "errors": [], "passed": False}
    for path, expected_file in expected_assets(manifest).items():
        try:
            record = records_by_path[path]
            obj = unreal.EditorAssetLibrary.load_asset(path)
            if not record.is_valid() or obj is None:
                raise RuntimeError("Required imported asset is not loadable/on disk: " + path)
            entry = persisted_source(unreal, root, obj, record, expected_file)
            result["assets"].append(entry)
            if not entry["matches"]:
                result["errors"].append("Persisted import fingerprint differs from current raw source: " + path)
        except Exception as error:
            result["errors"].append(path + ": " + str(error))
    for name, portrait in manifest["illustrations"].items():
        path = "/Game/Rift/Cards/DA_" + name
        try:
            obj = unreal.EditorAssetLibrary.load_asset(path)
            if obj is None:
                raise RuntimeError("Card data is unavailable")
            source = str(unreal.EditorAssetLibrary.get_metadata_tag(obj, "SourceManifest"))
            art = str(unreal.EditorAssetLibrary.get_metadata_tag(obj, "PortraitSHA256"))
            matches = source == manifest["source"]["sha256"] and art == portrait["sha256"]
            result["cards"].append({"cardId": name, "sourceManifest": source,
                                    "portraitSha256": art, "matches": matches})
            if not matches:
                result["errors"].append("Card metadata identifies a different model/portrait: " + name)
        except Exception as error:
            result["errors"].append(path + ": " + str(error))
    result["passed"] = not result["errors"]
    return result
