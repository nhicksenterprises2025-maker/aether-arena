import { COMBAT_MODEL_VERSION, combatFingerprint, adjustedWinRate, wilsonInterval } from './combat-rules.js';
import { createMetaEngine } from './meta-engine.js';

const clone=value=>JSON.parse(JSON.stringify(value));
const safe=value=>Number.isFinite(value)&&value>=0?value:0;
export function createMetaStore({cards,rules,styles,version='V15',saved=null,legacy=null}={}) {
  if(!cards||!rules)throw new Error('Meta history requires live cards and rules.');
  const identity={version,model:COMBAT_MODEL_VERSION,fingerprint:combatFingerprint(cards,rules,styles)},activeId=`${version}-${identity.fingerprint}`;
  const state={schema:2,activeId,datasets:[]},viewCache=new Map();
  if(saved?.schema===2&&Array.isArray(saved.datasets)){
    for(const entry of saved.datasets){if(!entry||typeof entry.id!=='string'||!entry.identity||!entry.dataset||state.datasets.some(d=>d.id===entry.id))continue;state.datasets.push(clone(entry));}
  }
  if(!state.datasets.some(d=>d.id===activeId))state.datasets.push({id:activeId,label:version,identity:{...identity},dataset:null,limited:false,createdAt:new Date().toISOString()});
  if(legacy?.cards&&safe(legacy.games)>0&&!state.datasets.some(d=>d.id==='legacy-v1210-v14')){
    state.datasets.push({id:'legacy-v1210-v14',label:'V12.10 / V14 legacy',identity:{version:'Legacy',model:'analytical-v2',fingerprint:'unknown'},limited:true,dataset:{games:Math.floor(safe(legacy.games)),cards:clone(legacy.cards)},notice:'The old save key spans V12.10 through V14. Its session version and per-style, pair, history and new event metrics were not recorded. This archive is not assigned to either patch.'});
  }
  const store={state,identity,current,commit,view,versions,compare,serialize:()=>clone(state),resetCurrent};
  function current(){const entry=state.datasets.find(d=>d.id===activeId);return entry?.dataset?clone(entry.dataset):null;}
  function commit(dataset){if(dataset?.schema!==2||dataset.identity?.fingerprint!==identity.fingerprint)return false;const entry=state.datasets.find(d=>d.id===activeId);entry.dataset=clone(dataset);entry.updatedAt=new Date().toISOString();viewCache.delete(activeId);return true;}
  function resetCurrent(){const entry=state.datasets.find(d=>d.id===activeId);if(entry.dataset?.games){const id=`${activeId}-run-${Date.now()}`;state.datasets.push({...clone(entry),id,label:`${version} prior run`,archivedAt:new Date().toISOString()});}entry.dataset=null;viewCache.delete(activeId);return true;}
  function versions(){
    const entries=state.datasets.map(entry=>({id:entry.id,label:entry.label,version:entry.identity.version,games:entry.dataset?.games||0,available:!!entry.dataset,limited:!!entry.limited,fingerprint:entry.identity.fingerprint,model:entry.identity.model,notice:entry.notice||''}));
    for(const historical of ['V12.8','V12.10','V14'])if(!entries.some(e=>e.version===historical))entries.unshift({id:`unavailable-${historical}`,label:historical,version:historical,games:0,available:false,limited:true,notice:'No recorded dataset for this exact patch was supplied. Patch notes contain stats and audit summaries, not reusable per-card match observations.'});
    return entries;
  }
  function limitedView(entry,filters={}){
    const games=entry?.dataset?.games||0,eventArchive=entry?.dataset?.schema===2;
    let bucket=eventArchive?entry.dataset.all:null;
    if(eventArchive&&filters.style&&filters.style!=='all'&&filters.archetype&&filters.archetype!=='all')bucket=entry.dataset.slices?.[`${filters.style}|${filters.archetype}`]||null;
    else if(eventArchive&&filters.style&&filters.style!=='all')bucket=entry.dataset.styles?.[filters.style]||null;
    else if(eventArchive&&filters.archetype&&filters.archetype!=='all')bucket=entry.dataset.archetypes?.[filters.archetype]||null;
    const recordedCards=eventArchive?(bucket?.cards||{}):(entry?.dataset?.cards||{}),sideDecks=eventArchive?(bucket?.n||0):games*2;
    return {identity:entry?.identity||{version:'Unavailable',model:'unknown'},games,limited:true,filteredAppearances:sideDecks,notice:entry?.notice||(eventArchive?'Recorded aggregates from this original model are projected without re-simulation or a new identity. Unrecorded or incompatible mechanics stay unavailable.':'No recorded dataset for this exact patch. Unavailable metrics remain blank.'),cards:Object.values(entry?.library||cards).map(card=>{
      const s=recordedCards[card.id],n=s?safe(eventArchive?s.cleanN:s.nonMirrorGames):0,score=s?eventArchive?safe(s.score):safe(s.nonMirrorWins)+safe(s.nonMirrorDraws)*.5:0,appearances=s?safe(eventArchive?s.appearances:s.decks):0,ci=wilsonInterval(score,n),den=appearances||1;
      const perAppearance=key=>s&&Number.isFinite(s[key])?s[key]/den:null;
      return {id:card.id,name:card.name,category:card.spell?'Spell':card.building?'Building':'Troop',cost:card.cost,flying:!!card.flying,winCondition:!!card.buildingsOnly,splash:!!card.splash||!!card.spell||!!card.auraDamage,swarm:(card.count||1)>1,appearances,cleanN:n,sample:n,pickRate:s&&sideDecks?appearances/sideDecks*100:null,rawWinRate:n?score/n*100:null,adjustedWinRate:s?adjustedWinRate(score,n):null,winRate:s?adjustedWinRate(score,n):null,ciLow:n?ci.low:null,ciHigh:n?ci.high:null,usesPerGame:perAppearance('uses'),aetherPerGame:perAppearance('aether'),troopDamagePerGame:perAppearance('troopDamage'),towerDamagePerGame:perAppearance('towerDamage'),buildingDamagePerGame:perAppearance('buildingDamage'),damageTakenPerGame:perAppearance('damageTaken'),killsPerGame:perAppearance('kills'),deathsPerGame:perAppearance('deaths'),damagePerAether:s&&s.aether?(safe(s.troopDamage)+safe(s.towerDamage)+(eventArchive?safe(s.buildingDamage):0))/s.aether:null};
    }),matchups:[],synergies:[],archetypes:[],personalities:[],alerts:[],history:eventArchive?(entry.dataset.history||[]):[],historyScope:'Recorded cumulative whole-dataset checkpoints from the original archived model.',validation:{games,invalidSimulations:eventArchive?entry.dataset.invalidSimulations??null:null,nanCount:eventArchive?entry.dataset.nanCount??null:null,mirrorExclusions:eventArchive?entry.dataset.mirrorExclusions??null:null,averageDuration:eventArchive&&games?entry.dataset.duration/games:null,averageAetherSpent:eventArchive&&games?entry.dataset.aetherSpent/(games*2):null,pickRateTotal:sideDecks?Object.values(recordedCards).reduce((n,s)=>n+safe(eventArchive?s.appearances:s.decks)/sideDecks*100,0):null,gamesPerMinute:null,workerUsage:'Archived; not running'}};
  }
  function view(filters={}){
    const id=filters.version||activeId,entry=state.datasets.find(d=>d.id===id);
    if(!entry?.dataset||entry.limited)return {...limitedView(entry,filters),versions:versions(),activeVersion:id};
    // Archived card data is stored beside observations so future patch comparisons retain original descriptors.
    let cached=viewCache.get(id);
    if(!cached){const archiveCards=entry.library||cards,archiveRules=entry.rules||rules,archiveStyles=entry.styles||styles;const engine=createMetaEngine(archiveCards,{rules:archiveRules,styles:archiveStyles,version:entry.identity.version,saved:entry.dataset});if(engine.dataset.games!==entry.dataset.games)return {...limitedView({...entry,limited:true,notice:'This dataset uses a different model or stat fingerprint. Its recorded aggregate card evidence and checkpoints remain visible with their original identity; incompatible new mechanics are not reinterpreted.'},filters),versions:versions(),activeVersion:id};cached=engine;viewCache.set(id,engine);}
    return {...cached.view(filters),versions:versions(),activeVersion:id};
  }
  function compare(beforeId,afterId){
    const before=view({version:beforeId}),after=view({version:afterId}),byId=Object.fromEntries(before.cards.map(r=>[r.id,r])),beforeDataset=state.datasets.find(d=>d.id===beforeId)?.dataset,afterDataset=state.datasets.find(d=>d.id===afterId)?.dataset;
    const delta=(a,b)=>Number.isFinite(a)&&Number.isFinite(b)?a-b:null;
    return after.cards.map(row=>{
      const old=byId[row.id]||{},damage=r=>Number.isFinite(r.troopDamagePerGame)&&Number.isFinite(r.towerDamagePerGame)&&Number.isFinite(r.buildingDamagePerGame)?r.troopDamagePerGame+r.towerDamagePerGame+r.buildingDamagePerGame:null;
      const archetypeChanges=[];
      for(const arch of after.archetypes){const prev=beforeDataset?.archetypes?.[arch.name],next=afterDataset?.archetypes?.[arch.name],oldStat=prev?.cards?.[row.id],newStat=next?.cards?.[row.id];if(prev?.n&&next?.n&&oldStat?.cleanN&&newStat?.cleanN)archetypeChanges.push({name:arch.name,pickRateDelta:delta(newStat.appearances/next.n*100,oldStat.appearances/prev.n*100),winRateDelta:delta(adjustedWinRate(newStat.score,newStat.cleanN),adjustedWinRate(oldStat.score,oldStat.cleanN)),nBefore:oldStat.cleanN,nAfter:newStat.cleanN});}
      const largest=archetypeChanges.slice().sort((a,b)=>Math.abs(b.winRateDelta)-Math.abs(a.winRateDelta))[0];
      return {id:row.id,name:row.name,winRateDelta:delta(row.adjustedWinRate,old.adjustedWinRate),pickRateDelta:delta(row.pickRate,old.pickRate),damageDelta:delta(damage(row),damage(old)),usageDelta:delta(row.usesPerGame,old.usesPerGame),archetypeDelta:largest?.winRateDelta??null,archetypeName:largest?.name??null,archetypeChanges,nBefore:old.cleanN||0,nAfter:row.cleanN||0,before:before.identity.version,after:after.identity.version,limited:before.limited||after.limited||false,notice:before.notice||after.notice||'Different simulator fingerprints are separate evidence; changes can reflect model changes as well as card changes.'};
    });
  }
  // Save the immutable inputs with each new run. A changed card/rule/model fingerprint opens a new archive.
  const active=state.datasets.find(d=>d.id===activeId);active.library=clone(cards);active.rules=clone(rules);active.styles=clone(styles||{});
  return store;
}
