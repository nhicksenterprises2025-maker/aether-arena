import { createDeckAnalyzer } from './deck-analysis.js';
import { COMBAT_MODEL_VERSION, clamp, aetherIncomeBetween, spellDamageFor, canDirectTarget, refreshSlow, refreshStun, movementMultiplier, directionalSight, wilsonInterval, adjustedWinRate, seededRandom, combatFingerprint } from './combat-rules.js';

export const META_ARCHETYPES = ['Beatdown','Control','Cycle','Bridge Pressure','Split Lane','Air Pressure','Siege','Spell Control','Defensive','Hybrid'];
export const META_STYLE_NAMES = { beatdown:'Beatdown', aggro:'Aggro', control:'Control', cycle:'Cycle', split:'Split Lane', spell_cycle:'Spell Cycle', counter:'Counter Push' };
const METRICS = ['uses','aether','troopDamage','towerDamage','buildingDamage','damageTaken','kills','deaths','spawned','survivors','lifetime','placementX','placementZ','killValue','openingEligible','openingPlayed','firstPlays','otUses','connected','crowns','prevented','pulls','buildingLifetime','buildingCapacity','targets','spellValue','overkill','slowSeconds','slowTrackedSeconds','auraDamage','stunnedUnits','stunSeconds','stunTrackedSeconds','occupancySeconds','zoneSeconds','dotTicks','initialDamage','dotDamage'];
const divide = (a,b) => b ? a / b : 0;
const emptyStat = () => Object.fromEntries(['appearances','cleanN','score','mirror',...METRICS].map(k => [k,0]));
const emptyBucket = ids => ({ n:0, score:0, crowns:0, duration:0, cards:Object.fromEntries(ids.map(id => [id,emptyStat()])), pairs:{}, opponents:{} });
function shuffle(ids,rng) { const out=ids.slice(); for(let i=out.length-1;i;i--){const j=Math.floor(rng()*(i+1));[out[i],out[j]]=[out[j],out[i]];} return out; }
function finiteCount(value) { return Number.isFinite(value) && value >= 0 ? value : 0; }
function distance(a,b) { return Math.hypot(a.x-b.x,a.z-b.z); }
function rowStat() { return { ...Object.fromEntries(METRICS.map(k => [k,0])), casts:new Set(), connections:new Set(), openingUsed:false }; }

export function createMetaEngine(cards,options={}) {
  const ids=Object.keys(cards), rules=options.rules;
  if(ids.length<8 || !rules?.match || !rules?.arena || !rules?.sight || !rules?.towers?.guard || !rules?.towers?.core) throw new Error('Meta engine requires the live card library, arena, sight, match and tower rules.');
  const analyzer=options.analyzer || createDeckAnalyzer(cards), styles=options.styles || META_STYLE_NAMES;
  const styleIds=Object.keys(META_STYLE_NAMES), identity={version:options.version||'V15',model:COMBAT_MODEL_VERSION,fingerprint:combatFingerprint(cards,rules,styles)};
  const now=()=>typeof performance!=='undefined'?performance.now():Date.now();
  let seed=(options.seed>>>0)||151515, startClock=now(), startGames=0, startSimulationMilliseconds=0, dataset;
  const counterCache=new Map(),synergyCache=new Map();
  const counter=(a,b)=>{const key=`${a}|${b}`;if(!counterCache.has(key))counterCache.set(key,analyzer.counterScore(a,b));return counterCache.get(key);};
  const synergy=(a,b)=>{const key=[a,b].sort().join('|');if(!synergyCache.has(key))synergyCache.set(key,analyzer.synergy(a,b).score);return synergyCache.get(key);};
  const fresh=()=>({schema:2,identity:{...identity},seed,nextSeed:seed,games:0,invalidSimulations:0,nanCount:0,mirrorExclusions:0,duration:0,aetherSpent:0,aetherLeaked:0,crowns:0,simulationMilliseconds:0,cards:Object.fromEntries(ids.map(id=>[id,emptyStat()])),all:emptyBucket(ids),styles:Object.fromEntries(styleIds.map(style=>[style,emptyBucket(ids)])),archetypes:Object.fromEntries(META_ARCHETYPES.map(name=>[name,emptyBucket(ids)])),slices:{},history:[]});
  dataset=fresh();
  const engine={get dataset(){return dataset;},identity,simulate,ingest,runBatch,view,snapshot:()=>JSON.parse(JSON.stringify(dataset)),validation,reset,restore,matchup};
  if(options.saved)restore(options.saved);
  function reset(newSeed=seed){seed=(newSeed>>>0)||151515;dataset=fresh();startClock=now();startGames=0;startSimulationMilliseconds=0;return engine.snapshot();}
  function restore(saved) {
    if(!saved || saved.schema!==2 || saved.identity?.fingerprint!==identity.fingerprint) return false;
    const clean=fresh();
    for(const key of ['seed','nextSeed','games','invalidSimulations','nanCount','mirrorExclusions','duration','aetherSpent','aetherLeaked','crowns','simulationMilliseconds'])clean[key]=finiteCount(saved[key]);
    function loadBucket(raw){const b=emptyBucket(ids);if(!raw)return b;for(const k of ['n','score','crowns','duration'])b[k]=finiteCount(raw[k]);for(const id of ids)for(const k of Object.keys(b.cards[id])){const value=raw.cards?.[id]?.[k];b.cards[id][k]=(k==='placementX'||k==='placementZ')?(Number.isFinite(value)?value:0):finiteCount(value);}for(const [key,p]of Object.entries(raw.pairs||{})){const [a,bId]=key.split('|');if(ids.includes(a)&&ids.includes(bId))b.pairs[key]={n:finiteCount(p.n),score:finiteCount(p.score)};}for(const name of META_ARCHETYPES)if(raw.opponents?.[name])b.opponents[name]={n:finiteCount(raw.opponents[name].n),score:finiteCount(raw.opponents[name].score)};return b;}
    clean.all=loadBucket(saved.all);clean.cards=clean.all.cards;
    for(const style of styleIds)clean.styles[style]=loadBucket(saved.styles?.[style]);
    for(const name of META_ARCHETYPES)clean.archetypes[name]=loadBucket(saved.archetypes?.[name]);
    for(const [key,b]of Object.entries(saved.slices||{})){const [style,name]=key.split('|');if(styleIds.includes(style)&&META_ARCHETYPES.includes(name))clean.slices[key]=loadBucket(b);}
    clean.history=Array.isArray(saved.history)?saved.history.filter(h=>Number.isFinite(h.games)&&h.games<=clean.games).slice(-2000):[];
    dataset=clean;seed=clean.nextSeed||seed;startClock=now();startGames=clean.games;startSimulationMilliseconds=clean.simulationMilliseconds;return true;
  }
  function validDeck(deck){return Array.isArray(deck)&&deck.length===8&&new Set(deck).size===8&&deck.every(id=>Object.hasOwn(cards,id));}
  function deckFor(style,rng){
    // Independent exploration prevents learned WR feeding back into future selection.
    if(rng()<.16){for(let i=0;i<40;i++){const deck=shuffle(ids,rng).slice(0,8),p=analyzer.analyzeDeck(deck);if(p.counts.antiAir>=2&&p.counts.winConditions>=1&&p.counts.spells>=1&&p.counts.troops>=4)return deck;}}
    return analyzer.buildAiDeck(style,rng);
  }
  function simulate(matchSeed=dataset.nextSeed++,settings={}) {
    const clock=now(),rng=seededRandom(matchSeed),match=rules.match,arena=rules.arena,tile=rules.grid?.tile||1,dt=.20;
    const styleA=settings.styleA||styleIds[Math.floor(rng()*styleIds.length)],styleB=settings.styleB||styleIds[Math.floor(rng()*styleIds.length)];
    const deckA=settings.deckA||deckFor(styleA,rng),deckB=settings.deckB||deckFor(styleB,rng);
    if(!validDeck(deckA)||!validDeck(deckB))throw new Error('Simulation decks must contain eight unique live cards.');
    const sides=[deckA,deckB].map((deck,team)=>{
      const order=shuffle(deck,rng),analysis=analyzer.analyzeDeck(deck);
      return {team,deck,style:team?styleB:styleA,archetypes:analysis.archetypes.filter(n=>META_ARCHETYPES.includes(n)),rows:Object.fromEntries(deck.map(id=>[id,rowStat()])),hand:order.slice(0,4),queue:order.slice(4),opening:order.slice(0,4),aether:5,spent:0,leaked:0,crowns:0,plays:0,nextDecision:rng()*.5,lane:rng()<.5?-1:1,ownProfile:analysis};
    });
    for(const side of sides){if(!side.archetypes.length)side.archetypes=['Hybrid'];for(const id of side.opening)side.rows[id].openingEligible++;}
    let time=0,uid=0,castId=0,winner=null,finished=false,overtime=false,tiebreaker=false;
    const units=[],buildings=[],towers=[],shots=[],zones=[],events=settings.record?[]:null;
    const emit=e=>{if(events)events.push({time:Number(time.toFixed(3)),...e});};
    for(let team=0;team<2;team++)for(const [kind,x,z,lane]of [['core',0,16.3,0],['guard',-8.2,12.4,-1],['guard',8.2,12.4,1]]){
      const c=rules.towers[kind];towers.push({uid:++uid,team,kind,tower:true,x,z:z*(team?-1:1),lane,hp:c.hp,maxHp:c.hp,damage:c.damage,attackSpeed:c.attackSpeed,range:c.range*tile,radius:c.radius||(kind==='core'?1.35:1.15),cooldown:0,active:kind!=='core',dead:false,target:null,contributions:{}});
    }
    const allEnemies=team=>[...units,...buildings,...towers].filter(e=>!e.dead&&e.team!==team);
    function row(source){return source?.card?sides[source.team].rows[source.card.id]:null;}
    function pathDistance(a,b){if(a.flying||a.projectile||a.tower||a.building||Math.sign(a.z)===Math.sign(b.z)||Math.abs(a.z)<arena.riverHalf||Math.abs(b.z)<arena.riverHalf)return distance(a,b);const bx=a.bridgeX??(b.lane?b.lane*arena.bridgeX:(a.x<0?-arena.bridgeX:arena.bridgeX));return Math.hypot(a.x-bx,a.z-Math.sign(a.z)*arena.riverHalf)+arena.riverHalf*2+Math.hypot(b.x-bx,b.z-Math.sign(b.z)*arena.riverHalf);}
    function kill(target,source=null,decay=false){
      if(target.dead)return;target.dead=true;target.deathAt=time;
      const victim=row(target);if(victim){victim.deaths++;victim.lifetime+=time-target.born;if(target.building)victim.buildingLifetime+=time-target.born;}
      const attacker=row(source);if(attacker&&!target.tower&&!decay){attacker.kills++;attacker.killValue+=(target.card?.cost||0)/(target.card?.count||1);}
      emit({type:'kill',source:source?.uid??null,target:target.uid,cardId:source?.card?.id??null,targetCardId:target.card?.id??null,team:source?.team??null});
      if(target.tower){
        const side=sides[1-target.team],previousCrowns=side.crowns;side.crowns=target.kind==='core'?3:side.crowns+1;
        const total=Object.values(target.contributions).reduce((a,b)=>a+b,0),award=side.crowns-previousCrowns;
        for(const [id,value]of Object.entries(target.contributions)){if(side.rows[id])side.rows[id].crowns+=award*divide(value,total);}
        const core=towers.find(t=>t.team===target.team&&t.kind==='core');if(core)core.active=true;
        emit({type:'tower-destroyed',target:target.uid,team:target.team,crowns:sides.map(s=>s.crowns)});
        if(target.kind==='core'||overtime){winner=1-target.team;finished=true;}
      }
    }
    function hit(source,target,amount,kind='direct',cast=null){
      if(finished||target.dead||amount<=0)return 0;
      const actual=Math.min(target.hp,amount),overkill=amount-actual;target.hp-=actual;
      const r=row(source),v=row(target);
      if(r){r[target.tower?'towerDamage':target.building?'buildingDamage':'troopDamage']+=actual;if(source.card.spell){r.overkill+=overkill;r.spellValue+=actual/target.maxHp*(target.card?.cost||0)/(target.card?.count||1);if(kind==='initial')r.initialDamage+=actual;if(kind==='dot')r.dotDamage+=actual;}if(kind==='aura')r.auraDamage+=actual;if(target.tower){r.connections.add(source.cast);target.contributions[source.card.id]=(target.contributions[source.card.id]||0)+actual;}}
      if(v){v.damageTaken+=actual;if(target.building)v.prevented+=actual;}
      if(target.kind==='core')target.active=true;
      emit({type:'damage',source:source?.uid??null,cardId:source?.card?.id??null,target:target.uid,targetCardId:target.card?.id??null,targetKind:target.tower?'tower':target.building?'building':'troop',team:source?.team??null,damage:actual,requested:amount,hp:target.hp,kind});
      if(target.hp<=0)kill(target,source);
      return actual;
    }
    function makeEntity(card,team,x,z,cast,offset=0){
      const entity={uid:++uid,card,team,x:x+offset,z,hp:card.hp,maxHp:card.hp,range:card.range*tile,radius:card.building?(card.footprint||1.6)*.52:.44*(card.scale||1),damage:card.damage,attackSpeed:card.attackSpeed,moveSpeed:(card.moveSpeed||0)*tile,projectile:!!card.projectile,projectileSpeed:card.projectileSpeed||14,flying:!!card.flying,building:!!card.building,buildingsOnly:!!card.buildingsOnly,canHitAir:!!card.canHitAir,born:time,cast,cooldown:card.building?.35:0,target:null,dead:false,chargeTime:0,charged:false,auraTimer:card.auraInterval||0,slowUntil:0,slowPct:0,stunUntil:0,facingX:0,facingZ:team?1:-1,lane:x<0?-1:1,lock:null,pulls:new Set(),forcedTarget:null,forcedUntil:0};
      row(entity).spawned++;
      if(card.building){buildings.push(entity);row(entity).buildingCapacity+=card.lifetime;}
      else{
        units.push(entity);
        for(const attacker of units){if(attacker===entity||attacker.dead||attacker.team===team||attacker.buildingsOnly||attacker.lock?.tower||!attacker.target||!(attacker.target.tower||attacker.target.building)||!canDirectTarget(attacker.card,entity))continue;const pullRadius=directionalSight(attacker,entity,rules.sight,rules.retarget?.towerPullBonusTiles||1.75)*tile+entity.radius+attacker.radius*.35;if(pathDistance(attacker,entity)<=pullRadius){attacker.forcedTarget=entity;attacker.forcedUntil=time+(rules.retarget?.forcedLockSeconds||1.2);attacker.target=entity;attacker.cooldown=Math.min(attacker.cooldown,Math.max(.08,attacker.attackSpeed*.32));attacker.chargeTime=0;attacker.charged=false;}}
      }
      return entity;
    }
    function castSpell(side,card,pos,cast){
      const source={uid:++uid,card,team:side.team,cast},r=side.rows[card.id],targets=allEnemies(side.team),hitSet=new Set();
      for(const target of targets){const structure=target.tower||target.building,pad=structure?(card.spellKind==='nova'?.4:.25):card.spellKind==='nova'?0:.2;if(distance(pos,target)<=card.radius*tile+target.radius*pad&&spellDamageFor(card,structure)>0){hitSet.add(target.uid);hit(source,target,spellDamageFor(card,structure),'initial');}}
      r.targets+=hitSet.size;
      if(card.dotDamage)zones.push({source,x:pos.x,z:pos.z,radius:card.radius*tile,born:time,end:time+card.dotDuration,next:time+card.dotInterval,targets:hitSet});
    }
    function play(side,id,pos,reason){
      const card=cards[id],r=side.rows[id],index=side.hand.indexOf(id);
      if(index<0||side.aether+1e-8<card.cost)return;
      side.aether-=card.cost;side.spent+=card.cost;r.aether+=card.cost;r.uses++;r.placementX+=pos.x;r.placementZ+=pos.z;const cast=++castId;r.casts.add(cast);
      if(side.plays===0)r.firstPlays++;
      if(side.opening.includes(id)&&!r.openingUsed&&side.plays<4){r.openingPlayed++;r.openingUsed=true;}
      if(overtime)r.otUses++;
      side.plays++;side.hand[index]=side.queue.shift();side.queue.push(id);
      emit({type:'play',team:side.team,cardId:id,tile:{x:pos.x,z:pos.z},aether:side.aether,reason});
      if(card.spell)castSpell(side,card,pos,cast);
      else{const formation=(card.count||1)>=5?[[-.86,-.18],[0,-.42],[.86,-.18],[-.43,.38],[.43,.38]]:card.count===2?[[-.42,.12],[.42,-.12]]:[[0,0]];for(const [x,z]of formation)makeEntity(card,side.team,pos.x+x,pos.z+z,cast);}
    }
    function decide(side){
      const style=side.style,config=styles[style]||{},dir=side.team?1:-1,enemyUnits=units.filter(u=>!u.dead&&u.team!==side.team),threats=enemyUnits.filter(u=>u.z*dir< -1),available=side.hand.filter(id=>cards[id].cost<=side.aether+1e-8);
      if(!available.length)return;
      let best=null;
      for(const id of available){
        const c=cards[id];let score=-100,pos,reason='bank';
        if(c.spell){
          let targetBest=null,targetValue=0;
          const targets=allEnemies(side.team);
          for(const center of targets){let value=0;for(const t of targets){if(distance(center,t)<=c.radius+t.radius*.2){const dmg=spellDamageFor(c,t.building||t.tower);value+=Math.min(t.hp,dmg)/t.maxHp*(t.card?.cost|| (t.tower?.7:0))/(t.card?.count||1);if(c.dotDamage&&!t.building&&!t.tower)value+=Math.min(Math.max(0,t.hp-dmg),c.dotDamage*Math.min(c.dotDuration,2*c.radius/Math.max(.1,t.moveSpeed)))/t.maxHp*t.card.cost/(t.card.count||1);}}if(value>targetValue){targetValue=value;targetBest=center;}}
          if(targetBest){score=targetValue*2-c.cost*.45+(style==='spell_cycle'?1.1:0);pos={x:Math.round(targetBest.x),z:Math.round(targetBest.z)};reason='area value';if(targetValue<c.cost*.38&&side.aether<9.6)score=-100;}
        }else if(c.building){
          if(threats.length){const threat=threats.slice().sort((a,b)=>Math.abs(b.z)-Math.abs(a.z))[0];score=2+(threat.buildingsOnly?2:0)+(style==='control'||style==='counter'?1:0);pos={x:Math.round(threat.x*.45),z:-dir*5};reason='defensive pull';}
        }else{
          if(threats.length){let threatScore=-1,target=null;for(const t of threats){const counterValue=counter(id,t.card.id);const s=counterValue/20+Math.abs(t.z)*.08;if(s>threatScore&&canDirectTarget(c,t)){threatScore=s;target=t;}}if(target){score=threatScore+(config.defendBias||1);pos={x:Math.round(target.x),z:Math.round(clamp(target.z-dir*(c.range>3?3:1),side.team?-19:arena.playerMinZ,side.team?arena.enemyMaxZ:19))};reason='visible defense';}}
          const threshold=config.bankThreshold||8,ready=side.aether>=threshold||style==='cycle'&&side.aether>=5||style==='aggro'&&side.aether>=6;
          if(ready||(!threats.length&&side.aether>9.5)){
            const own=units.filter(u=>!u.dead&&u.team===side.team&&Math.sign(u.x)===side.lane),anchor=own.find(u=>u.buildingsOnly),pairSynergy=anchor?synergy(id,anchor.card.id)/30:0;
            const priority=config.pushPriority?.indexOf(id)??-1;let pushScore=(c.buildingsOnly?2:1)+(priority<0?0:(6-priority)*.2)+pairSynergy+side.aether*.13+(style==='cycle'?(5-c.cost)*.4:0);
            if(pushScore>score){score=pushScore;const pocket=towers.some(t=>t.team!==side.team&&t.kind==='guard'&&t.lane===side.lane&&t.dead);pos={x:side.lane*7,z:pocket?dir*6:-dir*(c.buildingsOnly||style==='aggro'||style==='cycle'?3:15)};reason=anchor?'support visible push':'pressure';}
          }
        }
        if(pos&&score>(best?.score??-50))best={id,pos,score,reason};
      }
      if(best&&best.score>0){play(side,best.id,best.pos,best.reason);if(style==='split')side.lane*=-1;else if(rng()<.1)side.lane*=-1;side.nextDecision=time+.55+rng()*.5;}else side.nextDecision=time+.45;
    }
    function chooseTarget(source){
      if(source.lock&&!source.lock.dead)return source.lock;
      if(source.tower){if(!source.active)return null;return units.filter(u=>!u.dead&&u.team!==source.team&&distance(source,u)<=source.range+u.radius).sort((a,b)=>distance(source,a)-distance(source,b))[0]||null;}
      if(source.building)return units.filter(u=>!u.dead&&u.team!==source.team&&canDirectTarget(source.card,u)&&distance(source,u)<=source.range+u.radius+.35&&distance(source,u)<=directionalSight(source,u,rules.sight)*tile+u.radius+source.radius*.2).sort((a,b)=>distance(source,a)-distance(source,b))[0]||null;
      if(source.forcedTarget){const target=source.forcedTarget;if(!target.dead&&time<source.forcedUntil&&canDirectTarget(source.card,target)&&distance(source,target)<=directionalSight(source,target,rules.sight,rules.retarget?.troopLeashBonusTiles||2.25)*tile+target.radius)return target;source.forcedTarget=null;}
      const structures=[...buildings,...towers].filter(e=>!e.dead&&e.team!==source.team);
      if(!source.buildingsOnly){const current=source.target;if(current&&!current.tower&&!current.building&&!current.dead&&canDirectTarget(source.card,current)&&distance(source,current)<=directionalSight(source,current,rules.sight,rules.retarget?.troopLeashBonusTiles||2.25)*tile+current.radius+source.radius*.2)return current;const visible=units.filter(u=>!u.dead&&u.team!==source.team&&canDirectTarget(source.card,u)&&distance(source,u)<=directionalSight(source,u,rules.sight)*tile+u.radius+source.radius*.2);if(visible.length)return visible.sort((a,b)=>pathDistance(source,a)-pathDistance(source,b)-(a.z-b.z)*(source.team?1:-1)*.06)[0];}
      if(source.buildingsOnly)return structures.sort((a,b)=>pathDistance(source,a)-pathDistance(source,b))[0]||null;
      const b=structures.filter(s=>s.building&&distance(source,s)<=directionalSight(source,s,rules.sight)*tile+s.radius+source.radius*.2).sort((a,b)=>pathDistance(source,a)-pathDistance(source,b))[0];
      return b||structures.find(s=>s.kind==='guard'&&s.lane===source.lane)||structures.sort((a,b)=>pathDistance(source,a)-pathDistance(source,b))[0]||null;
    }
    function move(source,target){
      let dest=target;
      if(!source.flying&&Math.sign(source.z)!==Math.sign(target.z)&&Math.abs(source.z)>arena.riverHalf){
        source.bridgeX??=target.lane?target.lane*arena.bridgeX:(source.x<0?-arena.bridgeX:arena.bridgeX);
        if(Math.abs(source.x-source.bridgeX)>.4||Math.abs(source.z)>arena.riverHalf+.3)dest={x:source.bridgeX,z:Math.sign(source.z)*(arena.riverHalf+.2)};
        else dest={x:source.bridgeX,z:-Math.sign(source.z)*(arena.riverHalf+.3)};
      }else if(!source.flying&&Math.abs(source.z)<=arena.riverHalf+.3){const bx=source.bridgeX??(source.x<0?-arena.bridgeX:arena.bridgeX);dest={x:bx,z:Math.sign(target.z)*(arena.riverHalf+.4)};}
      const dx=dest.x-source.x,dz=dest.z-source.z,len=Math.hypot(dx,dz),step=Math.min(len,source.moveSpeed*movementMultiplier(source,time)*dt);
      if(len){source.x+=dx/len*step;source.z+=dz/len*step;source.facingX=dx/len;source.facingZ=dz/len;}
      if(source.card.charger&&step>.002){source.chargeTime+=dt;if(source.chargeTime>1.65)source.charged=true;}
    }
    function attack(source,target){
      let dmg=source.damage;if(source.card?.charger&&source.charged&&(target.tower||target.building)){dmg=source.card.chargeDamage;source.charged=false;source.chargeTime=0;}
      source.cooldown+=source.attackSpeed;
      if(source.projectile||source.tower){const sourceY=source.tower?2.48:source.building?2.55:source.flying?2.1:1.35,targetY=target.tower||target.building?1.35:target.flying?1.95:.86;shots.push({source,target,damage:dmg,end:time+Math.max(.11,Math.hypot(distance(source,target),sourceY-targetY)/(source.tower?17:source.projectileSpeed)),splash:(source.card?.splash||0)*tile});}
      else{hit(source,target,dmg);if(source.card.slowPct&&!target.dead&&!target.tower&&!target.building){refreshSlow(target,source.card.slowPct,source.card.slowDuration,time);target.slowSource=source;target.slowTrackedSource=source;}}
    }
    emit({type:'start',seed:matchSeed,decks:sides.map(s=>s.deck),styles:sides.map(s=>s.style),aether:[5,5]});
    while(!finished&&time<match.regulation+match.overtime+30){
      const previous=time;time=Math.min(time+dt,match.regulation+match.overtime);
      const income=aetherIncomeBetween(previous,time,match);
      for(const side of sides){const added=side.aether+income;side.leaked+=Math.max(0,added-10);side.aether=Math.min(10,added);if(time>=side.nextDecision)decide(side);}
      for(let i=zones.length-1;i>=0;i--){const zone=zones[i],r=row(zone.source),active=Math.max(0,Math.min(time,zone.end)-Math.max(previous,zone.born)),inside=units.filter(u=>!u.dead&&u.team!==zone.source.team&&distance(zone,u)<=zone.radius+u.radius*.2);r.zoneSeconds+=active;r.occupancySeconds+=inside.length*active;while(zone.next<=time+1e-8&&zone.next<=zone.end+1e-8){r.dotTicks++;for(const u of inside){if(!zone.targets.has(u.uid)){zone.targets.add(u.uid);r.targets++;}hit(zone.source,u,zone.source.card.dotDamage,'dot');}zone.next+=zone.source.card.dotInterval;}if(time>=zone.end)zones.splice(i,1);}
      for(let i=shots.length-1;i>=0;i--){const shot=shots[i];if(shot.target.dead){shots.splice(i,1);continue;}if(shot.end<=time){hit(shot.source,shot.target,shot.damage);if(shot.splash)for(const other of allEnemies(shot.source.team)){if(other!==shot.target&&distance(other,shot.target)<=shot.splash+(other.building?other.radius*.35:other.tower?other.radius*.25:0))hit(shot.source,other,shot.damage,'splash');}shots.splice(i,1);}}
      const order=(matchSeed&1)?[...towers,...buildings,...units]:[...units,...buildings,...towers];
      for(const source of order){
        if(source.dead||finished)continue;
        if(source.building){source.hp-=source.maxHp/source.card.lifetime*dt;if(source.hp<=0){source.hp=0;kill(source,null,true);continue;}}
        if(source.slowTrackedSource)row(source.slowTrackedSource).slowTrackedSeconds+=dt;
        if(source.stunTrackedSource)row(source.stunTrackedSource).stunTrackedSeconds+=dt;
        if(time<source.slowUntil&&source.slowSource)row(source.slowSource).slowSeconds+=dt;
        if(time<source.stunUntil&&source.stunSource)row(source.stunSource).stunSeconds+=dt;
        source.cooldown=Math.max(-dt,source.cooldown-dt);
        if(source.card?.auraDamage){source.auraTimer-=dt;if(source.auraTimer<=0){source.auraTimer+=source.card.auraInterval;for(const target of units){if(!target.dead&&target.team!==source.team&&distance(source,target)<=source.card.auraRadius*tile+target.radius){hit(source,target,source.card.auraDamage,'aura');if(!target.dead){refreshStun(target,source.card.stunDuration,time);target.stunSource=source;target.stunTrackedSource=source;row(source).stunnedUnits++;}}}}}
        if(time<source.stunUntil)continue;
        if(source.tower&&source.kind==='core'&&!source.active)continue;
        const previousTarget=source.target;
        if(source.tower&&source.target&&!source.target.dead&&distance(source,source.target)<=source.range+.7){}else source.target=chooseTarget(source);
        const target=source.target;if(!target)continue;
        if(target.building&&source.buildingsOnly&&previousTarget!==target&&!source.pulls.has(target.uid)){source.pulls.add(target.uid);row(target).pulls++;}
        const attackRange=source.range+target.radius;
        if(source.tower||source.building||pathDistance(source,target)<=attackRange){if(target.tower&&!source.tower&&!source.building||source.buildingsOnly&&target.building)source.lock=target;source.chargeTime=Math.max(0,source.chargeTime-dt*.7);if(source.cooldown<=0)attack(source,target);}
        else if(!source.tower&&!source.building)move(source,target);
      }
      if(finished)break;
      for(let i=units.length-1;i>=0;i--)if(units[i].dead)units.splice(i,1);
      for(let i=buildings.length-1;i>=0;i--)if(buildings[i].dead)buildings.splice(i,1);
      if(time>=match.regulation-1e-8&&!overtime){
        time=match.regulation;
        if(!sides[0].crowns&&!sides[1].crowns){overtime=true;emit({type:'overtime'});}
        else{winner=sides[0].crowns===sides[1].crowns?null:sides[0].crowns>sides[1].crowns?0:1;finished=true;}
      }
      if(overtime&&time>=match.regulation+match.overtime-1e-8){
        tiebreaker=true;const living=[0,1].map(team=>towers.filter(t=>!t.dead&&t.team===team)),mins=living.map(ts=>Math.min(...ts.map(t=>t.hp))),totals=living.map(ts=>ts.reduce((a,t)=>a+t.hp,0));
        winner=mins[0]!==mins[1]?(mins[0]>mins[1]?0:1):totals[0]!==totals[1]?(totals[0]>totals[1]?0:1):rng()<.5?0:1;
        time+=.85+Math.min(...mins)/match.tiebreakDrainPerSecond;sides[winner].crowns=1;sides[1-winner].crowns=0;finished=true;emit({type:'tiebreaker',winner});
      }
    }
    for(const source of [...units,...buildings]){const r=row(source);r.survivors++;r.lifetime+=time-source.born;if(source.building)r.buildingLifetime+=Math.min(source.card.lifetime,time-source.born);}
    for(const side of sides)for(const r of Object.values(side.rows)){r.connected=r.connections.size;delete r.connections;delete r.casts;delete r.openingUsed;}
    emit({type:'end',winner,crowns:sides.map(s=>s.crowns)});
    const result={seed:matchSeed,winner,duration:time,overtime,tiebreaker,sides:sides.map(s=>({team:s.team,deck:s.deck,style:s.style,archetypes:s.archetypes,rows:s.rows,spent:s.spent,leaked:s.leaked,crowns:s.crowns,remainingAether:s.aether})),milliseconds:now()-clock,events};
    return result;
  }
  function ingest(result){
    let nonfinite=0;for(const side of result.sides||[])for(const r of Object.values(side.rows||{}))for(const value of Object.values(r))if(typeof value==='number'&&!Number.isFinite(value))nonfinite++;
    if(nonfinite){dataset.nanCount+=nonfinite;dataset.invalidSimulations++;return false;}
    if(!result.sides||result.sides.length!==2||!result.sides.every(s=>validDeck(s.deck))||!Number.isFinite(result.duration)||result.duration<=0){dataset.invalidSimulations++;return false;}
    for(const side of result.sides){const budget=5+aetherIncomeBetween(0,Math.min(result.duration,rules.match.regulation+rules.match.overtime),rules.match);if(side.spent>budget+1e-5||side.remainingAether< -1e-6||side.remainingAether>10+1e-6){dataset.invalidSimulations++;return false;}}
    dataset.games++;dataset.duration+=result.duration;dataset.aetherSpent+=result.sides.reduce((n,s)=>n+s.spent,0);dataset.aetherLeaked+=result.sides.reduce((n,s)=>n+s.leaked,0);dataset.crowns+=result.sides.reduce((n,s)=>n+s.crowns,0);dataset.simulationMilliseconds+=result.milliseconds||0;
    function add(bucket,side,opponent,score){
      bucket.n++;bucket.score+=score;bucket.crowns+=side.crowns;bucket.duration+=result.duration;
      for(const id of side.deck){const s=bucket.cards[id],r=side.rows[id];s.appearances++;if(opponent.deck.includes(id))s.mirror++;else{s.cleanN++;s.score+=score;}for(const key of METRICS)s[key]+=r?.[key]||0;}
      const sorted=side.deck.slice().sort();for(let a=0;a<sorted.length;a++)for(let b=a+1;b<sorted.length;b++){if(opponent.deck.includes(sorted[a])&&opponent.deck.includes(sorted[b]))continue;const key=`${sorted[a]}|${sorted[b]}`,pair=bucket.pairs[key]||(bucket.pairs[key]={n:0,score:0});pair.n++;pair.score+=score;}
      for(const name of opponent.archetypes){const p=bucket.opponents[name]||(bucket.opponents[name]={n:0,score:0});p.n++;p.score+=score;}
    }
    for(const side of result.sides){const opponent=result.sides[1-side.team],score=result.winner===null?.5:result.winner===side.team?1:0;add(dataset.all,side,opponent,score);add(dataset.styles[side.style],side,opponent,score);for(const name of side.archetypes){if(!dataset.archetypes[name])continue;add(dataset.archetypes[name],side,opponent,score);const key=`${side.style}|${name}`,slice=dataset.slices[key]||(dataset.slices[key]=emptyBucket(ids));add(slice,side,opponent,score);}dataset.mirrorExclusions+=side.deck.filter(id=>opponent.deck.includes(id)).length;}
    dataset.cards=dataset.all.cards;
    if(dataset.games%100===0){dataset.history.push({games:dataset.games,cards:Object.fromEntries(cardRows(dataset.all).map(r=>[r.id,{winRate:r.adjustedWinRate,pickRate:r.pickRate,damagePerAether:r.damagePerAether}]))});if(dataset.history.length>2000)dataset.history=[...dataset.history.slice(0,1000).filter((h,i)=>i%2===0),...dataset.history.slice(1000)];}
    return true;
  }
  function runBatch(count,settings={}){const n=Math.max(0,Math.floor(count));for(let i=0;i<n;i++){const match=simulate(settings.seed===undefined?dataset.nextSeed++:settings.seed+i,settings);ingest(match);settings.onMatch?.(match,i+1);}return validation();}
  function bucketFor(filters={}){if(filters.style&&filters.style!=='all'&&filters.archetype&&filters.archetype!=='all')return dataset.slices[`${filters.style}|${filters.archetype}`]||emptyBucket(ids);if(filters.style&&filters.style!=='all')return dataset.styles[filters.style]||emptyBucket(ids);if(filters.archetype&&filters.archetype!=='all')return dataset.archetypes[filters.archetype]||emptyBucket(ids);return dataset.all;}
  function cardRows(bucket){return ids.map(id=>{
    const c=cards[id],s=bucket.cards[id],n=s.appearances,ci=wilsonInterval(s.score,s.cleanN),damage=s.troopDamage+s.towerDamage+s.buildingDamage;
    return {id,name:c.name,category:c.spell?'Spell':c.building?'Building':'Troop',cost:c.cost,flying:!!c.flying,winCondition:!!c.buildingsOnly,splash:!!c.splash||!!c.spell||!!c.auraDamage,swarm:(c.count||1)>1,pickRate:divide(n,bucket.n)*100,rawWinRate:s.cleanN?divide(s.score,s.cleanN)*100:null,adjustedWinRate:adjustedWinRate(s.score,s.cleanN),winRate:adjustedWinRate(s.score,s.cleanN),ciLow:ci.low,ciHigh:ci.high,cleanN:s.cleanN,sample:s.cleanN,appearances:n,mirrorExclusions:s.mirror,usesPerGame:divide(s.uses,n),aetherPerGame:divide(s.aether,n),troopDamagePerGame:divide(s.troopDamage,n),towerDamagePerGame:divide(s.towerDamage,n),buildingDamagePerGame:divide(s.buildingDamage,n),damageTakenPerGame:divide(s.damageTaken,n),killsPerGame:divide(s.kills,n),deathsPerGame:divide(s.deaths,n),averageLifetime:divide(s.lifetime,s.spawned),averagePlacementX:divide(s.placementX,s.uses),averagePlacementZ:divide(s.placementZ,s.uses),averageKillValue:divide(s.killValue,s.kills),damagePerAether:divide(damage,s.aether),towerDamagePerAether:divide(s.towerDamage,s.aether),survivalRate:divide(s.survivors,s.spawned)*100,openingHandPlayRate:divide(s.openingPlayed,s.openingEligible)*100,firstPlayRate:divide(s.firstPlays,n)*100,otPlayRate:divide(s.otUses,s.uses)*100,connectionRate:divide(s.connected,s.uses)*100,crownContribution:divide(s.crowns,n),damagePreventedPerGame:divide(s.prevented,n),pullsPerGame:divide(s.pulls,n),lifetimeUtilization:divide(s.buildingLifetime,s.buildingCapacity)*100,targetsPerCast:divide(s.targets,s.uses),spellAetherValuePerCast:divide(s.spellValue,s.uses),overkillPerCast:divide(s.overkill,s.uses),damagePerCast:divide(damage,s.uses),slowUptime:divide(s.slowSeconds,s.slowTrackedSeconds)*100,auraDamagePerGame:divide(s.auraDamage,n),unitsStunnedPerGame:divide(s.stunnedUnits,n),stunUptime:divide(s.stunSeconds,s.stunTrackedSeconds)*100,zoneOccupancy:divide(s.occupancySeconds,s.zoneSeconds),dotTicksPerCast:divide(s.dotTicks,s.uses),initialDamagePerGame:divide(s.initialDamage,n),dotDamagePerGame:divide(s.dotDamage,n)};
  });}
  function matchup(aId,bId){
    const a=cards[aId],b=cards[bId];if(!a||!b)return null;
    if(aId===bId)return {aId,bId,advantage:0,label:'Even mirror',reasons:['Identical card mechanics; deployment and support determine the trade.']};
    if(a.spell&&b.spell)return {aId,bId,advantage:0,label:'No direct interaction',reasons:['Neither spell leaves a damageable unit. Their value depends on which troops or structures occupy the impact area; spells cannot damage each other.']};
    const advantage=clamp(counter(aId,bId)-counter(bId,aId),-100,100),reasons=[];
    const explainRestriction=(source,target)=>{if(target.spell)return `${target.name} is an area spell, not a damageable troop or building; attacks cannot remove the spell itself.`;if(source.spell&&target.building)return `${source.name} deals zero structure damage and cannot damage ${target.name}.`;if(source.building&&target.building)return `${source.name} acquires enemy troops only and cannot attack another building.`;if(source.buildingsOnly)return `${source.name} directs attacks at structures and cannot directly hit ${target.name}.`;return `${source.name} cannot target air; ${target.name} is flying.`;};
    if(!canDirectTarget(a,b))reasons.push(explainRestriction(a,b));
    if(!canDirectTarget(b,a))reasons.push(explainRestriction(b,a));
    if(a.spell||b.spell){const spell=a.spell?a:b,target=a.spell?b:a;reasons.push(`${spell.name} deals ${spellDamageFor(spell,!!target.building)} initial damage to ${target.building?'buildings':'each troop'} in ${spell.radius} tiles${spell.dotDamage?`, then ${spell.dotDamage} per ${spell.dotInterval}s for up to ${spell.dotDuration}s while troops stay inside`:''}; ${target.name} has ${target.hp||0} HP per entity.`);}
    if(a.splash||b.splash){const splash=a.splash?a:b;reasons.push(`${splash.name}'s ${splash.splash}-tile splash can hit clustered ${((a===splash?b:a).count||1)>1?'swarm units':'support'} in addition to the primary target.`);}
    if(a.building||b.building)reasons.push('Defensive buildings decay over their live lifetime and can pull structure-targeting attackers before a tower lock.');
    for(const c of [a,b]){if(c.slowPct)reasons.push(`${c.name} refreshes a ${c.slowPct*100}% movement slow for ${c.slowDuration}s on ground melee hits; it does not stack or slow attack speed.`);if(c.auraDamage)reasons.push(`${c.name} still pulses ${c.auraDamage} damage to nearby air and ground troops every ${c.auraInterval}s, with ${c.stunDuration}s stun, despite structure-only direct attacks.`);}
    if(!a.spell&&!b.spell)reasons.push(`Live range ${a.range} vs ${b.range} tiles; HP ${a.hp*(a.count||1)} vs ${b.hp*(b.count||1)} across ${a.count||1} vs ${b.count||1} entities. The estimate uses legal targeting, range, damage, swarm and status mechanics.`);
    return {aId,bId,advantage,label:advantage>35?'Very favorable':advantage>10?'Favorable':advantage< -35?'Very unfavorable':advantage< -10?'Unfavorable':'Close matchup',reasons};
  }
  function pairRows(bucket,rows){const byId=Object.fromEntries(rows.map(r=>[r.id,r]));const pairs=[];for(let a=0;a<ids.length;a++)for(let b=a+1;b<ids.length;b++){const aId=ids[a],bId=ids[b],p=bucket.pairs[[aId,bId].sort().join('|')]||{n:0,score:0},baseline=(byId[aId].adjustedWinRate+byId[bId].adjustedWinRate)/2,raw=p.n?p.score/p.n*100:null,delta=p.n?(raw-baseline)*p.n/(p.n+48):0,ci=wilsonInterval(p.score,p.n);pairs.push({aId,bId,pairGames:p.n,pairWinRate:p.n?(p.score+baseline/100*48)/(p.n+48)*100:null,rawPairWinRate:raw,expectedBaseline:baseline,synergyDelta:delta,rawDelta:raw===null?null:raw-baseline,ciLow:ci.low,ciHigh:ci.high});}return pairs;}
  function validation(){const pickRateTotal=cardRows(dataset.all).reduce((n,r)=>n+r.pickRate,0),wall=Math.max(1,now()-startClock);return {games:dataset.games,gamesSimulated:dataset.games,invalidSimulations:dataset.invalidSimulations,nanCount:dataset.nanCount,mirrorExclusions:dataset.mirrorExclusions,averageDuration:divide(dataset.duration,dataset.games),averageAetherSpent:divide(dataset.aetherSpent,dataset.games*2),averageAetherLeaked:divide(dataset.aetherLeaked,dataset.games*2),pickRateTotal,gamesPerMinute:divide(dataset.games-startGames,wall/60000),computeGamesPerMinute:divide(dataset.games,dataset.simulationMilliseconds/60000),workerUtilization:clamp((dataset.simulationMilliseconds-startSimulationMilliseconds)/wall*100,0,100),workerUsage:options.workerUsage||'Headless engine; caller scheduled',model:COMBAT_MODEL_VERSION,fingerprint:identity.fingerprint,sanity:dataset.invalidSimulations===0&&dataset.nanCount===0&&(!dataset.games||Math.abs(pickRateTotal-800)<1e-7)};}
  function view(filters={}){
    const bucket=bucketFor(filters),rows=cardRows(bucket);
    const selectedStyle=filters.style&&filters.style!=='all'?filters.style:null,selectedArchetype=filters.archetype&&filters.archetype!=='all'?filters.archetype:null;
    const archetypeDenominator=selectedStyle?(dataset.styles[selectedStyle]?.n||0):dataset.games*2;
    const archetypes=META_ARCHETYPES.filter(name=>!selectedArchetype||name===selectedArchetype).map(name=>{const b=selectedStyle?(dataset.slices[`${selectedStyle}|${name}`]||emptyBucket(ids)):dataset.archetypes[name],cr=cardRows(b),opponents=Object.entries(b.opponents).map(([name,p])=>({name,winRate:adjustedWinRate(p.score,p.n),n:p.n})).filter(p=>p.n>=30).sort((a,b)=>b.winRate-a.winRate),ci=wilsonInterval(b.score,b.n);const qualified=cr.filter(r=>r.cleanN>=30).sort((a,b)=>b.adjustedWinRate-a.adjustedWinRate).map(r=>({id:r.id,name:r.name,winRate:r.adjustedWinRate,cleanN:r.cleanN}));return {name,n:b.n,pickRate:divide(b.n,archetypeDenominator)*100,winRate:adjustedWinRate(b.score,b.n),ciLow:ci.low,ciHigh:ci.high,averageCrowns:divide(b.crowns,b.n),averageDuration:divide(b.duration,b.n),bestCards:qualified.slice(0,3),weakestCards:qualified.slice(-3).reverse(),bestOpponent:opponents[0]||null,worstOpponent:opponents.at(-1)||null};});
    const allPersonalities=styleIds.map(style=>{const b=selectedArchetype?(dataset.slices[`${style}|${selectedArchetype}`]||emptyBucket(ids)):dataset.styles[style];return {style,name:META_STYLE_NAMES[style],n:b.n,winRate:adjustedWinRate(b.score,b.n),cards:cardRows(b).map(r=>({id:r.id,winRate:r.adjustedWinRate,rawWinRate:r.rawWinRate,cleanN:r.cleanN,ciLow:r.ciLow,ciHigh:r.ciHigh}))};});
    const personalities=allPersonalities.filter(p=>!selectedStyle||p.style===selectedStyle);
    const wincons=rows.filter(r=>r.winCondition&&r.appearances),averageWincon=divide(wincons.reduce((s,r)=>s+r.towerDamagePerGame,0),wincons.length);
    const alerts=rows.filter(r=>r.cleanN>=100).flatMap(r=>{const high=r.ciLow>50&&r.adjustedWinRate>=53,low=r.ciHigh<50&&r.adjustedWinRate<=47;if(!high&&!low)return [];const severity=r.cleanN>=1000&&(r.ciLow>=54||r.ciHigh<=46)?'STRONG SIGNAL':r.cleanN>=400&&(r.ciLow>=52||r.ciHigh<=48)?'CONCERN':'WATCH';const favorableStyles=allPersonalities.filter(p=>{const c=p.cards.find(c=>c.id===r.id);return c.cleanN>=100&&(high?c.ciLow>50:c.ciHigh<50);}).length;const reasons=[`Adjusted WR ${r.adjustedWinRate.toFixed(1)}%; Wilson 95% CI ${r.ciLow.toFixed(1)}–${r.ciHigh.toFixed(1)}%; clean N=${r.cleanN}.`,`${high?'Above':'Below'} neutral WR with observed support in ${favorableStyles}/7 personality slices.`];if(r.winCondition&&averageWincon)reasons.push(`Tower damage per appearance ${((r.towerDamagePerGame/averageWincon-1)*100).toFixed(0)}% vs the win-condition mean.`);if(low){if(!cards[r.id].spell)reasons.push(`Survival ${r.survivalRate.toFixed(1)}%; negative mechanical matchups into ${ids.filter(id=>id!==r.id&&!cards[id].spell&&matchup(r.id,id).advantage< -10).length}/${ids.filter(id=>id!==r.id&&!cards[id].spell).length} persistent combat cards.`);else reasons.push(`Observed spell damage ${r.damagePerCast.toFixed(0)} per cast; ${r.damagePerAether.toFixed(1)} per Aether. Spells have no unit survival statistic.`);}return [{id:r.id,name:r.name,severity,direction:high?'POSSIBLY OVERTUNED':'POSSIBLY UNDERTUNED',adjustedWinRate:r.adjustedWinRate,ciLow:r.ciLow,ciHigh:r.ciHigh,cleanN:r.cleanN,reasons}];});
    return {identity:{...identity},games:dataset.games,cards:rows,matchups:ids.flatMap(a=>ids.map(b=>matchup(a,b))),synergies:pairRows(bucket,rows),archetypes,personalities,alerts,history:dataset.history,historyScope:'Cumulative whole-dataset checkpoints across all personalities and archetypes.',validation:validation(),filteredAppearances:bucket.n,modelNote:'Seeded headless event combat using live stats and shared targeting/status/Aether rules. Bridge routing, deployment policy and 0.20s scheduling approximate rendered combat. Trends and balance signals are model evidence, not live-match telemetry.'};
  }
  return engine;
}
