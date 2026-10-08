const test=require('node:test');
const assert=require('node:assert/strict');
const path=require('node:path');
const {Worker}=require('node:worker_threads');
const {cards,rules,styles}=require('./meta-fixtures.cjs');
test('real worker yields progress, pauses/resumes, validates and snapshots safely',{timeout:30000},async()=>{
  const worker=new Worker(path.join(__dirname,'v15-worker-harness.mjs')),messages=[];
  const wait=predicate=>new Promise((resolve,reject)=>{const old=messages.find(predicate);if(old)return resolve(old);const timeout=setTimeout(()=>{cleanup();reject(new Error('Worker response timed out'));},10000);const onMessage=m=>{if(predicate(m)){cleanup();resolve(m);}},onError=e=>{cleanup();reject(e);},cleanup=()=>{clearTimeout(timeout);worker.off('message',onMessage);worker.off('error',onError);};worker.on('message',onMessage);worker.on('error',onError);});
  worker.on('message',m=>messages.push(m));
  try{
    worker.postMessage({type:'init',cards,rules,styles,seed:15});const ready=await wait(m=>m.type==='ready');assert.equal(ready.snapshot.games,0);
    worker.postMessage({type:'run',count:120,requestId:'validation-test',validation:true});const progress=await wait(m=>m.type==='progress');assert.ok(progress.completed>0&&progress.completed<120);worker.postMessage({type:'pause'});const pause=await wait(m=>m.type==='paused');assert.ok(pause.pending>0);const doneBefore=messages.filter(m=>m.type==='done').length;await new Promise(r=>setTimeout(r,60));assert.equal(messages.filter(m=>m.type==='done').length,doneBefore);
    worker.postMessage({type:'rate',rate:'max'});const benchmark=await wait(m=>m.type==='benchmark');assert.ok(benchmark.maxSafeRate>=100);assert.ok(benchmark.millisecondsPerGame>0);
    worker.postMessage({type:'resume'});const done=await wait(m=>m.type==='done');assert.equal(done.completed,120);assert.equal(done.report.validationRun.accepted,120);assert.equal(done.report.validationRun.invalidSimulations,0);assert.equal(done.report.nanCount,0);assert.ok(Math.abs(done.report.pickRateTotal-800)<1e-7);assert.equal(done.snapshot.games,120);
    worker.postMessage({type:'filters',filters:{style:'control'}});const filtered=await wait(m=>m.type==='snapshot'&&m.view.filteredAppearances<240);assert.ok(filtered.view.filteredAppearances>0);assert.ok(Math.abs(filtered.view.cards.reduce((n,r)=>n+r.pickRate,0)-800)<1e-7);
  }finally{await worker.terminate();}
});
