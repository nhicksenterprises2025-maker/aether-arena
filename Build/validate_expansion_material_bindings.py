"""Exercise the actual importer against cached-slot/reimport fixtures; no UE writes."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
import re
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]


class Material:
    def __init__(self, name):
        self.name = name

    def get_path_name(self):
        return "/Game/Rift/Materials/" + self.name + "." + self.name


class Slot:
    def __init__(self, displayed, imported, material):
        self.properties = {"material_slot_name": displayed,
                           "imported_material_slot_name": imported,
                           "material_interface": material}

    def get_editor_property(self, name):
        return self.properties[name]

    def set_editor_property(self, name, value):
        self.properties[name] = value

    def copy(self):
        result = Slot("", "", None)
        result.properties = dict(self.properties)
        return result


class StructArray:
    """UE reflected array: element pythonization copies structs, not UObject refs."""
    def __init__(self, values):
        self.values = values

    def __iter__(self):
        return (value.copy() for value in self.values)

    def __getitem__(self, index):
        return self.values[index].copy()

    def __setitem__(self, index, value):
        self.values[index] = value.copy()


class Mesh:
    def __init__(self, name, slots):
        self.name, self.slots, self.saved = name, slots, 0

    def get_editor_property(self, name):
        assert name == "materials"
        return StructArray(self.slots)

    def set_editor_property(self, name, value):
        assert name == "materials"
        self.slots = list(value)

    def get_path_name(self):
        return "/Game/Rift/Characters/" + self.name + "/SK_" + self.name


class StaticMesh:
    """Actual UE5.8 probe: static_materials property; no getter Python method."""
    def __init__(self, name, slots, ignore_assignments=False):
        self.name, self.slots, self.saved = name, slots, 0
        self.getter_calls, self.assignments = 0, []
        self.ignore_assignments = ignore_assignments

    def get_editor_property(self, name):
        if name != 'static_materials':
            raise AttributeError("StaticMesh property 'materials' is protected")
        self.getter_calls += 1
        return StructArray(self.slots)

    def set_material(self, index, material):
        self.assignments.append((index, material))
        if not self.ignore_assignments:
            self.slots[index].properties["material_interface"] = material

    def get_material(self, index):
        return self.slots[index].properties["material_interface"]

    def get_path_name(self):
        return "/Game/Rift/Meshes/SM_" + self.name


def validate(source, manifest, native_probe=None):
    tree = ast.parse(source)
    producer = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                    and node.name == "assign_materials")
    report = {}
    scope = {"unreal": SimpleNamespace(SkeletalMesh=Mesh), "REPORT": report,
             "save": lambda mesh: setattr(mesh, "saved", mesh.saved + 1)}
    exec(compile(ast.Module(body=[producer], type_ignores=[]), "<actual assign_materials>", "exec"), scope)
    bind = scope["assign_materials"]
    names = {"M_RiftSurface", "M_RiftGlow", "M_RiftGlass",
             "M_RiftExpansionSurface", "M_RiftExpansionGlow", "M_RiftExpansionGlass"}
    names.update(name for c in manifest['characters'].values() for name in c['materials'])
    names.update(name for s in manifest['statics'].values() for name in s['materials'])
    mats = {name: Material(name) for name in names}
    cases, checks = [], 0

    def require(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise ValueError(message)

    for card_id in ("mini_stampede", "stampede", "pulverizer"):
        names = manifest["characters"][card_id]["materials"]
        for state in ("source_imported_names", "retained_legacy_names", "unknown_cached_names"):
            slots = []
            for index, name in enumerate(names):
                legacy = name.replace("Expansion", "")
                imported = name if state == "source_imported_names" else legacy
                displayed = legacy
                if state == "unknown_cached_names":
                    imported = displayed = "CachedUnknown" + str(index)
                slots.append(Slot(displayed, imported, mats[legacy]))
            mesh = Mesh(card_id, slots)
            bind(mesh, names, mats)
            require([slot.properties["material_interface"].name for slot in mesh.slots] == names,
                    "Cached state bound a wrong palette: " + card_id + "/" + state)
            require(report["expansionMaterialBindings"][mesh.get_path_name()]["passed"], "Readback failed")
            # A subsequent real LOD import may restore old interfaces. Execute
            # the actual producer again against precisely that retained state.
            for slot, name in zip(mesh.slots, names):
                slot.properties["material_interface"] = mats[name.replace("Expansion", "")]
            bind(mesh, names, mats)
            require([slot.properties["material_interface"].name for slot in mesh.slots] == names,
                    "Post-LOD rebind failed: " + card_id)
            require(mesh.saved == 2, "Final binding was not persisted")
            cases.append(card_id + "/" + state + "/post_lod_restore")
    # Slot aliases can arrive out of manifest order; imported identity wins.
    mesh = Mesh("pulverizer", [Slot("M_RiftGlow", "M_RiftExpansionGlow", mats["M_RiftGlow"]),
                                Slot("M_RiftSurface", "M_RiftExpansionSurface", mats["M_RiftSurface"])])
    bind(mesh, ["M_RiftExpansionSurface", "M_RiftExpansionGlow"], mats)
    require([slot.properties["material_interface"].name for slot in mesh.slots] ==
            ["M_RiftExpansionGlow", "M_RiftExpansionSurface"], "Imported identity lost to slot index")
    cases.append("out_of_order_imported_slots")
    for asset_id in ('iron_shield_fragment','shadow_surge_wisp','shadow_surge_ribbon'):
        names = manifest['statics'][asset_id]['materials']
        mesh = StaticMesh(asset_id, [Slot('CachedSlot'+str(i), name, mats['M_RiftSurface'])
                                    for i,name in enumerate(names)])
        bind(mesh, names, mats)
        require(mesh.getter_calls == 1, 'Static readback did not use confirmed static_materials property: '+asset_id)
        require(mesh.saved == 1 and len(mesh.assignments) == len(names), 'Static bindings were not saved exactly once')
        require([mesh.get_material(i).name for i in range(len(names))] == names, 'Static native interfaces are incorrect')
        observed = report['expansionMaterialBindings'][mesh.get_path_name()]
        require(observed['passed'] and all(s['exactInterface'] for s in observed['slots']), 'Static readback acceptance failed')
        require([s['slot'] for s in observed['slots']] == ['CachedSlot'+str(i) for i in range(len(names))]
                and [s['importedSlot'] for s in observed['slots']] == names, 'Static slot metadata lost')
        cases.append(asset_id+'/confirmed_static_materials_property')
    if native_probe is not None:
        native = native_probe['iron_shield_fragment']
        require('static_materials' in native['methods'] and 'get_static_materials' not in native['methods'],
                'Actual engine probe does not confirm the chosen Python API')
        require(isinstance(native['static_materials'],list) and native['static_materials'], 'Actual engine static slots unavailable')
        require('protected' in native['materials'], 'Actual probe must retain failed protected-property evidence')
        slots = []
        for slot in native['static_materials']:
            match = re.search(r"'/Game/Rift/Materials/([^./']+)",slot['material'])
            require(match is not None and match[1] in mats, 'Actual engine material path is not manifest-owned')
            slots.append(Slot(slot['slot'],slot['imported'],mats[match[1]]))
        mesh = StaticMesh('iron_shield_fragment',slots)
        names = manifest['statics']['iron_shield_fragment']['materials']
        bind(mesh,names,mats)
        observed = report['expansionMaterialBindings'][mesh.get_path_name()]
        require(observed['passed'] and mesh.getter_calls==1, 'Actual engine slot data failed production readback')
        require([s['slot'] for s in observed['slots']]==[s['slot'] for s in native['static_materials']] and
                [s['importedSlot'] for s in observed['slots']]==[s['imported'] for s in native['static_materials']],
                'Actual engine slot metadata was altered')
        cases.append('real_native_probe_shield_fragment_slots')
    # This mutation reproduces the retained real UE5.8 import failure.
    broken_static = source.replace('else mesh.get_editor_property("static_materials"))', 'else mesh.get_editor_property("materials"))')
    require(broken_static != source, 'Missing actual static readback branch fixture')
    broken_static_producer = next(node for node in ast.parse(broken_static).body
                                 if isinstance(node,ast.FunctionDef) and node.name=='assign_materials')
    exec(compile(ast.Module(body=[broken_static_producer],type_ignores=[]),'<protected static readback regression>','exec'),scope)
    names = manifest['statics']['iron_shield_fragment']['materials']
    try:
        scope['assign_materials'](StaticMesh('failed_static', [Slot(n,n,mats['M_RiftSurface']) for n in names]),names,mats)
    except AttributeError as exc:
        require('protected' in str(exc), 'Static failure must be the real protected-property regression')
        cases.append('native_failed_import_protected_static_readback/rejected')
    else:
        raise ValueError('Static fixture did not catch actual protected-property regression')
    missing_method = source.replace('else mesh.get_editor_property("static_materials"))','else mesh.get_static_materials())')
    missing_producer = next(node for node in ast.parse(missing_method).body
                            if isinstance(node,ast.FunctionDef) and node.name=='assign_materials')
    exec(compile(ast.Module(body=[missing_producer],type_ignores=[]),'<unexported static getter regression>','exec'),scope)
    try:
        scope['assign_materials'](StaticMesh('missing_method',[Slot(n,n,mats['M_RiftSurface']) for n in names]),names,mats)
    except AttributeError as exc:
        require('get_static_materials' in str(exc), 'Fixture must reproduce the actual unexported-method failure')
        cases.append('native_failed_import_unexported_static_getter/rejected')
    else:
        raise ValueError('Static fixture incorrectly exported the C++-only getter as Python')
    try:
        bind(StaticMesh('ignored_static_assignment', [Slot(n,n,mats['M_RiftSurface']) for n in names],True),names,mats)
    except RuntimeError:
        checks += 1
        cases.append('stale_static_interface/rejected')
    else:
        raise ValueError('Static readback accepted interfaces which were not actually assigned')
    # Leave the original legacy producer byte behavior unchanged: its existing
    # struct edits do not write the reflected element back. Legacy cards are
    # deliberately outside this expansion-only repair.
    mesh = Mesh("legacy", [Slot("M_RiftGlow", "M_RiftExpansionGlow", mats["M_RiftSurface"]),
                            Slot("Unknown", "Unknown", mats["M_RiftGlass"])])
    bind(mesh, ["M_RiftSurface", "M_RiftGlow"], mats)
    require([slot.properties["material_interface"].name for slot in mesh.slots] ==
            ["M_RiftSurface", "M_RiftGlass"], "Legacy reflected-array behavior changed")
    require(mesh.get_path_name() not in report["expansionMaterialBindings"], "Legacy card gained expansion handling")
    cases.append("legacy_reflected_array_behavior_unchanged")
    # Reproduce the real failed import, rather than a reference-sharing list
    # that would incorrectly accept a struct edit with no array write-back.
    broken = source.replace("slots[index] = slot", "# omitted reflected struct write-back")
    broken_producer = next(node for node in ast.parse(broken).body
                          if isinstance(node, ast.FunctionDef) and node.name == "assign_materials")
    exec(compile(ast.Module(body=[broken_producer], type_ignores=[]), "<missing struct write-back fixture>", "exec"), scope)
    try:
        scope["assign_materials"](Mesh("mini_failed_import", [Slot("M_RiftSurface", "M_RiftExpansionSurface", mats["M_RiftSurface"])]),
                                  ["M_RiftExpansionSurface"], mats)
    except RuntimeError:
        checks += 1
        cases.append("native_failed_import_missing_struct_writeback/rejected")
    else:
        raise ValueError("Reflected struct fixture failed to reproduce the retained native failure")
    for name, mesh, material_map in (
        ("missing_expansion_material", Mesh("missing", [Slot("M_RiftSurface", "M_RiftExpansionSurface", mats["M_RiftSurface"])]),
         dict(mats, M_RiftExpansionSurface=None)),
        ("empty_native_slots", Mesh("empty", []), mats)):
        try:
            bind(mesh, ["M_RiftExpansionSurface"], material_map)
        except RuntimeError:
            checks += 1
            cases.append(name + "/rejected")
        else:
            raise ValueError("Invalid fixture was accepted: " + name)
    # Independently inspect every production skeletal LOD-import route. A
    # rebind must be the next statement after the completed loop, before clips.
    routes = 0
    for parent in ast.walk(tree):
        for field in ("body", "orelse"):
            body = getattr(parent, field, [])
            if not isinstance(body, list):
                continue
            for index, node in enumerate(body):
                if not isinstance(node, ast.For) or ast.unparse(node.iter) != "card['lods']":
                    continue
                require(index + 1 < len(body), "LOD loop lacks final binding guard")
                guard = body[index + 1]
                require(isinstance(guard, ast.If) and "M_RiftExpansion" in ast.unparse(guard.test),
                        "Post-LOD rebind must be expansion-only")
                require(any(isinstance(call, ast.Call) and isinstance(call.func, ast.Name)
                            and call.func.id == "assign_materials" for call in ast.walk(guard)),
                        "Post-LOD route has no actual assignment")
                routes += 1
    require(routes == 3, "Selected/PulverizerOnly/Full model routes must all rebind after LODs")
    return {"passed": True, "checks": checks, "fixtureCases": cases, "postLodRoutes": routes,
            "limitation": "Actual producer tested with reflected slots using engine-probed UE5.8 static_materials property; protected property and unexported C++ getter regressions rejected. Optional raw engine-probe slots are replayed without native mutation. All three native static interfaces still require completed fresh import readbacks."}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--importer", type=Path, default=ROOT / "Build/import_unreal_assets.py")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--native-probe", type=Path)
    args = parser.parse_args()
    report = validate(args.importer.read_text(encoding="utf-8"),
                      json.loads((ROOT / "Assets/asset_manifest.json").read_text(encoding="utf-8")),
                      json.loads(args.native_probe.read_text(encoding='utf-8')) if args.native_probe else None)
    if args.native_probe:
        report['nativeProbe']={'path':str(args.native_probe.resolve()),'sha256':hashlib.sha256(args.native_probe.read_bytes()).hexdigest(),
                               'bytes':args.native_probe.stat().st_size,'observedAsset':'iron_shield_fragment',
                               'notYetImported':['shadow_surge_wisp','shadow_surge_ribbon']}
    report["importerSha256"] = hashlib.sha256(args.importer.read_bytes()).hexdigest()
    text = json.dumps(report, indent=2)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
