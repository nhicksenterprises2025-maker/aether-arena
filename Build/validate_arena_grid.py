"""Read-only producer/125cm grid checks; actual UE compilation remains separate."""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
import math
from pathlib import Path
import re
from types import SimpleNamespace
from validate_shockwave_shader import Graph, Node, clamp, smoothstep

ROOT = Path(__file__).resolve().parents[1]


class Vec:
    def __init__(self, *values): self.values = tuple(values)
    @property
    def xy(self): return Vec(*self.values[:2])
    @property
    def x(self): return self.values[0]
    @property
    def y(self): return self.values[1]
    def __add__(self, value): return Vec(*(x+value for x in self.values))
    def __sub__(self, value): return Vec(*(x-value for x in self.values))
    def __mul__(self, value): return Vec(*(x*value for x in self.values))
    def __truediv__(self, value): return Vec(*(x/value for x in self.values))
    def __abs__(self): return Vec(*(abs(x) for x in self.values))


def evaluate(code, world, tile):
    scope = dict(World=Vec(*world), TileSize=tile, Base=Vec(1.,1.,1.), Gain=1., Grid=1.,
                 abs=abs, min=min, max=max, smoothstep=smoothstep,
                 saturate=lambda value: clamp(value,0.,1.),
                 frac=lambda vector: Vec(*(v-math.floor(v) for v in vector.values)),
                 fwidth=lambda vector: Vec(.25,.25),
                 length=lambda vector: math.sqrt(sum(v*v for v in vector.values)))
    for statement in code.split(';'):
        statement=statement.strip()
        if statement.startswith('return '):
            return eval(statement[7:],{'__builtins__':{}},scope).values
        if statement:
            match=re.fullmatch(r'float[234]?\s+(\w+)=(.+)',statement)
            if not match: raise ValueError('Unsupported actual grid expression: '+statement)
            scope[match[1]]=eval(match[2],{'__builtins__':{}},scope)
    raise ValueError('Grid expression has no result')


def producer(source, reject=False):
    tree=ast.parse(source);functions=[node for node in tree.body if isinstance(node,ast.FunctionDef)
                                     and node.name in ('material','arena_tile_size','finite_float')]
    graph=Graph();report={}
    class API:
        def __getattr__(self,name): return name
    api=API()
    def connect(a,b,c,d):
        if c.kind=='MaterialExpressionCustom':
            names=[value.properties['input_name'] for value in c.properties.get('inputs',[])]
            if d not in names: return False
        if reject and d=='TileSize': return False
        return graph.connect(a,b,c,d)
    api.MaterialEditingLibrary=SimpleNamespace(delete_all_material_expressions=lambda material:None,
        set_base_material_usage=lambda *args:None,create_material_expression=graph.create,
        connect_material_expressions=connect,connect_material_property=graph.output,
        recompile_material=lambda material:None)
    for enum in ('MaterialUsage','MaterialSamplerType','CustomMaterialOutputType','MaterialProperty','BlendMode'):
        setattr(api,enum,API())
    api.LinearColor=lambda *values:values;api.CustomInput=lambda:Node('CustomInput')
    scope={'ROOT':ROOT,'re':re,'math':math,'unreal':api,'REPORT':report,'TOOLS':None,
           'LIB':SimpleNamespace(does_asset_exist=lambda path:True,load_asset=lambda path:graph.material),
           'save':lambda material:material}
    exec(compile(ast.Module(body=functions,type_ignores=[]),'<actual arena material producer>','exec'),scope)
    scope['material']('M_RiftArena',*[object() for _ in range(4)])
    return graph,report


def validate(source):
    graph,report=producer(source);checks=0
    def require(value,message):
        nonlocal checks
        checks+=1
        if not value: raise ValueError(message)
    parameters={node.properties.get('parameter_name'):node for node in graph.nodes
                if node.kind=='MaterialExpressionScalarParameter'}
    tile=parameters['RiftTileSize'].properties['default_value']
    require(tile==125.,'Grid must share the approved native125cm tile')
    require(len(report['arenaGridConnections'])==6 and all(row['connected'] for row in report['arenaGridConnections']),
            'All five actual Custom inputs and BaseColor property returns must be checked')
    field=graph.outputs['MP_BASE_COLOR'];code=field.properties['code']
    require([item.properties['input_name'] for item in field.properties['inputs']]==['Base','World','Grid','Gain','TileSize'],
            'Grid Custom inputs are missing or initialized out of order')
    require('World.xy/tile' in code and '*tile' in code and 'Time' not in code,'Grid size must use the explicit parameter')
    for x in range(-12,12):
        for z in range(-17,17):
            center=evaluate(code,((x+.5)*tile,(z+.5)*tile,0),tile)
            require(all(abs(v-1.)<1.e-8 for v in center),'A tile center is covered by a seam')
            edge=evaluate(code,(x*tile,(z+.5)*tile,0),tile)
            require(all(abs(v-.42)<1.e-8 for v in edge),'An authoritative tile edge lacks its grid seam')
    for size in (100.,125.,150.):
        require(evaluate(code,(size*.5,size*.5,0),size)==(1.,1.,1.),'Runtime tile size override broke centers')
        require(all(abs(v-.42)<1.e-8 for v in evaluate(code,(size,size*.5,0),size)),
                'Runtime tile size override broke boundaries')
    try:producer(source,reject=True)
    except RuntimeError:checks+=1
    else:raise ValueError('Producer accepted a false native TileSize connection return')
    return {'passed':True,'checks':checks,'tilesSampled':24*34,'worldUnitsPerTile':tile,
            'actualProducerConnections':report['arenaGridConnections'],
            'limitation':'CPU execution of actual material producer and scalar HLSL subset. Native grid parameter/instance spacing, shader compilation and GPU appearance require fresh ArenaOnly import and capture.'}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--report',type=Path);args=parser.parse_args()
    path=ROOT/'Build/import_unreal_assets.py';source=path.read_text(encoding='utf-8');report=validate(source)
    report['importerSha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    text=json.dumps(report,indent=2)
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text+'\n',encoding='utf-8')
    print(text)


if __name__=='__main__':main()
