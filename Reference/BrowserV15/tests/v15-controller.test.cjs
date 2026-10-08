const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const{cards,rules,styles}=require('./meta-fixtures.cjs');
test('reset cancels stale validation messages, protects the new dataset, and restarts scheduling',async()=>{
  const{createMetaStore}=await import('../src/meta-store.js'),{createMetaEngine}=await import('../src/meta-engine.js');
  const source=fs.readFileSync(path.join(__dirname,'../src/meta-controller.js'),'utf8').replace(/^import .*;\s*$/gm,'').replace('export function createMetaLabController','function createMetaLabController').replace("new URL('./meta-worker.js',import.meta.url)","'meta-worker.js'");
  let callbacks,latest,worker,tick,clock=0;class FakeWorker{constructor(){worker=this;this.messages=[];}postMessage(message){this.messages.push(message);}terminate(){}emit(message){this.onmessage({data:message});}}
  const c={createMetaStore,createMetaLabUI:options=>{callbacks=options;return{update:view=>latest=view,destroy(){},showValidation(){}};},Worker:FakeWorker,performance:{now:()=>clock},setInterval:callback=>{tick=callback;return 1;},clearInterval(){},setTimeout(){},cards,rules,styles};vm.createContext(c);vm.runInContext(source+'\nthis.lab=createMetaLabController({cards,rules,styles});',c);
  const old=createMetaEngine(cards,{rules,styles});old.runBatch(10);worker.emit({type:'ready',snapshot:old.snapshot(),view:old.view()});c.lab.runValidation();const job=worker.messages.find(m=>m.type==='run');assert.ok(job.validation);callbacks.onReset();const reset=worker.messages.at(-1);assert.equal(reset.type,'reset');
  old.runBatch(4);worker.emit({type:'progress',requestId:job.requestId,completed:28,validation:true,snapshot:old.snapshot(),view:old.view()});assert.equal(c.lab.store.current(),null);assert.equal(latest.status.validation,null);
  const fresh=createMetaEngine(cards,{rules,styles});worker.emit({type:'reset',requestId:reset.requestId,snapshot:fresh.snapshot(),view:fresh.view()});assert.equal(c.lab.store.current().games,0);assert.equal(latest.status.validation,null);assert.ok(c.lab.store.versions().some(v=>v.label==='V15 prior run'&&v.games===10));
  clock=2000;tick();const next=worker.messages.at(-1);assert.equal(next.type,'run');assert.ok(next.requestId>reset.requestId);worker.emit({type:'done',requestId:job.requestId,validation:true,completed:10000,snapshot:old.snapshot(),view:old.view()});assert.equal(c.lab.store.current().games,0);assert.equal(latest.status.validation,null);clock=4000;tick();assert.equal(worker.messages.at(-1).requestId,next.requestId,'Stale done must not release a newer in-flight job');c.lab.destroy();
});

async function controllerHarness(){
  const{createMetaStore}=await import('../src/meta-store.js'),{createMetaEngine}=await import('../src/meta-engine.js');
  const source=fs.readFileSync(path.join(__dirname,'../src/meta-controller.js'),'utf8').replace(/^import .*;\s*$/gm,'').replace('export function createMetaLabController','function createMetaLabController').replace("new URL('./meta-worker.js',import.meta.url)","'meta-worker.js'");
  let callbacks,latest,worker,tick,clock=0;
  class FakeWorker{constructor(){worker=this;this.messages=[];}postMessage(message){this.messages.push(message);}terminate(){}emit(message){this.onmessage({data:message});}}
  const context={createMetaStore,createMetaLabUI:options=>{callbacks=options;return{update:view=>latest=view,destroy(){},showValidation(){}};},Worker:FakeWorker,performance:{now:()=>clock},setInterval:callback=>{tick=callback;return 1;},clearInterval(){},setTimeout(){},cards,rules,styles};
  vm.createContext(context);vm.runInContext(source+'\nthis.lab=createMetaLabController({cards,rules,styles});',context);
  return{lab:context.lab,engine:createMetaEngine(cards,{rules,styles}),get callbacks(){return callbacks;},get latest(){return latest;},get worker(){return worker;},advance(milliseconds){clock+=milliseconds;tick();}};
}

test('validation completion survives a live battle filter snapshot and ordinary scheduling resumes',async()=>{
  const h=await controllerHarness();
  try{
    h.engine.runBatch(5);h.worker.emit({type:'ready',snapshot:h.engine.snapshot(),view:h.engine.view()});h.lab.runValidation(40);
    const job=h.worker.messages.find(message=>message.type==='run');assert.equal(job.count,40);h.lab.setBattleRunning(true);
    h.engine.runBatch(40);const report={...h.engine.validation(),validationRun:{requested:40,completed:40,accepted:40,invalidSimulations:0,nanCount:0}};
    h.worker.emit({type:'done',requestId:job.requestId,validation:true,completed:40,snapshot:h.engine.snapshot(),view:h.engine.view(),report});
    h.callbacks.onFiltersChange({style:'control',archetype:'all',version:'current'});
    h.worker.emit({type:'snapshot',snapshot:h.engine.snapshot(),view:h.engine.view({style:'control'})});
    h.lab.setBattleRunning(false);
    assert.equal(h.latest.status.validation.running,false);assert.equal(h.latest.status.validation.completed,40);assert.equal(h.latest.status.validation.report.validationRun.accepted,40);
    assert.equal(h.lab.store.current().games,45);
    h.advance(2000);const resumed=h.worker.messages.at(-1);assert.equal(resumed.type,'run');assert.ok(resumed.requestId>job.requestId);assert.equal(resumed.validation,undefined);
  }finally{h.lab.destroy();}
});

test('MAX SAFE schedules its advertised 2000 games per minute across sixty one-second timers',async()=>{
  const h=await controllerHarness();
  try{
    const snapshot=h.engine.snapshot(),view=h.engine.view();h.worker.emit({type:'ready',snapshot,view});h.callbacks.onSpeed('max');h.worker.emit({type:'benchmark',maxSafeRate:2000});
    assert.match(h.latest.status.message,/2000\/min/);
    for(let i=0;i<60;i++){
      h.advance(1000);const job=h.worker.messages.at(-1);assert.equal(job.type,'run');assert.ok(job.count>=33&&job.count<=34);
      h.worker.emit({type:'done',requestId:job.requestId,completed:job.count,snapshot,view});
    }
    const scheduled=h.worker.messages.filter(message=>message.type==='run').reduce((sum,message)=>sum+message.count,0);
    assert.ok(Math.abs(scheduled-2000)<=1,`Advertised 2000/min but scheduled ${scheduled}`);
    assert.match(h.latest.status.message,/2000\/min/);
  }finally{h.lab.destroy();}
});
