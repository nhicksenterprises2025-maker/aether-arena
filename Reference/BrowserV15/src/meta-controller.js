import {createMetaStore} from './meta-store.js';
import {createMetaLabUI} from './meta-ui.js';

export function createMetaLabController({cards,rules,styles,analyzer,saved,legacy,onSave=()=>{},onNotice=()=>{}}){
  const store=createMetaStore({cards,rules,styles,version:'V15',saved,legacy});
  let worker,ready=false,paused=false,battle=false,busy=false,rate=100,safeRate=500,debt=0,last=performance.now(),filters={},currentView=null,comparison=[],validation=null,request=0,lastSave=0,lastProgress=null,failed=false,minimumRequest=0,resetPending=0;
  const ui=createMetaLabUI({cards,analyzer,onSpeed:setSpeed,onPause:setPaused,onReset:reset,onExport:exportData,onValidation:runValidation,onFiltersChange:setFilters,onCompare:compare});
  function effectiveRate(){return rate==='max'?safeRate:Number(rate);}
  function visibleView(){const version=filters.version;return version&&version!=='current'&&version!==store.state.activeId?store.view({...filters,version}):currentView||store.view({...filters,version:store.state.activeId});}
  function refresh(){
    const view=visibleView();
    ui.update({...view,versions:store.versions(),patchComparisons:comparison,status:{paused,battlePaused:battle,validation,message:failed?'Simulation worker unavailable; saved analytics remain accessible.':battle?'Paused for live battle':paused?'Simulation paused':validation?.running?`Validation ${validation.completed.toLocaleString()} / 10,000 games`:`${rate==='max'?'MAX SAFE · ':''}${effectiveRate()}/min · worker simulation`},validation:{...view.validation,workerUsage:battle?'Paused during live battle':paused?'Paused':busy?'Worker computing':'Worker idle; rate limited'}});
  }
  function save(force=false){if(battle)return;if(force||performance.now()-lastSave>15000){lastSave=performance.now();onSave();}}
  function accept(message){
    if(message.benchmark?.maxSafeRate)safeRate=Math.min(safeRate+50,message.benchmark.maxSafeRate);
    if(message.snapshot){store.commit(message.snapshot);currentView=message.view;}
    if(message.validation){validation={running:message.type!=='done',completed:message.completed||0,report:message.report||null};}
    refresh();save(message.type==='done'&&message.validation);
  }
  function setSpeed(value){rate=value==='max'?'max':[100,250,500].includes(Number(value))?Number(value):100;debt=0;worker?.postMessage({type:'rate',rate});refresh();}
  function setPaused(value){paused=!!value;debt=0;last=performance.now();worker?.postMessage({type:paused||battle?'pause':'resume'});refresh();save(true);}
  function setBattleRunning(value){battle=!!value;debt=0;last=performance.now();worker?.postMessage({type:battle||paused?'pause':'resume'});if(!battle&&lastProgress){accept(lastProgress);lastProgress=null;}refresh();if(!battle)save(true);}
  function setFilters(value){filters={...value};worker?.postMessage({type:'filters',filters:{...filters,version:undefined}});refresh();}
  function compare({from,to}){comparison=store.compare(from==='current'?store.state.activeId:from,to==='current'?store.state.activeId:to);refresh();}
  function reset(){store.resetCurrent();currentView=null;debt=0;busy=false;validation=null;lastProgress=null;resetPending=++request;minimumRequest=resetPending;worker?.postMessage({type:'reset',requestId:resetPending});refresh();onSave();onNotice('New Meta run started. The previous run is retained in history.');}
  function runValidation(count=10000){if(!ready||failed||resetPending||validation?.running)return;if(battle){onNotice('End the live battle before starting validation.');return;}paused=false;validation={running:true,completed:0};busy=true;worker.postMessage({type:'resume'});worker.postMessage({type:'run',count,requestId:++request,validation:true});refresh();}
  function download(content,filename,type){const url=URL.createObjectURL(new Blob([content],{type})),a=document.createElement('a');a.href=url;a.download=filename;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  function exportData({subject,format,filters:exportFilters={}}){
    const view=visibleView(),minimum=Number(exportFilters.minSample)||0;
    const matches=row=>{const card=cards[row.id];if(!card)return true;const type=card.spell?'spell':card.building?'building':'troop';if(exportFilters.type&&exportFilters.type!=='all'&&exportFilters.type.toLowerCase()!==type&&exportFilters.type.toLowerCase()!==`${type}s`)return false;if(exportFilters.cost&&exportFilters.cost!=='all'&&Number(exportFilters.cost)!==card.cost)return false;const trait=exportFilters.trait;if(trait&&trait!=='all'&&!({air:!!card.flying,ground:!card.flying&&!card.spell&&!card.building,winCondition:!!card.buildingsOnly,win:!!card.buildingsOnly,wincondition:!!card.buildingsOnly,win_condition:!!card.buildingsOnly,splash:!!(card.splash||card.spell||card.auraDamage),swarm:(card.count||1)>1}[trait]))return false;return(!exportFilters.search||card.name.toLowerCase().includes(exportFilters.search.toLowerCase()))&&(row.cleanN??row.n??Infinity)>=minimum;};
    const allowed=new Set(view.cards.filter(matches).map(row=>row.id));
    let rows=(subject==='patchComparisons'?comparison:view[subject]||[]).filter(row=>row.aId?allowed.has(row.aId)&&allowed.has(row.bId)&&(row.pairGames??Infinity)>=minimum:matches(row)&&(row.n??Infinity)>=minimum);
    if(subject==='personalities')rows=rows.map(row=>({...row,cards:row.cards?.filter(card=>allowed.has(card.id))}));
    const payload={version:view.identity||store.identity,filters:exportFilters,definitions:view.definitions||view.modelNote||null,exportedAt:new Date().toISOString(),subject,rows};
    if(format==='json')download(JSON.stringify(payload,null,2),`rift-v15-${subject}.json`,'application/json');
    else{const keys=[...new Set(rows.flatMap(row=>Object.keys(row)))],cell=value=>`"${(value==null?'':typeof value==='object'?JSON.stringify(value):String(value)).replaceAll('"','""')}"`;download([keys.map(cell).join(','),...rows.map(row=>keys.map(key=>cell(row[key])).join(','))].join('\r\n'),`rift-v15-${subject}.csv`,'text/csv;charset=utf-8');}
  }
  try{
    worker=new Worker(new URL('./meta-worker.js',import.meta.url),{type:'module'});
    worker.onmessage=event=>{const message=event.data;if(message.type==='ready')ready=true;if(resetPending&&!(message.type==='reset'&&message.requestId===resetPending))return;if(Number.isFinite(message.requestId)&&message.requestId<minimumRequest)return;if(message.type==='reset'){resetPending=0;validation=null;busy=false;}if(message.type==='error'){failed=true;busy=false;onNotice(`Meta simulation error: ${message.message}`);refresh();return;}if(message.type==='done')busy=false;if(message.validation)validation={running:message.type!=='done',completed:message.completed||0,report:message.report||null};if(battle&&message.snapshot){lastProgress=message;if(message.type==='snapshot')currentView=message.view;refresh();return;}if(message.snapshot)accept(message);else if(message.type==='benchmark'){safeRate=message.maxSafeRate||safeRate;refresh();}};
    worker.onerror=event=>{failed=true;busy=false;onNotice(`Meta worker could not load: ${event.message}`);refresh();};
    worker.postMessage({type:'init',cards,rules,styles,version:'V15',saved:store.current(),seed:151015});
  }catch(error){failed=true;onNotice(error.message);}
  const timer=setInterval(()=>{const now=performance.now(),elapsed=now-last;last=now;if(elapsed>1400&&rate==='max')safeRate=Math.max(100,Math.floor(safeRate*.8/50)*50);if(!ready||failed||resetPending||battle||paused||validation?.running)return;debt=Math.min(Math.max(20,Math.ceil(effectiveRate()/60)*2),debt+Math.min(elapsed,1500)*effectiveRate()/60000);if(!busy&&debt>=1){const count=Math.floor(debt);debt-=count;busy=true;worker.postMessage({type:'run',count,requestId:++request});}},1000);
  refresh();
  return{store,refresh,setBattleRunning,setPaused,runValidation,showValidation(){ui.showValidation();refresh();},serialize:()=>store.serialize(),destroy(){clearInterval(timer);worker?.terminate();ui.destroy();}};
}
