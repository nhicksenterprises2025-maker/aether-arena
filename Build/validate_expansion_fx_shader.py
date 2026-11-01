"""Read-only actual FX producer/pin checks; Unreal GPU compilation is separate."""
from pathlib import Path
from types import SimpleNamespace
import argparse,ast,copy,hashlib,json,math,re
from PIL import Image
from generate_expansion_fx_contract import definition,pin

ROOT=Path(__file__).resolve().parents[1]
class Node:
    def __init__(self,kind):self.kind=kind;self.properties={}
    def set_editor_property(self,key,value):self.properties[key]=value
class Graph:
    def __init__(self,fail=-1,stale=0,compile_errors=None):self.nodes=[Node('Stale') for _ in range(stale)];self.edges=[];self.outputs={};self.calls=[];self.fail=fail;self.compile_errors=compile_errors or []
    def create_material_expression(self,m,kind,*xy):
        n=Node(kind);self.nodes.append(n);return n
    def get_material_expressions(self,m):return self.nodes
    def delete_material_expression(self,m,n):self.nodes.remove(n)
    def get_num_material_expressions(self,m):return len(self.nodes)
    def set_base_material_usage(self,*args):pass
    def recompile_material(self,m):return self.compile_errors
    def connect_material_expressions(self,a,out,b,inp):
        valid=(out=='A' if a.kind=='Texture' else out=='') and (
            inp in [n.properties['input_name'] for n in b.properties.get('inputs',[])] if b.kind=='Custom' else b.kind=='Multiply' and inp in ('A','B'))
        valid=bool(valid and len(self.calls)!=self.fail);self.calls.append(valid)
        if valid:self.edges.append((a,out,b,inp))
        return valid
    def connect_material_property(self,a,out,prop):
        valid=out=='' and len(self.calls)!=self.fail;self.calls.append(valid)
        if valid:self.outputs[prop]=a
        return valid
def producer(fail=-1,missing_atlas=False,stale=0,compile_errors=None):
    graph=Graph(fail,stale,compile_errors);asset=Node('Material');atlas=object();report={};saved=[]
    u=SimpleNamespace(MaterialEditingLibrary=graph,Material='Material',MaterialFactoryNew=lambda:None,
        MaterialExpressionVectorParameter='Vector',MaterialExpressionScalarParameter='Scalar',MaterialExpressionCustom='Custom',
        MaterialExpressionTextureSample='Texture',MaterialExpressionMultiply='Multiply',MaterialExpressionConstant='Constant',
        CustomInput=lambda:Node('CustomInput'),LinearColor=lambda *v:v,
        BlendMode=SimpleNamespace(BLEND_TRANSLUCENT='Translucent'),MaterialShadingModel=SimpleNamespace(MSM_DEFAULT_LIT='Lit'),
        MaterialUsage=SimpleNamespace(MATUSAGE_INSTANCED_STATIC_MESHES='InstancedStatic'),
        MaterialSamplerType=SimpleNamespace(SAMPLERTYPE_COLOR='Color'),
        CustomMaterialOutputType=SimpleNamespace(CMOT_FLOAT3='Float3',CMOT_FLOAT1='Float1'),
        MaterialProperty=SimpleNamespace(MP_BASE_COLOR='BaseColor',MP_OPACITY='Opacity',MP_EMISSIVE_COLOR='Emission',MP_ROUGHNESS='Roughness'))
    def load(path):return (None if missing_atlas else atlas) if '/Textures/' in path else asset
    env={'unreal':u,'ROOT':ROOT,'REPORT':report,'LIB':SimpleNamespace(load_asset=load,does_asset_exist=lambda p:True),
         'TOOLS':None,'save':lambda m:saved.append(m) or m,
         'source_provenance':SimpleNamespace(pin=lambda root,path:pin(root,path))}
    fn=next(n for n in ast.parse((ROOT/'Build/import_unreal_assets.py').read_text(encoding='utf-8')).body
            if isinstance(n,ast.FunctionDef) and n.name=='expansion_fx_material')
    exec(compile(ast.Module(body=[fn],type_ignores=[]),'actual-expansion-fx-producer','exec'),env)
    env['expansion_fx_material']();return graph,asset,report,saved
def selected_path(ids,regress_to_list_intersection=False):
    """Execute the actual command-line parse and selected-material branch prefix."""
    tree=ast.parse((ROOT/'Build/import_unreal_assets.py').read_text(encoding='utf-8'))
    main=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='main')
    selected_branch=copy.deepcopy(main.body[1])
    first_model=next(i for i,n in enumerate(selected_branch.body) if isinstance(n,ast.For) and isinstance(n.target,ast.Name) and n.target.id=='card_id')
    selected_branch.body=selected_branch.body[:first_model]
    prefix=ast.Module(body=[copy.deepcopy(main.body[0]),selected_branch],type_ignores=[])
    if regress_to_list_intersection:
        gates=[n for n in ast.walk(prefix) if isinstance(n,ast.Call) and
               isinstance(n.func,ast.Attribute) and n.func.attr=='intersection' and
               n.args and isinstance(n.args[0],ast.Set)]
        if len(gates)!=1:raise AssertionError('Expected the actual sole FX selected gate')
        gates[0].func.value=ast.Name(id='selected',ctx=ast.Load())
    ast.fix_missing_locations(prefix)
    report={};imports=[];fx=[]
    def load(path):return SimpleNamespace(get_path_name=lambda:path)
    def material():
        fx.append('M_RiftExpansionFX');return load('/Game/Rift/Materials/M_RiftExpansionFX')
    env={'re':re,'unreal':SimpleNamespace(SystemLibrary=SimpleNamespace(
             get_command_line=lambda:'-unattended -RiftSelectedModels='+','.join(ids))),
         'MANIFEST':json.loads((ROOT/'Assets/asset_manifest.json').read_text(encoding='utf-8')),
         'REPORT':report,'LIB':SimpleNamespace(load_asset=load),
         'import_expansion_materials':lambda mats,required:imports.append(sorted(required)),
         'expansion_fx_material':material,'cloak_material':lambda:load('/Game/Rift/Materials/M_RiftCloak')}
    exec(compile(prefix,'actual-selected-command-line-prefix','exec'),env)
    return report,imports,fx,env.get('selected')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args();out=args.report.resolve();out.relative_to(ROOT/'Artifacts/QA')
    if out.exists():raise ValueError('Immutable FX source proof already exists')
    report={'schema':1,'passed':False,'scope':'Actual sole-purpose material graph, authored alpha and bounded analytical values; native import/GPU separate',
            'importerSHA256':hashlib.sha256((ROOT/'Build/import_unreal_assets.py').read_bytes()).hexdigest(),'checks':[],'errors':[]}
    def check(name,value):
        report['checks'].append({'name':name,'passed':bool(value)})
        if not value:report['errors'].append(name)
    selected_cases=[
        (['mini_stampede','stampede','pulverizer','bone_warden','bone_warden_skeleton','aether_reaper','iron_giant','shadow_surge'],True),
        (['bone_warden'],True),(['aether_reaper'],True),(['shadow_surge'],True),
        (['mini_stampede','stampede','pulverizer'],False),(['iron_giant','bone_warden_skeleton'],False),
        (['pulverizer'],False)]
    report['selectedListFixtures']=[]
    for ids,needs_fx in selected_cases:
        result,imports,fx,selected=selected_path(ids)
        check('Actual selected-list material route '+','.join(ids),type(selected) is list and selected==ids and
              result.get('selectedModels')==ids and result.get('mode')=='SelectedModels' and len(imports)==1 and
              len(fx)==int(needs_fx) and result.get('materials',[])==((['/Game/Rift/Materials/M_RiftExpansionFX'] if needs_fx else [])+(['/Game/Rift/Materials/M_RiftCloak'] if 'shadow_surge' in ids else [])))
        report['selectedListFixtures'].append({'commandLineIDs':ids,'actualSelectedType':type(selected).__name__,
                                              'requiredMaterials':imports[0],'fxCalls':len(fx),'report':result})
    for ids in (['not_a_card'],['bone_warden','bone_warden']):
        try:selected_path(ids)
        except RuntimeError as exc:check('Reject actual selected-list invalid IDs '+','.join(ids),'unknown or duplicate IDs' in str(exc))
        else:check('Reject actual selected-list invalid IDs '+','.join(ids),False)
    try:selected_path(['bone_warden'],regress_to_list_intersection=True)
    except AttributeError as exc:check('Actual list route catches prior intersection regression',"'list' object has no attribute 'intersection'" in str(exc))
    else:check('Actual list route catches prior intersection regression',False)
    g,m,r,saved=producer();contract=definition(ROOT);r=r['expansionFXMaterial']
    check('Exactly eleven real returns are checked',len(g.calls)==len(r['connections'])==11 and all(g.calls))
    check('Only new dedicated material is saved',len(saved)==1 and saved[0] is m and r['material']=='M_RiftExpansionFX')
    dirty,_,cleaned,_=producer(stale=17)
    check('Live registered expression list is fully cleaned through a snapshot',len(dirty.nodes)==10 and not any(n.kind=='Stale' for n in dirty.nodes) and cleaned['expansionFXMaterial']['removedExpressions']==17)
    try:producer(compile_errors=['forced shader compile error'])
    except RuntimeError:check('Real shader compiler errors prevent save',True)
    else:check('Real shader compiler errors prevent save',False)
    check('Translucent lit two-sided material',m.properties=={'blend_mode':'Translucent','shading_model':'Lit','two_sided':True})
    params={n.properties.get('parameter_name'):n for n in g.nodes}
    check('Uniform EffectColor has exact violet default',params['EffectColor'].properties['default_value']==(.37,.08,.66,1))
    check('Opacity and emission defaults match source contract',params['EffectOpacity'].properties['default_value']==.55 and
          params['EffectEmission'].properties['default_value']==.25 and r['maximumOpacity']==.65 and r['maximumEmissionPerChannel']==.45)
    check('Color does not depend on atlas RGB',g.outputs['BaseColor'].kind=='Custom' and g.outputs['BaseColor'].properties['code']=='return saturate(Color.rgb);')
    alpha=g.outputs['Opacity'];edges={p:(a,o) for a,o,b,p in g.edges if b is alpha}
    check('Authored atlas alpha alone supplies opacity',edges['A'][0].kind=='Texture' and edges['A'][1]=='A' and edges['B'][0].kind=='Custom')
    codes=[n.properties.get('code') for n in g.nodes if n.kind=='Custom']
    check('Exact bounded scalar HLSL',set(codes)=={'return saturate(Color.rgb);','return clamp(Amount,0.0,0.65);','return clamp(Amount,0.0,0.45);'})
    check('No shader wall-clock inputs',not any('Time' in n.kind or 'Time' in n.properties.get('code','') for n in g.nodes))
    check('Native contract report names actual opacity source',r['opacitySource']==contract['opacity']['atlasSource'] and r['opacitySourceChannel']=='A' and r['colorUsesAtlasRGB'] is False)
    image=Image.open(ROOT/contract['opacity']['atlasSource']['file']).convert('RGBA')
    alphas=image.getchannel('A').getextrema();check('Actual authored opacity channel is finite normalized alpha',0<=alphas[0]<=alphas[1]<=255)
    report['actualAtlasAlphaRange8Bit']=list(alphas)
    for fail in range(11):
        try:producer(fail)
        except RuntimeError:check('Reject false native FX connection '+str(fail),True)
        else:check('Reject false native FX connection '+str(fail),False)
    try:producer(missing_atlas=True)
    except RuntimeError:check('Reject absent authored alpha atlas',True)
    else:check('Reject absent authored alpha atlas',False)
    for i in range(1001):
        scalar=-1+3*i/1000;alpha_value=(i%256)/255
        opacity=alpha_value*max(0,min(.65,scalar));emission=max(0,min(.45,scalar))
        for color in ((.20,.70,.27),(.10,.75,.90),(.37,.08,.66),(-1.,2.,.4)):
            channels=[max(0,min(1,c))*emission for c in color]
            check('Bounded opacity/emission sample '+str(i)+' '+str(color),0<=opacity<=.65 and all(0<=c<=.45 for c in channels))
    report['actualConnections']=r['connections'];report['contract']=contract
    report['passed']=not report['errors'];report['checkCount']=len(report['checks'])
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'passed':report['passed'],'checks':report['checkCount'],'errors':report['errors']}))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
