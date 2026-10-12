"""Read-only source/graph/profile checks; does not compile or render Unreal shaders."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
import math
from pathlib import Path
import re
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]


class Node:
    def __init__(self, kind):
        self.kind, self.properties = kind, {}

    def set_editor_property(self, name, value):
        self.properties[name] = value


class Graph:
    def __init__(self, strict=False, reject_connection=False):
        self.material = Node("Material")
        self.nodes, self.edges, self.outputs = [], [], {}
        self.strict, self.reject_connection = strict, reject_connection
        self.connection_returns = []

    def create(self, material, kind, *args):
        node = Node(kind)
        self.nodes.append(node)
        return node

    def connect(self, source, output, target, pin):
        valid = not self.reject_connection
        if self.strict:
            if target.kind == "MaterialExpressionCustom":
                # UE resolves the current inputs array at connection time.
                names = [value.properties["input_name"]
                         for value in target.properties.get("inputs", [])]
                valid &= bool(names) and (pin in names or pin == "")
            elif target.kind == "MaterialExpressionComponentMask":
                # MaterialGraphNode::GetShortenPinName(Input) -> NAME_None.
                valid &= pin == ""
            elif target.kind == "MaterialExpressionMultiply":
                valid &= pin in ("", "A", "B")
            else:
                raise ValueError("Unknown checked UE input contract: " + target.kind)
            valid &= output == ""
        self.connection_returns.append(bool(valid))
        if valid:
            self.edges.append((source, output, target, pin))
        return bool(valid)

    def output(self, source, pin, property_name):
        valid = not self.reject_connection and (not self.strict or pin == "")
        self.connection_returns.append(valid)
        if valid:
            self.outputs[property_name] = source
        return valid


def build_graph(source, shader="shockwave", reject_connection=False):
    """Execute the actual producer function against a graph-recording API."""
    tree = ast.parse(source)
    function = next(n for n in tree.body if isinstance(n, ast.FunctionDef)
                    and n.name == "presentation_material")
    graph = Graph(strict=shader == "shockwave", reject_connection=reject_connection)
    class API:
        def __getattr__(self, name):
            return name
    api = API()
    api.MaterialEditingLibrary = SimpleNamespace(
        delete_all_material_expressions=lambda material: None,
        set_base_material_usage=lambda *args: None,
        create_material_expression=graph.create,
        connect_material_expressions=graph.connect,
        connect_material_property=graph.output,
        recompile_material=lambda material: None)
    for enum in ("BlendMode", "MaterialDomain", "MaterialShadingModel",
                 "MaterialUsage", "CustomMaterialOutputType", "MaterialProperty",
                 "WorldPositionIncludedOffsets"):
        setattr(api, enum, API())
    api.LinearColor = lambda *values: values
    api.CustomInput = lambda: Node("CustomInput")
    report = {}
    scope = {"unreal": api, "REPORT": report,
             "LIB": SimpleNamespace(does_asset_exist=lambda path: True,
                                    load_asset=lambda path: graph.material),
             "TOOLS": None, "save": lambda material: material}
    exec(compile(ast.Module(body=[function], type_ignores=[]),
                 "<actual presentation_material source>", "exec"), scope)
    scope["presentation_material"]("M_RiftShockwave", "MD_DEFERRED_DECAL", shader)
    return graph, report


class Vec:
    def __init__(self, *values):
        self.values = tuple(float(v) for v in values)

    def __mul__(self, scalar):
        return Vec(*(v * scalar for v in self.values))

    __rmul__ = __mul__

    def __sub__(self, scalar):
        return Vec(*(v - scalar for v in self.values))

    def __truediv__(self, scalar):
        return Vec(*(v / scalar for v in self.values))


def clamp(value, lo, hi):
    return min(hi, max(lo, value))


def smoothstep(lo, hi, value):
    t = clamp((value - lo) / (hi - lo), 0, 1)
    return t * t * (3 - 2 * t)


def hlsl_eval(code, inputs):
    """Evaluate the producer's scalar HLSL subset, rather than copy its profile."""
    scope = dict(inputs, length=lambda v: math.sqrt(sum(x*x for x in v.values)),
                 abs=abs, max=max, clamp=clamp, sin=math.sin,
                 saturate=lambda v: clamp(v, 0, 1), smoothstep=smoothstep,
                 step=lambda edge, value: float(value >= edge),
                 float4=lambda *v: tuple(v),
                 float3=lambda *v: Vec(*(v[0].values + (v[1],)
                                       if isinstance(v[0], Vec) else v)),
                 normalize=lambda v: tuple(x / math.sqrt(sum(a*a for a in v.values))
                                           for x in v.values))
    for statement in code.split(";"):
        statement = statement.strip()
        if not statement:
            continue
        if statement.startswith("return "):
            return eval(statement[7:], {"__builtins__": {}}, scope)
        match = re.fullmatch(r"float[234]?\s+(\w+)\s*=\s*(.+)", statement,
                             flags=re.DOTALL)
        if not match:
            raise ValueError("Unsupported shader-source expression: " + statement)
        scope[match[1]] = eval(match[2], {"__builtins__": {}}, scope)
    raise ValueError("Shader-source expression lacks a return")


def validate(source):
    graph, report = build_graph(source)
    checks = 0
    def require(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise ValueError(message)
    require(graph.material.properties["material_domain"] == "MD_DEFERRED_DECAL",
            "Wave must remain a deferred ground decal")
    require(graph.material.properties["shading_model"] == "MSM_DEFAULT_LIT",
            "Surface-normal/roughness decal requires the lit material path")
    require(graph.material.properties["blend_mode"] == "BLEND_TRANSLUCENT",
            "Wave must preserve translucent decal coverage")
    require(set(graph.outputs) == {"MP_OPACITY", "MP_EMISSIVE_COLOR",
            "MP_BASE_COLOR", "MP_NORMAL", "MP_ROUGHNESS"},
            "Wave output graph lacks actual color/normal/roughness connections")
    require(not any(node.kind == "MaterialExpressionTime" for node in graph.nodes),
            "Wave may not use a wall-clock material time node")
    custom = [node for node in graph.nodes if node.kind == "MaterialExpressionCustom"]
    require(len(custom) == 2, "Expected one profile and one normal expression")
    profile = next(node for node in custom if node.properties["output_type"] == "CMOT_FLOAT4")
    normal = next(node for node in custom if node.properties["output_type"] == "CMOT_FLOAT3")
    require(not re.search(r"\bTime\b|\bSceneTexture\b|\bRefraction\b",
                          profile.properties["code"] + normal.properties["code"]),
            "Wave must not depend on scene sampling, refraction, or wall-clock time")
    require(graph.outputs["MP_NORMAL"] is normal, "Normal expression is unbound")
    returns = report["shaderConnections"]["M_RiftShockwave"]
    require(len(returns) == 18 and len(graph.connection_returns) == 18,
            "Every actual wave connection/property return must be recorded")
    require(all(graph.connection_returns) and all(row["connected"] for row in returns),
            "A wave expression connection returned false")
    masks = [node for node in graph.nodes if node.kind == "MaterialExpressionComponentMask"]
    require(len(masks) == 4 and all(any(edge[0] is profile and edge[2] is mask
                                       and edge[1] == edge[3] == ""
                                       for edge in graph.edges) for mask in masks),
            "All four ComponentMasks require the real unnamed UE input pin")
    parameters = {node.properties["parameter_name"]: node.properties["default_value"]
                  for node in graph.nodes if node.kind == "MaterialExpressionScalarParameter"}
    require(parameters == {"FrontWidth": .016, "TrailWidth": .11,
                           "WarpStrength": .035, "RoughnessDip": .08},
            "Unexpected wave parameter/default contract")
    inputs = dict(parameters)
    samples = 0
    for overrides in ({}, {"FrontWidth": -1, "TrailWidth": -1, "WarpStrength": -1, "RoughnessDip": -1},
                      {"FrontWidth": 10, "TrailWidth": 10, "WarpStrength": 10, "RoughnessDip": 10}):
        values = dict(inputs, **overrides)
        for i in range(1201):
            radius = i / 1000
            uv = Vec(.5 + radius * .5, .5)
            alpha, brightness, slope, roughness = hlsl_eval(profile.properties["code"], dict(values, UV=uv))
            require(all(math.isfinite(x) for x in (alpha, brightness, slope, roughness)),
                    "Wave profile contains nonfinite values")
            require(0 <= alpha <= 1 and 0 <= brightness <= 1.75,
                    "Wave opacity/brightness escaped bounds")
            require(abs(slope) <= .080001 and .749999 <= roughness <= .900001,
                    "Floor distortion escaped bounded normal/roughness modulation")
            if radius < .75 or radius >= 1:
                require(alpha == 0 and brightness == 0 and slope == 0,
                        "Wave covers the center or exceeds the simulation radius")
            fw = clamp(values["FrontWidth"], .006, .035)
            if radius <= 1 - 2*fw:
                require(alpha <= .220001, "Trailing band became an opaque disk")
            samples += 1
        for uv in (Vec(.5, .5), Vec(1, .5), Vec(0, .5), Vec(.5, 1)):
            for slope in (-.08, 0, .08):
                result = hlsl_eval(normal.properties["code"], {"UV": uv, "Slope": slope})
                require(all(math.isfinite(x) for x in result), "Floor normal is nonfinite")
                require(abs(sum(x*x for x in result) - 1) < 1e-9, "Floor normal is not normalized")
    lead = hlsl_eval(profile.properties["code"], dict(inputs, UV=Vec(.992, .5)))
    trail = hlsl_eval(profile.properties["code"], dict(inputs, UV=Vec(.9565, .5)))
    require(lead[0] > .99 and lead[1] > 1.7, "Bright leading line is missing")
    require(.2 < trail[0] <= .22 and 0 < trail[1] < lead[1], "Soft trailing band is missing")
    require(report["shaderContracts"]["M_RiftShockwave"]["surfaceDistortion"].endswith("no refraction"),
            "Contract must accurately distinguish normal distortion from refraction")
    tree = ast.parse(source)
    main = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "main")
    wave_calls = [n for n in ast.walk(main) if isinstance(n, ast.Call)
                  and isinstance(n.func, ast.Name) and n.func.id == "presentation_material"
                  and n.args and isinstance(n.args[0], ast.Constant)
                  and n.args[0].value == "M_RiftShockwave"]
    require(len(wave_calls) == 3, "Selected/Full/PulverizerOnly wave producer routes must all exist")
    selected_branch = main.body[1]
    require(isinstance(selected_branch, ast.If), "Selected import branch changed unexpectedly")
    guards = [n for n in selected_branch.body if isinstance(n, ast.If)
              and ast.unparse(n.test) == "'pulverizer' in selected"]
    require(len(guards) == 1 and any(n in wave_calls for n in ast.walk(guards[0])),
            "Selected imports must rebuild only the selected Pulverizer wave material")
    return {"passed": True, "sourceChecks": checks, "profileSamples": samples,
            "graphNodes": len(graph.nodes), "contract": report["shaderContracts"]["M_RiftShockwave"],
            "connectionChecks": returns,
            "pinContractSource": "Installed UE5.8 MaterialEditingLibrary.cpp GetExpressionInputByName/ConnectMaterialExpressions and MaterialGraphNode.cpp GetShortenPinName; CPU API records real boolean returns instead of assuming success.",
            "limitation": "Actual producer graph and HLSL scalar expressions verified against a CPU model of the installed UE pin contract. Actual editor return values are recorded during import. Shader compilation and rendered appearance still require fresh native validation."}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--importer", type=Path, default=ROOT / "Build/import_unreal_assets.py")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    source = args.importer.read_text(encoding="utf-8")
    report = validate(source)
    report["importerSha256"] = hashlib.sha256(args.importer.read_bytes()).hexdigest()
    if args.self_test:
        failures = []
        mutations = {
            "opaque_center": source.replace("float alpha=saturate(lead+0.22*trail);", "float alpha=1.0;"),
            "opaque_trailing_band": source.replace("lead+0.22*trail", "lead+0.9*trail"),
            "missing_normal_connection": source.replace('wave_output(normal, "", unreal.MaterialProperty.MP_NORMAL)', 'wave_output(normal, "", unreal.MaterialProperty.MP_REFRACTION)'),
            "wrong_decal_shading": source.replace('MSM_DEFAULT_LIT if shader == "shockwave"', 'MSM_UNLIT if shader == "shockwave"'),
            "named_component_mask_input": source.replace('wave_connect(custom, "", result, "")', 'wave_connect(custom, "", result, "Input")'),
            "custom_inputs_connected_before_initialization": source.replace('parameters.append((value, parameter))', 'wave_connect(value, "", custom, parameter)'),
            "unchecked_false_connection": source.replace('if not connected:', 'if False:'),
        }
        for name, mutation in mutations.items():
            try:
                if name == "unchecked_false_connection":
                    graph, _ = build_graph(mutation, reject_connection=True)
                    if not graph.outputs:
                        raise ValueError("False-return fixture produced a disconnected graph")
                else:
                    validate(mutation)
            except (ValueError, StopIteration, RuntimeError):
                failures.append(name)
        if len(failures) != len(mutations):
            raise ValueError("Shader-source mutation rejection failed")
        report["rejectedMutations"] = failures
        try:
            build_graph(source, reject_connection=True)
        except RuntimeError:
            report["falseNativeConnectionReturnRejected"] = True
        else:
            raise ValueError("Actual producer did not reject the simulated native false return")
    text = json.dumps(report, indent=2)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
