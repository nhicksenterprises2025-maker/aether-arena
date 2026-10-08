import { createMetaEngine } from './meta-engine.js';

let engine=null,paused=false,queue=[],running=false,filters={},rate='100',benchSamples=[];
const clock=()=>performance.now();
const send=(type,extra={})=>postMessage({type,...extra});
function budget(){const ms=benchSamples.length?benchSamples.reduce((a,b)=>a+b,0)/benchSamples.length:120;return Math.max(100,Math.min(2000,Math.floor(60000/Math.max(1,ms)*.28/50)*50));}
function emit(type,extra={}){if(!engine)return;send(type,{...extra,snapshot:engine.snapshot(),view:engine.view(filters),benchmark:{millisecondsPerGame:benchSamples.length?benchSamples.reduce((a,b)=>a+b,0)/benchSamples.length:null,maxSafeRate:budget(),selectedRate:rate}});}
async function pump(){
  if(running||paused||!engine)return;
  running=true;
  try{
    while(queue.length&&!paused){
      const job=queue[0],chunk=Math.min(4,job.total-job.completed),start=clock();
      if(!job.baseline)job.baseline={games:engine.dataset.games,invalid:engine.dataset.invalidSimulations,nan:engine.dataset.nanCount,started:clock()};
      for(let i=0;i<chunk;i++){const match=engine.simulate(engine.dataset.nextSeed++);engine.ingest(match);job.completed++;}
      benchSamples.push((clock()-start)/Math.max(1,chunk));if(benchSamples.length>40)benchSamples.shift();
      if(job.completed===job.total){queue.shift();emit('done',{requestId:job.requestId,completed:job.completed,total:job.total,validation:job.validation,report:{...engine.validation(),validationRun:{requested:job.total,completed:job.completed,accepted:engine.dataset.games-job.baseline.games,invalidSimulations:engine.dataset.invalidSimulations-job.baseline.invalid,nanCount:engine.dataset.nanCount-job.baseline.nan,wallMilliseconds:clock()-job.baseline.started}}});}
      else if(job.completed-job.lastSent>=25||clock()-job.lastEmit>=500){job.lastSent=job.completed;job.lastEmit=clock();emit('progress',{requestId:job.requestId,completed:job.completed,total:job.total,validation:job.validation});}
      await new Promise(resolve=>setTimeout(resolve,0));
    }
  }catch(error){send('error',{message:error.message,stack:error.stack});queue=[];}finally{running=false;}
}
self.onmessage=event=>{
  const message=event.data||{};
  try{
    if(message.type==='init'){engine=createMetaEngine(message.cards,{rules:message.rules,styles:message.styles,version:message.version||'V15',seed:message.seed,saved:message.saved,workerUsage:'Web Worker; 4-match yielding chunks'});paused=false;queue=[];benchSamples=[];emit('ready');}
    else if(message.type==='run'){if(!engine)throw new Error('Initialize the Meta worker first.');const total=Math.max(0,Math.min(10000,Math.floor(Number(message.count)||0)));if(!total){emit('done',{requestId:message.requestId,completed:0,total:0,report:engine.validation()});return;}queue.push({requestId:message.requestId,total,completed:0,lastSent:0,lastEmit:clock(),validation:!!message.validation});pump();}
    else if(message.type==='pause'){paused=true;send('paused',{pending:queue.reduce((n,j)=>n+j.total-j.completed,0)});}
    else if(message.type==='resume'){paused=false;send('resumed');pump();}
    else if(message.type==='filters'){filters=message.filters||{};emit('snapshot');}
    else if(message.type==='snapshot'){emit('snapshot');}
    else if(message.type==='rate'){rate=String(message.rate||'100').toLowerCase();send('benchmark',{millisecondsPerGame:benchSamples.length?benchSamples.reduce((a,b)=>a+b,0)/benchSamples.length:null,maxSafeRate:budget(),selectedRate:rate});}
    else if(message.type==='reset'){queue=[];engine?.reset(message.seed);benchSamples=[];emit('reset',{requestId:message.requestId});}
  }catch(error){send('error',{message:error.message,stack:error.stack});}
};
