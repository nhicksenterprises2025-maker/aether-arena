"""Execute the actual cloak producer against strict named-pin UE fixtures.

Checks source graph/clock/connection contracts, not actual GPU compilation.
"""
import argparse,ast,hashlib,json,math
from pathlib import Path
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[1]
class Node:
    def __init__(self,kind='asset',path='/Game/Rift/Materials/M_RiftCloak.M_RiftCloak'):
        self.kind=kind;self.properties={};self.path=path
    def set_editor_property(self,key,value):self.properties[key]=value
    def get_path_name(self):return self.path
class Graph:
    def __init__(self,fail=-1):self.nodes=[];self.connections=[];self.fail=fail
    def delete_all_material_expressions(self,m):self.nodes=[]
    def create_material_expression(self,m,kind,x,y):
        node=Node(kind);self.nodes.append(node);return node
    def set_base_material_usage(self,*a):return True
    def recompile_material(self,m):pass
    def connect_material_expressions(self,a,out,b,inp):
        valid=out=='' and ((b.kind=='Custom' and inp in {n.properties['input_name'] for n in b.properties.get('inputs',[])}) or
                          (b.kind=='Multiply' and inp in {'A','B'}))
        valid=valid and len(self.connections)!=self.fail
        self.connections.append({'kind':'expression','target':b.kind,'input':inp,'succeeded':valid})
        return valid
    def connect_material_property(self,a,out,prop):
        valid=out=='' and len(self.connections)!=self.fail
        self.connections.append({'kind':'property','property':prop,'succeeded':valid});return valid

def run(fail=-1):
    graph=Graph(fail);asset=Node();saved=[];report={}
    U=SimpleNamespace(MaterialEditingLibrary=graph,Material='Material',MaterialFactoryNew=lambda:None,
        MaterialExpressionCustom='Custom',MaterialExpressionConstant3Vector='Constant3',
        MaterialExpressionScalarParameter='Parameter',MaterialExpressionWorldPosition='World',
        MaterialExpressionFresnel='Fresnel',MaterialExpressionMultiply='Multiply',MaterialExpressionConstant='Constant',
        CustomInput=lambda:Node('CustomInput'),LinearColor=lambda *values:values,
        BlendMode=SimpleNamespace(BLEND_MASKED='Masked'),MaterialShadingModel=SimpleNamespace(MSM_DEFAULT_LIT='Lit'),
        MaterialUsage=SimpleNamespace(MATUSAGE_SKELETAL_MESH='Skeletal'),
        CustomMaterialOutputType=SimpleNamespace(CMOT_FLOAT1='Float1'),
        MaterialProperty=SimpleNamespace(MP_BASE_COLOR='BaseColor',MP_ROUGHNESS='Roughness',MP_OPACITY_MASK='OpacityMask',MP_EMISSIVE_COLOR='Emission'))
    lib=SimpleNamespace(load_asset=lambda path:asset,does_asset_exist=lambda path:False)
    tools=SimpleNamespace(create_asset=lambda *args:asset)
    env={'unreal':U,'LIB':lib,'TOOLS':tools,'REPORT':report,'save':lambda value:saved.append(value) or value}
    tree=ast.parse((ROOT/'Build/import_unreal_assets.py').read_text(encoding='utf-8'))
    fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='cloak_material')
    exec(compile(ast.Module(body=[fn],type_ignores=[]),'actual-cloak-producer','exec'),env)
    env['cloak_material']()
    return graph,asset,report,saved

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();out=args.report.resolve();out.relative_to((ROOT/'Artifacts/QA').resolve())
    if out.exists():raise ValueError('Immutable shader proof already exists')
    result={'schema':1,'passed':False,'scope':'Actual Python shader producer and CPU analytical bounds; native compile/GPU pending',
            'importerSHA256':hashlib.sha256((ROOT/'Build/import_unreal_assets.py').read_bytes()).hexdigest(),'checks':[],'errors':[]}
    def check(name,passed):
        result['checks'].append({'name':name,'passed':bool(passed)})
        if not passed:result['errors'].append(name)
    graph,asset,report,saved=run()
    check('Eight real connections are checked and true',len(graph.connections)==8 and all(c['succeeded'] for c in graph.connections) and len(report['cloakMaterial']['connections'])==8)
    check('Only dedicated owner cloak material is saved',len(saved)==1 and saved[0].get_path_name()=='/Game/Rift/Materials/M_RiftCloak.M_RiftCloak')
    check('Masked lit skeletal material',asset.properties=={'blend_mode':'Masked','shading_model':'Lit','two_sided':True})
    check('No wall-clock node',not any('Time' in n.kind for n in graph.nodes))
    custom=next(n for n in graph.nodes if n.kind=='Custom');code=custom.properties['code']
    check('World pore code uses bounded amount and no time',all(text in code for text in ('floor(World/6.0)','clamp(Amount,0.0,0.35)','step(')) and 'Time' not in code)
    check('Thin Fresnel edge exponent 3',next(n for n in graph.nodes if n.kind=='Fresnel').properties=={'exponent':3.,'base_reflect_fraction':0.})
    check('Violet emission bounded by .45',(.27,.09,.45,1) in [n.properties.get('constant') for n in graph.nodes])
    for failure in range(8):
        rejected=False
        try:run(failure)
        except RuntimeError:rejected=True
        check('Reject real false connection '+str(failure),rejected)
    samples=0
    for amount in (0.,.18,.35,1.,-1.):
        for index in range(1250):
            world=(index*.173,index*.349,-index*.081);cell=tuple(math.floor(v/6) for v in world)
            h=(math.sin(sum(a*b for a,b in zip(cell,(12.9898,78.233,37.719))))*43758.5453)%1
            opacity=1. if h>=max(0,min(.35,amount)) else 0.
            check('Deterministic bounded pore '+str(samples),opacity in (0.,1.))
            samples+=1
    result['sampleCount']=samples;result['actualConnections']=graph.connections;result['passed']=not result['errors'];result['checkCount']=len(result['checks'])
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({'passed':result['passed'],'checks':result['checkCount'],'report':str(out)}))
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
