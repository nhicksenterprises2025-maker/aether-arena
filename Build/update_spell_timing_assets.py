"""Update only delayed-spell metadata using the compiled authoritative roster."""
import json
import unreal


definitions = json.loads(unreal.RiftAssetLibrary.card_definitions_json())
expected = {"meteor_shards": 0.75, "bullet_burst": 0.30}
for card in definitions:
    delay = card["castDelay"]
    if abs(delay - expected.get(card["id"], 0.0)) > 1e-7:
        raise RuntimeError("Unexpected spell timing rule for " + card["id"])
    path = "/Game/Rift/Cards/DA_" + card["id"]
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise RuntimeError("Missing card DataAsset: " + path)
    current = asset.get_editor_property("CastDelay")
    if abs(current - delay) > 1e-7:
        asset.set_editor_property("CastDelay", delay)
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
            raise RuntimeError("Could not save timing metadata: " + path)
        unreal.log("Updated Rift spell timing: " + card["id"] + "=" + str(delay))
    if abs(asset.get_editor_property("CastDelay") - delay) > 1e-7:
        raise RuntimeError("Timing metadata did not match the roster: " + path)
unreal.log("All 14 Rift cards match their authoritative spell timing rules.")
