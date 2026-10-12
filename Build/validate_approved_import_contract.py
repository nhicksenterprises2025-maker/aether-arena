"""Execute the new material/stride bindings with isolated UE pin/slot fixtures."""
from pathlib import Path
from types import SimpleNamespace
import argparse,ast,hashlib,json,math
from validate_shockwave_shader import Graph,Node
from validate_expansion_material_bindings import Material,Slot,Mesh

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'Build/import_unreal_assets.py'
TREE=ast.parse(SOURCE.read_text(encoding='utf-8'))
class API:
    def __getattr__(self,name):return name
def functions(names,env):
    nodes=[n for n in TREE.body if isinstance(n,ast.FunctionDef) and n.name in names]
    exec(compile(ast.Module(body=nodes,type_ignores=[]),'actual-approved-producer','exec'),env)
def graph_for(name,glass=False,fail=None):
    graph=Graph();api=API();report={}
    def connect(a,out,b,inp):
        valid=not (b.kind=='MaterialExpressionCustom' and inp not in [v.properties['input_name'] for v in b.properties.get('inputs',[])])
        if fail==inp and b.kind=='MaterialExpressionMultiply':valid=False
        return graph.connect(a,out,b,inp) if valid else False
    def output(a,out,prop):return False if fail=='Opacity' and prop=='MP_OPACITY' else graph.output(a,out,prop)
    api.MaterialEditingLibrary=SimpleNamespace(delete_all_material_expressions=lambda *a:None,
        set_base_material_usage=lambda *a:None,create_material_expression=graph.create,
        connect_material_expressions=connect,connect_material_property=output,recompile_material=lambda *a:None)
    for enum in ('MaterialUsage','MaterialSamplerType','CustomMaterialOutputType','MaterialProperty','BlendMode'):
        setattr(api,enum,API())
    api.CustomInput=lambda:Node('CustomInput');api.LinearColor=lambda *v:v
    env={'unreal':api,'REPORT':report,'LIB':SimpleNamespace(does_asset_exist=lambda p:True,load_asset=lambda p:graph.material),
         'TOOLS':None,'save':lambda m:m}
    functions({'material'},env);env['material'](name,*[object() for _ in range(4)],glass=glass)
    return graph,report
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();out=args.report.resolve();out.relative_to(ROOT/'Artifacts/QA')
    if out.exists():raise ValueError('Immutable producer fixture already exists')
    report={'schema':1,'passed':False,'scope':'Actual new Python producers under isolated reflected-array and shader-pin fixtures; native import/GPU separate',
            'importerSHA256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'checks':[],'errors':[]}
    def check(name,value):
        report['checks'].append({'name':name,'passed':bool(value)})
        if not value:report['errors'].append(name)
    g,r=graph_for('M_RiftExpansionAscensionGlass',True)
    parameters={n.properties.get('parameter_name'):n for n in g.nodes}
    alpha=parameters['RiftBaseOpacity'];opacity=g.outputs['MP_OPACITY']
    check('Actual lower-tip opacity default is .68',alpha.properties['default_value']==.68)
    incoming={pin:source for source,_,target,pin in g.edges if target is opacity}
    check('Actual new translucent graph multiplies dissolve by opacity',opacity.kind=='MaterialExpressionMultiply' and
          incoming['B']==alpha and incoming['A'].kind=='MaterialExpressionCustom')
    check('New cloth producer checks all three added connection returns',r['reaperClothOpacity']['checkedConnections']==3)
    check('No Material Time node in cloth graph',not any('Time' in n.kind for n in g.nodes))
    for pin in ('A','B','Opacity'):
        try:graph_for('M_RiftExpansionAscensionGlass',True,pin)
        except RuntimeError:check('Reject false native lower-tip '+pin+' return',True)
        else:check('Reject false native lower-tip '+pin+' return',False)
    for name in ('M_RiftGlass','M_RiftExpansionGlass'):
        old,_=graph_for(name,True)
        check(name+' retains accepted opacity path',not any(n.properties.get('parameter_name')=='RiftBaseOpacity' for n in old.nodes) and
              old.outputs['MP_OPACITY'].kind==('MaterialExpressionConstant' if name=='M_RiftGlass' else 'MaterialExpressionCustom'))
    M=json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8'))
    env={'unreal':SimpleNamespace(SkeletalMesh=Mesh),'REPORT':{},'save':lambda mesh:None}
    functions({'assign_materials'},env)
    names={n for c in M['characters'].values() for n in c['materials']}
    mats={name:Material(name) for name in names|{'M_RiftSurface','M_RiftGlow','M_RiftGlass'}}
    for card in ('bone_warden','bone_warden_skeleton','aether_reaper','iron_giant'):
        expected=M['characters'][card]['materials']
        mesh=Mesh(card,[Slot('CachedUnknown'+str(i),name,mats['M_RiftSurface']) for i,name in enumerate(expected)])
        env['assign_materials'](mesh,expected,mats)
        check(card+' actual reflected struct writeback preserves new palette',
              [s.properties['material_interface'].name for s in mesh.slots]==expected)
    e={'math':math};functions({'finite_float','animation_contract_properties'},e)
    for card,scale in (('mini_stampede',1.3),('stampede',.85)):
        c=M['characters'][card]['modelDesign']['animationContract'];values=e['animation_contract_properties'](c,scale)
        expected=c['locomotion']['strideMeters']
        check(card+' approved canonical scale preserves exact reference gait',abs(values['GaitStrideMeters']-expected)<1e-9)
    reaper=M['characters']['aether_reaper']['modelDesign'];values=e['animation_contract_properties'](reaper['animationContract'],1.1,reaper['flightBobTiles'])
    check('Actual Reaper authored bob bound is .08 logical tiles',values['FlightBobTiles']==.08)
    check('Null gait stride safely binds zero',e['animation_contract_properties']({'locomotion':{'strideMeters':None}},1)['GaitStrideMeters']==0)
    report['passed']=not report['errors'];report['checkCount']=len(report['checks'])
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'passed':report['passed'],'checks':report['checkCount'],'errors':report['errors']}))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
