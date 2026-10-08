// Views of measured data. Scheduling, persistence and simulation stay with the caller.
const esc = value => String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
const finite = value => value !== null && value !== undefined && Number.isFinite(Number(value));
const num = (value, digits = 1) => finite(value) ? Number(value).toLocaleString(undefined, {maximumFractionDigits: digits}) : '—';
const pct = value => finite(value) ? `${num(value)}%` : '—';
const delta = value => finite(value) ? `${Number(value) > 0 ? '+' : ''}${num(value)}` : '—';
const ci = row => finite(row?.ciLow) && finite(row?.ciHigh) ? `${num(row.ciLow)}–${num(row.ciHigh)}%` : '—';
const count = row => row?.cleanN ?? row?.n ?? row?.pairGames ?? 0;
const art = id => `./assets/ui/${encodeURIComponent(id)}.svg`;
const COLUMNS = [
  ['pickRate','Pick %','rate'],['adjustedWinRate','Adjusted WR','rate'],['rawWinRate','Raw WR','rate'],['ci','95% CI','ci'],['cleanN','Clean N','int'],
  ['usesPerGame','Uses/G'],['aetherPerGame','Aether/G'],['troopDamagePerGame','Troop dmg/G'],['towerDamagePerGame','Tower dmg/G'],['buildingDamagePerGame','Building dmg/G'],['damageTakenPerGame','Damage taken/G'],['killsPerGame','Kills/G'],['deathsPerGame','Deaths/G'],
  ['averageLifetime','Lifetime, s'],['averagePlacementX','Placement X'],['averagePlacementZ','Placement Z'],['averageKillValue','Kill value'],['damagePerAether','Damage/Aether'],['towerDamagePerAether','Tower dmg/Aether'],['survivalRate','Survival %','rate'],
  ['openingHandPlayRate','Opening-hand play %','rate'],['firstPlayRate','First-play %','rate'],['otPlayRate','OT play %','rate'],['connectionRate','Win-con connection %','rate'],['crownContribution','Crown contribution'],
  ['damagePreventedPerGame','Damage prevented/G'],['pullsPerGame','Win-con pulls/G'],['lifetimeUtilization','Building utilization %','rate'],['targetsPerCast','Targets/cast'],['spellAetherValuePerCast','Spell value/cast'],['overkillPerCast','Overkill/cast'],['damagePerCast','Damage/cast'],
  ['slowUptime','Slow uptime %','rate'],['auraDamagePerGame','Aura dmg/G'],['unitsStunnedPerGame','Units stunned/G'],['stunUptime','Stun uptime %','rate'],['zoneOccupancy','Average zone occupants'],['dotTicksPerCast','DoT ticks/cast'],['initialDamagePerGame','Initial dmg/G'],['dotDamagePerGame','DoT dmg/G'],
];
const GROUPS = {
  overview:['pickRate','adjustedWinRate','rawWinRate','ci','cleanN','usesPerGame','towerDamagePerGame','damagePerAether','survivalRate'],
  combat:['pickRate','adjustedWinRate','ci','cleanN','troopDamagePerGame','towerDamagePerGame','buildingDamagePerGame','damageTakenPerGame','killsPerGame','deathsPerGame','averageLifetime','averageKillValue'],
  economy:['pickRate','adjustedWinRate','ci','cleanN','usesPerGame','aetherPerGame','damagePerAether','towerDamagePerAether','averageKillValue','crownContribution'],
  timing:['adjustedWinRate','ci','cleanN','averageLifetime','averagePlacementX','averagePlacementZ','openingHandPlayRate','firstPlayRate','otPlayRate','connectionRate','survivalRate'],
  special:['adjustedWinRate','ci','cleanN','damagePreventedPerGame','pullsPerGame','lifetimeUtilization','targetsPerCast','spellAetherValuePerCast','overkillPerCast','damagePerCast','slowUptime','auraDamagePerGame','unitsStunnedPerGame','stunUptime','zoneOccupancy','dotTicksPerCast','initialDamagePerGame','dotDamagePerGame'],
  all:COLUMNS.map(column=>column[0]),
};
const LABELS={cards:'Card analytics',matchups:'Mechanical card matchups',synergies:'Measured pair synergy',archetypes:'Deck archetypes',personalities:'AI personality analytics',alerts:'Balance signals',history:'Meta trends',patches:'Patch comparisons',validation:'Meta validation'};
const valueHtml = (row,column,card={}) => {
  const key=column[0],spell=card.spell||row.category==='Spell',building=card.building||row.category==='Building';
  if(spell&&['damageTakenPerGame','deathsPerGame','averageLifetime','survivalRate'].includes(key))return'—';
  if(!building&&['damagePreventedPerGame','pullsPerGame','lifetimeUtilization'].includes(key))return'—';
  if(!spell&&['targetsPerCast','spellAetherValuePerCast','overkillPerCast','damagePerCast'].includes(key))return'—';
  if(key==='slowUptime'&&!card.slowPct)return'—';
  if(['auraDamagePerGame','unitsStunnedPerGame','stunUptime'].includes(key)&&!card.auraDamage)return'—';
  if(['zoneOccupancy','dotTicksPerCast','initialDamagePerGame','dotDamagePerGame'].includes(key)&&!card.dotDamage)return'—';
  return column[2]==='ci'?esc(ci(row)):column[2]==='rate'?esc(pct(row[key])):esc(num(row[key],column[2]==='int'?0:1));
};
const empty = message => `<p class="meta-empty">${esc(message)}</p>`;

/** Root passes an engine snapshot; callbacks perform every operation outside this view. */
export function createMetaLabUI({cards,analyzer,onSpeed,onPause,onReset,onExport,onValidation,onFiltersChange,onCompare}={}) {
  const modal=document.getElementById('meta-modal');
  const content=document.getElementById('meta-content');
  const filterDisclosure=document.getElementById('meta-filter-disclosure');
  if(!modal||!content)throw new Error('Meta Lab containers are missing.');
  const state={tab:'cards',group:'overview',sort:'adjustedWinRate',direction:-1,styleSort:null,pairListOpen:false,fullReportOpen:false,card:Object.keys(cards||{})[0],pair:null,trendCard:Object.keys(cards||{})[0],trendMetric:'winRate',window:'all',type:'all',cost:'all',trait:'all',search:'',style:'all',archetype:'all',minSample:0,version:'current',compareFrom:'',compareTo:'',validationVisible:false};
  let view={cards:[],matchups:[],synergies:[],archetypes:[],personalities:[],alerts:[],history:[],versions:[],patchComparisons:[],validation:{},status:{}};
  let narrow=window.innerWidth<=700;
  if(filterDisclosure)filterDisclosure.open=!narrow;
  const byId=id=>cards?.[id]||{};
  const cardName=id=>byId(id).name||view.cards.find(row=>row.id===id)?.name||id||'—';
  const badge=row=>`<button class="meta-card-cell meta-card-button" data-meta-card="${esc(row.id)}"><img src="${art(row.id)}" alt=""/><span><strong>${esc(row.name||cardName(row.id))}</strong><small>${esc(row.category||byId(row.id).category)} · ${num(row.cost??byId(row.id).cost,0)} Aether</small></span></button>`;
  const filters=()=>({style:state.style,archetype:state.archetype,minSample:state.minSample,version:state.version});
  function rowVisible(row){
    const card=byId(row.id),type=card.spell?'Spell':card.building?'Building':'Troop';
    if(state.type!=='all'&&type!==state.type)return false;
    if(state.cost!=='all'&&Number(row.cost??card.cost)!==Number(state.cost))return false;
    if(state.search&&!cardName(row.id).toLowerCase().includes(state.search.toLowerCase()))return false;
    if(Number(count(row))<state.minSample)return false;
    if(state.trait==='air'&&!(row.flying??card.flying))return false;
    if(state.trait==='ground'&&(card.spell||card.building||(row.flying??card.flying)))return false;
    if(state.trait==='winCondition'&&!(row.winCondition??card.buildingsOnly))return false;
    if(state.trait==='splash'&&!(row.splash??(card.splash||card.spell||card.auraDamage)))return false;
    if(state.trait==='swarm'&&!(row.swarm??(card.count>1)))return false;
    return true;
  }
  function sorted(rows,key=state.sort){
    return [...rows].sort((a,b)=>{const av=key==='ci'?(finite(a.ciHigh)&&finite(a.ciLow)?a.ciHigh-a.ciLow:null):a[key],bv=key==='ci'?(finite(b.ciHigh)&&finite(b.ciLow)?b.ciHigh-b.ciLow:null):b[key];if(typeof av==='string'||typeof bv==='string')return String(av??'').localeCompare(String(bv??''))*state.direction;if(!finite(av))return finite(bv)?1:0;if(!finite(bv))return -1;return(Number(av)-Number(bv))*state.direction||cardName(a.id).localeCompare(cardName(b.id));});
  }
  const heading=(key,label)=>`<th scope="col"${state.sort===key?' aria-sort="'+(state.direction===1?'ascending':'descending')+'"':''}><button data-meta-sort="${esc(key)}">${esc(label)}${state.sort===key?(state.direction===1?' ↑':' ↓'):''}</button></th>`;
  const table=(head,rows,className='')=>`<div class="meta-table-wrap"><table class="meta-table ${className}"><thead><tr>${head}</tr></thead><tbody${state.tab==='cards'?' id="meta-table-body"':''}>${rows}</tbody></table></div>`;
  function cardsPanel(){
    const columns=GROUPS[state.group].map(key=>COLUMNS.find(column=>column[0]===key));
    const rows=sorted(view.cards.filter(rowVisible));
    if(rows.length&&!rows.some(row=>row.id===state.card))state.card=rows[0].id;
    const groupOptions=[['overview','Overview'],['combat','Combat'],['economy','Economy'],['timing','Placement + timing'],['special','Special mechanics'],['all','All metrics']];
    const toolbar=`<div class="meta-section-bar"><div><strong>${rows.length} / ${Object.keys(cards||{}).length} CARDS</strong><span>Click a card for its complete report. Click any column to sort.</span></div><label>COLUMNS <select id="meta-columns">${groupOptions.map(([value,label])=>`<option value="${value}"${state.group===value?' selected':''}>${label}</option>`).join('')}</select></label></div>`;
    const body=rows.map(row=>`<tr${state.card===row.id?' class="meta-selected-row"':''}><th scope="row">${badge(row)}</th>${columns.map(column=>`<td${column[0]==='adjustedWinRate'&&row.cleanN<30?' class="meta-uncertain"':''}>${valueHtml(row,column,byId(row.id))}</td>`).join('')}</tr>`).join('');
    const card=rows.find(row=>row.id===state.card);
    return toolbar+table(heading('name','CARD')+columns.map(column=>heading(column[0],column[1])).join(''),body||`<tr><td colspan="${columns.length+1}">No cards meet the current filters.</td></tr>`)+cardDetail(card);
  }
  function cardDetail(row){
    if(!row)return'';
    const card=byId(row.id);
    const special=new Set(card.building?['damagePreventedPerGame','pullsPerGame','lifetimeUtilization']:card.spell?['targetsPerCast','spellAetherValuePerCast','overkillPerCast','damagePerCast']:[]);
    if(row.id==='frost_fang')special.add('slowUptime');
    if(row.id==='storm_raven')['auraDamagePerGame','unitsStunnedPerGame','stunUptime'].forEach(key=>special.add(key));
    if(row.id==='meteor_shards')['zoneOccupancy','dotTicksPerCast','initialDamagePerGame','dotDamagePerGame'].forEach(key=>special.add(key));
    const base=COLUMNS.filter((column,index)=>index<25&&column[0]!=='ci');
    const detail=(columns)=>`<dl class="meta-detail-metrics">${columns.map(column=>`<div><dt>${esc(column[1])}</dt><dd>${valueHtml(row,column,card)}</dd></div>`).join('')}</dl>`;
    const definitions=[];
    if(card.building)definitions.push('Damage prevented is HP absorbed by this building, a proxy for protection rather than a counterfactual tower-damage estimate.');
    if(card.spell)definitions.push('Spell Aether value estimates the removed fraction of each target’s replacement cost. Towers contribute zero Aether value; overkill is damage beyond remaining HP.');
    if(row.id==='frost_fang')definitions.push('Slow uptime tracks affected time after a target’s first slow application. Movement slow refreshes without stacking.');
    if(row.id==='storm_raven')definitions.push('Units stunned counts successful stun applications; the same unit may be stunned again. Stun uptime tracks affected time after the first application.');
    if(row.id==='meteor_shards')definitions.push('Zone occupancy is the average number of enemy units inside the active zone. Initial and DoT damage are measured separately.');
    return `<section class="meta-card-report"><div class="meta-report-title">${badge(row)}<div><span>CLEAN N ${num(row.cleanN,0)}</span><strong>${pct(row.adjustedWinRate)} adjusted · ${ci(row)} CI</strong></div></div><details class="meta-full-report" id="meta-full-card-report"${state.fullReportOpen?' open':''}><summary>COMPLETE CARD REPORT <span>Economy, combat, timing and placement</span></summary>${detail(base)}<p class="meta-inline-note">Placement uses arena tile coordinates. Per-game metrics use deck appearances; clean N excludes opposing decks with the same card. Opening-hand play means deployment among the side’s first four plays; first-play rate counts its first cast. Connection rate measures casts that damage a Crown Tower. Unavailable historical telemetry is shown as —.</p></details>${special.size?`<div class="meta-special-report"><h3>${card.building?'BUILDING':card.spell?'SPELL':'SPECIAL MECHANIC'} REPORT</h3>${detail(COLUMNS.filter(column=>special.has(column[0])))}</div><p class="meta-inline-note">${esc(definitions.join(' '))}</p>`:''}</section>`;
  }
  function matrixPanel(synergy){
    const available=view.cards.filter(rowVisible),ids=available.map(row=>row.id);
    const entries=synergy?view.synergies:view.matchups;
    const findPair=(a,b)=>entries.find(pair=>pair.aId===a&&pair.bId===b)|| (synergy?entries.find(pair=>pair.aId===b&&pair.bId===a):null);
    let matrix='<thead><tr><th scope="col">ROW vs COLUMN</th>'+ids.map(id=>`<th scope="col" title="${esc(cardName(id))}"><img src="${art(id)}" alt="${esc(cardName(id))}"/><span>${esc(cardName(id))}</span></th>`).join('')+'</tr></thead><tbody>';
    matrix+=ids.map(a=>`<tr><th scope="row"><img src="${art(a)}" alt=""/>${esc(cardName(a))}</th>${ids.map(b=>{if(a===b)return'<td class="meta-matrix-mirror">—</td>';const pair=findPair(a,b),value=synergy?(pair?.pairGames>0?pair.synergyDelta:null):pair?.advantage,scale=synergy?15:100,strength=finite(value)?Math.min(.62,.12+Math.abs(value)/scale*.45):.05,tone=finite(value)&&value>0?'83,175,113':finite(value)&&value<0?'210,98,111':'100,119,137';return`<td><button data-meta-pair="${esc(a)}:${esc(b)}" style="background:rgba(${tone},${strength})" aria-label="${esc(cardName(a))} ${synergy?'paired with':'against'} ${esc(cardName(b))}: ${finite(value)?delta(value)+(synergy?' percentage points':' advantage'):'no samples'}">${finite(value)?delta(value):'—'}</button></td>`;}).join('')}</tr>`).join('')+'</tbody>';
    const current=state.pair?.every(id=>ids.includes(id))?findPair(...state.pair):null;
    let report=empty(`Select a cell to inspect ${synergy?'pair samples, expected baseline and shrunk win-rate change':'the targeting, range and special mechanics behind its advantage'}.`);
    if(current){
      report=`<div class="meta-pair-heading"><img src="${art(state.pair[0])}" alt=""/><div><h3>${esc(cardName(state.pair[0]))} ${synergy?'+':'vs'} ${esc(cardName(state.pair[1]))}</h3><p>${synergy?'Pair win rate minus expected baseline, with Bayesian shrinkage.':esc(current.label||'Mechanical advantage')}</p></div><img src="${art(state.pair[1])}" alt=""/></div>`;
      if(synergy)report+=`<dl class="meta-detail-metrics"><div><dt>Pair games</dt><dd>${num(current.pairGames,0)}</dd></div><div><dt>Adjusted pair WR</dt><dd>${pct(current.pairWinRate)}</dd></div><div><dt>Raw pair WR</dt><dd>${pct(current.rawPairWinRate)}</dd></div><div><dt>Expected baseline</dt><dd>${pct(current.expectedBaseline)}</dd></div><div><dt>Shrunk synergy Δ</dt><dd>${delta(current.pairGames>0?current.synergyDelta:null)} pp</dd></div><div><dt>Raw Δ</dt><dd>${delta(current.rawDelta)} pp</dd></div><div><dt>95% raw pair CI</dt><dd>${ci(current)}</dd></div></dl><p class="meta-inline-note">Pairs share deck context. A positive delta describes association, not proof that either card causes a stronger deck. Low N receives stronger shrinkage. The Wilson interval uses observed pair outcomes.</p>`;
      else report+=`<strong class="meta-pair-score">${delta(current.advantage)} / 100 advantage</strong><ul class="meta-reasons">${(current.reasons||[]).map(reason=>`<li>${esc(reason)}</li>`).join('')}</ul><p class="meta-inline-note">Mechanics estimate for a direct interaction, not an empirical battle win rate. Cost, support, timing and placement can change the result.</p>`;
    }
    const pairTable=synergy?`<details class="meta-full-report" id="meta-pair-list"${state.pairListOpen?' open':''}><summary>ALL PAIR SAMPLES <span>Sort sample size, baseline and synergy delta</span></summary>${table([['name','PAIR'],['pairGames','PAIR GAMES'],['pairWinRate','ADJUSTED PAIR %'],['rawPairWinRate','RAW PAIR %'],['expectedBaseline','BASELINE %'],['synergyDelta','SHRUNK Δ, pp'],['rawDelta','RAW Δ, pp'],['ci','95% RAW PAIR CI']].map(([key,label])=>heading(key,label)).join(''),sorted(entries.filter(pair=>ids.includes(pair.aId)&&ids.includes(pair.bId)&&pair.pairGames>=state.minSample).map(pair=>({...pair,name:`${cardName(pair.aId)} + ${cardName(pair.bId)}`}))).map(pair=>`<tr><th scope="row"><button class="meta-pair-list-button" data-meta-pair="${esc(pair.aId)}:${esc(pair.bId)}">${esc(pair.name)}</button></th><td>${num(pair.pairGames,0)}</td><td>${pct(pair.pairWinRate)}</td><td>${pct(pair.rawPairWinRate)}</td><td>${pct(pair.expectedBaseline)}</td><td>${delta(pair.pairGames>0?pair.synergyDelta:null)}</td><td>${delta(pair.rawDelta)}</td><td>${ci(pair)}</td></tr>`).join(''))}</details>`:'';
    return`<div class="meta-section-bar"><div><strong>${synergy?'EMPIRICAL PAIR SYNERGY':'CARD MATCHUP MATRIX'}</strong><span>${synergy?'Win-rate delta in percentage points · observed paired decks':'Row advantage against column · mechanics score −100 to +100'}</span></div><div class="meta-matrix-legend"><i class="favorable"></i>Favorable <i class="neutral"></i>Even <i class="unfavorable"></i>Unfavorable</div></div><div class="meta-matrix-wrap"><table class="meta-matrix">${matrix}</table></div><section class="meta-pair-report">${report}</section>${pairTable}`;
  }
  function archetypesPanel(){
    const nameList=list=>(list||[]).map(row=>`${cardName(row.id??row)} (${pct(row.winRate)}, N ${num(row.cleanN,0)})`).join(' · ')||'—';
    const opponent=row=>row?`${row.name} · ${pct(row.winRate)}, N ${num(row.n,0)}`:'—';
    const rows=sorted(view.archetypes.filter(row=>count(row)>=state.minSample)).map(row=>`<tr><th scope="row">${esc(row.name)}</th><td>${pct(row.pickRate)}</td><td>${pct(row.winRate)}</td><td>${ci(row)}</td><td>${num(row.n,0)}</td><td>${num(row.averageCrowns)}</td><td>${num(row.averageDuration)} s</td><td class="meta-long-cell">${esc(nameList(row.bestCards))}</td><td class="meta-long-cell">${esc(nameList(row.weakestCards))}</td><td class="meta-long-cell">${esc(opponent(row.bestOpponent))}</td><td class="meta-long-cell">${esc(opponent(row.worstOpponent))}</td></tr>`).join('');
    return`<p class="meta-inline-note">Decks can carry multiple strategic labels. Their archetype pick percentages may overlap. Opposing matchups and card slices retain their own sample sizes.</p>`+table([['name','ARCHETYPE'],['pickRate','PICK %'],['winRate','WIN %'],['ci','95% CI'],['n','N'],['averageCrowns','CROWNS/G'],['averageDuration','LENGTH'],['bestCards','BEST CARDS'],['weakestCards','WEAKEST CARDS'],['bestOpponent','BEST OPPONENT'],['worstOpponent','WORST OPPONENT']].map(([key,label])=>heading(key,label)).join(''),rows||'<tr><td colspan="11">No archetype samples meet the filters.</td></tr>','meta-archetype-table');
  }
  function personalitiesPanel(){
    const styles=view.personalities;
    let cardRows=sorted(view.cards.filter(rowVisible));
    if(state.styleSort){const slice=styles.find(style=>style.style===state.styleSort);cardRows=cardRows.sort((a,b)=>{const av=slice?.cards?.find(item=>item.id===a.id)?.winRate,bv=slice?.cards?.find(item=>item.id===b.id)?.winRate;if(!finite(av))return finite(bv)?1:0;if(!finite(bv))return-1;return(Number(av)-Number(bv))*state.direction;});}
    const rows=cardRows.map(row=>`<tr><th scope="row">${badge(row)}</th>${styles.map(style=>{const result=style.cards?.find(item=>item.id===row.id);return`<td><strong>${pct(result?.winRate)}</strong><small>${result?`${ci(result)} · N ${num(result.cleanN,0)}`:'No clean samples'}</small></td>`;}).join('')}</tr>`).join('');
    return`<p class="meta-inline-note">Card win rate within each AI personality. Each style's adjusted rate, confidence interval and clean N are independent; sparse slices remain uncertain. Click a personality column to sort its card win rates.</p>`+table(heading('name','CARD')+styles.map(style=>`<th scope="col"${state.styleSort===style.style?' aria-sort="'+(state.direction===1?'ascending':'descending')+'"':''}><button data-meta-style-sort="${esc(style.style)}">${esc(style.name||style.style)}${state.styleSort===style.style?(state.direction===1?' ↑':' ↓'):''}</button><small>${pct(style.winRate)} overall · N ${num(style.n,0)}</small></th>`).join(''),rows||'<tr><td>No card samples meet the filters.</td></tr>','meta-style-table');
  }
  function alertsPanel(){
    const alerts=view.alerts.filter(row=>!row.id||rowVisible(view.cards.find(card=>card.id===row.id)||row));
    return`<div class="meta-section-bar"><div><strong>BALANCE SIGNALS</strong><span>Confidence gates protect against conclusions from tiny samples.</span></div></div>${alerts.length?`<div class="meta-alert-list">${alerts.map(row=>`<article class="meta-alert ${String(row.severity||'WATCH').toLowerCase().replace(/\s+/g,'-')}"><div><span>${esc(row.severity||'WATCH')}</span><h3>${esc(row.name||cardName(row.id))}</h3><strong>${esc(row.label||row.title||row.message||row.direction||'Balance signal')}</strong></div><p>${esc(row.description||row.reason||'')}</p><ul class="meta-reasons">${(row.reasons||row.evidence||[]).map(reason=>`<li>${esc(typeof reason==='string'?reason:reason.text||JSON.stringify(reason))}</li>`).join('')}</ul><small>Adjusted WR ${pct(row.adjustedWinRate??view.cards.find(card=>card.id===row.id)?.adjustedWinRate)} · ${ci(row.ciLow===undefined?view.cards.find(card=>card.id===row.id):row)} · Clean N ${num(row.cleanN??count(view.cards.find(card=>card.id===row.id)),0)}</small></article>`).join('')}</div>`:empty('No confidence-qualified balance signals in this dataset and filter slice.')}`;
  }
  function chart(points,label,unit){
    const valid=points.filter(point=>finite(point.x)&&finite(point.y));
    if(!valid.length)return empty('No saved checkpoints for this card and window. Simulated games add measured history.');
    const w=860,h=230,l=55,r=22,t=18,b=35;
    const xmin=Math.min(...valid.map(point=>point.x)),xmax=Math.max(...valid.map(point=>point.x));let ymin=Math.min(...valid.map(point=>point.y)),ymax=Math.max(...valid.map(point=>point.y));const padding=Math.max((ymax-ymin)*.15,unit==='%'?1:.1);ymin-=padding;ymax+=padding;
    const x=value=>l+(value-xmin)/Math.max(1,xmax-xmin)*(w-l-r),y=value=>h-b-(value-ymin)/(ymax-ymin)*(h-t-b);
    const ticks=Array.from({length:5},(_,i)=>ymin+(ymax-ymin)*i/4);
    return`<svg class="meta-trend-chart" viewBox="0 0 ${w} ${h}" role="img" aria-label="${esc(label)} across ${num(xmin,0)} to ${num(xmax,0)} simulated games"><title>${esc(label)} · ${valid.length} measured checkpoints</title>${ticks.map(value=>`<line x1="${l}" y1="${y(value)}" x2="${w-r}" y2="${y(value)}" class="meta-chart-grid"/><text x="${l-8}" y="${y(value)+3}" text-anchor="end">${num(value)}${unit}</text>`).join('')}<polyline class="meta-chart-line" points="${valid.map(point=>`${x(point.x)},${y(point.y)}`).join(' ')}"/>${valid.map(point=>`<circle cx="${x(point.x)}" cy="${y(point.y)}" r="3.2"><title>${num(point.x,0)} games · ${num(point.y)}${unit}</title></circle>`).join('')}<text x="${l}" y="${h-9}">${num(xmin,0)} games</text><text x="${w-r}" y="${h-9}" text-anchor="end">${num(xmax,0)} games</text></svg>`;
  }
  function historyPanel(){
    const keys=[['winRate','Adjusted win rate'],['pickRate','Pick rate'],['damagePerAether','Damage / Aether']];
    const checkpoints=view.history||[],latest=Math.max(0,...checkpoints.map(row=>row.games||0)),start=state.window==='all'?0:Math.max(0,latest-Number(state.window));
    const points=checkpoints.filter(row=>row.games>=start).map(row=>({x:row.games,y:row.cards?.[state.trendCard]?.[state.trendMetric]}));
    return`<div class="meta-chart-controls"><label>CARD <select id="meta-trend-card">${Object.keys(cards||{}).map(id=>`<option value="${esc(id)}"${state.trendCard===id?' selected':''}>${esc(cardName(id))}</option>`).join('')}</select></label><label>METRIC <select id="meta-trend-metric">${keys.map(([key,label])=>`<option value="${key}"${state.trendMetric===key?' selected':''}>${label}</option>`).join('')}</select></label><div class="meta-window-buttons" aria-label="History window">${[['100','100'],['500','500'],['1000','1k'],['5000','5k'],['10000','10k'],['all','All']].map(([value,label])=>`<button data-meta-window="${value}"${state.window===value?' class="active"':''}>${label}</button>`).join('')}</div></div><section class="meta-chart-panel"><h3>${esc(cardName(state.trendCard))} · ${keys.find(([key])=>key===state.trendMetric)[1]}</h3>${chart(points,`${cardName(state.trendCard)} ${state.trendMetric}`,state.trendMetric==='damagePerAether'?'':'%')}</section><p class="meta-inline-note">Trends use the full selected dataset across all styles and archetypes. Each point is a saved cumulative estimate at its game count. Window buttons choose the last N simulated games; they do not change the underlying sample denominator.</p>`;
  }
  function versionOptions(selected){return(view.versions||[]).map(version=>{const id=version.id??version.key??version.version;return`<option value="${esc(id)}"${String(id)===String(selected)?' selected':''}>${esc(version.label||version.version||id)} · ${version.available===false?'not recorded':`${num(version.games,0)} games`}</option>`;}).join('');}
  function patchesPanel(){
    const rows=view.patchComparisons||[];
    const comparisonNotice=rows.find(row=>row.limited||row.notice);
    const note=comparisonNotice?`<p class="meta-dataset-note">${esc(comparisonNotice.notice||'Historical metrics are unavailable for part of this comparison.')}</p>`:'';
    return`<div class="meta-chart-controls"><label>FROM <select id="meta-compare-from">${versionOptions(state.compareFrom)}</select></label><label>TO <select id="meta-compare-to">${versionOptions(state.compareTo)}</select></label><button id="meta-compare" class="secondary-button"${view.versions.length<2?' disabled':''}>COMPARE DATASETS</button></div><p class="meta-inline-note">History is retained by version and balance fingerprint. A difference is shown only when both datasets contain that metric; missing historical telemetry remains unavailable. Archetype Δ names the largest overall archetype win-rate change.</p>${note}${rows.length?table([['name','CARD'],['winRateDelta','WR Δ, pp'],['pickRateDelta','PICK Δ, pp'],['damageDelta','DAMAGE Δ'],['usageDelta','USES/G Δ'],['archetypeDelta','ARCHETYPE Δ, pp'],['nBefore','CLEAN N BEFORE'],['nAfter','CLEAN N AFTER']].map(([key,label])=>heading(key,label)).join(''),sorted(rows.filter(row=>!row.id||rowVisible({...row,cleanN:Math.min(row.nBefore??0,row.nAfter??0)}))).map(row=>`<tr><th scope="row">${esc(row.name||cardName(row.id))}</th><td>${delta(row.winRateDelta)}</td><td>${delta(row.pickRateDelta)}</td><td>${delta(row.damageDelta)}</td><td>${delta(row.usageDelta)}</td><td class="meta-long-cell">${esc(row.archetypeName?`${row.archetypeName}: ${delta(row.archetypeDelta)} pp`:row.archetypeDelta!==null&&typeof row.archetypeDelta==='object'?JSON.stringify(row.archetypeDelta):finite(row.archetypeDelta)?`${delta(row.archetypeDelta)} pp`:'—')}</td><td>${num(row.nBefore,0)}</td><td>${num(row.nAfter,0)}</td></tr>`).join('')):empty(view.versions.length<2?'A second saved dataset is required for a patch comparison. Previous game versions appear when their saved data is available.':'Choose two saved datasets and compare their measured card results.')}`;
  }
  function validationPanel(){
    const v=view.validation||{},run=view.status?.validation||view.validationRun||{},running=!!(run.running||run.status==='running');
    const metrics=[['games','Games simulated',0],['gamesPerMinute','Games/minute'],['invalidSimulations','Invalid simulations',0],['nanCount','NaN / Infinity count',0],['mirrorExclusions','Mirror exclusions',0],['averageDuration','Average duration, s'],['averageAetherSpent','Average Aether spent'],['pickRateTotal','Pick-rate total, %'],['workerUsage','Worker usage']];
    const report=run.report||v.report||view.validationReport;
    return`<div class="meta-section-bar"><div><strong>META VALIDATION</strong><span>Numerical and economy sanity checks for the analytical simulator.</span></div><button id="meta-run-validation" class="secondary-button"${running?' disabled':''}>${running?'VALIDATION RUNNING':'RUN 10,000 GAME VALIDATION'}</button></div><dl class="meta-validation-metrics">${metrics.map(([key,label,digits])=>`<div><dt>${label}</dt><dd>${key==='workerUsage'&&typeof v[key]==='string'?esc(v[key]):num(v[key],digits)}</dd></div>`).join('')}</dl>${running?`<div class="meta-validation-progress"><progress max="10000" value="${Number(run.games??run.completed??0)}"></progress><span>${num(run.games??run.completed??0,0)} / 10,000 validation games</span></div>`:''}<div id="meta-validation-report" class="meta-validation-report" role="status">${report?`<h3>${esc(report.title||'Validation report')}</h3>${Array.isArray(report.checks)?`<ul>${report.checks.map(check=>`<li><strong>${check.passed?'PASS':'FAIL'}</strong> ${esc(check.name||check.label)}${check.detail?` · ${esc(check.detail)}`:''}</li>`).join('')}</ul>`:`<pre>${esc(typeof report==='string'?report:JSON.stringify(report,null,2))}</pre>`}`:empty('Run a measured 10,000-game batch to produce a sanity report. Existing dataset totals are displayed above.')}</div>`;
  }
  function render(){
    const panels={cards:cardsPanel,matchups:()=>matrixPanel(false),synergies:()=>matrixPanel(true),archetypes:archetypesPanel,personalities:personalitiesPanel,alerts:alertsPanel,history:historyPanel,patches:patchesPanel,validation:validationPanel};
    content.innerHTML=(view.notice?`<p class="meta-dataset-note">${esc(view.notice)}</p>`:'')+panels[state.tab]();content.setAttribute('aria-label',LABELS[state.tab]);
    const cardFilters=['meta-filter','meta-cost','meta-trait','meta-search'];
    const globalFilters=[...cardFilters,'meta-style','meta-archetype','meta-min-sample'];
    for(const id of [...globalFilters,'meta-version']){const disable=state.tab==='validation'||state.tab==='history'&&globalFilters.includes(id)||state.tab==='archetypes'&&cardFilters.includes(id)||state.tab==='patches'&&['meta-style','meta-archetype'].includes(id);document.getElementById(id).disabled=disable;}
    const activeFilters=['type','cost','trait','style','archetype'].filter(key=>state[key]!=='all').length+(state.search?1:0)+(state.minSample>0?1:0)+(state.version!=='current'?1:0);
    document.getElementById('meta-filter-summary').textContent=state.tab==='validation'?'Full current dataset':state.tab==='history'?'Full dataset · patch only':activeFilters?`${activeFilters} active · tap to edit`:'Card pool and sample slice';
    modal.querySelectorAll('[data-meta-tab]').forEach(button=>{const active=button.dataset.metaTab===state.tab;button.classList.toggle('active',active);button.setAttribute('aria-selected',String(active));if(button.dataset.metaTab==='validation')button.classList.toggle('hidden',!state.validationVisible);});
  }
  function summary(){
    const validation=view.validation||{},status=view.status||{};
    document.getElementById('meta-games').textContent=num(validation.games??status.games??view.games??0,0);
    document.getElementById('meta-rate').textContent=`${num(validation.gamesPerMinute??status.gamesPerMinute??0,0)}/min`;
    const eligible=view.cards.filter(row=>row.cleanN>=30&&finite(row.adjustedWinRate));
    document.getElementById('meta-top').textContent=eligible.length?pct(Math.max(...eligible.map(row=>row.adjustedWinRate))):'N < 30';
    document.getElementById('meta-pause').textContent=status.paused?'RESUME SIM':'PAUSE SIM';
    document.getElementById('meta-sim-status').textContent=status.message|| (status.battlePaused?'Paused for live battle':status.paused?'Simulation paused':status.benchmarking?'Measuring safe simulation budget…':'Simulation active');
    document.getElementById('meta-stat-note').textContent='Adjusted card win rate excludes mirrors and shrinks toward 50% with a 24-game neutral prior. Raw win rate remains separate. Wilson 95% CI uses clean samples; N is always shown. '+(view.modelNote||'Card pick-rate totals approach 800% for eight-card decks. Simulation results are analytical estimates.')+' Developer validation: Ctrl+Shift+V or F9.';
  }
  function updateVersions(){
    const select=document.getElementById('meta-version'),options=versionOptions(state.version);
    const html=`<option value="current"${state.version==='current'?' selected':''}>Current dataset</option>${options}`;
    if(select.innerHTML!==html)select.innerHTML=html;
    if(!state.compareFrom&&view.versions.length)state.compareFrom=String(view.versions[0].id??view.versions[0].key??view.versions[0].version);
    if(!state.compareTo&&view.versions.length)state.compareTo=String(view.versions.at(-1).id??view.versions.at(-1).key??view.versions.at(-1).version);
  }
  const click=event=>{
    const button=event.target.closest('button');if(!button||!modal.contains(button))return;
    if(button.dataset.metaTab){state.tab=button.dataset.metaTab;state.pair=null;render();}
    else if(button.dataset.metaSort){const key=button.dataset.metaSort;state.direction=state.sort===key?-state.direction:(key==='name'?1:-1);state.sort=key;state.styleSort=null;render();}
    else if(button.dataset.metaStyleSort){state.direction=state.styleSort===button.dataset.metaStyleSort?-state.direction:-1;state.styleSort=button.dataset.metaStyleSort;render();}
    else if(button.dataset.metaCard){state.card=button.dataset.metaCard;if(state.tab!=='cards')state.tab='cards';render();content.querySelector('.meta-card-report')?.scrollIntoView({block:'nearest',behavior:'smooth'});}
    else if(button.dataset.metaPair){state.pair=button.dataset.metaPair.split(':');render();content.querySelector('.meta-pair-report')?.scrollIntoView({block:'nearest',behavior:'smooth'});}
    else if(button.dataset.metaWindow){state.window=button.dataset.metaWindow;render();}
    else if(button.id==='meta-pause')onPause?.(!view.status.paused);
    else if(button.id==='meta-reset')onReset?.(filters());
    else if(button.id==='meta-export')onExport?.({subject:document.getElementById('meta-export-subject').value,format:document.getElementById('meta-export-format').value,filters:{...filters(),type:state.type,cost:state.cost,trait:state.trait,search:state.search}});
    else if(button.id==='meta-run-validation')onValidation?.(10000);
    else if(button.id==='meta-compare')onCompare?.({from:state.compareFrom,to:state.compareTo});
  };
  const change=event=>{
    if(event.target.id==='meta-speed'){onSpeed?.(event.target.value==='max'?'max':Number(event.target.value));return;}
    const map={'meta-filter':'type','meta-cost':'cost','meta-trait':'trait','meta-style':'style','meta-archetype':'archetype','meta-version':'version','meta-min-sample':'minSample','meta-columns':'group','meta-trend-card':'trendCard','meta-trend-metric':'trendMetric','meta-compare-from':'compareFrom','meta-compare-to':'compareTo'};
    const key=map[event.target.id];if(!key)return;state[key]=key==='minSample'?Math.max(0,Number(event.target.value)||0):event.target.value;
    if(['style','archetype','version','minSample'].includes(key))onFiltersChange?.(filters());
    render();
  };
  const input=event=>{if(event.target.id==='meta-search'){state.search=event.target.value;render();}};
  const toggle=event=>{if(event.target.id==='meta-pair-list')state.pairListOpen=event.target.open;if(event.target.id==='meta-full-card-report')state.fullReportOpen=event.target.open;};
  const keydown=event=>{if(!event.target.matches('[data-meta-tab]')||!['ArrowLeft','ArrowRight','Home','End'].includes(event.key))return;const tabs=[...modal.querySelectorAll('[data-meta-tab]')].filter(button=>!button.classList.contains('hidden'));const current=tabs.indexOf(event.target),next=event.key==='Home'?0:event.key==='End'?tabs.length-1:(current+(event.key==='ArrowRight'?1:-1)+tabs.length)%tabs.length;event.preventDefault();tabs[next].focus();tabs[next].click();};
  const shortcut=event=>{if(!modal.classList.contains('hidden')&&(event.key==='F9'||(event.ctrlKey||event.metaKey)&&event.shiftKey&&event.key.toLowerCase()==='v')){event.preventDefault();state.validationVisible=true;state.tab='validation';render();}};
  const resize=()=>{const next=window.innerWidth<=700;if(next!==narrow){narrow=next;if(filterDisclosure)filterDisclosure.open=!narrow;}};
  modal.addEventListener('click',click);modal.addEventListener('change',change);modal.addEventListener('input',input);modal.addEventListener('toggle',toggle,true);modal.addEventListener('keydown',keydown);
  document.addEventListener('keydown',shortcut);
  window.addEventListener('resize',resize);
  render();
  return{
    update(next){view={...view,...next,notice:next?.notice||'',limited:!!next?.limited,modelNote:next?.modelNote||'',status:{...view.status,...next?.status},validation:{...view.validation,...next?.validation}};for(const key of ['cards','matchups','synergies','archetypes','personalities','alerts','history','versions','patchComparisons'])if(!Array.isArray(view[key]))view[key]=[];summary();updateVersions();render();},
    setValidationVisible(visible){state.validationVisible=!!visible;if(!visible&&state.tab==='validation')state.tab='cards';render();},
    showValidation(){state.validationVisible=true;state.tab='validation';render();},
    getFilters:filters,
    destroy(){modal.removeEventListener('click',click);modal.removeEventListener('change',change);modal.removeEventListener('input',input);modal.removeEventListener('toggle',toggle,true);modal.removeEventListener('keydown',keydown);document.removeEventListener('keydown',shortcut);window.removeEventListener('resize',resize);},
  };
}
