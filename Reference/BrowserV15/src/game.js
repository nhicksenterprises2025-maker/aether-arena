import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { createDeckAnalyzer } from './deck-analysis.js';
import { createPresetStore } from './deck-presets.js';
import { renderDeckWorkspace, renderHomeDeckProfile, renderCardIntelligence } from './deck-ui.js';
import { refreshSlow, refreshStun, spellDamageFor, directionalSight } from './combat-rules.js';
import { createBattleRecorder } from './battle-recorder.js';
import { createReplayViewer } from './replay-viewer.js';
import { createMetaLabController } from './meta-controller.js';
import { createLabPersistence } from './lab-storage.js';

const canvas = document.querySelector('#game');
const ui = {
  timer: document.querySelector('#timer'),
  phaseLabel: document.querySelector('.timer-stack small'),
  playerCrowns: document.querySelector('#player-crowns'),
  enemyCrowns: document.querySelector('#enemy-crowns'),
  hand: document.querySelector('#hand'),
  next: document.querySelector('#next-card-mini'),
  aether: document.querySelector('#aether-value'),
  aetherFill: document.querySelector('#aether-fill'),
  selectedStats: document.querySelector('#selected-stats'),
  placementHint: document.querySelector('#placement-hint'),
  dragGhost: document.querySelector('#drag-card-ghost'),
  doubleAether: document.querySelector('#double-aether'),
  toast: document.querySelector('#toast'),
  help: document.querySelector('#help-modal'),
  end: document.querySelector('#end-screen'),
  endTitle: document.querySelector('#end-title'),
  endScore: document.querySelector('#end-score'),
  restart: document.querySelector('#restart-btn'),
  loading: document.querySelector('#loading-screen'),
  home: document.querySelector('#home-screen'),
  homeBattle: document.querySelector('#home-battle'),
  homeHelp: document.querySelector('#home-help'),
  homeDeck: document.querySelector('#home-deck-grid'),
  homeAvg: document.querySelector('#home-avg-cost'),
  openDeck: document.querySelector('#open-deck'),
  openLoadout: document.querySelector('#open-loadout'),
  deckModal: document.querySelector('#deck-modal'),
  loadoutModal: document.querySelector('#loadout-modal'),
  loadoutSlots: document.querySelector('#loadout-slots'),
  loadoutPool: document.querySelector('#loadout-card-pool'),
  loadoutCount: document.querySelector('#loadout-count'),
  loadoutAvg: document.querySelector('#loadout-avg'),
  loadoutSave: document.querySelector('#loadout-save'),
  loadoutReset: document.querySelector('#loadout-reset'),
  deckDetail: document.querySelector('#deck-detail-grid'),
  training: document.querySelector('#training-btn'),
  homeBtn: document.querySelector('#home-btn'),
  profileName: document.querySelector('#profile-name'),
  profileWins: document.querySelector('#profile-wins'),
  profileCrowns: document.querySelector('#profile-crowns'),
  profileCards: document.querySelector('#profile-cards'),
  walletGems: document.querySelector('#wallet-gems'),
  walletGold: document.querySelector('#wallet-gold'),
  profileEdit: document.querySelector('#profile-edit'),
  accountModal: document.querySelector('#account-modal'),
  accountUsername: document.querySelector('#account-username'),
  accountId: document.querySelector('#account-id'),
  accountMatches: document.querySelector('#account-matches'),
  accountRecord: document.querySelector('#account-record'),
  accountSave: document.querySelector('#account-save'),
  devToggle: document.querySelector('#dev-toggle'),
  devPanel: document.querySelector('#dev-panel'),
  devClose: document.querySelector('#dev-close'),
  devStatus: document.querySelector('#dev-status'),
  devSpawnTeam: document.querySelector('#dev-spawn-team'),
  devSpawnCard: document.querySelector('#dev-spawn-card'),
  devArmSpawn: document.querySelector('#dev-arm-spawn'),
  devAiStyle: document.querySelector('#dev-ai-style'),
  devAiToggle: document.querySelector('#dev-ai-toggle'),
  devAiReadout: document.querySelector('#dev-ai-readout'),
  devTowerSelect: document.querySelector('#dev-tower-select'),
  devTowerHp: document.querySelector('#dev-tower-hp'),
  devApplyTower: document.querySelector('#dev-apply-tower'),
  devShowPaths: document.querySelector('#dev-show-paths'),
  devShowSight: document.querySelector('#dev-show-sight'),
  devShowRanges: document.querySelector('#dev-show-ranges'),
  devShowTargets: document.querySelector('#dev-show-targets'),
  devShowTiles: document.querySelector('#dev-show-tiles'),
  devClearField: document.querySelector('#dev-clear-field'),
};

const TEAM = { PLAYER: 'player', ENEMY: 'enemy' };
const ARENA = { width: 28, length: 42, riverHalf: 1.65, bridgeX: 7.2, bridgeWidth: 4.2, playerMinZ: 2.15, enemyMaxZ: -2.15 };
const GRID = { tile: 1.0 };
const SIGHT = { frontTiles: 8, rearTiles: 5 }; // directional acquisition for all deployable combat cards
const RETARGET = { towerPullBonusTiles: 1.75, troopLeashBonusTiles: 2.25, forcedLockSeconds: 1.2 };
const NAV = { cell: GRID.tile, repathSeconds: .68, targetMoveTiles: 1.05, maxNodes: 1400, bridgeMargin: .16, structurePadding: .22, bridgeEntryPad: .78, bridgeReleasePad: .92, stuckSampleSeconds: .28, stuckThresholdSeconds: .82 };
const MATCH = { regulation:180, overtime:120, baseAetherSeconds:2.8, tiebreakDrainPerSecond:180 };
const POCKET = { xInner: 2.0, xOuter: 13.2, zNear: 2.25, zFar: 9.25 }; // lane-only pocket: river edge to former Guard Tower approach
const pocketOverlays = { [TEAM.PLAYER]: {}, [TEAM.ENEMY]: {} };
const COLORS = {
  player: 0x39bfff,
  enemy: 0xff5369,
  neutral: 0xd8e6f2,
  grass: 0x508b52,
  grass2: 0x3e7048,
  river: 0x2c8fd6,
  stone: 0x9aa6b2,
  darkStone: 0x53606e,
  wood: 0x8d6241,
};

const CARD_LIBRARY = {
  ironclad: {
    id: 'ironclad', name: 'Ironclad', icon: '🛡️', cost: 3, type: 'Fighter', model: 'ironclad',
    desc: 'Reliable armored melee fighter.', hp: 840, damage: 96, attackSpeed: 1.0, moveSpeed: 2.25, range: 1.35, aggro: 5.2, projectile: false, canHitAir: false, combatClass: 'Ground Melee', category: 'Fighter',
    artA: '#3f6688', artB: '#17283b', scale: 1.0,
  },
  ember_archer: {
    id: 'ember_archer', name: 'Ember Archer', icon: '🏹', cost: 3, type: 'Ranged', model: 'ember_archer',
    desc: 'Long-range single-target pressure.', hp: 423, damage: 152, attackSpeed: 1.55, moveSpeed: 2.35, range: 6.0, aggro: 7.2, projectile: true, projectileSpeed: 16, canHitAir: true, combatClass: 'Ground Ranged', category: 'Ranged',
    artA: '#d46b43', artB: '#5d2524', scale: 0.95,
  },
  twin_blades: {
    id: 'twin_blades', name: 'Twin Blades', icon: '⚔️', cost: 2, type: 'Swarm ×2', model: 'twin_blade',
    desc: 'Two fast glass-cannon duelists.', hp: 262, damage: 58, attackSpeed: 0.72, moveSpeed: 3.35, range: 1.2, aggro: 5.0, projectile: false, count: 2, canHitAir: false, combatClass: 'Ground Melee', category: 'Swarm',
    artA: '#7d57c2', artB: '#282047', scale: 0.82,
  },
  boulderback: {
    id: 'boulderback', name: 'Boulderback', icon: '🪨', cost: 5, type: 'Siege Tank', model: 'boulderback',
    desc: 'Huge health. Ignores troops and hunts towers.', hp: 1620, damage: 118, attackSpeed: 2.00, moveSpeed: 1.30, range: 1.5, aggro: 99, projectile: false, buildingsOnly: true, canHitAir: false, combatClass: 'Ground Siege', category: 'Siege Tank',
    artA: '#747c85', artB: '#353a40', scale: 1.22,
  },
  arc_mage: {
    id: 'arc_mage', name: 'Arc Mage', icon: '🔮', cost: 4, type: 'Splash', model: 'arc_mage',
    desc: 'Ranged bolts splash nearby enemies.', hp: 547, damage: 120, attackSpeed: 1.00, moveSpeed: 2.0, range: 6.5, aggro: 7.0, projectile: true, projectileSpeed: 17, splash: 2.5, canHitAir: true, combatClass: 'Ground Ranged', category: 'Splash Ranged',
    artA: '#5858d8', artB: '#252458', scale: 0.96,
  },
  rambeast: {
    id: 'rambeast', name: 'Rambeast', icon: '🐏', cost: 4, type: 'Charger', model: 'rambeast',
    desc: 'Targets towers. Builds a devastating charge.', hp: 880, damage: 140, attackSpeed: 1.40, moveSpeed: 2.60, range: 1.45, aggro: 99, projectile: false, buildingsOnly: true, charger: true, chargeDamage: 255, canHitAir: false, combatClass: 'Ground Charger', category: 'Charger',
    artA: '#9f7146', artB: '#47301f', scale: 1.06,
  },
  sky_manta: {
    id: 'sky_manta', name: 'Sky Manta', icon: '🪽', cost: 3, type: 'Flying', model: 'sky_manta',
    desc: 'Flying skirmisher that ignores bridge pathing.', hp: 480, damage: 77, attackSpeed: 0.92, moveSpeed: 3.05, range: 3.4, aggro: 6.0, projectile: true, projectileSpeed: 15, flying: true, canHitAir: true, combatClass: 'Flying Ranged', category: 'Flying',
    artA: '#3ca4a3', artB: '#174e59', scale: 1.05,
  },
  vampire_bats: {
    id: 'vampire_bats', name: 'Vampire Bats', icon: '🦇', cost: 5, type: 'Air Swarm', model: 'vampire_bat',
    desc: 'Five vicious flying melee bats that overwhelm ground and air targets.', hp: 174, damage: 86, attackSpeed: 1.05, moveSpeed: 1.95, range: 2.0, aggro: 6.0, projectile: false, count: 5, flying: true, canHitAir: true, combatClass: 'Flying Melee', category: 'Air Swarm',
    artA: '#7e254d', artB: '#1a1023', scale: 0.68,
  },
  frost_fang: {
    id: 'frost_fang', name: 'Frost Fang', icon: '❄', cost: 5, type: 'Frost Melee', model: 'frost_fang',
    desc: 'Heavy frost melee fighter. Every melee hit slows enemy movement by 30%; the slow refreshes but never stacks.', hp: 1155, damage: 72, attackSpeed: 0.8, moveSpeed: 2.50, range: 1.1, aggro: 8, projectile: false, canHitAir: false, slowPct: .30, slowDuration: 2.0, combatClass: 'Frost Ground Melee', category: 'Fighter',
    artA: '#8fd8f0', artB: '#17314d', scale: 1.02,
  },
  storm_raven: {
    id: 'storm_raven', name: 'Storm Raven', icon: '⚡', cost: 6, type: 'Flying Win Con', model: 'storm_raven',
    desc: 'Flying ranged win condition. Attacks structures and releases a 2-tile lightning pulse every 3s for 82 damage and a 0.4s stun.', hp: 1337, damage: 251, attackSpeed: 1.70, moveSpeed: 1.18, range: 4.30, aggro: 8, projectile: true, projectileSpeed: 18, buildingsOnly: true, flying: true, canHitAir: false, auraDamage: 82, auraInterval: 3.0, auraRadius: 2.0, stunDuration: .4, combatClass: 'Flying Ranged Win Con', category: 'Flying Win Condition',
    artA: '#5268a8', artB: '#10182e', scale: 1.20,
  },
  meteor_shards: {
    id: 'meteor_shards', name: 'Meteor Shards', icon: '☄', cost: 5, type: 'Damage Over Time Spell', model: null,
    desc: 'Large impact zone. Deals 262 immediately, then 40 damage each second for 5 seconds to enemy ground and air troops that remain inside.', spell: true, spellKind: 'meteor_shards', radius: 4.5, damage: 262, dotDamage: 40, dotDuration: 5, dotInterval: 1, towerDamage: 0, combatClass: 'Damage Over Time Spell', category: 'Spell',
    artA: '#e56f37', artB: '#31152c',
  },
  archer_tower: {
    id: 'archer_tower', name: 'Archer Tower', icon: '🏹', cost: 4, type: 'Building', model: 'archer_tower',
    desc: 'Defensive building. Fires arrows at ground and air troops, then decays over time.', building: true, hp: 850, damage: 75, attackSpeed: 1.10, range: 7, projectile: true, projectileSpeed: 18, canHitAir: true, lifetime: 25, footprint: 1.65, combatClass: 'Defensive Building', category: 'Building',
    artA: '#6c8a58', artB: '#26392d', scale: 1.0,
  },
  bullet_burst: {
    id: 'bullet_burst', name: 'Bullet Burst', icon: '✦', cost: 2, type: 'Small Spell', model: null,
    desc: 'Rapid bullet volley over a compact area. Hits ground and air troops.', spell: true, spellKind: 'bullets', radius: 2.2, damage: 175, towerDamage: 55, bulletCount: 7, combatClass: 'Small Spell', category: 'Spell',
    artA: '#65758b', artB: '#17212d',
  },
  nova_flask: {
    id: 'nova_flask', name: 'Nova Flask', icon: '💥', cost: 4, type: 'Spell', model: null,
    desc: 'Area burst. 375 troop damage / 185 tower damage.', spell: true, spellKind: 'nova', radius: 3.25, damage: 375, towerDamage: 185, combatClass: 'Heavy Spell', category: 'Spell',
    artA: '#a23dc7', artB: '#4a1d66',
  },
};

const ALL_CARD_IDS = Object.keys(CARD_LIBRARY);
const DEFAULT_DECK = ['ironclad','ember_archer','archer_tower','boulderback','arc_mage','rambeast','sky_manta','nova_flask'];
const DECK_SIZE = 8;
const DECK_STORAGE_KEY = 'rift_crown_deck_v1';
const PROFILE_STORAGE_KEY = 'rift_crown_profile_v1';
const PRESET_STORAGE_KEY = 'rift_crown_deck_presets_v14';
const PENDING_SAVE_KEY = 'rift_crown_save_pending_v14';
const deckAnalyzer = createDeckAnalyzer(CARD_LIBRARY);
const presetStore = createPresetStore(CARD_LIBRARY,DEFAULT_DECK);
function newLocalPlayerId(){
  const seed=Math.random().toString(36).slice(2,8).toUpperCase();
  return `RC-${seed}`;
}
function defaultProfile(){return {username:'RIFTBOUND',playerId:newLocalPlayerId(),wins:0,losses:0,draws:0,matches:0,crowns:0,gems:1250,gold:8420};}
function nonnegativeNumber(value,fallback=0){
  if(typeof value!=='number'&&(typeof value!=='string'||!value.trim()))return fallback;
  const number=Number(value);
  return Number.isFinite(number)&&number>=0?number:fallback;
}
function normalizeProfile(raw){
  const defaults=defaultProfile();
  if(!raw||typeof raw!=='object'||Array.isArray(raw))return defaults;
  const profile={...defaults,...raw,username:String(raw.username||defaults.username).slice(0,18)};
  profile.playerId=typeof raw.playerId==='string'&&raw.playerId?raw.playerId:defaults.playerId;
  for(const key of ['wins','losses','draws','matches','crowns','gems','gold'])profile[key]=Math.floor(nonnegativeNumber(raw[key],defaults[key]));
  return profile;
}
function loadProfile(){
  try{
    const raw=JSON.parse(localStorage.getItem(PROFILE_STORAGE_KEY)||'null');
    if(raw&&typeof raw==='object')return normalizeProfile(raw);
  }catch{}
  return defaultProfile();
}
let playerProfile=loadProfile();
let persistentSaveRevision=0;
let serverSavePending=Promise.resolve();
function saveProfile(){
  try{localStorage.setItem(PROFILE_STORAGE_KEY,JSON.stringify(playerProfile));}catch{}
  persistServerSave();
}
async function persistServerSave(){
  persistentSaveRevision++;
  const token=`${Date.now()}-${persistentSaveRevision}`;
  try{localStorage.setItem(PENDING_SAVE_KEY,token);}catch{}
  const body=JSON.stringify({profile:playerProfile,deck:activeDeck,deckPresets:deckPresetState});
  serverSavePending=serverSavePending.catch(()=>{}).then(async()=>{
    const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);
    try{
      const response=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body,keepalive:true,signal:controller.signal});
      if(response.ok){try{if(localStorage.getItem(PENDING_SAVE_KEY)===token)localStorage.removeItem(PENDING_SAVE_KEY);}catch{}}
    }catch{}finally{clearTimeout(timeout);}
  });
  return serverSavePending;
}
async function hydratePersistentSave(){
  const revision=persistentSaveRevision;
  const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),5000);
  try{
    const response=await fetch('/api/save',{cache:'no-store',signal:controller.signal});if(!response.ok)return;
    const data=await response.json();
    if(revision!==persistentSaveRevision)return;
    // A failed server write must not let an older disk save overwrite the newest local deck/profile.
    if(hasPendingLocalSave()){persistServerSave();return;}
    if(data?.profile&&typeof data.profile==='object'&&!Array.isArray(data.profile)){
      playerProfile=normalizeProfile(data.profile);
      try{localStorage.setItem(PROFILE_STORAGE_KEY,JSON.stringify(playerProfile));}catch{}
    }
    if(data?.deckPresets||validateDeck(data?.deck)){
      deckPresetState=presetStore.normalize(data?.deckPresets||deckPresetState,data?.deck||activeDeck);
      if(!data?.deckPresets&&validateDeck(data?.deck)){
        deckPresetState=presetStore.update(deckPresetState,deckPresetState.activePresetId,{cards:data.deck})||deckPresetState;
      }
      activeDeck=deckPresetState.presets.find(p=>p.id===deckPresetState.activePresetId).cards.slice();
      if(!ui.loadoutModal||ui.loadoutModal.classList.contains('hidden'))syncLoadoutDraft();
      saveDeckStorage();
      if(!game.started)game.playerDeck=new DeckState(activeDeck);
    }
    renderProfile();renderHomeDeck();renderLoadoutBuilder();renderHand();
  }catch{}finally{clearTimeout(timeout);}
}
function hasPendingLocalSave(){try{return !!localStorage.getItem(PENDING_SAVE_KEY);}catch{return false;}}
function validateDeck(ids){
  return Array.isArray(ids) && ids.length===DECK_SIZE && new Set(ids).size===DECK_SIZE && ids.every(id=>typeof id==='string'&&Object.hasOwn(CARD_LIBRARY,id));
}
function loadSavedDeck(){
  try{const saved=JSON.parse(localStorage.getItem(DECK_STORAGE_KEY)||'null');if(validateDeck(saved))return saved.slice();}catch{}
  return DEFAULT_DECK.slice();
}
function shuffledCardPool(ids=ALL_CARD_IDS){
  const arr=ids.slice();
  for(let i=arr.length-1;i>0;i--){const j=Math.floor(Math.random()*(i+1));[arr[i],arr[j]]=[arr[j],arr[i]];}
  return arr;
}
let activeDeck=loadSavedDeck();
let deckPresetState=loadDeckPresetState();
activeDeck=deckPresetState.presets.find(p=>p.id===deckPresetState.activePresetId).cards.slice();
let loadoutDraft=activeDeck.slice();
let loadoutPresetId=deckPresetState.activePresetId;
let presetNameDraft=deckPresetState.presets.find(p=>p.id===loadoutPresetId).name;

function loadDeckPresetState(){
  try{return presetStore.normalize(JSON.parse(localStorage.getItem(PRESET_STORAGE_KEY)||'null'),activeDeck);}catch{return presetStore.createDefaultPresets(activeDeck);}
}
function saveDeckStorage(){
  try{localStorage.setItem(DECK_STORAGE_KEY,JSON.stringify(activeDeck));localStorage.setItem(PRESET_STORAGE_KEY,JSON.stringify(deckPresetState));}catch{}
}
function syncLoadoutDraft(){
  loadoutPresetId=deckPresetState.activePresetId;
  const preset=deckPresetState.presets.find(p=>p.id===loadoutPresetId);
  loadoutDraft=preset.cards.slice();presetNameDraft=preset.name;
}

const AI_STYLES = {
  beatdown: { name: 'Beatdown', bankThreshold: 9.9, desperationThreshold: 7.3, supportLimit: 3, reserveAether: 2.2, defendBias:1.00, punishBias:.58, tradeTolerance:.34, pushPriority: ['boulderback','storm_raven','rambeast','ironclad','arc_mage'], supportPriority: ['arc_mage','ember_archer','storm_raven','vampire_bats','sky_manta','frost_fang','ironclad'], overflow: ['ironclad','vampire_bats','ember_archer','twin_blades'] },
  aggro: { name: 'Aggro', bankThreshold: 7.8, desperationThreshold: 6.0, supportLimit: 2, reserveAether: 1.0, defendBias:.76, punishBias:1.00, tradeTolerance:.74, pushPriority: ['rambeast','storm_raven','ironclad','twin_blades','boulderback'], supportPriority: ['storm_raven','vampire_bats','ember_archer','sky_manta','frost_fang','twin_blades','arc_mage'], overflow: ['vampire_bats','twin_blades','ember_archer','ironclad'] },
  control: { name: 'Control', bankThreshold: 9.2, desperationThreshold: 6.8, supportLimit: 2, reserveAether: 2.8, defendBias:1.22, punishBias:.50, tradeTolerance:.16, pushPriority: ['boulderback','storm_raven','ironclad','rambeast','arc_mage'], supportPriority: ['arc_mage','frost_fang','ember_archer','vampire_bats','ironclad','sky_manta'], overflow: ['ember_archer','ironclad','twin_blades'] },
  cycle: { name: 'Cycle', bankThreshold: 6.4, desperationThreshold: 5.2, supportLimit: 1, reserveAether: .8, defendBias:.90, punishBias:.92, tradeTolerance:.48, pushPriority: ['rambeast','twin_blades','ironclad','sky_manta','ember_archer'], supportPriority: ['ember_archer','twin_blades','sky_manta','frost_fang','arc_mage'], overflow: ['twin_blades','ember_archer','ironclad','bullet_burst'] },
  split: { name: 'Split Lane', bankThreshold: 8.0, desperationThreshold: 6.1, supportLimit: 1, reserveAether: 1.4, defendBias:.88, punishBias:1.08, tradeTolerance:.60, pushPriority: ['rambeast','storm_raven','ironclad','vampire_bats','twin_blades'], supportPriority: ['ember_archer','sky_manta','twin_blades','frost_fang'], overflow: ['twin_blades','vampire_bats','ember_archer','ironclad'] },
  spell_cycle: { name: 'Spell Cycle', bankThreshold: 7.4, desperationThreshold: 5.8, supportLimit: 1, reserveAether: 1.5, defendBias:1.02, punishBias:.62, tradeTolerance:.38, pushPriority: ['ironclad','frost_fang','rambeast','sky_manta'], supportPriority: ['ember_archer','arc_mage','sky_manta'], overflow: ['bullet_burst','twin_blades','ember_archer','ironclad'] },
  counter: { name: 'Counter Push', bankThreshold: 9.0, desperationThreshold: 6.5, supportLimit: 3, reserveAether: 2.5, defendBias:1.30, punishBias:.72, tradeTolerance:.22, pushPriority: ['boulderback','frost_fang','ironclad','storm_raven','rambeast'], supportPriority: ['arc_mage','ember_archer','vampire_bats','sky_manta','frost_fang'], overflow: ['ember_archer','ironclad','twin_blades'] },
};

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x88b5cf);
scene.fog = new THREE.Fog(0x88b5cf, 50, 96);

const camera = new THREE.PerspectiveCamera(43, 1, 0.1, 120);
camera.position.set(0, 27.5, 27.5);
camera.lookAt(0, 0, 0);

const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: false, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.7));
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 0.88;

// Post-processing is intentionally restrained: bloom is reserved for emissive magic,
// projectiles, crystals, and impact FX so units stay readable instead of looking washed out.
const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));
const bloomPass = new UnrealBloomPass(new THREE.Vector2(1, 1), .48, .58, .78);
bloomPass.threshold = .72;
bloomPass.strength = .55;
bloomPass.radius = .68;
composer.addPass(bloomPass);
composer.addPass(new OutputPass());

scene.add(new THREE.HemisphereLight(0xd4e8f6, 0x435447, 1.62));
const sun = new THREE.DirectionalLight(0xffefda, 2.35);
sun.position.set(-14,26,18);
sun.castShadow=true;
sun.shadow.mapSize.set(2048,2048);
sun.shadow.bias=-0.00025;sun.shadow.normalBias=.035;
sun.shadow.camera.left = -28;
sun.shadow.camera.right = 28;
sun.shadow.camera.top = 32;
sun.shadow.camera.bottom=-32;
scene.add(sun);
const fillLight=new THREE.DirectionalLight(0xa7d5f1,.58);fillLight.position.set(16,15,-12);scene.add(fillLight);
const playerRim=new THREE.PointLight(0x44cfff,1.45,20,2);playerRim.position.set(0,5.2,15);scene.add(playerRim);
const enemyRim=new THREE.PointLight(0xff6077,1.28,20,2);enemyRim.position.set(0,5.2,-15);scene.add(enemyRim);

const clock = new THREE.Clock();
const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
const groundPlane = new THREE.Plane(new THREE.Vector3(0,1,0), 0);
const tmpV2 = new THREE.Vector3();
const cameraAnchor = new THREE.Vector3(-3.6, 29.5, 31.5);
const cameraLook = new THREE.Vector3(0, 0, -1.5);
let cameraShake = 0;

const loader = new GLTFLoader();
const modelCache = new Map();
const modelLoads = new Map();
const modelFailed = new Set();

// GLB clones share resources with the model cache; only dispose owned procedural resources.
function disposeObject(object){
  const geometries=new Set(),materials=new Set(),textures=new Set();
  function visit(node){
    if(node.userData?.riftBlenderModel)return;
    if(node.geometry&&!node.isSprite)geometries.add(node.geometry);
    for(const material of Array.isArray(node.material)?node.material:[node.material]){
      if(!material)continue;
      materials.add(material);
      for(const value of Object.values(material))if(value?.isTexture)textures.add(value);
    }
    for(const child of node.children||[])visit(child);
  }
  if(!object)return;
  visit(object);
  for(const texture of textures)texture.dispose();
  for(const material of materials)material.dispose();
  for(const geometry of geometries)geometry.dispose();
}

function mat(color, roughness = .72, metalness = .05) {
  return new THREE.MeshStandardMaterial({ color, roughness, metalness });
}

function makeProceduralTexture(base, fleckA, fleckB, density=1100){
  const c=document.createElement('canvas');c.width=c.height=256;const x=c.getContext('2d');
  x.fillStyle=base;x.fillRect(0,0,256,256);
  for(let i=0;i<density;i++){
    const r=Math.random();x.globalAlpha=.10+Math.random()*.18;x.fillStyle=r<.55?fleckA:fleckB;
    const sz=.5+Math.random()*2.2;x.fillRect(Math.random()*256,Math.random()*256,sz,sz);
  }
  x.globalAlpha=.08;x.strokeStyle=fleckB;x.lineWidth=1;
  for(let i=0;i<28;i++){x.beginPath();const y=Math.random()*256;x.moveTo(0,y);x.bezierCurveTo(70,y+(Math.random()-.5)*8,180,y+(Math.random()-.5)*8,256,y+(Math.random()-.5)*7);x.stroke();}
  x.globalAlpha=1;
  const t=new THREE.CanvasTexture(c);t.colorSpace=THREE.SRGBColorSpace;t.wrapS=t.wrapT=THREE.RepeatWrapping;t.repeat.set(6,9);t.anisotropy=Math.min(8,renderer.capabilities.getMaxAnisotropy());return t;
}
const grassTexture=makeProceduralTexture('#6fa563','#86b86f','#496f43',1350);

function mesh(geometry, material, cast = true, receive = true) {
  const m = new THREE.Mesh(geometry, material);
  m.castShadow = cast;
  m.receiveShadow = receive;
  return m;
}

function roundedBox(w,h,d,color) {
  return mesh(new THREE.BoxGeometry(w,h,d,2,2,2), mat(color));
}

const ambientAnimations=[];

function buildSkyBackdrop(){
  const skyMat=new THREE.ShaderMaterial({side:THREE.BackSide,depthWrite:false,uniforms:{},vertexShader:`varying vec3 vPos;void main(){vPos=position;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}`,fragmentShader:`varying vec3 vPos;void main(){float h=normalize(vPos).y*.5+.5;vec3 low=vec3(.40,.66,.77);vec3 mid=vec3(.34,.56,.76);vec3 high=vec3(.15,.28,.46);vec3 c=mix(low,mid,smoothstep(.15,.50,h));c=mix(c,high,smoothstep(.54,1.0,h));gl_FragColor=vec4(c,1.0);}`});
  const sky=mesh(new THREE.SphereGeometry(88,32,18),skyMat,false,false); sky.position.y=-8; scene.add(sky);
  // Keep sky dressing pushed to the horizon so the battlefield always stays unobstructed.
  for(let i=0;i<10;i++){
    const cloud=new THREE.Group();
    for(let j=0;j<3+Math.floor(Math.random()*2);j++){
      const puff=mesh(new THREE.SphereGeometry(1.4+Math.random()*1.2,10,7),new THREE.MeshStandardMaterial({color:0xe7f0f6,roughness:1,transparent:true,opacity:.28}),false,false);
      puff.scale.y=.36; puff.position.set((j-1.2)*1.18+(Math.random()-.5)*.35,Math.random()*.28,(Math.random()-.5)*.9); cloud.add(puff);
    }
    const side=Math.random()<.5?-1:1;
    const zBand=(Math.random()<.5?-1:1)*(22+Math.random()*20);
    cloud.position.set(side*(30+Math.random()*18),13+Math.random()*6,zBand); cloud.scale.setScalar(.8+Math.random()*.45); scene.add(cloud);
    ambientAnimations.push({type:'cloud',mesh:cloud,speed:.03+Math.random()*.04,phase:Math.random()*6});
  }
  const positions=[];
  for(let i=0;i<110;i++)positions.push((Math.random()-.5)*40,.5+Math.random()*8,(Math.random()-.5)*52);
  const geo=new THREE.BufferGeometry();geo.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
  const dust=new THREE.Points(geo,new THREE.PointsMaterial({color:0xb9ecff,size:.055,transparent:true,opacity:.38,blending:THREE.AdditiveBlending,depthWrite:false}));scene.add(dust);ambientAnimations.push({type:'dust',mesh:dust,phase:Math.random()*6});
}
buildSkyBackdrop();

function buildArena() {
  const arena = new THREE.Group();
  scene.add(arena);

  // Floating stone foundation gives the battlefield a premium miniature-diorama silhouette.
  const foundation = mesh(new THREE.BoxGeometry(ARENA.width + 5.4, 1.65, ARENA.length + 5.4), mat(0x34453f, .98), false, true);
  foundation.position.y = -1.05;
  arena.add(foundation);
  const trim = mesh(new THREE.BoxGeometry(ARENA.width + 4.3, .28, ARENA.length + 4.3), mat(0x81918b, .9, .05), false, true);
  trim.position.y = -.19;
  arena.add(trim);

  const fieldMat=new THREE.MeshStandardMaterial({map:grassTexture,color:0xffffff,roughness:.94,metalness:0});
  const field = mesh(new THREE.PlaneGeometry(ARENA.width, ARENA.length, 1, 1), fieldMat, false, true);
  field.rotation.x = -Math.PI/2;
  field.position.y = 0.02;
  field.name = 'deploySurface';
  arena.add(field);

  // Subtle battle lanes and central stone inlay.
  const laneMat = new THREE.MeshStandardMaterial({color:0x769b62,roughness:.96,transparent:true,opacity:.34});
  for (const x of [-ARENA.bridgeX, ARENA.bridgeX]) {
    const lane = mesh(new THREE.PlaneGeometry(5.15, ARENA.length-2.8), laneMat, false, true);
    lane.rotation.x=-Math.PI/2; lane.position.set(x,.042,0); arena.add(lane);
    for(let z=-17;z<=17;z+=3.4){
      const stone=mesh(new THREE.CylinderGeometry(.42,.48,.07,8),mat(0x7d8b78,.95),false,true);
      stone.rotation.x=Math.PI/2; stone.position.set(x+(Math.random()-.5)*1.8,.07,z+(Math.random()-.5)*.6); stone.rotation.z=Math.random(); arena.add(stone);
    }
  }
  const centerSeal = mesh(new THREE.RingGeometry(2.1,2.48,36), new THREE.MeshStandardMaterial({color:0xd3bf77,roughness:.72,metalness:.2,transparent:true,opacity:.65,side:THREE.DoubleSide}), false, true);
  centerSeal.rotation.x=-Math.PI/2; centerSeal.position.y=.095; arena.add(centerSeal);

  // Team-colored halves stay subtle so deployment space is readable.
  const pTint = mesh(new THREE.PlaneGeometry(ARENA.width, ARENA.length/2 - ARENA.riverHalf), new THREE.MeshStandardMaterial({color:0x49a6c7, roughness:.95, transparent:true, opacity:.10}), false, true);
  pTint.rotation.x = -Math.PI/2; pTint.position.set(0,.031, ARENA.length/4 + ARENA.riverHalf/2); arena.add(pTint);
  const eTint = mesh(new THREE.PlaneGeometry(ARENA.width, ARENA.length/2 - ARENA.riverHalf), new THREE.MeshStandardMaterial({color:0xc36a68, roughness:.95, transparent:true, opacity:.09}), false, true);
  eTint.rotation.x = -Math.PI/2; eTint.position.set(0,.032, -ARENA.length/4 - ARENA.riverHalf/2); arena.add(eTint);


  // Lane-specific pocket deployment zones unlock only after the matching Guard Tower falls.
  for(const deployTeam of [TEAM.PLAYER, TEAM.ENEMY]){
    const color=deployTeam===TEAM.PLAYER?COLORS.player:COLORS.enemy;
    const zSign=deployTeam===TEAM.PLAYER?-1:1;
    for(const lane of [-1,1]){
      const width=POCKET.xOuter-POCKET.xInner;
      const depth=POCKET.zFar-POCKET.zNear;
      const x=lane*(POCKET.xInner+width/2);
      const z=zSign*(POCKET.zNear+depth/2);
      const zone=mesh(new THREE.PlaneGeometry(width,depth),new THREE.MeshBasicMaterial({color,transparent:true,opacity:.0,side:THREE.DoubleSide,depthWrite:false}),false,false);
      zone.rotation.x=-Math.PI/2; zone.position.set(x,.072,z); zone.visible=false; zone.renderOrder=2;
      arena.add(zone); pocketOverlays[deployTeam][lane]=zone;
    }
  }


  // V9 complete arena grid: true line geometry sits above grass/water so every 1x1 tile stays visible.
  // Two passes keep major 4-tile guides stronger without any z-fighting/missing segments.
  const minorPositions=[], majorPositions=[];
  const xMin=-ARENA.width/2, xMax=ARENA.width/2, zMin=-ARENA.length/2, zMax=ARENA.length/2;
  for(let x=xMin,idx=0;x<=xMax+.001;x+=GRID.tile,idx++){
    const arr=idx%4===0?majorPositions:minorPositions;
    arr.push(x,.145,zMin,x,.145,zMax);
  }
  for(let z=zMin,idx=0;z<=zMax+.001;z+=GRID.tile,idx++){
    const arr=idx%4===0?majorPositions:minorPositions;
    arr.push(xMin,.145,z,xMax,.145,z);
  }
  const makeGridLines=(positions,color,opacity)=>{
    const geo=new THREE.BufferGeometry(); geo.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
    const material=new THREE.LineBasicMaterial({color,transparent:true,opacity,depthWrite:false,depthTest:true});
    const lines=new THREE.LineSegments(geo,material); lines.renderOrder=4; arena.add(lines); return lines;
  };
  makeGridLines(minorPositions,0xe7f5ef,.17);
  makeGridLines(majorPositions,0xffe39b,.29);
  // Crisp arena perimeter reinforces the legal board boundary.
  const borderGeo=new THREE.BufferGeometry().setFromPoints([
    new THREE.Vector3(xMin,.15,zMin),new THREE.Vector3(xMax,.15,zMin),
    new THREE.Vector3(xMax,.15,zMin),new THREE.Vector3(xMax,.15,zMax),
    new THREE.Vector3(xMax,.15,zMax),new THREE.Vector3(xMin,.15,zMax),
    new THREE.Vector3(xMin,.15,zMax),new THREE.Vector3(xMin,.15,zMin),
  ]);
  const borderLines=new THREE.LineSegments(borderGeo,new THREE.LineBasicMaterial({color:0xf7efd2,transparent:true,opacity:.55,depthWrite:false}));borderLines.renderOrder=5;arena.add(borderLines);

  // V4 stylized shader water: animated refraction-like bands, depth gradient and bright bank foam.
  const waterUniforms={uTime:{value:0},uDeep:{value:new THREE.Color(0x1687bd)},uShallow:{value:new THREE.Color(0x58d8ee)}};
  const waterMat=new THREE.ShaderMaterial({transparent:true,depthWrite:true,uniforms:waterUniforms,vertexShader:`varying vec2 vUv;void main(){vUv=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}`,fragmentShader:`varying vec2 vUv;uniform float uTime;uniform vec3 uDeep;uniform vec3 uShallow;void main(){float w1=sin(vUv.x*44.0+uTime*1.9+sin(vUv.y*10.0))*0.5+0.5;float w2=sin(vUv.x*23.0-uTime*1.25+vUv.y*18.0)*0.5+0.5;float edge=smoothstep(.0,.13,vUv.y)*smoothstep(1.0,.87,vUv.y);vec3 c=mix(uDeep,uShallow,.25+.40*w1+.14*w2);float sparkle=smoothstep(.91,1.0,w1*w2);c+=sparkle*.22;gl_FragColor=vec4(c,.96);}`});
  const river = mesh(new THREE.PlaneGeometry(ARENA.width, ARENA.riverHalf*2,48,8),waterMat,false,true);
  river.rotation.x=-Math.PI/2; river.position.y=.09; arena.add(river); ambientAnimations.push({type:'water',mesh:river});
  for(const z of [-ARENA.riverHalf+.04,ARENA.riverHalf-.04]){
    const foam=mesh(new THREE.PlaneGeometry(ARENA.width,.10),new THREE.MeshBasicMaterial({color:0xc8f7ff,transparent:true,opacity:.42,depthWrite:false}),false,false);
    foam.rotation.x=-Math.PI/2;foam.position.set(0,.118,z);arena.add(foam);ambientAnimations.push({type:'foam',mesh:foam,phase:Math.random()*6});
  }
  for(let i=0;i<12;i++){
    const ripple=mesh(new THREE.PlaneGeometry(2.5+Math.random()*3.5,.045),new THREE.MeshBasicMaterial({color:0xbaf5ff,transparent:true,opacity:.20,depthWrite:false,blending:THREE.AdditiveBlending}),false,false);
    ripple.rotation.x=-Math.PI/2; ripple.position.set(-13+Math.random()*26,.122,-1.25+Math.random()*2.5); arena.add(ripple);
    ambientAnimations.push({type:'ripple',mesh:ripple,speed:.8+Math.random()*.8,phase:Math.random()*6});
  }

  // Bridges now have stone abutments, rails, ropes and individual planks.
  for (const x of [-ARENA.bridgeX, ARENA.bridgeX]) {
    for(const z of [-ARENA.riverHalf-.55,ARENA.riverHalf+.55]){
      const abut=mesh(new THREE.BoxGeometry(ARENA.bridgeWidth+.8,.7,.75),mat(0x7f8176,.95),true,true);
      abut.position.set(x,.28,z); arena.add(abut);
    }
    const bridge = mesh(new THREE.BoxGeometry(ARENA.bridgeWidth, .34, ARENA.riverHalf*2 + 1.2), mat(0x7f5335, .92), true, true);
    bridge.position.set(x,.38,0); arena.add(bridge);
    for (let i=-3;i<=3;i++) {
      const plank = mesh(new THREE.BoxGeometry(ARENA.bridgeWidth-.18,.09,.43), mat(i%2?0x8f6241:0x9e6c46,.94), true, true);
      plank.position.set(x,.59,i*.55); arena.add(plank);
    }
    for(const sx of [-1,1]){
      const rail=mesh(new THREE.CylinderGeometry(.055,.055,ARENA.riverHalf*2+1.4,8),mat(0x563621,.9),true,true);
      rail.rotation.x=Math.PI/2; rail.position.set(x+sx*(ARENA.bridgeWidth*.47),1.0,0); arena.add(rail);
      for(const z of [-1.5,0,1.5]){
        const post=mesh(new THREE.CylinderGeometry(.07,.09,1.0,8),mat(0x4a2e1d,.95),true,true); post.position.set(x+sx*(ARENA.bridgeWidth*.47),.62,z); arena.add(post);
      }
    }
  }

  // Raised stone border, crystal torches, shrubs and banners outside the combat zone.
  const wallMat = mat(0x66736e, .95);
  for (let z=-ARENA.length/2; z<=ARENA.length/2; z+=2.65) {
    for (const x of [-ARENA.width/2-1.15, ARENA.width/2+1.15]) {
      const rock = mesh(new THREE.DodecahedronGeometry(.72 + Math.random()*.28, 0), wallMat, true, true);
      rock.position.set(x + (Math.random()-.5)*.4, .2, z + (Math.random()-.5)*.4);
      rock.rotation.set(Math.random(),Math.random(),Math.random()); arena.add(rock);
    }
  }
  for (let x=-ARENA.width/2; x<=ARENA.width/2; x+=2.8) {
    for (const z of [-ARENA.length/2-1.15, ARENA.length/2+1.15]) {
      const rock = mesh(new THREE.DodecahedronGeometry(.7 + Math.random()*.26, 0), wallMat, true, true);
      rock.position.set(x + (Math.random()-.5)*.35, .2, z + (Math.random()-.5)*.4);
      rock.rotation.set(Math.random(),Math.random(),Math.random()); arena.add(rock);
    }
  }
  for (let i=0;i<32;i++) {
    const side = Math.random()<.5 ? -1 : 1;
    const shrub = mesh(new THREE.IcosahedronGeometry(.5 + Math.random()*.34, 1), mat(i%3?0x315f39:0x417a46,1), true, true);
    shrub.position.set(side*(ARENA.width/2+2.0+Math.random()*3.0), .42, (Math.random()-.5)*(ARENA.length+8));
    shrub.scale.set(1,.72,1); arena.add(shrub);
  }
  for(const team of [TEAM.PLAYER,TEAM.ENEMY]){
    const sign=team===TEAM.PLAYER?1:-1;
    const color=team===TEAM.PLAYER?COLORS.player:COLORS.enemy;
    for(const x of [-11.1,11.1]){
      const plinth=mesh(new THREE.CylinderGeometry(.48,.58,.42,8),mat(0x59645f,.95),true,true);plinth.position.set(x,.18,sign*19.0);arena.add(plinth);
      const pole=mesh(new THREE.CylinderGeometry(.075,.09,2.6,8),mat(0x4d5660,.75,.18),true,true);pole.position.set(x,1.55,sign*19);arena.add(pole);
      const flag=mesh(new THREE.PlaneGeometry(1.15,.78),new THREE.MeshStandardMaterial({color,side:THREE.DoubleSide,roughness:.65}),true,false);flag.position.set(x+.58,2.1,sign*19);flag.rotation.y=Math.PI/2;arena.add(flag);
    }
    for(const x of [-13.1,13.1]){
      const torchBase=mesh(new THREE.CylinderGeometry(.25,.35,.75,8),mat(0x4b5657,.94),true,true);torchBase.position.set(x,.35,sign*8.5);arena.add(torchBase);
      const crystal=mesh(new THREE.OctahedronGeometry(.28,0),new THREE.MeshStandardMaterial({color,emissive:color,emissiveIntensity:1.3,roughness:.25}),true,true);crystal.position.set(x,1.08,sign*8.5);arena.add(crystal);
      ambientAnimations.push({type:'crystal',mesh:crystal,phase:Math.random()*6});
    }
  }

  // Side dioramas fill ultrawide screens without adding combat clutter.
  for(const side of [-1,1]){
    for(let i=0;i<6;i++){
      const island=new THREE.Group();
      const rock=mesh(new THREE.CylinderGeometry(2.2+Math.random()*1.8,1.35+Math.random()*1.2,1.7+Math.random()*1.3,7),mat(0x52635f,.98),true,true);rock.position.y=-.65;island.add(rock);
      const cap=mesh(new THREE.CylinderGeometry(2.0+Math.random()*1.6,2.15+Math.random()*1.7,.22,7),mat(i%2?0x537c4c:0x5f8855,.98),true,true);cap.position.y=.20;island.add(cap);
      if(i%2===0){
        for(let t=0;t<2+(i%3);t++){
          const trunk=mesh(new THREE.CylinderGeometry(.11,.16,.9,7),mat(0x5b3c26,.96),true,true);trunk.position.set((Math.random()-.5)*2.2,.70,(Math.random()-.5)*1.7);island.add(trunk);
          const crown=mesh(new THREE.IcosahedronGeometry(.55+Math.random()*.24,1),mat(0x366d42,.96),true,true);crown.position.set(trunk.position.x,1.35,trunk.position.z);crown.scale.y=.82;island.add(crown);
        }
      }else{
        const ruin=mesh(new THREE.BoxGeometry(1.15,1.2,.55),mat(0x7c8983,.96),true,true);ruin.position.set(0,.78,0);island.add(ruin);
        const broken=mesh(new THREE.BoxGeometry(1.5,.18,.72),mat(0x9aa39d,.96),true,true);broken.position.set(.18,1.42,0);broken.rotation.z=.10;island.add(broken);
      }
      island.position.set(side*(19.2+Math.random()*9.0),-1.2+Math.random()*1.4,-22+i*8.8+(Math.random()-.5)*2.0);island.rotation.y=Math.random()*Math.PI;arena.add(island);
    }
  }

  return field;
}

function updateAmbient(time){
  for(const a of ambientAnimations){
    if(a.type==='water'){
      a.mesh.material.uniforms.uTime.value=time;
    } else if(a.type==='foam'){
      a.mesh.material.opacity=.30+Math.sin(time*1.7+a.phase)*.10;
    } else if(a.type==='ripple'){
      a.mesh.position.x += Math.sin(time*a.speed+a.phase)*.0025;
      a.mesh.material.opacity=.11+Math.sin(time*1.8+a.phase)*.07;
    } else if(a.type==='crystal'){
      a.mesh.rotation.y=time*.65+a.phase;
      const s=1+Math.sin(time*2.1+a.phase)*.08; a.mesh.scale.setScalar(s);
    } else if(a.type==='cloud'){
      a.mesh.position.x+=a.speed*.0025; a.mesh.position.y+=Math.sin(time*.16+a.phase)*.0008;
    } else if(a.type==='dust'){
      a.mesh.rotation.y=time*.012; a.mesh.position.y=Math.sin(time*.25+a.phase)*.12;
    }
  }
}

const deploySurface = buildArena();

// Ground-space deployment feedback used by both click placement and true drag/drop.
const placementPreview = new THREE.Group();
const placementDisc = mesh(new THREE.CircleGeometry(1,64),new THREE.MeshBasicMaterial({color:0x54d8ff,transparent:true,opacity:.12,side:THREE.DoubleSide,depthWrite:false}),false,false);
placementDisc.rotation.x=-Math.PI/2; placementDisc.position.y=.10;
const placementRing = mesh(new THREE.RingGeometry(.82,1.0,64),new THREE.MeshBasicMaterial({color:0x8fe9ff,transparent:true,opacity:.92,side:THREE.DoubleSide,depthWrite:false,blending:THREE.AdditiveBlending}),false,false);
placementRing.rotation.x=-Math.PI/2; placementRing.position.y=.115;
const placementTile = mesh(new THREE.PlaneGeometry(.94,.94),new THREE.MeshBasicMaterial({color:0x72e7ff,transparent:true,opacity:.24,side:THREE.DoubleSide,depthWrite:false}),false,false);
placementTile.rotation.x=-Math.PI/2; placementTile.position.y=.108;
const placementFootprintTiles=new THREE.Group();
for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
  const t=mesh(new THREE.PlaneGeometry(.90,.90),new THREE.MeshBasicMaterial({color:0x72e7ff,transparent:true,opacity:.10,side:THREE.DoubleSide,depthWrite:false}),false,false);
  t.rotation.x=-Math.PI/2;t.position.set(dx*GRID.tile,.109,dz*GRID.tile);t.visible=false;t.userData.offset={dx,dz};placementFootprintTiles.add(t);
}
placementPreview.add(placementDisc,placementTile,placementFootprintTiles,placementRing); placementPreview.visible=false; scene.add(placementPreview);

function buildingOccupiesPreviewTile(card,dx,dz){
  if(!card?.building)return dx===0&&dz===0;
  const half=(card.footprint||1.6)*.5;
  const cellMinX=Math.abs(dx)*GRID.tile-GRID.tile*.5,cellMinZ=Math.abs(dz)*GRID.tile-GRID.tile*.5;
  return cellMinX<half&&cellMinZ<half;
}
function setPlacementPreview(card,point,valid){
  if(!card||!point){placementPreview.visible=false;return;}
  point = snapPointToTile(point, card);
  placementPreview.visible=true; placementPreview.position.set(point.x,0,point.z);
  const radius=card.spell?card.radius*GRID.tile:card.building?(card.footprint||1.6)*.62:.62; placementDisc.scale.setScalar(radius); placementRing.scale.setScalar(radius);
  placementTile.scale.set(1,1,1);placementTile.visible=!card.spell&&!card.building;
  for(const t of placementFootprintTiles.children){t.visible=!!card.building&&buildingOccupiesPreviewTile(card,t.userData.offset.dx,t.userData.offset.dz);}
  const color=valid?0x66e9ff:0xff5369; placementDisc.material.color.setHex(color); placementRing.material.color.setHex(color); placementTile.material.color.setHex(color);
  for(const t of placementFootprintTiles.children){t.material.color.setHex(color);t.material.opacity=valid?.16:.24;}
  placementDisc.material.opacity=valid?.10:.16; placementTile.material.opacity=valid?.28:.34;
}

function makeTeamRing(team, radius=0.8) {
  const color = team === TEAM.PLAYER ? COLORS.player : COLORS.enemy;
  const g = new THREE.RingGeometry(radius*.72, radius, 32);
  const m = new THREE.MeshBasicMaterial({ color, side: THREE.DoubleSide, transparent:true, opacity:.8, depthWrite:false });
  const ring = new THREE.Mesh(g,m);
  ring.rotation.x = -Math.PI/2; ring.position.y=.035;
  return ring;
}

function addCylinder(group, r1,r2,h,color, x,y,z, rotZ=0, rotX=0, rotY=0, sides=12) {
  const part = mesh(new THREE.CylinderGeometry(r1,r2,h,sides), mat(color));
  part.position.set(x,y,z); part.rotation.set(rotX,rotY,rotZ); group.add(part); return part;
}
function addBox(group,w,h,d,color,x,y,z,rx=0,ry=0,rz=0){ const p=roundedBox(w,h,d,color); p.position.set(x,y,z); p.rotation.set(rx,ry,rz); group.add(p); return p; }
function addSphere(group,r,color,x,y,z,sx=1,sy=1,sz=1){ const p=mesh(new THREE.IcosahedronGeometry(r,2),mat(color)); p.position.set(x,y,z); p.scale.set(sx,sy,sz); group.add(p); return p; }
function addCone(group,r1,r2,h,color,x,y,z,rx=0,ry=0,rz=0,sides=12){ const p=mesh(new THREE.CylinderGeometry(r2,r1,h,sides),mat(color));p.position.set(x,y,z);p.rotation.set(rx,ry,rz);group.add(p);return p; }
function addTorus(group,major,minor,color,x,y,z,rx=0,ry=0,rz=0){const p=mesh(new THREE.TorusGeometry(major,minor,7,18),mat(color,.5,.2));p.position.set(x,y,z);p.rotation.set(rx,ry,rz);group.add(p);return p;}
function addEye(group,x,y,z,color=0xd8fbff){const white=addSphere(group,.072,0xf2f7f8,x,y,z,1,.72,.42);const pupil=addSphere(group,.032,color,x,y+.008,z-.056,1,.82,.45);return [white,pupil];}

function fallbackModel(modelId) {
  // These are deliberately much more detailed than v1 so the game still looks authored
  // when Blender exports have not been generated on the player's machine yet.
  const g = new THREE.Group();
  switch(modelId) {
    case 'ironclad': {
      addCylinder(g,.39,.48,.92,0x425565,0,.78,0);
      addBox(g,.78,.48,.58,0x708897,0,1.10,0); addBox(g,.61,.18,.62,0x2b3c49,0,1.24,-.01);
      addSphere(g,.31,0xd7ae89,0,1.63,0); addCone(g,.37,.29,.31,0x354a59,0,1.91,0);
      addBox(g,.72,.10,.12,0xb7ced8,0,1.82,-.23); addBox(g,.58,.08,.15,0x20313d,0,1.71,-.27);
      for(const x of [-.26,.26]){addCylinder(g,.115,.13,.64,0x465a69,x,.43,0);addBox(g,.25,.18,.38,0x2e414f,x,.13,-.03);}
      addCylinder(g,.12,.15,.58,0x667c88,-.55,1.12,0,0,0,0,10); addBox(g,.60,.82,.14,0x4b6573,-.62,1.12,-.02,0,0,.08); addTorus(g,.29,.035,0xb8d4df,-.62,1.12,-.095,Math.PI/2);
      addCylinder(g,.055,.055,.86,0xdde8ee,.57,1.12,0,-.23); addBox(g,.10,.31,.11,0x6e482d,.47,.70,0,0,0,-.23); addBox(g,.36,.06,.08,0xc5d9e0,.51,.83,0,0,0,-.23);
      addBox(g,.08,.36,.08,0x49c9ff,0,1.11,-.34,0,0,.78); addBox(g,.08,.36,.08,0x49c9ff,0,1.11,-.34,0,0,-.78);
      break;
    }
    case 'ember_archer': {
      addCone(g,.46,.29,.93,0x8c3b2e,0,.65,0); addBox(g,.53,.17,.46,0xd26038,0,.98,-.03);
      addSphere(g,.29,0xe2b18c,0,1.42,0); addCone(g,.44,.09,.53,0x7b3029,0,1.63,.05);
      addBox(g,.16,.12,.33,0x5b2a25,-.31,1.20,.04,0,0,.22); addBox(g,.16,.12,.33,0x5b2a25,.31,1.20,.04,0,0,-.22);
      for(const x of [-.15,.15]) addBox(g,.21,.23,.34,0x3d302c,x,.15,0);
      const bow=addTorus(g,.47,.035,0x6c3d22,.55,1.03,0,0,Math.PI/2,0); bow.scale.x=.63;
      addCylinder(g,.018,.018,1.03,0xe9d7c5,.55,1.03,0,0,0,0,6);
      addCylinder(g,.025,.025,1.18,0xe5e6dc,.15,1.08,-.18,Math.PI/2); addCone(g,.07,.01,.18,0xd9d9d2,.15,1.08,-.83,Math.PI/2);
      addBox(g,.23,.5,.22,0x6c2e28,-.47,.89,.12,0,0,.15); addCylinder(g,.11,.13,.48,0x553127,-.47,.93,.18);
      addSphere(g,.08,0xffb34d,.29,1.00,-.17); addSphere(g,.04,0xffe08a,.29,1.00,-.21);
      break;
    }
    case 'twin_blade': {
      addCylinder(g,.28,.38,.76,0x312b49,0,.61,0); addBox(g,.54,.25,.45,0x57466f,0,.94,0);
      addSphere(g,.275,0xcfa17f,0,1.34,0); addCone(g,.36,.12,.43,0x28273b,0,1.55,.02);
      addBox(g,.56,.10,.16,0x151723,0,1.36,-.24); addBox(g,.12,.065,.05,0xa368ff,-.11,1.37,-.315); addBox(g,.12,.065,.05,0xa368ff,.11,1.37,-.315);
      for(const [x,sgn] of [[-.43,-1],[.43,1]]){
        addCylinder(g,.09,.105,.50,0x443756,x,.77,0,sgn*.38); addCylinder(g,.045,.045,.86,0xe3edf6,x+sgn*.13,.93,0,sgn*.43); addBox(g,.28,.055,.08,0x9a78bd,x+sgn*.04,.68,0,0,0,sgn*.43);
      }
      for(const x of [-.16,.16]) addBox(g,.20,.24,.30,0x242638,x,.15,0);
      addBox(g,.34,.08,.18,0x9465d4,0,.87,-.29); break;
    }
    case 'boulderback': {
      addSphere(g,.77,0x5d6866,0,.90,0,1.26,.86,.97); addSphere(g,.47,0x6f7b77,0,1.32,-.55,1,.92,.95);
      for(const [x,z,s] of [[-.48,.17,.42],[.45,.23,.5],[0,.46,.44],[-.15,-.18,.36]]) addSphere(g,s,0x77817f,x,1.52,z,1,.85,1);
      for(const x of [-.52,.52]) for(const z of [-.38,.38]){addBox(g,.39,.65,.39,0x4c5654,x,.32,z,0,0,(x+z)*.12);addBox(g,.46,.16,.52,0x394443,x,.08,z);}
      addEye(g,-.17,1.43,-.93,0xcf65ff);addEye(g,.17,1.43,-.93,0xcf65ff);
      addBox(g,.48,.08,.18,0x26302f,0,1.18,-.98); addTorus(g,.28,.055,0x9654c1,0,.99,-.64,Math.PI/2);
      addCone(g,.18,.03,.43,0x8b9290,-.53,1.28,-.67,-.35,0,.3); addCone(g,.18,.03,.43,0x8b9290,.53,1.28,-.67,-.35,0,-.3);
      break;
    }
    case 'arc_mage': {
      addCone(g,.55,.24,1.10,0x363786,0,.64,0); addBox(g,.61,.20,.48,0x5053b7,0,.97,0);
      addSphere(g,.29,0xdab08e,0,1.44,0); addCone(g,.49,.035,.68,0x4b3a9a,0,1.86,.02); addTorus(g,.28,.04,0x8a72df,0,1.61,.03,Math.PI/2);
      for(const x of [-.37,.37]) addCylinder(g,.10,.12,.52,0x3f408f,x,.94,0,x*.6);
      addCylinder(g,.045,.055,1.64,0x70472a,.53,1.03,0,.06); addTorus(g,.25,.032,0xb8dce4,.53,1.91,0,Math.PI/2); addSphere(g,.17,0x63dcff,.53,1.91,0); addSphere(g,.075,0xe9fcff,.53,1.91,-.12);
      addBox(g,.13,.27,.09,0x55d9ff,-.18,.91,-.34,0,0,.65); addBox(g,.13,.27,.09,0x55d9ff,.18,.91,-.34,0,0,-.65);
      for(const x of [-.14,.14]) addBox(g,.19,.20,.31,0x27295e,x,.13,0); break;
    }
    case 'rambeast': {
      addSphere(g,.67,0x805b40,0,.78,.10,1.3,.78,.92); addSphere(g,.43,0x604332,0,1.05,-.64,1,.95,.92); addSphere(g,.34,0x734c35,0,1.20,-.78,1,.8,.7);
      for(const x of [-.47,.47]) for(const z of [-.34,.34]){addCylinder(g,.13,.17,.60,0x593b2d,x,.32,z);addBox(g,.27,.16,.32,0x2f2926,x,.06,z);}
      addTorus(g,.47,.095,0xe1c89e,-.30,1.35,-.71,.2,.2,.5); addTorus(g,.47,.095,0xe1c89e,.30,1.35,-.71,.2,-.2,-.5);
      addBox(g,.80,.20,.58,0x536675,0,1.14,-.22); addBox(g,.90,.08,.64,0xd3a947,0,1.23,-.22); addSphere(g,.12,0xffc84b,0,1.25,-.55);
      addEye(g,-.14,1.18,-1.02,0xffd465);addEye(g,.14,1.18,-1.02,0xffd465); addBox(g,.30,.07,.12,0x2b211d,0,.98,-1.08);
      break;
    }
    case 'sky_manta': {
      addSphere(g,.55,0x337d86,0,1.02,0,1.36,.36,1.0);
      const wl=addBox(g,1.25,.08,.74,0x59b9ba,-.72,1.03,.04,0,.18,.14); wl.scale.x=1.05;
      const wr=addBox(g,1.25,.08,.74,0x59b9ba,.72,1.03,.04,0,-.18,-.14); wr.scale.x=1.05;
      addBox(g,1.3,.035,.42,0x8ae0db,-.72,1.06,-.02,0,.18,.14); addBox(g,1.3,.035,.42,0x8ae0db,.72,1.06,-.02,0,-.18,-.14);
      addCone(g,.12,.018,1.15,0x2f737b,0,1.02,.96,Math.PI/2); addSphere(g,.12,0x2d6f78,0,1.03,-.72,1.1,.75,.72);
      addEye(g,-.19,1.10,-.55,0x63f4ff);addEye(g,.19,1.10,-.55,0x63f4ff);
      addTorus(g,.30,.032,0x5fe6ee,0,1.04,-.11,Math.PI/2); break;
    }
    case 'vampire_bat': {
      const body=addSphere(g,.34,0x38243f,0,1.02,0,1.05,.72,1.08);
      addSphere(g,.22,0x4d2a4d,0,1.28,-.18,1,.82,.82);
      addCone(g,.11,.02,.26,0x2a1930,-.14,1.50,-.12,.12,0,-.18,7);
      addCone(g,.11,.02,.26,0x2a1930,.14,1.50,-.12,.12,0,.18,7);
      addEye(g,-.09,1.32,-.36,0xff5e8b); addEye(g,.09,1.32,-.36,0xff5e8b);
      const leftWing=addBox(g,.72,.035,.46,0x6f2c58,-.48,1.07,.04,0,.18,.20); leftWing.name='batWingL';
      const rightWing=addBox(g,.72,.035,.46,0x6f2c58,.48,1.07,.04,0,-.18,-.20); rightWing.name='batWingR';
      addCone(g,.08,.01,.22,0xe8d4df,-.08,1.21,-.43,Math.PI/2,0,0,6); addCone(g,.08,.01,.22,0xe8d4df,.08,1.21,-.43,Math.PI/2,0,0,6);
      const core=addSphere(g,.08,0xd84d79,0,1.00,-.32);core.material.emissive=new THREE.Color(0x9e234e);core.material.emissiveIntensity=.9;
      g.userData.batWingL=leftWing;g.userData.batWingR=rightWing;
      break;
    }
    case 'frost_fang': {
      // Heavy frost predator: clearer head, armor and spine crystals so the card reads from the gameplay camera.
      addSphere(g,.64,0xb8d3dd,0,.76,.08,1.24,.72,.95); addSphere(g,.44,0x8caebb,0,1.06,-.50,1.02,.86,.92);
      addSphere(g,.26,0xaed1dc,0,1.19,-.82,1.0,.72,.82);
      for(const x of [-.44,.44]) for(const z of [-.30,.30]){addCylinder(g,.12,.16,.60,0x7895a1,x,.31,z);addBox(g,.28,.15,.30,0x49616d,x,.06,z);}
      addEye(g,-.14,1.15,-.88,0xaef6ff);addEye(g,.14,1.15,-.88,0xaef6ff);
      addCone(g,.082,.012,.38,0xeafcff,-.17,.97,-.99,Math.PI/2,0,.05,7);addCone(g,.082,.012,.38,0xeafcff,.17,.97,-.99,Math.PI/2,0,-.05,7);
      addBox(g,.28,.05,.08,0x5a7486,0,1.26,-.74);
      for(const [x,z,h] of [[-.31,.16,.44],[-.10,.28,.34],[.10,.30,.48],[.31,.16,.42]]){const ice=addCone(g,.13,.015,h,0x9eeaff,x,1.48,z,.10,0,x*.22,7);ice.material.emissive=new THREE.Color(0x5fcfff);ice.material.emissiveIntensity=.65;}
      addBox(g,.74,.10,.54,0x4a6f7c,0,1.14,-.04);addBox(g,.80,.035,.60,0xc8f4ff,0,1.20,-.04);
      addBox(g,.20,.18,.18,0x7da4b4,-.36,1.10,-.38,0,.22,0);addBox(g,.20,.18,.18,0x7da4b4,.36,1.10,-.38,0,-.22,0);
      addTorus(g,.33,.025,0x9cecff,0,1.05,-.58,Math.PI/2);addCone(g,.12,.015,.28,0x78dfff,0,.95,.86,-.18,0,0,7);
      break;
    }
    case 'storm_raven': {
      const body=addSphere(g,.54,0x202a48,0,1.05,0,1.04,.70,1.20); addSphere(g,.33,0x2d3c66,0,1.34,-.34,1,.80,.88);
      const wl=addBox(g,1.24,.05,.62,0x344b7a,-.80,1.12,.03,0,.14,.22);wl.name='stormWingL';
      const wr=addBox(g,1.24,.05,.62,0x344b7a,.80,1.12,.03,0,-.14,-.22);wr.name='stormWingR';
      addBox(g,1.15,.022,.14,0x78dfff,-.82,1.16,-.04,0,.14,.22);addBox(g,1.15,.022,.14,0x78dfff,.82,1.16,-.04,0,-.14,-.22);
      addBox(g,.42,.08,.22,0x314672,0,1.16,-.08);
      addCone(g,.12,.015,.44,0x303f64,0,1.30,-.74,Math.PI/2,0,0,7);addCone(g,.09,.015,.28,0xb8d8ff,0,1.30,-.99,Math.PI/2,0,0,7);
      addEye(g,-.10,1.40,-.60,0x7beaff);addEye(g,.10,1.40,-.60,0x7beaff);
      const core=addSphere(g,.12,0x72dfff,0,1.11,-.42);core.material.emissive=new THREE.Color(0x42bfff);core.material.emissiveIntensity=1.55;
      addTorus(g,.74,.028,0x65dfff,0,.98,0,Math.PI/2);addTorus(g,.50,.014,0xc1f5ff,0,.98,0,Math.PI/2);
      for(const x of [-.62,-.36,-.12,.12,.36,.62])addCone(g,.065,.008,.28,0x99ecff,x,1.39,.15,0,0,x*.28,6);
      addCone(g,.10,.014,.35,0x4a608d,-.15,1.48,-.12,.18,0,-.16,7);addCone(g,.10,.014,.35,0x4a608d,.15,1.48,-.12,.18,0,.16,7);
      addCone(g,.09,.012,.64,0x2a395a,-.18,1.50,.20,Math.PI/2,0,-.12,7);addCone(g,.09,.012,.64,0x2a395a,.18,1.50,.20,Math.PI/2,0,.12,7);
      break;
    }
    case 'archer_tower': {
      addCylinder(g,1.05,1.18,.32,0x56625c,0,.16,0,0,0,0,10);
      addCylinder(g,.83,.96,1.95,0xa7b0a7,0,1.20,0,0,0,0,10);
      for(const y of [.55,1.05,1.55]) addBox(g,1.62,.08,1.62,0x748078,0,y,0);
      addBox(g,1.95,.20,1.75,0x6d482b,0,2.25,0);
      addBox(g,2.10,.10,1.90,0xd0ae57,0,2.39,0);
      for(const x of [-.76,.76]) for(const z of [-.62,.62]) addBox(g,.28,.55,.28,0x4c5751,x,2.60,z);
      addSphere(g,.22,0xd6a37b,0,2.70,-.18); addCone(g,.30,.06,.42,0x476b40,0,2.95,-.12);
      addTorus(g,.43,.032,0x754521,.50,2.65,-.04,Math.PI/2); addCylinder(g,.018,.018,.90,0xf4ddaf,.50,2.65,-.04,0,0,0,6);
      addBox(g,.58,.08,.16,0x3b4a43,0,1.18,-.84); addSphere(g,.12,0x62d9ff,0,1.18,-.95);
      break;
    }
    default: addSphere(g,.6,0xaaaaaa,0,.7,0); break;
  }
  // V3 silhouette/detail pass: secondary armor, trims, emissive accents and props make
  // the procedural fallback hold up even when Blender GLBs have not been generated yet.
  if(modelId==='ironclad'){
    addBox(g,.52,.72,.055,0x1e5268,0,.93,.27,.08,0,0); // cape
    addBox(g,.22,.11,.20,0x758995,-.18,.38,-.05); addBox(g,.22,.11,.20,0x758995,.18,.38,-.05);
    addCone(g,.10,.02,.27,0xd78e3c,0,2.16,.02,0,0,0,8); addSphere(g,.085,0x49d7ff,.55,.71,-.02);
    for(const x of [-.62,-.53,-.44]) addSphere(g,.035,0xc7d6dd,x,1.12,-.11);
  } else if(modelId==='ember_archer'){
    addBox(g,.48,.16,.30,0x612923,0,1.14,.13,.12,0,0); addBox(g,.18,.20,.14,0x4b2a23,-.23,.57,-.05); addBox(g,.18,.20,.14,0x4b2a23,.23,.57,-.05);
    const flame=addSphere(g,.09,0xff7138,.15,1.08,-.92,.72,1.3,.72); flame.material.emissive=new THREE.Color(0xff4b16);flame.material.emissiveIntensity=1.3;
    addBox(g,.09,.26,.08,0xe1a04a,-.04,.67,-.29,0,0,.45); addBox(g,.09,.26,.08,0xe1a04a,.04,.67,-.29,0,0,-.45);
  } else if(modelId==='twin_blade'){
    addBox(g,.42,.08,.22,0x7650a5,0,.61,.26,.18,0,0); addBox(g,.17,.13,.16,0x2c2e42,-.16,.34,-.04); addBox(g,.17,.13,.16,0x2c2e42,.16,.34,-.04);
    addCone(g,.08,.015,.22,0x9d72e8,-.20,1.72,.02,0,0,-.12,8); addCone(g,.08,.015,.22,0x9d72e8,.20,1.72,.02,0,0,.12,8);
  } else if(modelId==='boulderback'){
    for(const [x,z,h] of [[-.40,.18,.38],[.38,.30,.46],[.05,.54,.34]]){const c=addCone(g,.13,.015,h,0x9a59d0,x,1.82,z,.12,0,x*.4,7);c.material.emissive=new THREE.Color(0x6d2ca5);c.material.emissiveIntensity=.7;}
    addBox(g,.35,.09,.16,0x88908d,-.46,.72,-.58,0,0,.45);addBox(g,.35,.09,.16,0x88908d,.46,.72,-.58,0,0,-.45);
  } else if(modelId==='arc_mage'){
    addBox(g,.36,.48,.09,0x263b6d,-.47,.86,.08,0,.18,.08); addBox(g,.30,.42,.035,0x76cfff,-.47,.86,.025,0,.18,.08);
    for(const [x,z] of [[-.38,-.26],[.36,-.28]]){const r=addSphere(g,.07,0x78efff,x,1.18,z);r.material.emissive=new THREE.Color(0x5fdcff);r.material.emissiveIntensity=1.15;}
    addTorus(g,.39,.018,0x7ae9ff,0,1.34,.02,.7,.3,.3);
  } else if(modelId==='rambeast'){
    addBox(g,.46,.12,.42,0x485e70,-.55,.84,.10,0,0,.08); addBox(g,.46,.12,.42,0x485e70,.55,.84,.10,0,0,-.08);
    for(const x of [-.56,.56]){addTorus(g,.16,.028,0xd9b44e,x,1.33,-.78,.25,0,x<0?.35:-.35);addCone(g,.09,.015,.31,0xb8c4cb,x,.99,.18,0,0,x<0?-.3:.3,8);}
  } else if(modelId==='sky_manta'){
    for(const x of [-1.08,-.67,.67,1.08]){const dot=addSphere(g,.07,0x7ffff4,x,1.08,.02,1,.5,1);dot.material.emissive=new THREE.Color(0x5ffff0);dot.material.emissiveIntensity=1.0;}
    addBox(g,.34,.035,.44,0x46a7ae,-.30,1.04,.82,0,.20,-.25);addBox(g,.34,.035,.44,0x46a7ae,.30,1.04,.82,0,-.20,.25);
  }
  // V4 readability pass: stronger faces, material separation, layered trims and secondary props.
  if(modelId==='ironclad'){
    addEye(g,-.105,1.64,-.285,0x71ddff);addEye(g,.105,1.64,-.285,0x71ddff);
    addBox(g,.70,.045,.08,0xd6b45d,0,1.26,-.32);addBox(g,.18,.18,.22,0x9aaab1,-.42,1.26,-.02);addBox(g,.18,.18,.22,0x9aaab1,.42,1.26,-.02);
    addTorus(g,.115,.018,0x54d9ff,.55,.70,-.02,Math.PI/2);
  }else if(modelId==='ember_archer'){
    addEye(g,-.10,1.43,-.274,0xffd598);addEye(g,.10,1.43,-.274,0xffd598);
    addBox(g,.42,.055,.10,0xd9a14d,0,.99,-.28);addCone(g,.07,.015,.22,0xffb24b,-.17,1.73,-.08,-.18,0,-.15,7);addCone(g,.07,.015,.22,0xffb24b,.17,1.73,-.08,-.18,0,.15,7);
  }else if(modelId==='twin_blade'){
    addBox(g,.50,.035,.075,0xb890ff,0,.95,-.245);addSphere(g,.045,0xdcbdff,-.11,1.37,-.34);addSphere(g,.045,0xdcbdff,.11,1.37,-.34);
    addBox(g,.12,.24,.10,0x171a28,-.31,.50,.17,0,0,.22);addBox(g,.12,.24,.10,0x171a28,.31,.50,.17,0,0,-.22);
  }else if(modelId==='boulderback'){
    for(const [x,z] of [[-.62,.08],[.62,.02],[0,.68]])addSphere(g,.15,0x4a5452,x,1.24,z,1,.65,1);
    addTorus(g,.37,.025,0xbd7ce9,0,1.28,-.72,Math.PI/2);
  }else if(modelId==='arc_mage'){
    addEye(g,-.10,1.45,-.275,0x6eeaff);addEye(g,.10,1.45,-.275,0x6eeaff);
    addBox(g,.44,.04,.08,0xe0be61,0,.99,-.29);addSphere(g,.055,0xd8fbff,.53,2.03,0);addSphere(g,.045,0x79e7ff,-.39,1.34,-.24);
  }else if(modelId==='rambeast'){
    addBox(g,.62,.035,.11,0xe0bd5f,0,1.23,-.54);addBox(g,.23,.16,.12,0x31414a,-.50,.63,-.34);addBox(g,.23,.16,.12,0x31414a,.50,.63,-.34);
    addSphere(g,.055,0xffe28a,-.14,1.18,-1.08);addSphere(g,.055,0xffe28a,.14,1.18,-1.08);
  }else if(modelId==='sky_manta'){
    addTorus(g,.19,.018,0xd8ffff,0,1.04,-.44,Math.PI/2);addSphere(g,.055,0xbaffff,-.19,1.10,-.61);addSphere(g,.055,0xbaffff,.19,1.10,-.61);
    addBox(g,.54,.025,.16,0x2c6d76,-.66,1.02,.36,0,.18,.14);addBox(g,.54,.025,.16,0x2c6d76,.66,1.02,.36,0,-.18,-.14);
  }else if(modelId==='archer_tower'){
    for(const y of [.72,1.28,1.84])addBox(g,1.68,.035,1.68,0xc0c9be,0,y,0);
    addBox(g,.45,.55,.04,0x7d3f27,0,1.35,.86); addSphere(g,.06,0x8cecff,0,1.18,-1.02);
    addBox(g,.20,.14,.18,0x395f37,-.38,2.51,-.14); addBox(g,.20,.14,.18,0x395f37,.38,2.51,-.14);
  }
  // V9 model polish: denser readable silhouettes and small emissive/metal accents for the high camera.
  if(modelId==='ironclad'){
    addTorus(g,.20,.018,0xe4c46b,0,1.10,-.35,Math.PI/2);addBox(g,.26,.06,.08,0xdceaf0,0,1.78,-.28);addSphere(g,.045,0x7fe7ff,0,1.10,-.39);
  }else if(modelId==='ember_archer'){
    addBox(g,.34,.04,.08,0xffca69,0,1.02,-.31);addSphere(g,.055,0xffe49b,.15,1.08,-.96);addBox(g,.09,.30,.06,0x8f4b2f,-.42,.93,.24,0,0,-.15);
  }else if(modelId==='twin_blade'){
    addTorus(g,.16,.014,0xc397ff,0,.91,-.31,Math.PI/2);addBox(g,.08,.36,.05,0x7f5ab1,-.25,.73,.29,0,0,.22);addBox(g,.08,.36,.05,0x7f5ab1,.25,.73,.29,0,0,-.22);
  }else if(modelId==='boulderback'){
    for(const x of [-.33,.0,.33]){const c=addCone(g,.085,.01,.30,0xc66cff,x,1.92,.22,0,0,x*.4,7);c.material.emissive=new THREE.Color(0x8d32c7);c.material.emissiveIntensity=.75;}
  }else if(modelId==='arc_mage'){
    addTorus(g,.26,.012,0xbff8ff,.53,1.91,0,.5,.4,.2);addBox(g,.28,.025,.05,0xe4c766,0,.98,-.33);addSphere(g,.04,0xffffff,.53,1.91,-.20);
  }else if(modelId==='rambeast'){
    addTorus(g,.23,.018,0xf1d06b,0,1.24,-.59,Math.PI/2);addBox(g,.54,.04,.08,0xe0b957,0,1.18,-.65);addSphere(g,.045,0xffe5a0,0,1.25,-.62);
  }else if(modelId==='sky_manta'){
    addBox(g,1.55,.018,.07,0xc7ffff,0,1.08,.02);addSphere(g,.055,0xeaffff,0,1.02,-.79);addTorus(g,.24,.012,0x82fff7,0,1.03,-.48,Math.PI/2);
  }else if(modelId==='archer_tower'){
    addTorus(g,.30,.018,0xf0cf75,0,2.46,-.10,Math.PI/2);addBox(g,.70,.035,.07,0xd8bd6b,0,2.38,-.62);addSphere(g,.055,0xb8f5ff,0,1.18,-1.06);
  }
  g.traverse(o=>{ if(o.isMesh){o.castShadow=true;o.receiveShadow=true;} });
  return g;
}

async function loadModel(modelId) {
  if (!modelId || modelFailed.has(modelId)) return null;
  if(!modelCache.has(modelId)){
    if(!modelLoads.has(modelId)){
      modelLoads.set(modelId,loader.loadAsync(`./assets/models/${modelId}.glb`).then(gltf=>{
        const root=gltf.scene;
        root.userData.riftBlenderModel=true;
        root.traverse(o=>{if(o.isMesh){o.castShadow=true;o.receiveShadow=true;}});
        modelCache.set(modelId,root);
      }).catch(()=>{
        modelFailed.add(modelId);
        console.info(`[Rift Crown] Blender model ${modelId}.glb not found — procedural fallback active.`);
      }).finally(()=>modelLoads.delete(modelId)));
    }
    await modelLoads.get(modelId);
  }
  const root=modelCache.get(modelId);
  if(!root)return null;
  const clone=root.clone(true);clone.userData.riftBlenderModel=true;return clone;
}

function roundedRect(ctx,x,y,w,h,r){
  const rr=Math.min(r,w/2,h/2); ctx.beginPath(); ctx.moveTo(x+rr,y); ctx.arcTo(x+w,y,x+w,y+h,rr); ctx.arcTo(x+w,y+h,x,y+h,rr); ctx.arcTo(x,y+h,x,y,rr); ctx.arcTo(x,y,x+w,y,rr); ctx.closePath();
}

function makeTowerHpSprite(team, kind, hp, maxHp){
  const canvas=document.createElement('canvas'); canvas.width=640; canvas.height=160;
  const texture=new THREE.CanvasTexture(canvas); texture.colorSpace=THREE.SRGBColorSpace; texture.minFilter=THREE.LinearFilter; texture.magFilter=THREE.LinearFilter;
  const material=new THREE.SpriteMaterial({map:texture,transparent:true,depthTest:false,depthWrite:false});
  const sprite=new THREE.Sprite(material); sprite.scale.set(kind==='core'?3.2:2.9,.80,1); sprite.renderOrder=28;
  sprite.userData={canvas,texture,team,kind,lastHp:null,lastMax:null,lastDead:false};
  updateTowerHpSprite(sprite,hp,maxHp,false); return sprite;
}

function updateTowerHpSprite(sprite,hp,maxHp,dead=false){
  if(!sprite?.userData?.canvas)return;
  const {canvas,texture,team,kind}=sprite.userData;
  const safeMax=Math.max(1,Number.isFinite(maxHp)?Math.round(maxHp):1);
  const safeHp=THREE.MathUtils.clamp(Number.isFinite(hp)?hp:0,0,safeMax);
  const shown=Math.max(0,Math.ceil(safeHp));
  if(sprite.userData.lastHp===shown&&sprite.userData.lastMax===safeMax&&sprite.userData.lastDead===dead)return;
  sprite.userData.lastHp=shown; sprite.userData.lastMax=safeMax; sprite.userData.lastDead=dead;
  const p=safeHp/safeMax, ctx=canvas.getContext('2d');
  ctx.clearRect(0,0,canvas.width,canvas.height);
  ctx.save();
  ctx.shadowColor='rgba(0,0,0,.78)';ctx.shadowBlur=18;ctx.shadowOffsetY=6;
  roundedRect(ctx,20,18,600,122,24); ctx.fillStyle='rgba(3,8,14,.96)'; ctx.fill();
  ctx.shadowColor='transparent';
  ctx.lineWidth=7; ctx.strokeStyle=team===TEAM.PLAYER?'#55dcff':'#ff667d'; ctx.stroke();
  roundedRect(ctx,42,100,556,20,10);ctx.fillStyle='rgba(28,38,49,.98)';ctx.fill();
  if(p>0){roundedRect(ctx,42,100,556*p,20,10);ctx.fillStyle=p>.50?'#59ef83':p>.22?'#ffd75d':'#ff5263';ctx.fill();}
  ctx.fillStyle=dead?'#a6b1bc':'#ffffff'; ctx.font='1000 46px Inter,Arial,sans-serif'; ctx.textAlign='center';ctx.textBaseline='middle';
  ctx.fillText(dead?'DESTROYED':`${shown} / ${safeMax}`,320,68);
  ctx.fillStyle=kind==='core'?'#ffe084':'#cbd7e3'; ctx.font='1000 18px Inter,Arial,sans-serif';
  ctx.fillText(kind==='core'?'CORE TOWER':'GUARD TOWER',320,34);
  ctx.restore(); texture.needsUpdate=true;
}


function makeUnitHpSprite(team, hp, maxHp){
  const canvas=document.createElement('canvas'); canvas.width=360; canvas.height=48;
  const texture=new THREE.CanvasTexture(canvas); texture.colorSpace=THREE.SRGBColorSpace; texture.minFilter=THREE.LinearFilter; texture.magFilter=THREE.LinearFilter;
  const material=new THREE.SpriteMaterial({map:texture,transparent:true,depthTest:false,depthWrite:false});
  const sprite=new THREE.Sprite(material); sprite.scale.set(1.55,.21,1); sprite.renderOrder=29;
  sprite.userData={canvas,texture,team,lastHp:null,lastMax:null,lastDead:false};
  updateUnitHpSprite(sprite,hp,maxHp,false); return sprite;
}

function updateUnitHpSprite(sprite,hp,maxHp,dead=false){
  if(!sprite?.userData?.canvas)return;
  const {canvas,texture,team}=sprite.userData;
  const safeMax=Math.max(1,Number.isFinite(maxHp)?Math.round(maxHp):1);
  const safeHp=THREE.MathUtils.clamp(Number.isFinite(hp)?hp:0,0,safeMax);
  const shown=Math.max(0,Math.ceil(safeHp));
  if(sprite.userData.lastHp===shown&&sprite.userData.lastMax===safeMax&&sprite.userData.lastDead===dead)return;
  sprite.userData.lastHp=shown; sprite.userData.lastMax=safeMax; sprite.userData.lastDead=dead;
  const pct=safeHp/safeMax,ctx=canvas.getContext('2d');
  ctx.clearRect(0,0,canvas.width,canvas.height);
  ctx.save();
  roundedRect(ctx,5,7,350,34,13);ctx.fillStyle='rgba(4,9,15,.88)';ctx.fill();
  ctx.lineWidth=3;ctx.strokeStyle=team===TEAM.PLAYER?'rgba(93,220,255,.94)':'rgba(255,102,126,.94)';ctx.stroke();
  roundedRect(ctx,14,15,332,18,8);ctx.fillStyle='rgba(22,31,40,.98)';ctx.fill();
  if(pct>0){roundedRect(ctx,14,15,332*pct,18,8);ctx.fillStyle=pct>.5?'#5bef86':pct>.22?'#ffd75d':'#ff5364';ctx.fill();}
  ctx.restore();texture.needsUpdate=true;
}

function towerHudId(tower){
  const side=tower.team===TEAM.PLAYER?'player':'enemy';
  const slot=tower.kind==='core'?'core':tower.lane<0?'left':'right';
  return `${side}-${slot}-hp`;
}

// One authoritative sync path for every tower-health presentation. The in-world plate is
// never dependent on optional DOM HUD elements, preventing stale/mismatched HP readouts.
function updateTowerHud(tower){
  const safeMax=Math.max(1,Number.isFinite(tower.maxHp)?tower.maxHp:1);
  tower.hp=THREE.MathUtils.clamp(Number.isFinite(tower.hp)?tower.hp:0,0,safeMax);
  const p=tower.hp/safeMax;
  if(tower.hpSprite) updateTowerHpSprite(tower.hpSprite,tower.hp,safeMax,tower.dead);
  if(tower.bar){
    const fill=tower.bar.userData?.fill;
    if(fill){fill.scale.x=Math.max(.001,p);fill.position.x=-(tower.bar.userData.maxWidth*(1-p))/2;fill.material.color.setHex(p>.5?0x66e376:p>.22?0xffd65b:0xff5e65);}
  }
  // Backwards-compatible optional DOM health chips; V0.4 intentionally does not render them.
  const el=document.getElementById(towerHudId(tower));
  if(el){const strong=el.querySelector('strong'),meter=el.querySelector('i');if(strong)strong.textContent=tower.dead?'DESTROYED':`${Math.ceil(tower.hp)} / ${safeMax}`;if(meter)meter.style.width=`${p*100}%`;el.classList.toggle('critical',p>0&&p<=.25);el.classList.toggle('destroyed',tower.dead);}
  tower._displayHp=Math.ceil(tower.hp); tower._displayDead=tower.dead;
}


function updateUnitHud(unit){
  const safeMax=Math.max(1,Number.isFinite(unit.maxHp)?unit.maxHp:1);
  unit.hp=THREE.MathUtils.clamp(Number.isFinite(unit.hp)?unit.hp:0,0,safeMax);
  const p=unit.hp/safeMax;
  if(unit.bar){
    const fill=unit.bar.userData?.fill;
    if(fill){
      fill.scale.x=Math.max(.001,p);
      fill.position.x=-(unit.bar.userData.maxWidth*(1-p))/2;
      fill.material.color.setHex(p>.5?0x66e376:p>.22?0xffd65b:0xff5e65);
    }
  }
  if(unit.hpSprite){
    updateUnitHpSprite(unit.hpSprite,unit.hp,safeMax,unit.dead);
    const activeCombat=!!(unit.target&&!unit.target.dead&&horizontalDistance(unit,unit.target)<=Math.max(6,unit.range+2.2));
    unit.hpSprite.visible = !unit.dead && (unit.hudTimer > 0 || activeCombat);
  }
}

function updateBuildingHud(building){
  const safeMax=Math.max(1,building.maxHp||1), p=THREE.MathUtils.clamp(building.hp/safeMax,0,1);
  if(building.hpSprite){
    updateUnitHpSprite(building.hpSprite,building.hp,safeMax,building.dead);
    building.hpSprite.visible=!building.dead && building.hp < building.maxHp;
  }
}


class CombatEntity {
  constructor(team, hp) {
    this.team=team; this.maxHp=hp; this.hp=hp; this.dead=false; this.cooldown=0; this.target=null; this.bar=null; this.group=new THREE.Group(); this.radius=.55;
    this._replayId=`entity-${this.group.id}`;
  }
  takeDamage(amount, attacker=null) {
    if(this.dead||!game.running||game.tiebreaker) return;
    const previousHp=this.hp;
    this.hp=Math.max(0,this.hp-amount); this.hitAnim=.16; if(this instanceof Unit) this.hudTimer = 1.6;
    game.recorder?.event(game.elapsed,'damage',{
      sourceTeam:attacker?.team,sourceCardId:attacker?.card?.id,sourceId:attacker?._replayId,playId:attacker?._playId,
      targetId:this._replayId,targetTeam:this.team,targetKind:this instanceof Tower?'tower':this instanceof DefensiveBuilding?'building':'troop',
      targetCardId:this.card?.id,targetMaxHp:this.maxHp,targetCount:this.card?.count||1,targetCost:this.card?.cost||0,
      amount:Math.max(0,previousHp-this.hp),overkill:Math.max(0,amount-previousHp),damageKind:attacker?.damageKind||'attack',hp:this.hp,
      x:this.group.position.x,z:this.group.position.z,
    });
    if(this instanceof Tower) updateTowerHud(this);
    if(this instanceof Unit) updateUnitHud(this);
    this.onHpChanged?.();
    if(this instanceof Tower)createDamageNumber?.(this.group.position.clone().setY(3.65),amount,this.team);
    if(this instanceof Tower) cameraShake=Math.min(.34,cameraShake+.075); else if(this instanceof DefensiveBuilding) cameraShake=Math.min(.24,cameraShake+.035);
    if(this.hp<=0) this.destroy(attacker);
  }
  destroy(attacker){
    this.dead=true; this.deathAt=game.elapsed;
    game.recorder?.event(game.elapsed,'death',{targetId:this._replayId,targetTeam:this.team,targetKind:this instanceof Tower?'tower':this instanceof DefensiveBuilding?'building':'troop',targetCardId:this.card?.id,targetPlayId:this._playId,sourceTeam:attacker?.team,sourceCardId:attacker?.card?.id,playId:attacker?._playId,targetCost:this.card?.cost||0,targetCount:this.card?.count||1,x:this.group.position.x,z:this.group.position.z});
  }
}

function coreShouldBeActive(team){
  return towers.some(t=>t.team===team&&t.kind==='guard'&&t.dead);
}
function activateCoreTower(team){
  const core=towers.find(t=>t.team===team&&t.kind==='core'&&!t.dead);
  if(core) core.setCoreActive(true,true);
}

class Tower extends CombatEntity {
  constructor(team, kind, x,z,lane=0) {
    const hp=kind==='core'?3600:2250;
    super(team,hp); this.kind=kind; this.lane=lane; this.rangeTiles=kind==='guard'?10:8.9; this.range=this.rangeTiles*GRID.tile; this.damage=kind==='core'?112:86; this.attackSpeed=kind==='core'?.92:1.02; this.radius=kind==='core'?1.35:1.15; this.position=new THREE.Vector3(x,0,z); this.group.position.copy(this.position); this.recoil=0; this.active=kind!=='core'; this.activationPulse=0;
    this.buildVisual(); scene.add(this.group); towers.push(this); updateTowerHud(this);
  }
  buildVisual(){
    const color=this.team===TEAM.PLAYER?COLORS.player:COLORS.enemy;
    const dark=this.team===TEAM.PLAYER?0x176a94:0x8f263e;
    this.visualRoot=new THREE.Group(); this.group.add(this.visualRoot);
    // multi-tier stone base
    addCylinder(this.visualRoot,1.42,1.63,.48,0x59686f,0,.24,0,0,0,0,12);
    addCylinder(this.visualRoot,1.18,1.36,.22,0x87949a,0,.56,0,0,0,0,12);
    const bodyH=this.kind==='core'?2.38:1.96;
    addBox(this.visualRoot,this.kind==='core'?2.05:1.73,bodyH,this.kind==='core'?2.05:1.73,0xb7c2c4,0,this.kind==='core'?1.74:1.52,0);
    // V4 masonry courses, recessed slits and buttress caps add scale without cluttering the silhouette.
    const bw=this.kind==='core'?2.09:1.77;
    for(const yy of (this.kind==='core'?[1.05,1.65,2.25]:[.96,1.46,1.96]))addBox(this.visualRoot,bw,.055,bw,0x8c999c,0,yy,0);
    const frontZ=-(this.kind==='core'?1.035:.875);
    for(const yy of (this.kind==='core'?[1.34,2.02,2.60]:[1.22,1.78]))addBox(this.visualRoot,.20,.32,.045,0x28343a,0,yy,frontZ-.03);
    for(const sx of [-1,1])addBox(this.visualRoot,.24,.50,.44,0x909da0,sx*(this.kind==='core'?1.03:.86),.86,.15,0,0,sx*.05);
    // stone corner pillars
    const corner=this.kind==='core'?.92:.77;
    for(const x of [-corner,corner])for(const z of [-corner,corner]){
      addCylinder(this.visualRoot,.19,.22,bodyH+.2,0x79878e,x,this.kind==='core'?1.73:1.50,z,0,0,0,8);
      addCone(this.visualRoot,.27,.19,.26,0x606e76,x,this.kind==='core'?3.06:2.61,z,0,0,0,8);
    }
    // team crest / glowing crystal front
    addBox(this.visualRoot,.64,.72,.10,dark,0,this.kind==='core'?1.78:1.55,-(this.kind==='core'?1.07:.91));
    const crest=addSphere(this.visualRoot,.22,color,0,this.kind==='core'?1.79:1.56,-(this.kind==='core'?1.15:.99),1,.95,.35); crest.material.emissive=new THREE.Color(color);crest.material.emissiveIntensity=.55;
    // battlement ring and merlons
    const topY=this.kind==='core'?3.02:2.59;
    addBox(this.visualRoot,this.kind==='core'?2.36:2.02,.24,this.kind==='core'?2.36:2.02,color,0,topY,0);
    addBox(this.visualRoot,this.kind==='core'?2.46:2.12,.09,this.kind==='core'?2.46:2.12,0xe0c26a,0,topY+.17,0);
    const m=this.kind==='core'?1.03:.86;
    for(const x of [-m,0,m])for(const z of [-m,m])addBox(this.visualRoot,.28,.43,.30,0x64737b,x,topY+.38,z);
    for(const x of [-m,m])addBox(this.visualRoot,.30,.43,.28,0x64737b,x,topY+.38,0);
    if(this.kind==='core'){
      addCylinder(this.visualRoot,.13,.16,1.10,0x3e4a52,0,3.74,0,0,0,0,10);
      addTorus(this.visualRoot,.42,.045,0xdcb852,0,4.10,0,Math.PI/2);
      const gem=addSphere(this.visualRoot,.30,color,0,4.30,0); gem.material.emissive=new THREE.Color(color);gem.material.emissiveIntensity=.12; this.coreGem=gem;
      this.coreHalo=addTorus(this.visualRoot,.54,.025,color,0,4.30,0,Math.PI/2); this.coreHalo.material.transparent=true; this.coreHalo.material.opacity=.16;
      addCone(this.visualRoot,.32,.03,.48,0xe0c26a,0,4.71,0);
    } else {
      addCylinder(this.visualRoot,.20,.25,.78,0x36434d,0,2.96,-.30,Math.PI/2,0,0,12);
      addCylinder(this.visualRoot,.11,.14,.52,0x252e35,0,2.96,-.90,Math.PI/2,0,0,12);
      addBox(this.visualRoot,.86,.18,.76,0x4c5a63,0,2.79,.08);
    }
    const ring=makeTeamRing(this.team,this.radius*1.2); this.group.add(ring);
    // Towers use one compact numeric in-world HP plate; the duplicate generic bar was removed in V0.4.
    this.bar=null;
    this.hpSprite=makeTowerHpSprite(this.team,this.kind,this.hp,this.maxHp); this.hpSprite.position.y=this.kind==='core'?5.35:3.78; this.group.add(this.hpSprite);
    if(this.kind==='core'){
      this.coreAura=mesh(new THREE.RingGeometry(1.48,1.78,40),new THREE.MeshBasicMaterial({color,transparent:true,opacity:.08,side:THREE.DoubleSide,depthWrite:false,blending:THREE.AdditiveBlending}),false,false);
      this.coreAura.rotation.x=-Math.PI/2;this.coreAura.position.y=.13;this.group.add(this.coreAura);
    }
    this.group.userData.entity=this;
    this.tryBlenderVisual();
  }
  async tryBlenderVisual(){
    const loaded=await loadModel(this.kind==='core'?'tower_core':'tower_guard');
    if(!loaded||this.dead)return;
    disposeObject(this.visualRoot);this.visualRoot.clear(); loaded.scale.setScalar(1); this.visualRoot.add(loaded);
  }
  setCoreActive(active,burst=false){
    if(this.kind!=='core')return;
    const changed=this.active!==active; this.active=active;
    if(this.coreGem?.material){this.coreGem.material.emissiveIntensity=active?1.45:.12;this.coreGem.material.opacity=active?1:.72;}
    if(this.coreHalo?.material){this.coreHalo.material.opacity=active?.72:.16;}
    if(this.coreAura?.material){this.coreAura.material.opacity=active?.58:.08;this.coreAura.material.color.setHex(this.team===TEAM.PLAYER?(active?0x6ae8ff:0x5e7480):(active?0xff6d81:0x765c62));}
    if(active&&changed&&burst){
      createBurst(this.group.position.clone().setY(.18),this.team===TEAM.PLAYER?0x62ddff:0xff627a,1.15);
      createImpactFlash(this.group.position.clone().setY(3.2),this.team===TEAM.PLAYER?0x8bebff:0xff97a6,1.2);
      toast(`${this.team===TEAM.PLAYER?'Your':'Enemy'} Core Tower activated!`);
    }
  }
  update(dt){
    if(this.dead) return;
    if(this._displayHp!==Math.ceil(this.hp)||this._displayDead!==this.dead)updateTowerHud(this);
    if(this.kind==='core'&&!this.active&&coreShouldBeActive(this.team))this.setCoreActive(true,true);
    this.cooldown-=dt; this.recoil=Math.max(0,this.recoil-dt*4.8); this.activationPulse+=dt;
    if(this.visualRoot){ const kick=Math.sin(this.recoil*Math.PI)*.08; this.visualRoot.position.z=(this.team===TEAM.PLAYER?1:-1)*kick; }
    if(this.kind==='core'&&!this.active){
      this.target=null;
      const pulse=1+Math.sin(this.activationPulse*1.8)*.04;if(this.coreHalo)this.coreHalo.scale.setScalar(pulse);if(this.coreAura)this.coreAura.scale.setScalar(pulse);
      return;
    }
    if(this.coreAura&&this.kind==='core'){const pulse=1+Math.sin(this.activationPulse*3.2)*.08;this.coreAura.scale.setScalar(pulse);}
    if(!this.target || this.target.dead || horizontalDistance(this,this.target)>this.range+.7) {
      this.target=findNearestUnit(this.team,this.group.position,this.range);
    }
    if(this.target && this.cooldown<=0){
      this.cooldown=this.attackSpeed; this.recoil=1;
      fireProjectile(this,this.target,this.damage,17,0,this.kind==='core'?0xffe38b:0xffffff);
      createMuzzleFlash(this.group.position.clone().add(new THREE.Vector3(0,this.kind==='core'?3.1:2.8,this.team===TEAM.PLAYER?-1:1)),this.team);
    }
  }
  onHpChanged(){ updateTowerHud(this); }
  destroy(attacker){
    if(this.dead) return; super.destroy(attacker);
    createTowerCollapse(this.group.position.clone(),this.team,this.kind==='core');
    this.group.visible=false;updateTowerHud(this);
    const winnerTeam=this.team===TEAM.PLAYER?TEAM.ENEMY:TEAM.PLAYER;
    game.recorder?.event(game.elapsed,'tower_destroy',{targetId:this._replayId,targetTeam:this.team,sourceCardId:attacker?.card?.id,playId:attacker?._playId,crownsAwarded:this.kind==='core'?3-game.crowns[winnerTeam]:1});
    if(this.kind==='core') {
      game.crowns[winnerTeam]=3; updateScore(); finishMatch(winnerTeam, true);
    } else {
      game.crowns[winnerTeam]++; updateScore(); refreshPocketOverlays(); activateCoreTower(this.team);
      const laneName=this.lane<0?'left':'right';
      toast(`${winnerTeam===TEAM.PLAYER?'You':'Enemy'} destroyed a Guard Tower — ${laneName} pocket unlocked!`);
      if(game.overtime) finishMatch(winnerTeam, false);
    }
  }
}

function entityWorldPos(entity){ return entity?.group?.position || entity?.position || new THREE.Vector3(); }
function sightRangeFor(source,target,extraTiles=0){
  const a=entityWorldPos(source), b=entityWorldPos(target);
  const rot=source?.group?.rotation?.y ?? (source?.team===TEAM.ENEMY?Math.PI:0);
  // Models are normalized to look down local -Z. Convert that into a world-space facing vector.
  const fx=-Math.sin(rot), fz=-Math.cos(rot);
  const tiles=directionalSight({x:a.x,z:a.z,facingX:fx,facingZ:fz},{x:b.x,z:b.z},SIGHT);
  return (tiles+extraTiles)*GRID.tile;
}
function isWithinSight(source,target,extraTiles=0){
  if(!source||!target)return false;
  const distance=horizontalDistance(source,target);
  const padding=(source.radius||0)*.20+(target.radius||0);
  return distance<=sightRangeFor(source,target,extraTiles)+padding;
}
function findNearestVisibleTargetableUnit(source,canHitAir=true){
  return units.filter(u=>!u.dead&&u.team!==source.team&&(canHitAir||!u.flying)&&isWithinSight(source,u))
    .sort((a,b)=>horizontalDistance(source,a)-horizontalDistance(source,b))[0]||null;
}

class DefensiveBuilding extends CombatEntity {
  constructor(team, card, x, z){
    super(team, card.hp);
    this.card=card; this.damage=card.damage; this.attackSpeed=card.attackSpeed; this.range=card.range*GRID.tile; this.projectileSpeed=card.projectileSpeed||18; this.canHitAir=!!card.canHitAir;
    this.radius=(card.footprint||1.6)*.52; this.lifetime=card.lifetime||30; this.age=0; this.cooldown=.35; this.target=null; this.targetScan=0; this.recoil=0;
    this.group.position.set(x,0,z); this.group.rotation.y=team===TEAM.PLAYER?0:Math.PI; this.group.userData.entity=this; scene.add(this.group); buildings.push(this); this.buildVisual();
  }
  async buildVisual(){
    this.visualRoot=new THREE.Group(); this.group.add(this.visualRoot);
    const loaded=await loadModel(this.card.model);
    if(this.dead)return;
    const visual=loaded||fallbackModel(this.card.model);
    visual.scale.setScalar(this.card.scale||1); this.visualRoot.add(visual); this.visual=visual;
    const ring=makeTeamRing(this.team,this.radius*1.15); ring.material.opacity=.62; this.group.add(ring);
    this.hpSprite=makeUnitHpSprite(this.team,this.hp,this.maxHp); this.hpSprite.position.y=3.36; this.hpSprite.scale.set(1.55,.21,1); this.group.add(this.hpSprite); updateBuildingHud(this);
  }
  update(dt){
    if(this.dead)return;
    this.age+=dt; this.cooldown-=dt; this.targetScan-=dt; this.recoil=Math.max(0,this.recoil-dt*5.5);
    // Clash-like building decay: lifetime expiration steadily consumes HP instead of disappearing abruptly.
    const decay=(this.maxHp/this.lifetime)*dt; this.hp=Math.max(0,this.hp-decay); updateBuildingHud(this);
    if(this.hp<=0){this.destroy();return;}
    if(this.visualRoot) this.visualRoot.position.z=(this.team===TEAM.PLAYER?1:-1)*Math.sin(this.recoil*Math.PI)*.055;
    if(this.targetScan<=0 || !this.target || this.target.dead || horizontalDistance(this,this.target)>this.range+this.target.radius+.35 || !isWithinSight(this,this.target)){
      this.targetScan=.12;
      const visible=findNearestVisibleTargetableUnit(this,this.canHitAir);
      this.target=visible&&horizontalDistance(this,visible)<=this.range+visible.radius+.35?visible:null;
    }
    if(this.target&&this.cooldown<=0){
      this.cooldown=this.attackSpeed; this.recoil=1;
      fireProjectile(this,this.target,this.damage,this.projectileSpeed,0,0xffd985);
      const muzzle=this.group.position.clone().add(new THREE.Vector3(0,2.65,this.team===TEAM.PLAYER?-0.45:.45));
      createMuzzleFlash(muzzle,this.team); createBowSnapFx(muzzle,this.team);
    }
  }
  takeDamage(amount,attacker=null){ super.takeDamage(amount,attacker); updateBuildingHud(this); }
  destroy(attacker){
    if(this.dead)return; super.destroy(attacker); updateBuildingHud(this);
    const p=this.group.position.clone(); createTowerCollapse(p,this.team,false); createDebrisBurst(p.clone().setY(1.3),0x9a7953,18,1.15); this.group.visible=false;
  }
}

function laneBridgeX(lane){ return lane * ARENA.bridgeX; }

function navCellFromWorld(pos){ return {x:Math.round(pos.x/NAV.cell),z:Math.round(pos.z/NAV.cell)}; }
function navWorldFromCell(cell){ return new THREE.Vector3(cell.x*NAV.cell,0,cell.z*NAV.cell); }
function navKey(x,z){ return `${x},${z}`; }
function navHeuristic(a,b){ const dx=Math.abs(a.x-b.x),dz=Math.abs(a.z-b.z); const mn=Math.min(dx,dz),mx=Math.max(dx,dz); return (mx-mn)+mn*1.41421356; }
function navInsideArenaCell(x,z,unit=null){ const wx=x*NAV.cell,wz=z*NAV.cell,margin=.40+(unit?.radius||0)*.72; return Math.abs(wx)<=ARENA.width/2-margin&&Math.abs(wz)<=ARENA.length/2-margin; }
function navOnBridge(wx,wz,unit=null,forcedBridgeX=null){
  if(Math.abs(wz)>ARENA.riverHalf+.38)return false;
  const radius=unit?.radius||0;
  const half=Math.max(.34,ARENA.bridgeWidth*.5-NAV.bridgeMargin-radius*.92);
  const bridges=forcedBridgeX===null||forcedBridgeX===undefined?[-ARENA.bridgeX,ARENA.bridgeX]:[forcedBridgeX];
  return bridges.some(bx=>Math.abs(wx-bx)<=half);
}
function navCellBlockedByStructure(unit,wx,wz,target){
  for(const structure of [...towers,...buildings]){
    if(structure.dead||structure===target)continue;
    const clearance=structure.radius+unit.radius+NAV.structurePadding;
    if(Math.hypot(wx-structure.group.position.x,wz-structure.group.position.z)<clearance)return true;
  }
  return false;
}
function navWalkable(unit,x,z,target,startCell=null,goalCell=null){
  if(!navInsideArenaCell(x,z,unit))return false;
  if(startCell&&x===startCell.x&&z===startCell.z)return true;
  const wx=x*NAV.cell,wz=z*NAV.cell;
  // Radius-aware bridge legality: large troops such as Boulderback must fit fully on the deck.
  if(Math.abs(wz)<ARENA.riverHalf+.28&&!navOnBridge(wx,wz,unit,unit?.navForcedBridgeX??null))return false;
  const isGoal=goalCell&&x===goalCell.x&&z===goalCell.z;
  if(isGoal&&((target instanceof Tower)||(target instanceof DefensiveBuilding)))return true;
  return !navCellBlockedByStructure(unit,wx,wz,target);
}
function navGoalCellFor(unit,target,start){
  const raw=navCellFromWorld(target.group.position);
  if(navWalkable(unit,raw.x,raw.z,target,start,raw))return raw;
  let best=null,bestD=Infinity;
  for(let r=1;r<=5;r++){
    for(let dx=-r;dx<=r;dx++)for(let dz=-r;dz<=r;dz++){
      if(Math.max(Math.abs(dx),Math.abs(dz))!==r)continue;
      const c={x:raw.x+dx,z:raw.z+dz};
      if(!navWalkable(unit,c.x,c.z,target,start,null))continue;
      const d=dx*dx+dz*dz;
      if(d<bestD){bestD=d;best=c;}
    }
    if(best)return best;
  }
  return raw;
}
function navBridgeCongestionSnapshot(unit){
  const snap={left:0,right:0};
  for(const other of units){
    if(other===unit||other.dead||other.flying||other.team!==unit.team)continue;
    if(Math.abs(other.group.position.z)>ARENA.riverHalf+1.55)continue;
    const key=Math.abs(other.group.position.x+ARENA.bridgeX)<Math.abs(other.group.position.x-ARENA.bridgeX)?'left':'right';
    snap[key]+=Math.min(.34,.10+Math.max(.7,other.radius)*.07);
  }
  snap.left=Math.min(1.35,snap.left);snap.right=Math.min(1.35,snap.right);return snap;
}
function navBridgeCongestionAt(snapshot,wx,wz){
  if(Math.abs(wz)>ARENA.riverHalf+1.35)return 0;
  return Math.abs(wx+ARENA.bridgeX)<Math.abs(wx-ARENA.bridgeX)?snapshot.left:snapshot.right;
}
function reconstructNavPath(came,current){
  const out=[current];
  let key=navKey(current.x,current.z);
  while(came.has(key)){ current=came.get(key); out.push(current); key=navKey(current.x,current.z); }
  out.reverse();
  if(out.length<=2)return out;
  const simple=[out[0]];
  let lastDx=null,lastDz=null;
  for(let i=1;i<out.length;i++){
    const prev=out[i-1],cur=out[i],dx=Math.sign(cur.x-prev.x),dz=Math.sign(cur.z-prev.z);
    if(lastDx!==null&&(dx!==lastDx||dz!==lastDz))simple.push(prev);
    lastDx=dx;lastDz=dz;
  }
  simple.push(out[out.length-1]);
  return simple;
}
function buildGroundPath(unit,target){
  if(!unit||!target)return [];
  const start=navCellFromWorld(unit.group.position),targetSide=sideOfRiver(target.group.position.z),startSide=sideOfRiver(unit.group.position.z);
  const crossing=startSide!==0&&targetSide!==0&&startSide!==targetSide;
  let forcedBridge=null;
  if(crossing){
    forcedBridge=unit.bridgeCommitX??bestBridgeFor(unit,target);
    unit.bridgeCommitX=forcedBridge;unit.bridgeCommitActive=true;unit.bridgeCommitSourceSide=startSide;unit.bridgeCommitTarget=target;
  }else if(unit.bridgeCommitActive){
    forcedBridge=unit.bridgeCommitX;
  }
  unit.navForcedBridgeX=forcedBridge;
  const goal=navGoalCellFor(unit,target,start);
  const congestionSnapshot=navBridgeCongestionSnapshot(unit);
  if(start.x===goal.x&&start.z===goal.z){unit.navForcedBridgeX=null;return [navWorldFromCell(goal)];}
  const open=[{x:start.x,z:start.z,f:navHeuristic(start,goal)}];
  const openKeys=new Set([navKey(start.x,start.z)]),came=new Map(),g=new Map([[navKey(start.x,start.z),0]]);
  const dirs=[[1,0,1],[-1,0,1],[0,1,1],[0,-1,1],[1,1,1.41421356],[1,-1,1.41421356],[-1,1,1.41421356],[-1,-1,1.41421356]];
  let expanded=0;
  while(open.length&&expanded++<NAV.maxNodes){
    let bestI=0;for(let i=1;i<open.length;i++)if(open[i].f<open[bestI].f)bestI=i;
    const cur=open.splice(bestI,1)[0],curKey=navKey(cur.x,cur.z);openKeys.delete(curKey);
    if(cur.x===goal.x&&cur.z===goal.z){
      const cells=reconstructNavPath(came,{x:cur.x,z:cur.z});
      let points=cells.map(navWorldFromCell);
      if(crossing&&forcedBridge!==null){
        const safeHalf=Math.max(.28,ARENA.bridgeWidth*.5-unit.radius-.30);
        const travelX=forcedBridge+THREE.MathUtils.clamp(unit.navSlot,-safeHalf*.58,safeHalf*.58);
        const src=startSide;
        const fixed=[
          new THREE.Vector3(travelX,0,src*(ARENA.riverHalf+NAV.bridgeEntryPad)),
          new THREE.Vector3(travelX,0,src*(ARENA.riverHalf+.18)),
          new THREE.Vector3(travelX,0,0),
          new THREE.Vector3(travelX,0,-src*(ARENA.riverHalf+.18)),
          new THREE.Vector3(travelX,0,-src*(ARENA.riverHalf+NAV.bridgeReleasePad)),
        ];
        let first=points.findIndex(pt=>src*pt.z<=ARENA.riverHalf+NAV.bridgeEntryPad+.15);
        if(first<0)first=Math.max(0,points.length-1);
        let last=first;for(let i=first;i<points.length;i++){if(src*points[i].z>=-(ARENA.riverHalf+NAV.bridgeReleasePad+.15))last=i;else break;}
        const head=points.slice(0,first),tail=points.slice(Math.min(points.length,last+1));
        points=[...head,...fixed,...tail];
      }else{
        for(const point of points){
          if(Math.abs(point.z)<ARENA.riverHalf+.55){
            const bx=Math.abs(point.x-ARENA.bridgeX)<Math.abs(point.x+ARENA.bridgeX)?ARENA.bridgeX:-ARENA.bridgeX;
            const safeHalf=Math.max(.28,ARENA.bridgeWidth*.5-unit.radius-.30);
            point.x=THREE.MathUtils.clamp(point.x+unit.navSlot,bx-safeHalf,bx+safeHalf);
          }
        }
      }
      unit.navForcedBridgeX=null;
      return points;
    }
    const curG=g.get(curKey)??Infinity;
    for(const [dx,dz,step] of dirs){
      const nx=cur.x+dx,nz=cur.z+dz;
      if(!navWalkable(unit,nx,nz,target,start,goal))continue;
      if(dx&&dz){
        if(!navWalkable(unit,cur.x+dx,cur.z,target,start,goal)||!navWalkable(unit,cur.x,cur.z+dz,target,start,goal))continue;
      }
      const wx=nx*NAV.cell,wz=nz*NAV.cell;
      const ng=curG+step+navBridgeCongestionAt(congestionSnapshot,wx,wz);
      const nk=navKey(nx,nz);
      if(ng+1e-6<(g.get(nk)??Infinity)){
        g.set(nk,ng);came.set(nk,{x:cur.x,z:cur.z});
        const f=ng+navHeuristic({x:nx,z:nz},goal);
        if(!openKeys.has(nk)){open.push({x:nx,z:nz,f});openKeys.add(nk);}else{
          const existing=open.find(n=>n.x===nx&&n.z===nz);if(existing)existing.f=f;
        }
      }
    }
  }
  unit.navForcedBridgeX=null;
  return [];
}
function pathDistanceFrom(unit,path,index,target){
  if(!path?.length)return navigableDistance(unit,target);
  let dist=0,prev=unit.group.position;
  for(let i=Math.min(index,path.length-1);i<path.length;i++){dist+=Math.hypot(path[i].x-prev.x,path[i].z-prev.z);prev=path[i];}
  dist+=Math.hypot(target.group.position.x-prev.x,target.group.position.z-prev.z);
  return dist;
}

function navPathStillValid(unit,target,path,index){
  if(!path?.length)return false;
  const start=navCellFromWorld(unit.group.position),goal=navCellFromWorld(target.group.position);
  for(let i=index;i<Math.min(path.length,index+4);i++){
    const c=navCellFromWorld(path[i]);
    if(!navWalkable(unit,c.x,c.z,target,start,goal))return false;
  }
  return true;
}
function clearBridgeCommit(unit){
  unit.bridgeCommitActive=false;unit.bridgeCommitX=null;unit.bridgeCommitSourceSide=0;unit.bridgeCommitTarget=null;unit.navForcedBridgeX=null;
}
function updateBridgeCommit(unit){
  if(!unit.bridgeCommitActive)return;
  if(unit.bridgeCommitTarget?.dead){clearBridgeCommit(unit);return;}
  const side=sideOfRiver(unit.group.position.z),targetSide=sideOfRiver(unit.target?.group?.position?.z??0);
  if(side!==0&&targetSide!==0&&side===targetSide&&Math.abs(unit.group.position.z)>ARENA.riverHalf+NAV.bridgeReleasePad+.12)clearBridgeCommit(unit);
}


class Unit extends CombatEntity {
  constructor(team, card, x,z, offset=0){
    super(team,card.hp); this.card=card; this.damage=card.damage; this.attackSpeed=card.attackSpeed; this.moveSpeedTiles=card.moveSpeed; this.moveSpeed=this.moveSpeedTiles*GRID.tile; this.rangeTiles=card.range; this.range=this.rangeTiles*GRID.tile; this.frontSightTiles=SIGHT.frontTiles; this.rearSightTiles=SIGHT.rearTiles; this.aggroTiles=this.frontSightTiles; this.aggro=this.frontSightTiles*GRID.tile; this.projectile=card.projectile; this.canHitAir=!!card.canHitAir; this.projectileSpeed=card.projectileSpeed||14; this.splashTiles=card.splash||0; this.splash=this.splashTiles*GRID.tile; this.buildingsOnly=!!card.buildingsOnly; this.flying=!!card.flying; this.charger=!!card.charger; this.chargeTime=0; this.charged=false; this.animTime=Math.random()*6; this.attackAnim=0; this.hitAnim=0; this.wasMoving=false; this.baseScale=card.scale||1; this.radius=.44*(card.scale||1); this.lane=laneForX(x); this.targetScan=0; this.bridgeCommitX=null; this.bridgeCommitActive=false; this.bridgeCommitSourceSide=0; this.bridgeCommitTarget=null; this.stuckTimer=0; this.progressClock=0; this.lastProgressDistance=Infinity; this.lastMoveSample=new THREE.Vector3(x,0,z); this.stepFxTimer=Math.random()*.15; this.forcedTarget=null; this.forcedTargetTimer=0; this.structureLockTarget=null; this.hudTimer = 1.1; this.slowPct=0; this.slowUntil=0; this.stunUntil=0; this.auraTimer=card.auraInterval||0; this.navPath=[]; this.navIndex=0; this.navRepath=0; this.navTarget=null; this.navTargetPos=new THREE.Vector3(999,0,999); this.navSlot=((this.group.id%5)-2)*.22; this.group.position.set(x, this.flying?.95:0, z); this.group.rotation.y=team===TEAM.PLAYER?0:Math.PI; this.group.userData.entity=this; scene.add(this.group); units.push(this); this.buildVisual(offset);
  }
  async buildVisual(offset){
    const ring=makeTeamRing(this.team,.68*(this.card.scale||1)); this.group.add(ring);
    const loaded=await loadModel(this.card.model);
    if(this.dead)return;
    const visual=loaded || fallbackModel(this.card.model);
    // Blender exports use the opposite forward axis from the procedural fallback models.
    // A dedicated pivot normalizes every model so PLAYER units face away from the camera
    // (their backs visible) and ENEMY units face toward the camera.
    const facingPivot=new THREE.Group();
    facingPivot.rotation.y=loaded?Math.PI:0;
    visual.scale.setScalar(this.baseScale);
    facingPivot.add(visual); this.group.add(facingPivot);
    this.visualPivot=facingPivot; this.visual=visual;
    visual.userData.restY=0;
    this.bar=null;
    this.hpSprite = makeUnitHpSprite(this.team,this.hp,this.maxHp); this.hpSprite.position.y = this.flying?2.82:2.42*(this.card.scale||1); this.group.add(this.hpSprite);
    updateUnitHud(this);
  }
  update(dt){
    if(this.dead) return;
    this.animTime+=dt; this.attackAnim=Math.max(0,this.attackAnim-dt); this.hitAnim=Math.max(0,(this.hitAnim||0)-dt); this.hudTimer=Math.max(0,this.hudTimer-dt);
    this.cooldown-=dt; this.targetScan-=dt; this.stepFxTimer-=dt; this.forcedTargetTimer=Math.max(0,this.forcedTargetTimer-dt); this.navRepath=Math.max(0,this.navRepath-dt);
    this.updateStormAura(dt);
    const nowSeconds=game.elapsed;
    const stunned=nowSeconds < (this.stunUntil||0);
    if(nowSeconds >= (this.slowUntil||0)){this.slowPct=0;this.slowUntil=0;}
    else if((this.slowPct||0)>0&&Math.random()<dt*3.2){createFrostFootstep(this.group.position.clone(),this.team);}
    if(stunned){
      if(this.visual){
        const pulse=.92+Math.sin(this.animTime*28)*.035;this.visual.scale.setScalar(this.baseScale*pulse);
        this.visual.position.y+=(this.flying?.025:.012)*Math.sin(this.animTime*23);
      }
      if(Math.random()<dt*9)createElectricSpark(this.group.position.clone().setY(this.flying?1.15:.85),0x91eaff,.22);
      updateUnitHud(this);return;
    }
    if(this.structureLockTarget && this.structureLockTarget.dead) this.structureLockTarget=null;
    if(this.bridgeCommitActive&&this.bridgeCommitTarget?.dead)clearBridgeCommit(this);
    if(this.forcedTarget && (this.forcedTarget.dead || !this.canTargetUnit(this.forcedTarget) || horizontalDistance(this,this.forcedTarget) > this.sightRangeFor(this.forcedTarget,RETARGET.troopLeashBonusTiles) + this.forcedTarget.radius)){
      this.forcedTarget=null; this.forcedTargetTimer=0;
    } else if(this.forcedTarget && this.forcedTargetTimer<=0){
      // Forced pull is temporary. After the lock expires the troop remains a normal candidate,
      // but the unit is free to reassess if a better reachable target appears.
      this.forcedTarget=null; this.targetScan=0;
    }
    if(this.structureLockTarget && !this.structureLockTarget.dead){
      // V12.1: a Crown Tower is a true hard-lock only AFTER the unit reaches attack range.
      // Until then, normal troops must continue noticing valid enemy troops around them.
      this.target=this.structureLockTarget;
      this.forcedTarget=null;this.forcedTargetTimer=0;
    } else if(this.forcedTarget && !this.forcedTarget.dead){
      this.target=this.forcedTarget;
    } else {
      if(this.targetScan<=0){ this.targetScan=.10+Math.random()*.06; this.acquireTarget(); }
      if(!this.target || this.target.dead){ this.acquireTarget(); }
      // Do not hard-lock merely because a tower was selected as the travel target.
      // Normal troops may still switch to a valid enemy troop until they actually reach the tower and begin attacking it.
    }
    // V12.1 approach-priority guard: before a normal troop reaches structure attack range,
    // a valid nearby enemy troop always takes priority over a travel target structure.
    if(!this.buildingsOnly && !this.structureLockTarget && (this.target instanceof Tower || this.target instanceof DefensiveBuilding)){
      const nearbyTroop=this.findTroopTarget(0);
      if(nearbyTroop){
        this.target=nearbyTroop;
        this.targetScan=.10;
        this.navTarget=null;
        this.navRepath=0;
      }
    }
    let moving=false;
    if(this.target){
      // Ranged units may fire across the river; melee units must be physically reachable through a bridge.
      const dist=this.projectile?horizontalDistance(this,this.target):(this.navTarget===this.target&&this.navPath.length?pathDistanceFrom(this,this.navPath,this.navIndex,this.target):navigableDistance(this,this.target));
      if(dist<=this.range+this.target.radius){
        if(this.target instanceof Tower){
          // This is the only Crown Tower hard-lock point: the unit has physically reached attack range.
          this.structureLockTarget=this.target;this.forcedTarget=null;this.forcedTargetTimer=0;
        }else if(this.buildingsOnly && this.target instanceof DefensiveBuilding){
          this.structureLockTarget=this.target;
        }
        this.chargeTime=Math.max(0,this.chargeTime-dt*.7);
        if(this.cooldown<=0){ this.attack(); }
      } else {
        moving=true;
        const targetPos=this.getMoveTarget();
        const before=this.group.position.clone();
        moveTowards(this.group.position,targetPos,this.currentMoveSpeed()*dt);
        this.applySoftSeparation(dt);
        this.constrainGroundToBridges(before);
        if(!this.flying){
          updateBridgeCommit(this);
          this.progressClock+=dt;
          if(this.progressClock>=NAV.stuckSampleSeconds){
            const dNow=Math.hypot(targetPos.x-this.group.position.x,targetPos.z-this.group.position.z);
            if(dNow>this.lastProgressDistance-.055)this.stuckTimer+=this.progressClock;else this.stuckTimer=Math.max(0,this.stuckTimer-this.progressClock*1.6);
            this.lastProgressDistance=dNow;this.progressClock=0;
            if(this.stuckTimer>NAV.stuckThresholdSeconds){this.recoverFromStuck(targetPos);this.stuckTimer=0;this.lastProgressDistance=Infinity;}
          }
        }
        if(this.flying) this.group.position.y=.95 + Math.sin(game.elapsed*6 + this.group.id)*.08;
        const d=targetPos.clone().sub(this.group.position); if(d.lengthSq()>.001) this.group.rotation.y=Math.atan2(d.x,d.z)+Math.PI;
        if(this.charger){
          const moved=before.distanceTo(this.group.position);
          if(moved>.002){ this.chargeTime+=dt; if(this.chargeTime>1.65) this.charged=true; }
        }
      }
    }
    this.wasMoving=moving;
    if(moving&&!this.flying&&this.stepFxTimer<=0){this.stepFxTimer=this.charged?.08:.18;createMoveDust(this.group.position.clone(),this.team,this.charged?.75:.48);}
    if(moving&&this.charger&&this.charged&&Math.random()<dt*8)createTrailParticle(this.group.position.clone().setY(.45),0xffcf66,.11);
    if(this.visual){
      const walk=moving?Math.sin(this.animTime*(this.flying?6.4:10.8)):Math.sin(this.animTime*3.0)*.25;
      const bob=this.flying?.08:(moving?.036:.012);
      this.visual.position.y=(this.flying?0:.02)+walk*bob;
      this.visual.rotation.z=(moving&&!this.flying?Math.sin(this.animTime*7.2)*.03:(this.card.id==='sky_manta'?Math.sin(this.animTime*6.2)*.045:0));
      const attackP=this.attackAnim>0?Math.sin((this.attackAnim/.22)*Math.PI):0;
      const hitP=this.hitAnim>0?Math.sin((this.hitAnim/.16)*Math.PI):0;
      const s=this.baseScale*(1+attackP*.085-hitP*.04);
      this.visual.scale.set(s,s*(1-attackP*.04+hitP*.045),s);
      this.visual.position.x=hitP*Math.sin(this.group.id*2.13)*.055;
      if(this.card.id!=='ember_archer')this.visual.position.z=hitP*.035;
      if(this.card.id==='ironclad'){ this.visual.rotation.y = attackP * .18; }
      if(this.card.id==='ember_archer'){ this.visual.rotation.x = -attackP * .18; this.visual.position.z = -attackP * .08; }
      if(this.card.id==='arc_mage'){ this.visual.rotation.y = Math.sin(this.animTime*3.1)*.05 + attackP*.14; }
      if(this.card.id==='twin_blades'){ this.visual.rotation.y = Math.sin(this.animTime*8.8)*.06 + attackP*.12; }
      if(this.card.id==='boulderback'){ this.visual.position.y += Math.abs(Math.sin(this.animTime*4.4)) * .035; }
      if(this.card.id==='rambeast'){ this.visual.rotation.x = (this.charged ? -.14 : 0) - attackP*.08; }
      if(this.charger&&this.charged){ this.visual.rotation.x=-.10; } else if(!['ember_archer','rambeast'].includes(this.card.id)) this.visual.rotation.x=0;
      if(this.card.id==='sky_manta'){ this.visual.rotation.y = Math.sin(this.animTime*6.0)*.08; this.visual.scale.y = s*(1+Math.sin(this.animTime*11.0)*.05); }
      if(this.card.id==='vampire_bats'){
        const flap=Math.sin(this.animTime*15.5)*.52;
        const wl=this.visual.getObjectByName?.('batWingL')||this.visual.getObjectByName?.('WingL'),wr=this.visual.getObjectByName?.('batWingR')||this.visual.getObjectByName?.('WingR');
        if(wl)wl.rotation.z=.20+flap;if(wr)wr.rotation.z=-.20-flap;
        this.visual.position.y+=Math.sin(this.animTime*7.2+this.group.id)*.055;
      }
      if(this.card.id==='frost_fang'){
        const stride=moving?Math.sin(this.animTime*11.8):0;
        this.visual.rotation.x=-attackP*.14 + stride*.01;
        this.visual.rotation.y=attackP*.15;
        this.visual.rotation.z+=stride*.028;
        this.visual.position.y+=Math.abs(Math.sin(this.animTime*6.2))*.018;
        if(moving&&Math.random()<dt*5.8)createFrostFootstep(this.group.position.clone(),this.team);
        if((moving||this.target)&&Math.random()<dt*1.6){
          const frostDir=this.target?.group?.position
            ? this.target.group.position.clone().sub(this.group.position).setY(0)
            : new THREE.Vector3(0,0,this.team===TEAM.PLAYER?-1:1);
          createFrostBreath(this.group.position.clone(),this.team,frostDir);
        }
        if(attackP>.35&&Math.random()<dt*11)createFrostHit(this.group.position.clone().add(new THREE.Vector3(0,.90,this.team===TEAM.PLAYER?-0.58:.58)));
      }
      if(this.card.id==='storm_raven'){
        const flap=(moving?Math.sin(this.animTime*8.2):Math.sin(this.animTime*5.4))*.56;
        const wl=this.visual.getObjectByName?.('stormWingL')||this.visual.getObjectByName?.('WingL'),wr=this.visual.getObjectByName?.('stormWingR')||this.visual.getObjectByName?.('WingR');
        if(wl)wl.rotation.z=.14+flap;if(wr)wr.rotation.z=-.14-flap;
        this.visual.rotation.y=Math.sin(this.animTime*1.8)*.06+attackP*.12;
        this.visual.position.y+=Math.sin(this.animTime*4.8+this.group.id)*.08;
        if(Math.random()<dt*3.4){
          const wingSpark=this.group.position.clone().setY(1.15).add(new THREE.Vector3((Math.random()-.5)*1.1,Math.random()*.18,(Math.random()-.5)*.8));
          createElectricSpark(wingSpark,0x7ae9ff,.18+Math.random()*.08);
        }
      }
    }
    updateUnitHud(this);
  }
  applySlow(percent=.30,duration=2.0){
    if(this.dead)return;
    // Slow never stacks. A new hit can refresh duration or replace a weaker slow only.
    refreshSlow(this,percent,duration,game.elapsed);
    game.recorder?.event(game.elapsed,'status',{targetId:this._replayId,kind:'slow',until:this.slowUntil,percent:this.slowPct});
  }
  applyStun(duration=.4){
    if(this.dead)return;
    refreshStun(this,duration,game.elapsed);
    game.recorder?.event(game.elapsed,'status',{targetId:this._replayId,kind:'stun',until:this.stunUntil});
    this.hudTimer=Math.max(this.hudTimer,.55);
  }
  currentMoveSpeed(){
    const now=game.elapsed;
    if(now>=this.slowUntil){this.slowPct=0;this.slowUntil=0;}
    return this.moveSpeed*(1-(this.slowPct||0));
  }
  updateStormAura(dt){
    if(!this.card.auraDamage)return;
    this.auraTimer-=dt;
    if(this.auraTimer>0)return;
    this.auraTimer=this.card.auraInterval||3;
    const radius=(this.card.auraRadius||2)*GRID.tile;
    createStormPulse(this.group.position.clone(),radius,this.team);
    for(const u of units){
      if(u===this||u.dead||u.team===this.team)continue;
      if(horizontalDistance(this,u)<=radius+u.radius){u.takeDamage(this.card.auraDamage,battleDamageSource(this,'aura'));u.applyStun?.(this.card.stunDuration||.4);}
    }
  }
  canTargetUnit(unit){
    if(!unit || unit.dead || unit.team===this.team) return false;
    if(unit.flying && !this.canHitAir) return false;
    return true;
  }
  sightRangeFor(target,extraTiles=0){ return sightRangeFor(this,target,extraTiles); }
  canSeeTarget(target,extraTiles=0){ return isWithinSight(this,target,extraTiles); }
  forceRetargetTo(unit){
    if(this.structureLockTarget instanceof Tower && !this.structureLockTarget.dead)return false;
    if(this.buildingsOnly || !this.canTargetUnit(unit)) return false;
    this.forcedTarget=unit;
    this.forcedTargetTimer=RETARGET.forcedLockSeconds;
    this.target=unit;
    this.targetScan=.18;
    // If the attacker was mid tower swing, do not let that stale swing keep hitting the tower.
    // It must turn and complete its next attack against the defender instead.
    this.cooldown=Math.min(this.cooldown,Math.max(.08,this.attackSpeed*.32));
    this.chargeTime=0; this.charged=false;
    return true;
  }
  targetDistance(unit){
    if(this.projectile) return horizontalDistance(this,unit);
    return navigableDistance(this,unit);
  }
  findTroopTarget(extraRange=0){
    const extraTiles=extraRange/GRID.tile;
    const candidates=units.filter(u=>this.canTargetUnit(u) && this.canSeeTarget(u,extraTiles));
    if(!candidates.length) return null;
    // Rank by reachable combat distance, not raw straight-line distance through the river.
    const dir=this.team===TEAM.PLAYER?-1:1;
    candidates.sort((a,b)=>{
      const da=this.targetDistance(a), db=this.targetDistance(b);
      const forwardA=(a.group.position.z-this.group.position.z)*dir;
      const forwardB=(b.group.position.z-this.group.position.z)*dir;
      return (da-forwardA*.06)-(db-forwardB*.06);
    });
    return candidates[0];
  }
  acquireTarget(){
    if(this.structureLockTarget&&!this.structureLockTarget.dead){this.target=this.structureLockTarget;return;}
    if(this.buildingsOnly){
      // Win-condition troops can still be redirected by a closer defensive building while traveling.
      // Their hard lock begins only once they enter attack range and start attacking a structure.
      this.target=chooseStructureTarget(this);
      return;
    }
    if(this.forcedTarget && !this.forcedTarget.dead && this.canTargetUnit(this.forcedTarget)){
      this.target=this.forcedTarget; return;
    }
    if(this.target instanceof Unit && this.canTargetUnit(this.target) && this.canSeeTarget(this.target,RETARGET.troopLeashBonusTiles)){
      return;
    }
    const troop=this.findTroopTarget(0);
    if(troop){this.target=troop;return;}
    const structure=chooseStructureTarget(this);
    this.target=structure;
    // Traveling toward a tower is not a hard lock. Nearby valid enemy troops can still take priority.
  }
  getMoveTarget(){
    const target=this.target;
    if(!target)return this.group.position.clone();
    if(this.flying)return target.group.position.clone();
    const targetMoved=this.navTarget!==target||this.navTargetPos.distanceToSquared(target.group.position)>(NAV.targetMoveTiles*GRID.tile)**2;
    const routeDone=!this.navPath.length||this.navIndex>=this.navPath.length;
    const validationDue=this.navRepath<=0;
    const routeBlocked=validationDue&&!routeDone&&!navPathStillValid(this,target,this.navPath,this.navIndex);
    if(targetMoved||routeDone||routeBlocked){
      const path=buildGroundPath(this,target);
      if(path.length){
        this.navPath=path;this.navIndex=Math.min(1,path.length-1);this.navTarget=target;this.navTargetPos.copy(target.group.position);this.navRepath=NAV.repathSeconds;this.lastProgressDistance=Infinity;this.stuckTimer=0;this.progressClock=0;
        const bridgePoint=path.find(pt=>Math.abs(pt.z)<ARENA.riverHalf+.55);
        if(bridgePoint){this.bridgeCommitX=Math.abs(bridgePoint.x-ARENA.bridgeX)<Math.abs(bridgePoint.x+ARENA.bridgeX)?ARENA.bridgeX:-ARENA.bridgeX;this.lane=this.bridgeCommitX<0?-1:1;}
      }else{
        this.navPath=[];this.navIndex=0;this.navTarget=target;this.navTargetPos.copy(target.group.position);this.navRepath=.18;
      }
    }else if(validationDue){
      this.navRepath=NAV.repathSeconds;
    }
    if(this.navPath.length){
      while(this.navIndex<this.navPath.length-1&&Math.hypot(this.group.position.x-this.navPath[this.navIndex].x,this.group.position.z-this.navPath[this.navIndex].z)<.34){this.navIndex++;this.lastProgressDistance=Infinity;this.stuckTimer=0;}
      const wp=this.navPath[Math.min(this.navIndex,this.navPath.length-1)];
      if(wp)return wp.clone();
    }
    // Emergency fallback if no A* route could be produced in the frame budget.
    const pos=this.group.position,targetPos=target.group.position;
    const side=sideOfRiver(pos.z),targetSide=sideOfRiver(targetPos.z);
    if(side!==targetSide&&side!==0&&targetSide!==0){
      const bx=bestBridgeFor(this,target);this.bridgeCommitX=bx;this.lane=bx<0?-1:1;
      if(side>0)return new THREE.Vector3(bx,0,pos.z>ARENA.riverHalf+.15?ARENA.riverHalf+.1:-ARENA.riverHalf-.45);
      return new THREE.Vector3(bx,0,pos.z<-ARENA.riverHalf-.15?-ARENA.riverHalf-.1:ARENA.riverHalf+.45);
    }
    return targetPos.clone();
  }
  constrainGroundToBridges(previous){
    if(this.flying)return;
    const p=this.group.position;
    const inRiverBand=Math.abs(p.z)<ARENA.riverHalf+.38;
    if(!inRiverBand)return;
    const bx=this.bridgeCommitX ?? (Math.abs(p.x+ARENA.bridgeX)<Math.abs(p.x-ARENA.bridgeX)?-ARENA.bridgeX:ARENA.bridgeX);
    this.bridgeCommitX=bx;
    const half=Math.max(.72,ARENA.bridgeWidth*.36);
    // Clamp laterally to the bridge instead of resetting Z progress. Resetting Z was the main bridge deadlock source.
    p.x=THREE.MathUtils.clamp(p.x,bx-half,bx+half);
  }
  recoverFromStuck(targetPos){
    if(this.flying)return;
    // Invalidate the route first. The next frame gets a fresh path around whatever caused the block.
    this.navPath=[];this.navIndex=0;this.navRepath=0;
    if(Math.abs(this.group.position.z)>ARENA.riverHalf+NAV.bridgeReleasePad)clearBridgeCommit(this);
    const pos=this.group.position;
    const d=targetPos.clone().sub(pos);d.y=0;
    if(d.lengthSq()>.001){
      d.normalize();
      const candidates=[d.clone(),new THREE.Vector3(-d.z,0,d.x),new THREE.Vector3(d.z,0,-d.x)];
      for(const dir of candidates){
        const probe=pos.clone().addScaledVector(dir,.22),cell=navCellFromWorld(probe);
        if(navWalkable(this,cell.x,cell.z,this.target,navCellFromWorld(pos),this.target?navCellFromWorld(this.target.group.position):cell)){
          pos.addScaledVector(dir,.14);break;
        }
      }
    }
    createMoveDust(pos.clone(),this.team,.30);
  }

  applySoftSeparation(dt){
    if(this.flying) return;
    const p=this.group.position;
    const committedBridge=this.bridgeCommitX!==null && Math.abs(p.z)<ARENA.riverHalf+.85;
    let pushX=0,pushZ=0,count=0;
    for(const other of units){
      if(other===this||other.dead||other.team!==this.team||other.flying) continue;
      const dx=p.x-other.group.position.x, dz=p.z-other.group.position.z;
      const dist=Math.hypot(dx,dz), minDist=this.radius+other.radius+(committedBridge?.03:.12);
      if(dist>0.001 && dist<minDist){
        const force=(minDist-dist)/minDist;
        // On a bridge, never let crowd separation push units sideways off the route.
        if(!committedBridge) pushX+=(dx/dist)*force;
        pushZ+=(dz/dist)*force*(committedBridge?.42:1);
        count++;
      }
    }
    if(!committedBridge){
      for(const structure of [...towers,...buildings]){
        if(structure.dead||structure===this.target)continue;
        const dx=p.x-structure.group.position.x,dz=p.z-structure.group.position.z;
        const dist=Math.hypot(dx,dz),minDist=this.radius+structure.radius*.72;
        if(dist>.001&&dist<minDist){const force=(minDist-dist)/minDist;pushX+=(dx/dist)*force*1.3;pushZ+=(dz/dist)*force*1.3;count++;}
      }
    }
    if(count){ p.x+=pushX/count*dt*(committedBridge?.8:2.1); p.z+=pushZ/count*dt*(committedBridge?.8:2.1); }
  }
  attack(){
    if(!this.target||this.target.dead) return;
    if(this.buildingsOnly && this.structureLockTarget && !this.structureLockTarget.dead) this.target=this.structureLockTarget;
    this.cooldown=this.attackSpeed; this.attackAnim=.22; this.hudTimer = 1.2;
    let dmg=this.damage;
    if(this.charger && this.charged && (this.target instanceof Tower || this.target instanceof DefensiveBuilding)){ dmg=this.card.chargeDamage ?? (dmg*1.9); this.charged=false; this.chargeTime=0; createBurst(this.group.position.clone().setY(.5),0xffc35e,.8); }
    const projColor=this.card.id==='arc_mage'?0x6edcff:this.card.id==='storm_raven'?0x72dfff:this.card.id==='sky_manta'?0x86fff5:this.card.id==='ember_archer'?0xffb879:0xffffff;
    if(this.projectile){
      const muzzle=this.group.position.clone().setY(this.flying?1.15:1.25);createMuzzleFlash(muzzle,this.team);
      if(this.card.id==='ember_archer'){createBurst(muzzle.clone().setY(.18),0xff9d45,.22);createDebrisBurst(muzzle,0xffc069,3,.18);}
      if(this.card.id==='arc_mage'){createBurst(muzzle.clone().setY(.18),0x78eaff,.28);createImpactFlash(muzzle,0x74dfff,.34);}
      if(this.card.id==='sky_manta'){createBurst(muzzle.clone().setY(.18),0x7effec,.20);}if(this.card.id==='storm_raven'){createElectricSpark(muzzle.clone().setY(.18),0x78eaff,.42);createBurst(muzzle.clone().setY(.18),0x7bdcff,.24);}
      fireProjectile(this,this.target,dmg,this.projectileSpeed,this.splash,projColor);
    }
    else {const hitPos=this.target.group.position.clone().setY(this.target instanceof Tower?1.2:.9);createSlashArc(hitPos,this.team,this.baseScale||1);if(this.card.id==='twin_blades')createSlashArc(hitPos.clone().add(new THREE.Vector3(.10,.03,.06)),this.team,(this.baseScale||1)*.75);this.target.takeDamage(dmg,this);if(this.card.id==='frost_fang'&&this.target instanceof Unit){this.target.applySlow?.(this.card.slowPct||.30,this.card.slowDuration||2);createFrostHit(hitPos);}createHitSpark(hitPos,this.team);createImpactFlash(hitPos,this.team===TEAM.PLAYER?0xbdefff:0xffc0c8,.65);cameraShake=Math.min(.20,cameraShake+.022);}
  }
  destroy(attacker){if(this.dead)return;super.destroy(attacker);const p=this.group.position.clone();createBurst(p.clone().setY(.65),this.team===TEAM.PLAYER?0x3bbfff:0xff5369,.48);createDebrisBurst(p.clone().setY(.75),this.team===TEAM.PLAYER?0x5ad5ff:0xff7183,8,.7);createSmokePuff(p.clone().setY(.55),.55);if(this.bar)this.bar.visible=false;if(this.hpSprite)this.hpSprite.visible=false;this.group.visible=false;}
}

const units=[];
const towers=[];
const buildings=[];
const projectiles=[];
const effects=[];
const hazards=[];

function horizontalDistance(a,b){
  const ap=a.group?a.group.position:a.position; const bp=b.group?b.group.position:b.position;
  return Math.hypot(ap.x-bp.x,ap.z-bp.z);
}
function moveTowards(pos,target,maxDelta){
  tmpV2.set(target.x-pos.x,0,target.z-pos.z); const len=tmpV2.length(); if(len<=maxDelta){pos.x=target.x;pos.z=target.z;return;} tmpV2.multiplyScalar(maxDelta/len); pos.x+=tmpV2.x;pos.z+=tmpV2.z;
}
function structureEntities(team=null){
  return [...towers,...buildings].filter(e=>!e.dead&&(team===null||e.team===team));
}
function sideOfRiver(z){ return z>ARENA.riverHalf?1:z<-ARENA.riverHalf?-1:0; }
function bridgePathDistanceFromPositions(a,b,preferredLane=0){
  const sa=sideOfRiver(a.z), sb=sideOfRiver(b.z);
  if(sa===0||sb===0||sa===sb) return Math.hypot(a.x-b.x,a.z-b.z);
  const bridgeXs=preferredLane?[laneBridgeX(preferredLane),laneBridgeX(-preferredLane)]:[-ARENA.bridgeX,ARENA.bridgeX];
  let best=Infinity;
  for(const bx of bridgeXs){
    const entryZ=sa>0?ARENA.riverHalf:-ARENA.riverHalf;
    const exitZ=sb>0?ARENA.riverHalf:-ARENA.riverHalf;
    const d=Math.hypot(a.x-bx,a.z-entryZ)+(ARENA.riverHalf*2)+Math.hypot(b.x-bx,b.z-exitZ);
    if(d<best)best=d;
  }
  return best;
}
function navigableDistance(entity,target){
  if(!entity||!target)return Infinity;
  if(entity.flying) return horizontalDistance(entity,target);
  const a=entity.group?.position||entity.position, b=target.group?.position||target.position;
  return bridgePathDistanceFromPositions(a,b,entity.lane||0);
}
function bestBridgeFor(entity,target){
  const a=entity.group.position,b=target.group.position;
  if(target instanceof Tower&&target.kind==='guard')return laneBridgeX(target.lane);
  let choices=[-ARENA.bridgeX,ARENA.bridgeX],best=choices[0],bestD=Infinity;
  for(const bx of choices){
    const entryZ=a.z>=0?ARENA.riverHalf:-ARENA.riverHalf;
    const exitZ=b.z>=0?ARENA.riverHalf:-ARENA.riverHalf;
    const d=Math.hypot(a.x-bx,a.z-entryZ)+ARENA.riverHalf*2+Math.hypot(b.x-bx,b.z-exitZ);
    if(d<bestD){bestD=d;best=bx;}
  }
  return best;
}
function findNearestTargetableUnit(team,pos,range,canHitAir=true){
  return units.filter(u=>!u.dead&&u.team!==team&&(canHitAir||!u.flying)&&Math.hypot(u.group.position.x-pos.x,u.group.position.z-pos.z)<=range+u.radius)
    .sort((a,b)=>a.group.position.distanceToSquared(pos)-b.group.position.distanceToSquared(pos))[0]||null;
}
function findNearestUnit(team,pos,range){ return findNearestTargetableUnit(team,pos,range,true); }
function chooseStructureTarget(unit){
  const enemies=structureEntities(opponentTeam(unit.team));
  if(!enemies.length)return null;
  // Building-targeting units use true navigable distance, allowing a defensive Archer Tower to pull them naturally.
  if(unit.buildingsOnly)return enemies.sort((a,b)=>navigableDistance(unit,a)-navigableDistance(unit,b))[0]||null;
  const laneGuard=enemies.find(t=>t instanceof Tower&&t.kind==='guard'&&t.lane===unit.lane);
  const nearbyBuilding=enemies.filter(e=>e instanceof DefensiveBuilding).sort((a,b)=>navigableDistance(unit,a)-navigableDistance(unit,b))[0];
  if(nearbyBuilding&&isWithinSight(unit,nearbyBuilding))return nearbyBuilding;
  if(laneGuard)return laneGuard;
  return enemies.sort((a,b)=>navigableDistance(unit,a)-navigableDistance(unit,b))[0]||null;
}

function fireProjectile(source,target,damage,speed,splash=0,color=0xffffff){
  if(!target||target.dead) return;
  const sourceIsStructure=(source instanceof Tower)||(source instanceof DefensiveBuilding);
  const targetIsStructure=(target instanceof Tower)||(target instanceof DefensiveBuilding);
  const isArrow=source?.card?.id==='ember_archer'||source?.card?.id==='archer_tower';
  const size=sourceIsStructure?.18:.135;
  let orb;
  if(isArrow){
    orb=new THREE.Group();
    const shaft=mesh(new THREE.CylinderGeometry(.025,.025,.62,7),new THREE.MeshStandardMaterial({color:0xdcc59c,roughness:.55,metalness:.05}),false,false);shaft.rotation.x=Math.PI/2;orb.add(shaft);
    const tip=mesh(new THREE.ConeGeometry(.075,.19,7),new THREE.MeshStandardMaterial({color:0xeaf2f3,roughness:.25,metalness:.7}),false,false);tip.rotation.x=-Math.PI/2;tip.position.z=-.39;orb.add(tip);
    const glow=mesh(new THREE.SphereGeometry(.065,8,6),new THREE.MeshBasicMaterial({color,transparent:true,opacity:.32,blending:THREE.AdditiveBlending,depthWrite:false}),false,false);glow.position.z=-.18;orb.add(glow);
  }else{
    const material=new THREE.MeshBasicMaterial({color,transparent:true,opacity:1,blending:THREE.AdditiveBlending,depthWrite:false});
    orb=mesh(new THREE.IcosahedronGeometry(size,2),material,false,false);
    const halo=mesh(new THREE.SphereGeometry(size*1.75,10,8),new THREE.MeshBasicMaterial({color,transparent:true,opacity:.18,blending:THREE.AdditiveBlending,depthWrite:false}),false,false);orb.add(halo);
  }
  const start=source.group.position.clone();start.y+=source instanceof Tower?2.48:source instanceof DefensiveBuilding?2.55:(source.flying?1.15:1.35);orb.position.copy(start);scene.add(orb);
  const dest=target.group.position.clone();dest.y+=targetIsStructure?1.35:(target.flying?1.0:.86);
  const distance=Math.max(.1,start.distanceTo(dest));
  projectiles.push({mesh:orb,target,damage,speed,splash,team:source.team,source:battleDamageSource(source),life:3,color,trail:0,start,t:0,duration:Math.max(.11,distance/speed),arc:Math.min(1.65,.28+distance*.055),towerShot:sourceIsStructure,isArrow});
}

function updateProjectiles(dt){
  for(let i=projectiles.length-1;i>=0;i--){
    const p=projectiles[i];p.life-=dt;p.trail-=dt;p.t+=dt;
    if(p.life<=0||!p.target||p.target.dead){scene.remove(p.mesh);disposeObject(p.mesh);projectiles.splice(i,1);continue;}
    const targetIsStructure=(p.target instanceof Tower)||(p.target instanceof DefensiveBuilding);
    const dest=p.target.group.position.clone();dest.y+=targetIsStructure?1.35:(p.target.flying?1.0:.86);
    const q=THREE.MathUtils.clamp(p.t/p.duration,0,1);
    const eased=q*q*(3-2*q);
    p.mesh.position.lerpVectors(p.start,dest,eased);p.mesh.position.y+=Math.sin(Math.PI*q)*p.arc;
    if(p.isArrow){p.mesh.lookAt(dest);p.mesh.rotateY(Math.PI);}else{p.mesh.rotation.x+=dt*15;p.mesh.rotation.y+=dt*20;}
    if(p.trail<=0){p.trail=p.towerShot?.025:.035;createTrailParticle(p.mesh.position.clone(),p.color,p.towerShot?.11:.075);}
    if(q>=1){
      // Always apply the primary impact first. V7 could visually hit a tower with splash and deal zero damage.
      if(!p.target.dead) p.target.takeDamage(p.damage,p.source);
      if(p.splash>0){
        for(const u of units){if(u!==p.target&&!u.dead&&u.team!==p.team&&horizontalDistance({group:{position:dest}},u)<=p.splash)u.takeDamage(p.damage,p.source);}
        for(const b of buildings){if(b!==p.target&&!b.dead&&b.team!==p.team&&horizontalDistance({group:{position:dest}},b)<=p.splash+b.radius*.35)b.takeDamage(p.damage,p.source);}
        for(const t of towers){if(t!==p.target&&!t.dead&&t.team!==p.team&&horizontalDistance({group:{position:dest}},t)<=p.splash+t.radius*.25)t.takeDamage(p.damage,p.source);}
        createBurst(dest,p.color,p.splash*.55);createDebrisBurst(dest,p.color,12,p.splash*.5);createImpactFlash(dest,p.color,.95+p.splash*.18);
      }else{createHitSpark(dest,p.team);createImpactFlash(dest,p.color,targetIsStructure?1.05:.65);}
      if(targetIsStructure)createTowerChipBurst(dest,p.target.team);
      scene.remove(p.mesh);disposeObject(p.mesh);projectiles.splice(i,1);
    }
  }
}

function createTrailParticle(pos,color,size=.075){
  const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.46,blending:THREE.AdditiveBlending,depthWrite:false});
  const s=mesh(new THREE.SphereGeometry(size,7,6),m,false,false);s.position.copy(pos);scene.add(s);effects.push({type:'trail',mesh:s,t:0,d:.20});
}

function createHitSpark(pos,team){
  const color=team===TEAM.PLAYER?0xa5ecff:0xffa6b2;
  const core=mesh(new THREE.IcosahedronGeometry(.14,0),new THREE.MeshBasicMaterial({color,transparent:true,opacity:1,blending:THREE.AdditiveBlending,depthWrite:false}),false,false); core.position.copy(pos); scene.add(core); effects.push({type:'spark',mesh:core,t:0,d:.20});
  createDebrisBurst(pos,color,5,.38);
}

function createImpactFlash(pos,color,size=.7){
  const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.78,blending:THREE.AdditiveBlending,depthWrite:false,side:THREE.DoubleSide});
  const disc=mesh(new THREE.CircleGeometry(.22,24),m,false,false);disc.position.copy(pos);disc.rotation.copy(camera.rotation);scene.add(disc);effects.push({type:'impact',mesh:disc,t:0,d:.19,size});
  const light=new THREE.PointLight(color,2.2,4.2,2);light.position.copy(pos);scene.add(light);effects.push({type:'light',mesh:light,t:0,d:.14,size});
}

function createSlashArc(pos,team,size=.8){
  const color=team===TEAM.PLAYER?0xcaf5ff:0xffc9d0;
  const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.82,blending:THREE.AdditiveBlending,depthWrite:false,side:THREE.DoubleSide});
  const arc=mesh(new THREE.TorusGeometry(.42*size,.035*size,6,24,Math.PI*1.35),m,false,false);arc.position.copy(pos);arc.position.y+=.55;arc.rotation.set(Math.PI/2,0,(Math.random()-.5)*.7);scene.add(arc);effects.push({type:'slash',mesh:arc,t:0,d:.24,size});
}

function createTowerChipBurst(pos,team){
  const color=team===TEAM.PLAYER?0x8fc8d7:0xd6a0a5;
  createDebrisBurst(pos.clone().add(new THREE.Vector3(0,.05,0)),color,7,.52);
}

function createSmokePuff(pos,size=.55){
  const m=new THREE.MeshBasicMaterial({color:0x39434a,transparent:true,opacity:.30,depthWrite:false});
  const puff=mesh(new THREE.IcosahedronGeometry(.34,1),m,false,false);puff.position.copy(pos);puff.scale.setScalar(size);scene.add(puff);
  effects.push({type:'smoke',mesh:puff,t:0,d:.75+Math.random()*.38,vel:new THREE.Vector3((Math.random()-.5)*.30,.50+Math.random()*.42,(Math.random()-.5)*.30),spin:(Math.random()-.5)*1.8});
}

function createGroundDecal(pos,color=0x2a2632,size=1.0,duration=1.4){
  const material=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.20,depthWrite:false,side:THREE.DoubleSide});
  const decal=mesh(new THREE.CircleGeometry(size,28),material,false,false);decal.rotation.x=-Math.PI/2;decal.position.copy(pos);decal.position.y=.155;scene.add(decal);effects.push({type:'decal',mesh:decal,t:0,d:duration});
}

function createTowerCollapse(pos,team,core=false){
  const color=team===TEAM.PLAYER?0x56d5ff:0xff6378;
  createBurst(pos.clone().setY(.10),color,core?2.4:1.9);createBurst(pos.clone().setY(.18),0xffd979,core?1.45:1.1);
  createDebrisBurst(pos.clone().setY(1.0),0x8a9798,core?44:30,core?2.05:1.6);
  createDebrisBurst(pos.clone().setY(1.4),color,core?22:14,core?1.55:1.2);
  for(let i=0;i<(core?11:8);i++)createSmokePuff(pos.clone().add(new THREE.Vector3((Math.random()-.5)*1.6,.3+Math.random()*1.8,(Math.random()-.5)*1.4)),.75+Math.random()*.9);
  createImpactFlash(pos.clone().setY(1.25),0xffe0a1,core?2.2:1.7);cameraShake=Math.max(cameraShake,core?.62:.48);
}

function createDeployColumns(pos,team){
  const color=team===TEAM.PLAYER?0x6de2ff:0xff7184;
  for(let i=0;i<5;i++){
    const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.38,blending:THREE.AdditiveBlending,depthWrite:false});
    const h=.55+Math.random()*.75;const beam=mesh(new THREE.CylinderGeometry(.018,.045,h,6),m,false,false);beam.position.copy(pos).add(new THREE.Vector3((Math.random()-.5)*.75,h*.5+.05,(Math.random()-.5)*.75));scene.add(beam);effects.push({type:'rise',mesh:beam,t:0,d:.42+Math.random()*.16,vel:new THREE.Vector3(0,.65+Math.random()*.45,0)});
  }
}

function createBurst(pos,color,size=1){
  const material=new THREE.MeshBasicMaterial({color,side:THREE.DoubleSide,transparent:true,opacity:.94,depthWrite:false,blending:THREE.AdditiveBlending});
  const ring=new THREE.Mesh(new THREE.RingGeometry(.18,.31,36),material); ring.rotation.x=-Math.PI/2; ring.position.copy(pos); scene.add(ring); effects.push({type:'burst',mesh:ring,t:0,d:.52,size});
  const ring2=new THREE.Mesh(new THREE.RingGeometry(.10,.16,32),material.clone()); ring2.rotation.x=-Math.PI/2; ring2.position.copy(pos).add(new THREE.Vector3(0,.035,0)); scene.add(ring2); effects.push({type:'burst',mesh:ring2,t:0,d:.38,size:size*.68});
}

function createDebrisBurst(pos,color,count=8,power=.65){
  for(let i=0;i<count;i++){
    const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.9,blending:THREE.AdditiveBlending,depthWrite:false});
    const shard=mesh(new THREE.BoxGeometry(.055+Math.random()*.06,.055+Math.random()*.08,.055+Math.random()*.06),m,false,false); shard.position.copy(pos); scene.add(shard);
    const a=Math.random()*Math.PI*2, sp=power*(.55+Math.random()*.8); const vel=new THREE.Vector3(Math.cos(a)*sp, power*(.55+Math.random()*1.1),Math.sin(a)*sp);
    effects.push({type:'particle',mesh:shard,t:0,d:.38+Math.random()*.32,vel,gravity:2.8});
  }
}

function createMuzzleFlash(pos,team){
  const color=team===TEAM.PLAYER?0xb7f2ff:0xffc0c8; const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.9,blending:THREE.AdditiveBlending,depthWrite:false});
  const s=mesh(new THREE.OctahedronGeometry(.22,0),m,false,false); s.position.copy(pos); scene.add(s); effects.push({type:'spark',mesh:s,t:0,d:.14});
}

function createBowSnapFx(pos,team){
  const color=team===TEAM.PLAYER?0xffe2a2:0xffc0a2;
  const arc=mesh(new THREE.TorusGeometry(.20,.018,5,18,Math.PI*1.4),new THREE.MeshBasicMaterial({color,transparent:true,opacity:.8,blending:THREE.AdditiveBlending,depthWrite:false}),false,false);
  arc.position.copy(pos);arc.rotation.set(Math.PI/2,0,.4);scene.add(arc);effects.push({type:'slash',mesh:arc,t:0,d:.18,size:.55});
}
function createConstructionFx(pos,team){
  const color=team===TEAM.PLAYER?0x8eeaff:0xff9aa8;
  for(let i=0;i<10;i++){const p=pos.clone().add(new THREE.Vector3((Math.random()-.5)*1.3,.12,(Math.random()-.5)*1.3));createDebrisBurst(p, i%2?0xa48258:color, 1, .35);}
  createBurst(pos.clone().setY(.08),0xffd982,.9);
}
function createMoveDust(pos,team,scale=.45){
  const m=new THREE.MeshBasicMaterial({color:team===TEAM.PLAYER?0xbdd7ce:0xd8c2bd,transparent:true,opacity:.20,depthWrite:false});
  const puff=mesh(new THREE.IcosahedronGeometry(.18,1),m,false,false);puff.position.copy(pos);puff.position.y=.08;puff.scale.set(scale,scale*.35,scale);scene.add(puff);effects.push({type:'smoke',mesh:puff,t:0,d:.38,vel:new THREE.Vector3((Math.random()-.5)*.12,.12,(Math.random()-.5)*.12),spin:(Math.random()-.5)*1.2});
}

function createElectricSpark(pos,color=0x81e8ff,size=.25){
  const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.92,blending:THREE.AdditiveBlending,depthWrite:false});
  const s=mesh(new THREE.OctahedronGeometry(.12,0),m,false,false);s.position.copy(pos);s.scale.setScalar(size/.25);scene.add(s);effects.push({type:'spark',mesh:s,t:0,d:.18});
}
function createStormPulse(pos,radius,team){
  const color=team===TEAM.PLAYER?0x6bdfff:0xb18dff;
  const m=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.88,blending:THREE.AdditiveBlending,depthWrite:false,side:THREE.DoubleSide});
  const ring=mesh(new THREE.RingGeometry(.82,1,64),m,false,false);ring.rotation.x=-Math.PI/2;ring.position.copy(pos);ring.position.y=.18;ring.scale.setScalar(Math.max(.35,radius));scene.add(ring);effects.push({type:'stormPulse',mesh:ring,t:0,d:.38,startScale:Math.max(.35,radius)});
  const ring2=mesh(new THREE.RingGeometry(.68,.80,48),m.clone(),false,false);ring2.rotation.x=-Math.PI/2;ring2.position.copy(pos);ring2.position.y=.20;ring2.scale.setScalar(Math.max(.32,radius*.82));scene.add(ring2);effects.push({type:'stormPulse',mesh:ring2,t:0,d:.30,startScale:Math.max(.32,radius*.82)});
  for(let i=0;i<14;i++){const a=Math.random()*Math.PI*2,r=Math.random()*radius;createElectricSpark(pos.clone().add(new THREE.Vector3(Math.cos(a)*r,.25+Math.random()*.7,Math.sin(a)*r)),color,.18+Math.random()*.16);}
  createImpactFlash(pos.clone().setY(.45),color,Math.max(.55,radius*.20));
  cameraShake=Math.max(cameraShake,.10);
}
function createFrostHit(pos){
  createBurst(pos.clone(),0x9deaff,.42);createBurst(pos.clone().setY(pos.y+.04),0xe7fdff,.24);createImpactFlash(pos.clone(),0xc9f7ff,.46);createGroundDecal(pos.clone().setY(.08),0x8edcf4,.30,.6);
  for(let i=0;i<10;i++){const p=pos.clone().add(new THREE.Vector3((Math.random()-.5)*.52,(Math.random()-.2)*.50,(Math.random()-.5)*.52));createElectricSpark(p,0xcaf7ff,.11+Math.random()*.05);}
}
function createFrostFootstep(pos,team){
  const m=new THREE.MeshBasicMaterial({color:0xaeeeff,transparent:true,opacity:.22,depthWrite:false});
  const ice=mesh(new THREE.CircleGeometry(.14,10),m,false,false);ice.rotation.x=-Math.PI/2;ice.position.copy(pos);ice.position.y=.06;scene.add(ice);effects.push({type:'decal',mesh:ice,t:0,d:.45});
}
function createFrostBreath(pos,team,forward=new THREE.Vector3(0,0,-1)){
  const dir=forward.clone().setY(0); if(dir.lengthSq()<.0001) dir.set(0,0,team===TEAM.PLAYER?-1:1); dir.normalize();
  const origin=pos.clone().add(dir.clone().multiplyScalar(.34)).setY(.92);
  createBurst(origin.clone(),0xcff7ff,.18);
  for(let i=0;i<4;i++){
    const drift=origin.clone().add(dir.clone().multiplyScalar(.18+Math.random()*.24)).add(new THREE.Vector3((Math.random()-.5)*.12,Math.random()*.10,(Math.random()-.5)*.12));
    createElectricSpark(drift,0xbff4ff,.09+Math.random()*.04);
  }
}
function createMeteorShardFx(pos,radius){
  const impact=pos.clone().setY(.16);createBurst(impact,0xff8b4a,1.22);createBurst(impact.clone().add(new THREE.Vector3(0,.04,0)),0xffd67a,.84);createBurst(impact.clone().add(new THREE.Vector3(0,.16,0)),0xfff0b5,.40);createGroundDecal(impact,0x4a1d18,Math.max(1.15,radius*.74),5.6);createGroundDecal(impact,0x7b301d,Math.max(.70,radius*.45),2.2);createDebrisBurst(impact.clone().setY(.32),0xff9a54,40,1.65);createImpactFlash(impact.clone().setY(.26),0xffc068,1.95);
  for(let i=0;i<11;i++){
    const a=Math.random()*Math.PI*2,r=Math.sqrt(Math.random())*radius*.82,end=pos.clone().add(new THREE.Vector3(Math.cos(a)*r,.18,Math.sin(a)*r));
    const start=end.clone().add(new THREE.Vector3((Math.random()-.5)*2.4,7+Math.random()*4.8,(Math.random()-.5)*2.4));
    const shard=mesh(new THREE.ConeGeometry(.10+Math.random()*.06,.52+Math.random()*.34,7),new THREE.MeshStandardMaterial({color:0xffb15e,emissive:0xff5a22,emissiveIntensity:2.4,roughness:.3}),false,false);shard.position.copy(start);scene.add(shard);effects.push({type:'meteor',mesh:shard,t:0,d:.30+Math.random()*.24,start,end});
  }
  cameraShake=Math.max(cameraShake,.33);
}
function createMeteorZone(pos,radius,duration){
  const m=new THREE.MeshBasicMaterial({color:0xff6a3d,transparent:true,opacity:.13,depthWrite:false,side:THREE.DoubleSide,blending:THREE.AdditiveBlending});
  const zone=mesh(new THREE.RingGeometry(.84,1,64),m,false,false);zone.rotation.x=-Math.PI/2;zone.position.copy(pos);zone.position.y=.12;zone.scale.setScalar(radius);scene.add(zone);effects.push({type:'meteorZone',mesh:zone,t:0,d:duration,size:radius});
  const inner=mesh(new THREE.RingGeometry(.48,.64,48),m.clone(),false,false);inner.rotation.x=-Math.PI/2;inner.position.copy(pos);inner.position.y=.14;inner.scale.setScalar(radius*.92);scene.add(inner);effects.push({type:'meteorZone',mesh:inner,t:0,d:duration,size:radius*.92});
}

function createDamageNumber(pos,amount,targetTeam){
  const canvas=document.createElement('canvas'); canvas.width=256; canvas.height=128; const ctx=canvas.getContext('2d');
  ctx.font='1000 58px Inter,Arial,sans-serif'; ctx.textAlign='center'; ctx.textBaseline='middle'; ctx.lineWidth=10; ctx.strokeStyle='rgba(4,8,14,.88)'; ctx.strokeText(`-${Math.round(amount)}`,128,62);
  ctx.fillStyle=targetTeam===TEAM.PLAYER?'#ff8d9d':'#8de9ff'; ctx.fillText(`-${Math.round(amount)}`,128,62);
  const tex=new THREE.CanvasTexture(canvas); tex.colorSpace=THREE.SRGBColorSpace; const spr=new THREE.Sprite(new THREE.SpriteMaterial({map:tex,transparent:true,depthTest:false,depthWrite:false})); spr.scale.set(1.28,.64,1); spr.position.copy(pos); spr.renderOrder=30; scene.add(spr); effects.push({type:'text',mesh:spr,t:0,d:.42,vel:new THREE.Vector3((Math.random()-.5)*.14,.82,0)});
}

function createNovaImpact(pos,color=0xd46bff,radius=3.25){
  createBurst(pos,color,radius*.55);createBurst(pos.clone().add(new THREE.Vector3(0,.045,0)),0x8feeff,radius*.34);createDebrisBurst(pos.clone().setY(.28),color,34,1.45);createGroundDecal(pos,0x5c2770,Math.max(.65,radius*.52),1.7);
  createImpactFlash(pos.clone().setY(.22),0xe28cff,1.9);
  const beamMat=new THREE.MeshBasicMaterial({color,transparent:true,opacity:.28,blending:THREE.AdditiveBlending,depthWrite:false});
  const beam=mesh(new THREE.CylinderGeometry(.20,.60,7.5,20,1,true),beamMat,false,false); beam.position.copy(pos).add(new THREE.Vector3(0,3.75,0)); scene.add(beam); effects.push({type:'beam',mesh:beam,t:0,d:.42});
  cameraShake=Math.max(cameraShake,.42);
}


function createBulletBurstFx(pos,radius,team,count=7){
  const color=team===TEAM.PLAYER?0xffdd82:0xffad7c;
  createBurst(pos.clone().setY(.07),color,.52);createGroundDecal(pos,0x4b4030,Math.max(.45,radius*.46),1.1);
  for(let i=0;i<count;i++){
    const a=Math.random()*Math.PI*2, r=Math.sqrt(Math.random())*radius*.78;
    const end=pos.clone().add(new THREE.Vector3(Math.cos(a)*r,.18,Math.sin(a)*r));
    const start=end.clone().add(new THREE.Vector3(-1.4+Math.random()*.45,4.5+Math.random()*1.3,1.05+Math.random()*.55));
    const dir=end.clone().sub(start),len=dir.length();
    const tracer=mesh(new THREE.CylinderGeometry(.018,.028,Math.min(1.15,len*.28),6),new THREE.MeshBasicMaterial({color,transparent:true,opacity:.95,blending:THREE.AdditiveBlending,depthWrite:false}),false,false);
    tracer.position.copy(start); tracer.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),dir.clone().normalize()); scene.add(tracer);
    effects.push({type:'bulletTrace',mesh:tracer,t:0,d:.16+Math.random()*.12,start,end});
    effects.push({type:'bulletImpact',t:0,d:i*.012,end,color});
  }
  cameraShake=Math.max(cameraShake,.14);
}

function createDeployEffect(pos,team,size=.8,color=null){
  color = color ?? (team===TEAM.PLAYER?0x58d8ff:0xff667c);
  createBurst(pos.clone().setY(.08),color,size);createDebrisBurst(pos.clone().setY(.18),color,13,.70);createDeployColumns(pos.clone().setY(.05),team);createImpactFlash(pos.clone().setY(.20),color,1.05);
}

function updateEffects(dt){
  for(let i=effects.length-1;i>=0;i--){
    const e=effects[i]; e.t+=dt;
    if(e.type==='bulletImpact'){
      if(e.t>=e.d){createImpactFlash(e.end,e.color,.30);createDebrisBurst(e.end,0xd1bc8e,2,.22);effects.splice(i,1);}
      continue;
    }
    const p=Math.min(1,e.t/e.d);
    if(e.type==='burst'){ e.mesh.scale.setScalar(1+p*e.size*3.1); e.mesh.material.opacity=(1-p)*.9; }
    else if(e.type==='spark'){ e.mesh.scale.setScalar(1+p*2.3); e.mesh.material.opacity=1-p; e.mesh.rotation.x+=dt*12; e.mesh.rotation.y+=dt*16; }
    else if(e.type==='trail'){ e.mesh.scale.setScalar(1+p*1.7); e.mesh.material.opacity=(1-p)*.45; }
    else if(e.type==='impact'){e.mesh.scale.setScalar(.7+p*e.size*2.1);e.mesh.material.opacity=(1-p)*.72;e.mesh.quaternion.copy(camera.quaternion);}
    else if(e.type==='slash'){e.mesh.scale.setScalar(.86+p*.45);e.mesh.material.opacity=(1-p)*.78;e.mesh.rotation.z+=dt*7;}
    else if(e.type==='rise'){e.mesh.position.addScaledVector(e.vel,dt);e.mesh.material.opacity=(1-p)*.38;e.mesh.scale.y=1+p*.7;}
    else if(e.type==='light'){e.mesh.intensity=(1-p)*2.2*e.size;}
    else if(e.type==='smoke'){e.mesh.position.addScaledVector(e.vel,dt);e.mesh.rotation.y+=dt*e.spin;e.mesh.scale.multiplyScalar(1+dt*.85);e.mesh.material.opacity=(1-p)*.30;}
    else if(e.type==='particle'){ e.vel.y-=e.gravity*dt; e.mesh.position.addScaledVector(e.vel,dt); e.mesh.rotation.x+=dt*9; e.mesh.rotation.z+=dt*11; e.mesh.material.opacity=1-p; }
    else if(e.type==='text'){ e.mesh.position.addScaledVector(e.vel,dt); e.mesh.material.opacity=1-p; e.mesh.scale.multiplyScalar(1+dt*.22); }
    else if(e.type==='beam'){ e.mesh.scale.x=e.mesh.scale.z=1+p*2.8; e.mesh.material.opacity=(1-p)*.28; }
    else if(e.type==='bulletTrace'){e.mesh.position.lerpVectors(e.start,e.end,p);e.mesh.material.opacity=(1-p)*.95;}
    else if(e.type==='decal'){e.mesh.material.opacity=(1-p)*.20;e.mesh.scale.setScalar(1+p*.08);}else if(e.type==='stormPulse'){e.mesh.scale.setScalar(e.startScale*(1+p*.22));e.mesh.material.opacity=(1-p)*.82;}else if(e.type==='meteor'){e.mesh.position.lerpVectors(e.start,e.end,p);e.mesh.rotation.x+=dt*8;e.mesh.rotation.z+=dt*5;e.mesh.material.opacity=1-p*.15;}else if(e.type==='meteorZone'){e.mesh.material.opacity=.08+.07*Math.sin(e.t*5.5);e.mesh.rotation.z+=dt*.18;}
    if(p>=1){scene.remove(e.mesh);disposeObject(e.mesh);effects.splice(i,1);}
  }
}

function castNova(team,position,card=CARD_LIBRARY.nova_flask,source=null){
  source=source||{team,card,damageKind:'initial'};
  const radius=card.radius*GRID.tile; const point=new THREE.Vector3(position.x,.13,position.z); createNovaImpact(point,0xd46bff,radius);
  for(const u of units){ if(!u.dead&&u.team!==team&&Math.hypot(u.group.position.x-point.x,u.group.position.z-point.z)<=radius)u.takeDamage(spellDamageFor(card),source); }
  for(const t of towers){ if(!t.dead&&t.team!==team&&Math.hypot(t.group.position.x-point.x,t.group.position.z-point.z)<=radius+t.radius*.4)t.takeDamage(spellDamageFor(card,true),source); }
  for(const b of buildings){ if(!b.dead&&b.team!==team&&Math.hypot(b.group.position.x-point.x,b.group.position.z-point.z)<=radius+b.radius*.4)b.takeDamage(spellDamageFor(card,true),source); }
}
function castBulletBurst(team,position,card=CARD_LIBRARY.bullet_burst,source=null){
  source=source||{team,card,damageKind:'initial'};
  const radius=card.radius*GRID.tile,point=new THREE.Vector3(position.x,.13,position.z);createBulletBurstFx(point,radius,team,card.bulletCount||7);
  for(const u of units){if(!u.dead&&u.team!==team&&Math.hypot(u.group.position.x-point.x,u.group.position.z-point.z)<=radius+u.radius*.2)u.takeDamage(spellDamageFor(card),source);}
  for(const t of towers){if(!t.dead&&t.team!==team&&Math.hypot(t.group.position.x-point.x,t.group.position.z-point.z)<=radius+t.radius*.25)t.takeDamage(spellDamageFor(card,true),source);}
  for(const b of buildings){if(!b.dead&&b.team!==team&&Math.hypot(b.group.position.x-point.x,b.group.position.z-point.z)<=radius+b.radius*.25)b.takeDamage(spellDamageFor(card,true),source);}
}
function castMeteorShards(team,position,card=CARD_LIBRARY.meteor_shards,source=null){
  source=source||{team,card,damageKind:'initial'};
  const radius=card.radius*GRID.tile,point=new THREE.Vector3(position.x,.13,position.z);createMeteorShardFx(point,radius);createMeteorZone(point,radius,card.dotDuration||5);
  for(const u of units){if(!u.dead&&u.team!==team&&Math.hypot(u.group.position.x-point.x,u.group.position.z-point.z)<=radius+u.radius*.2)u.takeDamage(spellDamageFor(card),source);}
  hazards.push({kind:'meteor_shards',team,source:{...source,damageKind:'dot'},position:point.clone(),radius,remaining:card.dotDuration||5,tick:card.dotInterval||1,tickEvery:card.dotInterval||1,damage:spellDamageFor(card,false,true),fxTick:.20});
}
function updateHazards(dt){
  for(let i=hazards.length-1;i>=0;i--){
    const h=hazards[i],activeDt=Math.min(dt,Math.max(0,h.remaining));h.remaining-=dt;h.tick-=activeDt;
    if(h.kind==='meteor_shards'){
      h.fxTick=(h.fxTick??.20)-activeDt;
      while(h.fxTick<=0&&h.remaining>0){
        h.fxTick+=.20;
        const a=Math.random()*Math.PI*2,r=Math.sqrt(Math.random())*h.radius*.85;
        const p=h.position.clone().add(new THREE.Vector3(Math.cos(a)*r,.14,Math.sin(a)*r));
        createElectricSpark(p,0xff9b59,.08+Math.random()*.04);
        if(Math.random()<.40)createBurst(p.clone().setY(.18),0xff7c45,.10+Math.random()*.05);
      }
    }
    while(h.tick<=1e-9){
      h.tick+=h.tickEvery;
      for(const u of units){
        if(!u.dead&&u.team!==h.team&&Math.hypot(u.group.position.x-h.position.x,u.group.position.z-h.position.z)<=h.radius+u.radius*.2){
          u.takeDamage(h.damage,h.source);
          const hitPos=u.group.position.clone().setY(u.flying?1.15:.8);
          createElectricSpark(hitPos,0xff9b59,.12);
          createBurst(hitPos.clone(),0xff9448,.16);
        }
      }
    }
    if(h.remaining<=0)hazards.splice(i,1);
  }
}
function castSpell(team,card,position,source=null){
  if(card.spellKind==='bullets'||card.id==='bullet_burst')castBulletBurst(team,position,card,source);
  else if(card.spellKind==='meteor_shards'||card.id==='meteor_shards')castMeteorShards(team,position,card,source);
  else castNova(team,position,card,source);
}

function triggerDeploymentPull(spawnedUnit){
  if(!spawnedUnit || spawnedUnit.dead) return;
  for(const attacker of units){
    if(attacker===spawnedUnit || attacker.dead || attacker.team===spawnedUnit.team || attacker.buildingsOnly) continue;
    if(attacker.structureLockTarget instanceof Tower && !attacker.structureLockTarget.dead) continue;
    if(!((attacker.target instanceof Tower)||(attacker.target instanceof DefensiveBuilding))) continue;
    if(!attacker.canTargetUnit(spawnedUnit)) continue;
    const pullRadius=attacker.sightRangeFor(spawnedUnit,RETARGET.towerPullBonusTiles) + spawnedUnit.radius + attacker.radius*.35;
    if(attacker.targetDistance(spawnedUnit) <= pullRadius){
      attacker.forceRetargetTo(spawnedUnit);
      createImpactFlash(attacker.group.position.clone().setY(attacker.flying?1.35:1.0), attacker.team===TEAM.PLAYER?0x8cecff:0xff9aad, .35);
    }
  }
}

function triggerBuildingPull(building){
  for(const attacker of units){
    if(attacker.dead||attacker.team===building.team)continue;
    if(attacker.structureLockTarget instanceof Tower && !attacker.structureLockTarget.dead)continue;
    const dist=navigableDistance(attacker,building);
    if(attacker.buildingsOnly){
      if(attacker.structureLockTarget && !attacker.structureLockTarget.dead) continue;
      const current=attacker.target;
      if(!current||current.dead||dist<=navigableDistance(attacker,current)+.35){attacker.target=building;attacker.targetScan=.05;}
    }else if((attacker.target instanceof Tower||attacker.target instanceof DefensiveBuilding)&&dist<=attacker.sightRangeFor(building,RETARGET.towerPullBonusTiles)){
      attacker.target=building;attacker.targetScan=.05;attacker.cooldown=Math.min(attacker.cooldown,attacker.attackSpeed*.35);
    }
  }
}

function spawnCard(team,card,position,play=null){
  if(card.spell){ castSpell(team,card,position,{team,card,_playId:play?.playId,_replayId:play?.playId,damageKind:'initial'}); return []; }
  position = snapPointToTile(position, card, team);
  if(card.building){
    const building=new DefensiveBuilding(team,card,position.x,position.z);building._playId=play?.playId;game.recorder?.event(game.elapsed,'entity_spawn',{entity:replayEntity(building)});triggerBuildingPull(building);
    createDeployEffect(new THREE.Vector3(position.x,.05,position.z),team,.92,0xffd982); createConstructionFx(position,team); return [building];
  }
  const count=card.count||1; const spawned=[];
  const formation=count>=5?[[-.86,-.18],[0,-.42],[.86,-.18],[-.43,.38],[.43,.38]]:count===2?[[-.42,.12],[.42,-.12]]:[[0,0]];
  for(let i=0;i<count;i++){
    const f=formation[i]||[(i-(count-1)/2)*.72,(i%2?-.20:.20)],offset=f[0];
    const unit=new Unit(team,card,THREE.MathUtils.clamp(position.x+f[0],-12.2,12.2),position.z+f[1],offset);
    unit._playId=play?.playId;game.recorder?.event(game.elapsed,'entity_spawn',{entity:replayEntity(unit)});
    spawned.push(unit);
    triggerDeploymentPull(unit);
  }
  const fxColor = card.id==='arc_mage'?0x6edcff:card.id==='frost_fang'?0xa8efff:card.id==='storm_raven'?0x78dfff:card.id==='ember_archer'?0xffa45d:card.id==='sky_manta'?0x7ef8ea:card.id==='vampire_bats'?0xd95788:card.id==='rambeast'?0xffcf6b:team===TEAM.PLAYER?0x58d8ff:0xff667c;
  createDeployEffect(new THREE.Vector3(position.x,.05,position.z),team,.68,fxColor);
  return spawned;
}

function createTowers(){
  new Tower(TEAM.PLAYER,'core',0,16.3,0);
  new Tower(TEAM.PLAYER,'guard',-8.2,12.4,-1);
  new Tower(TEAM.PLAYER,'guard',8.2,12.4,1);
  new Tower(TEAM.ENEMY,'core',0,-16.3,0);
  new Tower(TEAM.ENEMY,'guard',-8.2,-12.4,-1);
  new Tower(TEAM.ENEMY,'guard',8.2,-12.4,1);
}
createTowers();

class DeckState {
  constructor(deck){ const safe=validateDeck(deck)?deck.slice():DEFAULT_DECK.slice(); this.hand=safe.slice(0,4); this.queue=safe.slice(4); }
  play(handIndex){ const id=this.hand[handIndex]; if(!id)return null; const next=this.queue.shift(); if(!next)return null; this.hand[handIndex]=next; this.queue.push(id); return id; }
  next(){ return this.queue[0]||null; }
}

const game={
  started:false, running:false, elapsed:0, time:MATCH.regulation, overtime:false, overtimeTime:MATCH.overtime, tiebreaker:false, tiebreakTime:0, tiebreakSnapshot:null,
  crowns:{player:0,enemy:0}, aether:{player:5,enemy:5}, aetherMultiplier:1, selectedIndex:null,
  playerDeck:new DeckState(activeDeck), enemyDeck:new DeckState(DEFAULT_DECK), aiThink:.9, doubleShown:false, winner:null, profileCommitted:false,
  ai:{
    enabled:true,phase:'bank',pushLane:0,anchor:null,supportPlays:0,lastPlay:0,lastDefense:0,planAge:0,style:'beatdown',
    memory:{playerAetherEstimate:5,lastObservedPlay:0,playerPlays:[],playerCycle:[],recentSpend:0,lastDecision:'BANK',decisionReason:'Opening read',lastPunish:0,lastSpellCycle:0}
  },
};

const sandbox={speed:1,spawnArmed:false,spawnTeam:TEAM.PLAYER,spawnCard:'ironclad',debug:{paths:false,sight:false,ranges:false,targets:false,tiles:false},debugClock:0,statusClock:0};
function battleDamageSource(source,damageKind='attack'){
  return {team:source?.team,card:source?.card,_replayId:source?._replayId,_playId:source?._playId,damageKind};
}
function replayEntity(entity){
  return {id:entity._replayId,team:entity.team,kind:entity instanceof Tower?'tower':entity instanceof DefensiveBuilding?'building':'troop',cardId:entity.card?.id,playId:entity._playId,hp:entity.hp,maxHp:entity.maxHp,x:entity.group.position.x,z:entity.group.position.z,targetId:entity.target?._replayId,count:entity.card?.count||1,flying:!!entity.flying,towerKind:entity.kind,slowUntil:entity.slowUntil||0,stunUntil:entity.stunUntil||0};
}
function recordCardPlay(team,card,point,aetherBefore,free=false){
  const playId=`play-${game.playSequence=(game.playSequence||0)+1}`;
  const deck=team===TEAM.PLAYER?game.playerDeck:game.enemyDeck;
  const firstPlay=!free&&!(game.playsByTeam?.[team]);
  if(game.playsByTeam&&!free)game.playsByTeam[team]++;
  const handAfter=deck?.hand?.slice()||[];const handIndex=handAfter.indexOf(card.id);if(!free&&handIndex>=0&&deck?.queue?.[0])handAfter[handIndex]=deck.queue[0];
  const play={team,cardId:card.id,playId,cost:free?0:card.cost,sandbox:free,tile:{x:point.x,z:point.z},aetherBefore,aetherAfter:game.aether[team],openingHand:!free&&(game.playsByTeam?.[team]||0)<=4&&!!game.openingHands?.[team]?.includes(card.id),firstPlay,overtime:game.overtime,hand:deck?.hand?.slice()||[],handAfter};
  game.recorder?.event(game.elapsed,'card_play',play);return play;
}
function captureBattleState(force=false){
  if(!game.recorder)return;
  if(!force&&game.elapsed<(game.nextReplaySnapshot||0))return;
  game.nextReplaySnapshot=game.elapsed+.25;
  for(const team of [TEAM.PLAYER,TEAM.ENEMY]){
    const amount=game.leakPending?.[team]||0;
    if(amount)game.recorder.event(game.elapsed,'aether_leak',{team,amount});
    if(game.leakPending)game.leakPending[team]=0;
  }
  game.recorder.snapshot(game.elapsed,{aether:{...game.aether},hands:{player:game.playerDeck.hand.slice(),enemy:game.enemyDeck.hand.slice()},crowns:{...game.crowns},phase:game.tiebreaker?'tiebreaker':game.overtime?'overtime':'regulation',entities:[...towers,...buildings,...units].filter(e=>!e.dead).map(replayEntity),hazards:hazards.map(h=>({id:h.source?._playId,team:h.team,cardId:h.source?.card?.id,x:h.position.x,z:h.position.z,radius:h.radius,remaining:h.remaining}))});
}
function completeBattleRecording(result){
  if(!game.recorder)return;
  captureBattleState(true);
  const replay=game.recorder.finish(game.elapsed,result);
  if(replay){game.lastReplayId=replay.id;game.replayViewer?.refresh();persistLabSave();}
}
const debugOverlayGroup=new THREE.Group();debugOverlayGroup.renderOrder=50;scene.add(debugOverlayGroup);
let debugTileLabelGroup=null;

function cardArtPath(id){ return `./assets/ui/${id}.svg`; }

function targetModeLabel(card){
  if(card.spell) return (card.towerDamage??0)>0 ? 'Ground + Air troops / structures' : 'Ground + Air troops only';
  if(card.building) return card.canHitAir ? 'Ground + Air troops' : 'Ground troops';
  if(card.buildingsOnly) return 'Buildings only';
  return card.canHitAir ? 'Ground + Air' : 'Ground only';
}
function combatClassLabel(card){
  if(card.spell) return card.combatClass||'Spell';
  return card.combatClass || (card.flying ? 'Flying Ranged' : (card.projectile ? 'Ground Ranged' : 'Ground Melee'));
}
function categoryLabel(card){
  return card.category || card.type || 'Troop';
}

function n1(v){ return Number.isInteger(v)?String(v):Number(v).toFixed(1); }
function statNumber(v){ return Number(v).toFixed(2).replace(/\.?0+$/,''); }
function toTiles(v){ return Number(v)/GRID.tile; }
function cardDps(card){ return card.spell?null:card.damage/card.attackSpeed; }
function cardStatRows(card){
  if(card.spell){
    const rows=[['Category',categoryLabel(card)],['Class',combatClassLabel(card)],['Aether Cost',`${card.cost}`],['Hitpoints','N/A — spell'],['Initial Damage',`${card.damage}`],['Tower / Building Damage',`${card.towerDamage??0}`],['DPS',card.dotDamage?`${card.dotDamage}/s inside zone`:'N/A — area burst'],['Hit Speed','N/A — single cast'],['Move Speed','N/A — area cast'],['Attack Range','Anywhere in arena'],['Radius',`${statNumber(toTiles(card.radius))} tiles`],['Front Sight','N/A — spell'],['Rear Sight','N/A — spell'],['Units','1 cast'],['Targeting',targetModeLabel(card)],['Deploy','Anywhere']];
    if(card.dotDamage)rows.push(['Damage Over Time',`${card.dotDamage}/s for ${statNumber(card.dotDuration)}s`],['Maximum Troop Damage',`${card.damage+card.dotDamage*card.dotDuration} while stationary`]);
    if(card.bulletCount)rows.push(['Rounds',`${card.bulletCount}`]);
    return rows;
  }
  if(card.building){
    return [
      ['Category',categoryLabel(card)],['Class',combatClassLabel(card)],['Hitpoints',`${card.hp}`],['Damage / Hit',`${card.damage}`],['DPS',`${(card.damage/card.attackSpeed).toFixed(1)}`],['Hit Speed',`${statNumber(card.attackSpeed)} s`],['Move Speed','0 tiles/s — stationary'],['Attack Range',`${statNumber(toTiles(card.range))} tiles`],['Front Sight',`${SIGHT.frontTiles} tiles`],['Rear Sight',`${SIGHT.rearTiles} tiles`],['Units','1'],['Targeting',targetModeLabel(card)],['Lifetime',`${statNumber(card.lifetime)} s`],['Footprint',`${statNumber(card.footprint)} × ${statNumber(card.footprint)} tiles`],['Projectile Speed',`${statNumber(toTiles(card.projectileSpeed))} tiles/s`],['Aether Cost',`${card.cost}`]
    ];
  }
  const rows=[
    ['Category',categoryLabel(card)],
    ['Class',combatClassLabel(card)],
    ['Hitpoints',`${card.hp}${card.count?` each • ${card.hp*card.count} total`:''}`],
    ['Damage / Hit',`${card.damage}${card.count?` each`:''}`],
    ['DPS',`${cardDps(card).toFixed(1)}${card.count?` each • ${(cardDps(card)*card.count).toFixed(1)} total`:''}`],
    ['Hit Speed',`${statNumber(card.attackSpeed)} s`],
    ['Move Speed',`${statNumber(toTiles(card.moveSpeed))} tiles/s`],
    ['Attack Range',`${statNumber(toTiles(card.range))} tiles`],
  ];
  rows.push(['Front Sight',`${SIGHT.frontTiles} tiles`],['Rear Sight',`${SIGHT.rearTiles} tiles`]);
  rows.push(['Units',`${card.count||1}`],['Targeting',targetModeLabel(card)],['Movement',card.flying?'Flying':'Ground']);
  if(card.projectileSpeed)rows.push(['Projectile Speed',`${statNumber(toTiles(card.projectileSpeed))} tiles/s`]);
  if(card.splash) rows.push(['Splash Radius',`${statNumber(toTiles(card.splash))} tiles`]);
  if(card.charger) rows.push(['Charged Hit',`${Math.round(card.chargeDamage ?? card.damage*1.9)} damage after 1.65 s run`]);
  if(card.slowPct)rows.push(['Frost Slow',`${Math.round(card.slowPct*100)}% movement for ${statNumber(card.slowDuration)}s • refreshes, no stack`]);
  if(card.auraDamage)rows.push(['Storm Ring',`${card.auraDamage} dmg / ${statNumber(card.auraInterval)}s • ${statNumber(card.auraRadius)} tiles • ${statNumber(card.stunDuration)}s stun • Ground + Air troops`]);
  rows.push(['Aether Cost',`${card.cost}`]);
  return rows;
}
function compactCardStats(card){
  if(card.spell) return `${combatClassLabel(card)} • ${card.damage} initial${card.dotDamage?` + ${card.dotDamage}/s×${card.dotDuration}s`:''} • ${n1(toTiles(card.radius))} tiles`;
  if(card.building) return `${combatClassLabel(card)} • ${targetModeLabel(card)} • HP ${card.hp} • DPS ${(card.damage/card.attackSpeed).toFixed(0)}`;
  return `${combatClassLabel(card)} • ${targetModeLabel(card)} • HP ${card.hp}${card.count?`×${card.count}`:''} • DPS ${cardDps(card).toFixed(0)}`;
}

function renderProfile(){
  if(ui.profileName)ui.profileName.textContent=(playerProfile.username||'RIFTBOUND').toUpperCase();
  if(ui.profileWins)ui.profileWins.textContent=playerProfile.wins||0;
  if(ui.profileCrowns)ui.profileCrowns.textContent=playerProfile.crowns||0;
  if(ui.profileCards)ui.profileCards.textContent=ALL_CARD_IDS.length;
  if(ui.walletGems)ui.walletGems.textContent=Number(playerProfile.gems||0).toLocaleString();
  if(ui.walletGold)ui.walletGold.textContent=Number(playerProfile.gold||0).toLocaleString();
  if(ui.accountId)ui.accountId.textContent=playerProfile.playerId||'RC-LOCAL';
  if(ui.accountMatches)ui.accountMatches.textContent=playerProfile.matches||0;
  if(ui.accountRecord)ui.accountRecord.textContent=`${playerProfile.wins||0}-${playerProfile.losses||0}-${playerProfile.draws||0}`;
  if(ui.accountUsername)ui.accountUsername.value=playerProfile.username||'RIFTBOUND';
}
function openAccountModal(){renderProfile();ui.accountModal?.classList.remove('hidden');}
function saveAccountFromUi(){
  const next=String(ui.accountUsername?.value||'RIFTBOUND').trim().replace(/[^a-zA-Z0-9 _-]/g,'').slice(0,18)||'RIFTBOUND';
  playerProfile.username=next;saveProfile();renderProfile();ui.accountModal?.classList.add('hidden');toast('Profile saved locally');
}
function recordMatchResult(winner){
  if(game.profileCommitted)return;game.profileCommitted=true;
  playerProfile.matches=(playerProfile.matches||0)+1;
  playerProfile.crowns=(playerProfile.crowns||0)+(game.crowns.player||0);
  if(winner===TEAM.PLAYER)playerProfile.wins=(playerProfile.wins||0)+1;
  else if(winner===TEAM.ENEMY)playerProfile.losses=(playerProfile.losses||0)+1;
  else playerProfile.draws=(playerProfile.draws||0)+1;
  saveProfile();renderProfile();
}

function renderHomeDeck(){
  if(!ui.homeDeck)return;
  ui.homeDeck.innerHTML='';
  let total=0;
  for(const id of activeDeck){
    const c=CARD_LIBRARY[id]; total+=c.cost;
    const el=document.createElement('div'); el.className='mini-card'; el.title=`${c.name} — ${c.cost} Aether`;
    el.innerHTML=`<img src="${cardArtPath(id)}" alt="${c.name}"><span class="mini-cost">${c.cost}</span>`;
    ui.homeDeck.appendChild(el);
  }
  if(ui.homeAvg)ui.homeAvg.textContent=(total/activeDeck.length).toFixed(1);
  renderHomeDeckProfile(deckAnalyzer,activeDeck);
}

function renderDeckDetails(){
  if(!ui.deckDetail)return;
  ui.deckDetail.innerHTML='';
  for(const id of ALL_CARD_IDS){
    const c=CARD_LIBRARY[id];
    const el=document.createElement('article'); el.className='deck-detail';
    const rows=cardStatRows(c).map(([k,v])=>`<div class="exact-stat"><span>${k}</span><strong>${v}</strong></div>`).join('');
    const tags = `<div class="detail-tags"><span>${combatClassLabel(c)}</span><span>${targetModeLabel(c)}</span></div>`;
    el.innerHTML=`<div class="detail-art"><img src="${cardArtPath(id)}" alt="${c.name}"><span class="detail-cost">${c.cost}</span></div><div class="detail-copy"><div class="detail-title"><div><small>${categoryLabel(c).toUpperCase()}</small><h4>${c.name}</h4></div><span>LV 1</span></div>${tags}<p>${c.desc}</p><div class="exact-stat-grid">${rows}</div></div>`;
    renderCardIntelligence(deckAnalyzer,id,el.querySelector('.detail-copy'));
    ui.deckDetail.appendChild(el);
  }
}


function renderLoadoutBuilder(){
  renderDeckWorkspace({
    cards:CARD_LIBRARY,analyzer:deckAnalyzer,draft:loadoutDraft,presets:deckPresetState.presets,
    selectedPresetId:loadoutPresetId,presetName:presetNameDraft,
    onDraftChange(ids){loadoutDraft=ids.slice();renderLoadoutBuilder();},
    onSelectPreset(id){
      const preset=deckPresetState.presets.find(p=>p.id===id);if(!preset)return;
      loadoutPresetId=id;loadoutDraft=preset.cards.slice();presetNameDraft=preset.name;renderLoadoutBuilder();
    },
    onNameChange(name){presetNameDraft=name;},onSave:saveLoadout,onTest:testDeck,onReset:resetLoadout,
  });
}
function openLoadoutBuilder(){
  syncLoadoutDraft();renderLoadoutBuilder();ui.loadoutModal?.classList.remove('hidden');
}
function saveLoadout(){
  if(!validateDeck(loadoutDraft)){toast(`Choose exactly ${DECK_SIZE} unique cards`);return false;}
  const updated=presetStore.update(deckPresetState,loadoutPresetId,{name:presetNameDraft,cards:loadoutDraft});
  if(!updated){toast('Give this deck a name');return false;}
  deckPresetState=presetStore.select(updated,loadoutPresetId);
  activeDeck=loadoutDraft.slice();presetNameDraft=deckPresetState.presets.find(p=>p.id===loadoutPresetId).name;
  saveDeckStorage();persistServerSave();
  if(!game.started){game.playerDeck=new DeckState(activeDeck);renderHand();}
  renderHomeDeck();renderLoadoutBuilder();ui.loadoutModal?.classList.add('hidden');
  toast(game.started?'Deck saved for your next battle':'Battle deck saved');return true;
}
function resetLoadout(){loadoutDraft=DEFAULT_DECK.slice();renderLoadoutBuilder();}
async function testDeck(){
  if(!saveLoadout())return;
  if(game.started){await leaveBattle();sessionStorage.setItem('riftAutostart','training');location.reload();}
  else{await labReady;startMatch(true);}
}

// ---------------------------- V15 META LAB — WORKER / HISTORY / REPLAYS ----------------------------
// V2 fixes the old estimator's biggest biases:
// 1) uses the live 3:00 regulation / conditional OT Aether budget instead of assuming 5:00 every game,
// 2) removes win-rate feedback from deck sampling,
// 3) excludes mirror-card games from card-specific win rate,
// 4) uses Bayesian shrinkage + Wilson confidence intervals,
// 5) computes matchup-sensitive spell/swarm/air/building value on comparable scales.
const labPersistence=createLabPersistence();
function persistLabSave(){
  if(!game.metaLab||!game.recorder||game.running)return Promise.resolve(false);
  return labPersistence.save({meta:game.metaLab.serialize(),replays:game.recorder.serialize().replays});
}
async function initializeLab(){
  const saved=await labPersistence.load();
  game.recorder=createBattleRecorder(CARD_LIBRARY,{version:'V15'});
  if(saved?.replays)game.recorder.hydrate(saved.replays);
  game.replayViewer=createReplayViewer({recorder:game.recorder,cards:CARD_LIBRARY,onChange:persistLabSave});
  let legacy=null;try{legacy=JSON.parse(localStorage.getItem('rift_crown_meta_v1210_balance_math')||'null');}catch{}
  game.metaLab=createMetaLabController({cards:CARD_LIBRARY,analyzer:deckAnalyzer,styles:AI_STYLES,saved:saved?.meta,legacy,onSave:persistLabSave,onNotice:toast,rules:{match:MATCH,arena:ARENA,grid:GRID,sight:SIGHT,retarget:RETARGET,towers:{guard:{hp:2250,damage:86,attackSpeed:1.02,range:10,radius:1.15},core:{hp:3600,damage:112,attackSpeed:.92,range:8.9,radius:1.35}}}});
}
async function leaveBattle(){
  if(game.running){completeBattleRecording({winner:null,crowns:{...game.crowns},abandoned:true});game.running=false;game.metaLab?.setBattleRunning(false);}
  await persistLabSave();await Promise.all([serverSavePending,labPersistence.flush()]);
}

function renderHand(){
  ui.hand.innerHTML='';
  game.playerDeck.hand.forEach((id,index)=>{
    const c=CARD_LIBRARY[id]; const el=document.createElement('button'); el.className='card'+(game.selectedIndex===index?' selected':'')+(game.aether.player<c.cost?' disabled':'');
    el.style.setProperty('--card-a',c.artA); el.style.setProperty('--card-b',c.artB); el.dataset.index=index; el.title=`${c.name}: ${compactCardStats(c)}`;
    el.innerHTML=`<div class="card-art"><img src="${cardArtPath(id)}" alt=""></div><div class="card-info"><div class="card-name">${c.name}</div><div class="card-type">${c.type}</div></div><div class="card-cost">${c.cost}</div>`;
    el.addEventListener('pointerdown',ev=>beginCardPointer(ev,index,el));
    ui.hand.appendChild(el);
  });
  const nextId=game.playerDeck.next();
  ui.next.innerHTML=nextId?`<img src="${cardArtPath(nextId)}" alt="${CARD_LIBRARY[nextId].name}">`:'•';
}

function hideCardInspector(){
  const panel=document.querySelector('#selected-card-panel');
  panel?.classList.add('hidden');panel?.classList.remove('pinned');
}
function resetSelectionUi(){
  if(ui.selectedStats)ui.selectedStats.innerHTML='';
  hideCardInspector();
  ui.placementHint?.classList.remove('active');
}
function selectCard(index){
  if(!game.running)return; const c=CARD_LIBRARY[game.playerDeck.hand[index]]; if(!c)return;
  game.selectedIndex=game.selectedIndex===index?null:index; renderHand();
  if(game.selectedIndex===null){resetSelectionUi();return;}
}

function toast(message){ clearTimeout(toast._t); ui.toast.textContent=message; ui.toast.classList.add('show'); toast._t=setTimeout(()=>ui.toast.classList.remove('show'),1200); }

function arenaPointFromClient(clientX,clientY){
  const rect=canvas.getBoundingClientRect(); pointer.x=((clientX-rect.left)/rect.width)*2-1; pointer.y=-((clientY-rect.top)/rect.height)*2+1; raycaster.setFromCamera(pointer,camera); const hit=new THREE.Vector3(); return raycaster.ray.intersectPlane(groundPlane,hit)?hit:null;
}
function arenaPointFromPointer(ev){ return arenaPointFromClient(ev.clientX,ev.clientY); }
function pointInsideArena(point){ return !!point && Math.abs(point.x)<=ARENA.width/2 && Math.abs(point.z)<=ARENA.length/2; }
function laneForX(x){ return x<0?-1:1; }
function opponentTeam(team){ return team===TEAM.PLAYER?TEAM.ENEMY:TEAM.PLAYER; }
function pocketGuard(team,lane){ return towers.find(t=>t.team===opponentTeam(team)&&t.kind==='guard'&&t.lane===lane); }
function isPocketUnlocked(team,lane){ return !!pocketGuard(team,lane)?.dead; }
function pointInPocket(team,point){
  if(!point) return false;
  const ax=Math.abs(point.x);
  if(ax<POCKET.xInner||ax>POCKET.xOuter) return false;
  const lane=laneForX(point.x);
  const guard=pocketGuard(team,lane);
  if(!guard?.dead) return false;

  // Pocket deployment is intentionally the lane rectangle between the river and
  // the former Guard Tower approach. It never reaches back onto the Core Tower.
  // These exact bounds are also used by the visible overlay, so legal placement
  // always matches what the player sees.
  if(team===TEAM.PLAYER){
    return point.z<=-POCKET.zNear && point.z>=-POCKET.zFar;
  }
  return point.z>=POCKET.zNear && point.z<=POCKET.zFar;
}
function refreshPocketOverlays(){
  for(const team of [TEAM.PLAYER,TEAM.ENEMY])for(const lane of [-1,1]){
    const zone=pocketOverlays[team]?.[lane]; if(!zone)continue;
    const unlocked=isPocketUnlocked(team,lane);
    zone.visible=unlocked&&team===TEAM.PLAYER&&game?.started;
    zone.material.opacity=zone.visible?.16:0;
  }
}
function placementOccupied(card,point,team){
  if(!card?.building)return false;
  const r=(card.footprint||1.6)*.55;
  for(const t of towers){if(!t.dead&&Math.hypot(t.group.position.x-point.x,t.group.position.z-point.z)<t.radius+r+.45)return true;}
  for(const b of buildings){if(!b.dead&&Math.hypot(b.group.position.x-point.x,b.group.position.z-point.z)<b.radius+r+.35)return true;}
  return false;
}
function pointInLegalDeployZone(team,point){
  const ownSide=team===TEAM.PLAYER?point.z>=ARENA.playerMinZ:point.z<=ARENA.enemyMaxZ;
  return ownSide||pointInPocket(team,point);
}
function validTeamPlacement(card,point,team){
  if(!pointInsideArena(point))return false;
  if(card.spell)return true;
  if(card.building){
    const half=(card.footprint||1.6)*.5;
    const corners=[[-half,-half],[-half,half],[half,-half],[half,half]];
    for(const [dx,dz] of corners){const q=new THREE.Vector3(point.x+dx,0,point.z+dz);if(!pointInsideArena(q)||!pointInLegalDeployZone(team,q))return false;}
  }else if(!pointInLegalDeployZone(team,point))return false;
  if(placementOccupied(card,point,team))return false;
  return true;
}
function snapAxisToTileCenter(value,min,max){
  const idx=Math.floor((value-min)/GRID.tile);
  const center=min+(idx+.5)*GRID.tile;
  return THREE.MathUtils.clamp(center,min+GRID.tile*.5,max-GRID.tile*.5);
}
function snapPointToTile(point, card=null, team=TEAM.PLAYER){
  if(!point) return point;
  const snapped = point.clone();
  snapped.x=snapAxisToTileCenter(snapped.x,-ARENA.width/2,ARENA.width/2);
  snapped.z=snapAxisToTileCenter(snapped.z,-ARENA.length/2,ARENA.length/2);
  return snapped;
}
function validPlayerPlacement(card,point){ return validTeamPlacement(card,point,TEAM.PLAYER); }

function attemptPlayerDeploy(index,point){
  if(!game.running||game.tiebreaker)return false; const id=game.playerDeck.hand[index]; const card=CARD_LIBRARY[id]; if(!card||!pointInsideArena(point))return false;
  point = snapPointToTile(point, card, TEAM.PLAYER);
  if(game.aether.player+1e-5<card.cost){toast(`Need ${card.cost} Aether — you have ${game.aether.player.toFixed(1)}`);return false;}
  if(card.building&&placementOccupied(card,point,TEAM.PLAYER)){toast('Building footprint is blocked');return false;}
  if(!validPlayerPlacement(card,point)){toast('That tile is locked — use your half or an unlocked pocket');return false;}
  captureBattleState(true);game.aether.player-=card.cost;
  aiObservePlayerPlay(card,point);
  const play=recordCardPlay(TEAM.PLAYER,card,point,game.aether.player+card.cost);
  spawnCard(TEAM.PLAYER,card,point,play); game.playerDeck.play(index);captureBattleState(true); game.selectedIndex=null; renderHand(); resetSelectionUi(); placementPreview.visible=false; return true;
}

let dragState=null;
function beginCardPointer(ev,index,el){
  if(ev.button!==0||dragState||!game.running||game.tiebreaker)return; ev.preventDefault();
  dragState={pointerId:ev.pointerId,index,el,startX:ev.clientX,startY:ev.clientY,active:false};
  el.setPointerCapture?.(ev.pointerId);
}
function updateCardDrag(ev){
  if(!dragState||ev.pointerId!==dragState.pointerId)return;
  const dist=Math.hypot(ev.clientX-dragState.startX,ev.clientY-dragState.startY);
  if(!dragState.active&&dist>7){
    dragState.active=true; document.body.classList.add('card-dragging');
    const c=CARD_LIBRARY[game.playerDeck.hand[dragState.index]];
    ui.dragGhost.innerHTML=`<img src="${cardArtPath(c.id)}" alt=""><span>${c.cost}</span><strong>${c.name}</strong>`; ui.dragGhost.classList.remove('hidden');
  }
  if(!dragState.active)return;
  ui.dragGhost.style.transform=`translate3d(${ev.clientX+18}px,${ev.clientY-84}px,0) rotate(3deg)`;
  const returning=pointerInsideElement(ev,document.querySelector('#hand-wrap'))||pointerInsideElement(ev,document.querySelector('#bottom-hud')); document.body.classList.toggle('card-return-zone',returning);
  const c=CARD_LIBRARY[game.playerDeck.hand[dragState.index]]; const point=arenaPointFromPointer(ev); const snapped=pointInsideArena(point)?snapPointToTile(point,c,TEAM.PLAYER):null; const valid=!returning&&validPlayerPlacement(c,snapped)&&game.aether.player+1e-5>=c.cost;
  setPlacementPreview(c,snapped,valid); ui.dragGhost.classList.toggle('invalid',!valid);
}
function pointerInsideElement(ev,el){
  if(!el)return false; const r=el.getBoundingClientRect(); return ev.clientX>=r.left&&ev.clientX<=r.right&&ev.clientY>=r.top&&ev.clientY<=r.bottom;
}
function cancelCardPlacement(showToast=false){
  game.selectedIndex=null; placementPreview.visible=false; resetSelectionUi(); renderHand();
  if(showToast)toast('Card returned to hand');
}
function finishCardDrag(ev,cancelled=false){
  if(!dragState||ev.pointerId!==dragState.pointerId)return;
  const state=dragState; const returning=pointerInsideElement(ev,document.querySelector('#hand-wrap'))||pointerInsideElement(ev,document.querySelector('#bottom-hud'));
  dragState=null; document.body.classList.remove('card-dragging','card-return-zone'); ui.dragGhost.classList.add('hidden'); ui.dragGhost.classList.remove('invalid'); placementPreview.visible=false; if(game.selectedIndex===null)hideCardInspector();
  if(cancelled||(state.active&&returning)){cancelCardPlacement(state.active&&returning);return;}
  if(state.active){ const point=arenaPointFromPointer(ev); if(!pointInsideArena(point)){cancelCardPlacement(true);return;} attemptPlayerDeploy(state.index,point); }
  else selectCard(state.index);
}
window.addEventListener('pointermove',updateCardDrag,{passive:true});
window.addEventListener('pointerup',ev=>finishCardDrag(ev,false));
window.addEventListener('pointercancel',ev=>finishCardDrag(ev,true));
window.addEventListener('keydown',ev=>{if(ev.key==='Escape'&&game.running){if(dragState){dragState=null;document.body.classList.remove('card-dragging','card-return-zone');ui.dragGhost.classList.add('hidden');}cancelCardPlacement(false);}});
canvas.addEventListener('contextmenu',ev=>{if(game.running){ev.preventDefault();cancelCardPlacement(false);}});

canvas.addEventListener('pointermove',(ev)=>{
  if(!game.running||game.tiebreaker||dragState?.active||game.selectedIndex===null)return;
  const card=CARD_LIBRARY[game.playerDeck.hand[game.selectedIndex]], point=arenaPointFromPointer(ev), snapped=pointInsideArena(point)?snapPointToTile(point,card,TEAM.PLAYER):null; setPlacementPreview(card,snapped,validPlayerPlacement(card,snapped)&&game.aether.player+1e-5>=card.cost);
});
canvas.addEventListener('pointerleave',()=>{if(!dragState?.active)placementPreview.visible=false;});
canvas.addEventListener('pointerdown',(ev)=>{
  if(ev.button!==0)return;
  if(sandbox.spawnArmed&&game.running&&!game.tiebreaker){
    const point=arenaPointFromPointer(ev);if(pointInsideArena(point)){devSpawnAt(point);return;}
  }
  if(!game.running||game.tiebreaker||dragState?.active||game.selectedIndex===null)return;
  const point=arenaPointFromPointer(ev); attemptPlayerDeploy(game.selectedIndex,point);
});

function aiHandEntries(){
  return game.enemyDeck.hand.map((id,i)=>({id,i,c:CARD_LIBRARY[id]}));
}
function aiAffordable(minReserve=0){
  return aiHandEntries().filter(o=>o.c.cost<=game.aether.enemy-minReserve+.001);
}

function aiObservePlayerPlay(card,point){
  const m=game.ai.memory;if(!m||!card)return;
  m.playerAetherEstimate=Math.max(0,(m.playerAetherEstimate??5)-card.cost);
  m.recentSpend=(m.recentSpend||0)+card.cost;
  m.lastObservedPlay=game.elapsed;
  m.playerPlays.push({id:card.id,cost:card.cost,lane:laneForX(point?.x||0),at:m.lastObservedPlay});
  if(m.playerPlays.length>20)m.playerPlays.shift();
  m.playerCycle.push(card.id);if(m.playerCycle.length>12)m.playerCycle.shift();
}
function aiPlayerCardLikelyReady(id){
  const cycle=game.ai.memory?.playerCycle||[];
  const idx=cycle.lastIndexOf(id);
  if(idx<0)return false;
  return cycle.length-1-idx>=4;
}
function aiLikelyCounterReady(cardId){
  const counters=[...new Set(game.ai.memory?.playerCycle||[])].filter(id=>deckAnalyzer.counterScore(id,cardId)>=55);
  return counters.some(aiPlayerCardLikelyReady);
}
function aiDecision(label,reason=''){
  if(!game.ai.memory)return;game.ai.memory.lastDecision=label;game.ai.memory.decisionReason=reason;
  game.recorder?.event(game.elapsed,'ai_decision',{label,reason,style:game.ai.style});
}
function aiPressureOnPlayer(){
  const attackers=units.filter(u=>!u.dead&&u.team===TEAM.ENEMY&&u.group.position.z>1.2);
  let total=0;for(const u of attackers){const depth=THREE.MathUtils.clamp((u.group.position.z-1.5)/12,0,1.3);total+=(u.card?.cost||3)*(.65+depth)*(u.hp/u.maxHp);}return total;
}
function aiCanTradeDamage(snapshot){
  const style=currentAiStyle(),ourPressure=aiPressureOnPlayer();
  if(snapshot.imminent||snapshot.total>5.3)return false;
  if(ourPressure<3.2)return false;
  return ourPressure-snapshot.total>1.1-style.tradeTolerance;
}
function aiTryOppositeLanePunish(snapshot){
  const m=game.ai.memory,style=currentAiStyle(),now=game.elapsed;
  if(!snapshot.threats.length||snapshot.imminent||game.aether.enemy<4.0)return false;
  if((m?.playerAetherEstimate??10)>4.4)return false;
  if(now-(m?.lastPunish||0)<4.5)return false;
  const heavyLane=snapshot.lane,opposite=-heavyLane;
  const entries=aiAffordable(style.reserveAether*.4).filter(o=>!o.c.spell&&!o.c.building);
  const pref=['rambeast','storm_raven','twin_blades','vampire_bats','ironclad','sky_manta'];
  let pick=null;for(const id of pref){pick=entries.find(o=>o.id===id);if(pick)break;}
  if(!pick)return false;
  const x=opposite*(pick.id==='rambeast'?6.9:6.6),z=pick.id==='rambeast'?-11.8:-13.6;
  const spawned=aiPlay(pick,x,z,'punish');
  if(!spawned.length)return false;
  m.lastPunish=now;game.ai.pushLane=opposite;game.ai.anchor=spawned[0]||null;game.ai.supportPlays=0;
  aiDecision('OPPOSITE-LANE PUNISH',`Estimated player Aether ${(m.playerAetherEstimate??0).toFixed(1)} • pressure committed ${heavyLane<0?'left':'right'}`);
  return true;
}
function aiTrySpellCycle(){
  const style=currentAiStyle();if(game.ai.style!=='spell_cycle'&&game.ai.style!=='cycle')return false;
  const now=game.elapsed,m=game.ai.memory;if(now-(m?.lastSpellCycle||0)<2.2)return false;
  const target=towers.filter(t=>!t.dead&&t.team===TEAM.PLAYER).sort((a,b)=>a.hp-b.hp)[0];if(!target)return false;
  const low=target.hp/target.maxHp<.34;if(!low&&game.ai.style!=='spell_cycle')return false;
  const spells=aiAffordable().filter(o=>o.c.spell&&(o.c.towerDamage||0)>0).sort((a,b)=>(b.c.towerDamage/b.c.cost)-(a.c.towerDamage/a.c.cost));
  const spell=spells[0];if(!spell)return false;
  if(!low&&game.aether.enemy<8.8)return false;
  captureBattleState(true);const point=target.group.position.clone();game.aether.enemy-=spell.c.cost;const play=recordCardPlay(TEAM.ENEMY,spell.c,point,game.aether.enemy+spell.c.cost);spawnCard(TEAM.ENEMY,spell.c,point,play);game.enemyDeck.play(spell.i);captureBattleState(true);m.lastSpellCycle=now;
  aiDecision('SPELL CYCLE',`${spell.c.name} into ${target.kind==='core'?'Core':'Guard'} • ${Math.ceil(target.hp)} HP`);return true;
}
function aiDefenseScore(entry,snapshot){
  const c=entry.c,threats=snapshot.threats;let score=0;
  score+=Math.max(0,7-c.cost)*.52;
  if(c.building)score+=snapshot.primary?.buildingsOnly?3.4:1.1;
  if(c.splash||c.id==='arc_mage')score+=Math.max(0,threats.length-1)*1.2;
  const air=threats.filter(u=>u.flying).length,ground=threats.length-air;
  if(air&&c.canHitAir)score+=air*1.15;if(air&&!c.canHitAir&&!c.spell)score-=4;
  if(ground&&c.id==='frost_fang')score+=1.2;if(threats.some(u=>u.buildingsOnly)&&['frost_fang','vampire_bats','archer_tower'].includes(entry.id))score+=1.8;
  if(c.spell){const best=aiBestSpellTarget(c,threats);score+=(best?.value||0)*1.25-c.cost*.25;}
  const answers=threats.map(u=>deckAnalyzer.counterScore(entry.id,u.card.id));
  score+=answers.reduce((sum,value)=>sum+value/100,0)*2.4;
  if(answers.length&&!answers.some(value=>value>0)&&!c.building)score-=12;
  score-=c.cost*.18;return score;
}
function aiBestDefenseEntry(snapshot){
  return aiAffordable().filter(o=>!o.c.spell&&snapshot.threats.some(u=>deckAnalyzer.counterScore(o.id,u.card.id)>0)).map(entry=>({entry,score:aiDefenseScore(entry,snapshot)})).sort((a,b)=>b.score-a.score||a.entry.c.cost-b.entry.c.cost)[0]?.entry||null;
}
function aiThreatSnapshot(){
  const threats=units.filter(u=>!u.dead&&u.team===TEAM.PLAYER&&u.group.position.z<-.35);
  const laneScores={[-1]:0,[1]:0}; let total=0, imminent=false, primary=null, best=-1;
  for(const u of threats){
    const depth=THREE.MathUtils.clamp((-u.group.position.z-1.5)/12,0,1.35);
    const towerBias=u.buildingsOnly?1.45:1;
    const hpFactor=.55+.45*(u.hp/u.maxHp);
    const value=(u.card.cost||3)*(.65+depth*1.45)*towerBias*hpFactor;
    laneScores[u.lane]=(laneScores[u.lane]||0)+value; total+=value;
    if(u.group.position.z<-8.0||u.target instanceof Tower&&horizontalDistance(u,u.target)<5.6) imminent=true;
    if(value>best){best=value;primary=u;}
  }
  const lane=laneScores[-1]>laneScores[1]?-1:1;
  return {threats,laneScores,total,lane,imminent,primary};
}
function aiChooseAttackLane(){
  const playerGuards=towers.filter(t=>!t.dead&&t.team===TEAM.PLAYER&&t.kind==='guard');
  if(!playerGuards.length)return Math.random()<.5?-1:1;
  playerGuards.sort((a,b)=>(a.hp/a.maxHp)-(b.hp/b.maxHp));
  const weak=playerGuards[0], other=playerGuards[1];
  if(!other)return weak.lane;
  // Mostly pressure the weaker guard, but don't become completely deterministic.
  return (weak.hp/weak.maxHp)+.13<(other.hp/other.maxHp)||Math.random()<.78?weak.lane:other.lane;
}
function currentAiStyle(){ return AI_STYLES[game.ai.style] || AI_STYLES.beatdown; }
function randomAiStyle(){ const keys = Object.keys(AI_STYLES); return keys[Math.floor(Math.random()*keys.length)]; }
function setAiStyle(style){ game.ai.style = AI_STYLES[style] ? style : 'beatdown'; }
function configureAiDeck(){
  const ids=deckAnalyzer.buildAiDeck(game.ai.style);
  game.enemyDeck=new DeckState(shuffledCardPool(ids));
  game.ai.deckProfile=deckAnalyzer.analyzeDeck(ids);
  game.ai.anchor=null;game.ai.phase='bank';game.ai.supportPlays=0;
  // This omniscient report is only rendered in the private Developer Lab. AI decisions never consume it.
  game.ai.debugMatchup=deckAnalyzer.matchup(ids,game.battleDeck||activeDeck);
  renderDeveloperMatchup();
}
function renderDeveloperMatchup(){
  const panel=document.querySelector('#dev-matchup-report');if(!panel)return;
  panel.replaceChildren();
  if(!game.started||!game.ai.debugMatchup)return;
  const report=game.ai.debugMatchup,profile=game.ai.deckProfile;
  const line=(label,value)=>{const p=document.createElement('p');const strong=document.createElement('strong');strong.textContent=label;p.append(strong,document.createTextNode(` ${value}`));panel.append(p);};
  line('AI deck:',profile.profile);
  line('AI cards:',profile.ids.map(id=>CARD_LIBRARY[id].name).join(', '));
  line('Projected advantage:',`${report.label} (${report.score>0?'+':''}${report.score}; AI perspective)`);
  line('Lane plan:',report.laneStrategy);
  line('Threat cards:',report.threats.slice(0,3).map(t=>CARD_LIBRARY[t.id].name).join(', ')||'No pronounced threat');
  line('Available counters:',report.counters.slice(0,3).map(c=>`${CARD_LIBRARY[c.id].name} → ${CARD_LIBRARY[c.againstId].name}`).join('; ')||'Use coordinated defense');
  line('Strengths:',profile.strengths.join('; ')||'Balanced coverage');line('Weaknesses:',profile.weaknesses.join('; ')||'No critical coverage gap');
  line('Active win conditions:',profile.winConditions.map(id=>CARD_LIBRARY[id].name).join(', '));
  line('Current hand:',game.enemyDeck.hand.map(id=>CARD_LIBRARY[id].name).join(', '));
  line('Next cycle:',game.enemyDeck.queue.map(id=>CARD_LIBRARY[id].name).join(' → '));
  line('Model:',report.disclaimer);
}
function aiPlay(entry,x,z,phase='play'){
  if(!entry||entry.c.cost>game.aether.enemy+.001)return [];
  let point=snapPointToTile(new THREE.Vector3(THREE.MathUtils.clamp(x,-12.2,12.2),0,THREE.MathUtils.clamp(z,-ARENA.length/2+1.2,ARENA.length/2-1.2)),entry.c,TEAM.ENEMY);
  // Enemy deployment obeys the exact same side/pocket rules as the player.
  // Invalid tactical coordinates fall back to a legal backfield tile rather than cheating.
  if(!validTeamPlacement(entry.c,point,TEAM.ENEMY)){
    point=snapPointToTile(new THREE.Vector3(THREE.MathUtils.clamp(x,-12.2,12.2),0,THREE.MathUtils.clamp(z,-15.2,-ARENA.riverHalf-.55)),entry.c,TEAM.ENEMY);
  }
  if(!validTeamPlacement(entry.c,point,TEAM.ENEMY))return [];
  captureBattleState(true);game.aether.enemy-=entry.c.cost;
  const play=recordCardPlay(TEAM.ENEMY,entry.c,point,game.aether.enemy+entry.c.cost);
  const spawned=spawnCard(TEAM.ENEMY,entry.c,point,play);
  game.enemyDeck.play(entry.i);captureBattleState(true); game.ai.lastPlay=game.elapsed; game.ai.planAge=0; game.ai.phase=phase; return spawned||[];
}
function aiBestSpellTarget(spell,threats){
  let best=null;const radius=(spell.radius||0)*GRID.tile;
  for(const u of threats){
    let count=0,value=0;
    for(const v of threats){
      if(horizontalDistance(u,v)<=radius){count++;const potential=spell.damage+(spell.dotDamage||0)*(spell.dotDuration||0)*.72;value+=(v.card?.cost||3)*Math.min(1,potential/Math.max(1,v.hp));}
    }
    const nearbyTower=towers.find(t=>!t.dead&&t.team===TEAM.ENEMY&&Math.hypot(t.group.position.x-u.group.position.x,t.group.position.z-u.group.position.z)<radius+2);
    if(nearbyTower)value+=.7;
    value-=spell.cost*.18;
    if(!best||value>best.value)best={unit:u,count,value};
  }
  return best;
}
function aiTryDefense(snapshot){
  if(!snapshot.threats.length)return false;
  const serious=snapshot.imminent||snapshot.total>=4.4/Math.max(.65,currentAiStyle().defendBias||1);
  if(!serious)return false;
  const affordable=aiAffordable(); if(!affordable.length)return false;
  const spellOptions=affordable.filter(o=>o.c.spell).map(entry=>({entry,best:aiBestSpellTarget(entry.c,snapshot.threats)})).filter(x=>x.best).sort((a,b)=>b.best.value-a.best.value);
  const spellPick=spellOptions[0];
  if(spellPick&&((spellPick.best.count>=2&&spellPick.best.value>=2.35)||spellPick.best.count>=3||(spellPick.entry.id==='bullet_burst'&&spellPick.best.value>=1.65))){
    captureBattleState(true);const point=spellPick.best.unit.group.position.clone();game.aether.enemy-=spellPick.entry.c.cost;const play=recordCardPlay(TEAM.ENEMY,spellPick.entry.c,point,game.aether.enemy+spellPick.entry.c.cost);spawnCard(TEAM.ENEMY,spellPick.entry.c,point,play);game.enemyDeck.play(spellPick.entry.i);captureBattleState(true); game.ai.phase='defend'; game.ai.lastDefense=game.elapsed; aiDecision('SPELL DEFENSE',`${spellPick.entry.c.name} • ${spellPick.best.count} targets`); return true;
  }
  const laneThreats=snapshot.threats.filter(u=>u.lane===snapshot.lane);
  const hasTank=laneThreats.some(u=>u.buildingsOnly||u.hp>850);
  let entry=null;
  const defensiveBuilding=affordable.find(o=>o.id==='archer_tower');
  if(defensiveBuilding&&(hasTank||snapshot.total>=6.0)){
    const lane=snapshot.lane||1; const x=lane*7.0; const z=THREE.MathUtils.clamp((snapshot.primary?.group.position.z??-6)-3.4,-12.8,-4.0);
    const point=snapPointToTile(new THREE.Vector3(x,0,z),defensiveBuilding.c,TEAM.ENEMY);
    if(validTeamPlacement(defensiveBuilding.c,point,TEAM.ENEMY)){aiPlay(defensiveBuilding,point.x,point.z,'defend');aiDecision('BUILDING DEFENSE',`Archer Tower • ${hasTank?'tank pull':'heavy pressure'}`);return true;}
  }
  entry=aiBestDefenseEntry(snapshot);
  if(!entry)return false;
  const primary=snapshot.primary||laneThreats[0]; const lane=primary?.lane||snapshot.lane;
  const threatZ=primary?.group.position.z??-5; const x=lane*7.0+(Math.random()-.5)*1.15;
  const z=THREE.MathUtils.clamp(threatZ-2.35,-13.6,-3.35);
  const played=aiPlay(entry,x,z,'defend');
  if(played.length||entry.c.spell)aiDecision('DEFEND',`${entry.c.name} • threat ${snapshot.total.toFixed(1)} • lane ${lane<0?'left':'right'}`);
  return !!played.length;
}
function aiPocketDeployPoint(lane, card, depth='front'){
  if(!isPocketUnlocked(TEAM.ENEMY,lane))return null;
  const x=lane*(POCKET.xInner + (POCKET.xOuter-POCKET.xInner)*(depth==='deep'?.62:.42));
  const z=depth==='deep' ? POCKET.zFar-1.0 : POCKET.zNear+1.0;
  const point=snapPointToTile(new THREE.Vector3(x,0,z),card,TEAM.ENEMY);
  return validTeamPlacement(card,point,TEAM.ENEMY)?point:null;
}
function aiShouldUsePocket(style,lane){
  if(!isPocketUnlocked(TEAM.ENEMY,lane))return false;
  const chance=game.ai.style==='aggro'?.72:game.ai.style==='control'?.46:.36;
  return Math.random()<chance;
}

function aiStartPush(force=false){
  const entries=aiAffordable(); if(!entries.length)return false;
  const lane=aiChooseAttackLane();
  const style = currentAiStyle();
  let tank=null;
  const winConditions=game.ai.deckProfile?.winConditions||[];
  const prioritized=entries.filter(o=>!o.c.spell&&!o.c.building).map(entry=>{
    const priority=style.pushPriority.indexOf(entry.id);
    return {entry,score:(priority<0?0:style.pushPriority.length-priority)+(winConditions.includes(entry.id)?3:0)-(aiLikelyCounterReady(entry.id)?5:0)};
  }).sort((a,b)=>b.score-a.score).map(o=>o.entry);
  tank=prioritized.find(o=>!aiLikelyCounterReady(o.id))||prioritized[0]||null;
  if(!tank&&force) tank=entries.find(o=>o.id==='ironclad')||entries.filter(o=>!o.c.spell&&!o.c.building).sort((a,b)=>b.c.hp-a.c.hp)[0];
  if(!tank)return false;
  let x=lane*(tank.id==='rambeast'?6.9:6.5)+(Math.random()-.5)*.55;
  let z=tank.id==='rambeast'?-13.1:tank.id==='boulderback'?-14.65:-14.0;
  // Once a Guard Tower is down, AI can use the same unlocked pocket the player gets.
  // Fast pressure cards use it more often; Beatdown still prefers back-building most pushes.
  if(aiShouldUsePocket(style,lane) && tank.id!=='boulderback'){
    const pocket=aiPocketDeployPoint(lane,tank.c,tank.id==='rambeast'?'front':'deep');
    if(pocket){x=pocket.x;z=pocket.z;}
  }
  const spawned=aiPlay(tank,x,z,'support');
  game.ai.pushLane=lane; game.ai.anchor=spawned[0]||null; game.ai.supportPlays=0; return true;
}
function aiTrySupport(){
  const anchor=game.ai.anchor;
  if(!anchor||anchor.dead){game.ai.phase='bank';game.ai.anchor=null;game.ai.supportPlays=0;return false;}
  const style = currentAiStyle();
  if(game.ai.supportPlays>=style.supportLimit&&anchor.group.position.z>1.2){game.ai.phase='bank';return false;}
  const entries=aiAffordable(); if(!entries.length)return false;
  const enemyDefenders=units.filter(u=>!u.dead&&u.team===TEAM.PLAYER&&u.lane===game.ai.pushLane&&Math.abs(u.group.position.z-anchor.group.position.z)<8);
  const spellChoices=entries.filter(o=>o.c.spell).map(entry=>({entry,best:aiBestSpellTarget(entry.c,enemyDefenders)})).filter(x=>x.best).sort((a,b)=>b.best.value-a.best.value);
  const spell=spellChoices[0]?.entry;
  if(spell&&enemyDefenders.length>=(spell.id==='bullet_burst'?1:2)&&anchor.group.position.z>-3.5){
    let sx=0,sz=0; for(const u of enemyDefenders){sx+=u.group.position.x;sz+=u.group.position.z;} sx/=enemyDefenders.length;sz/=enemyDefenders.length;
    captureBattleState(true);const point=new THREE.Vector3(sx,0,sz);game.aether.enemy-=spell.c.cost;const play=recordCardPlay(TEAM.ENEMY,spell.c,point,game.aether.enemy+spell.c.cost);spawnCard(TEAM.ENEMY,spell.c,point,play);game.enemyDeck.play(spell.i);captureBattleState(true);game.ai.supportPlays++;return true;
  }
  let support=null;
  support=entries.filter(o=>!o.c.buildingsOnly&&!o.c.spell&&!o.c.building).map(entry=>{
    const priority=style.supportPriority.indexOf(entry.id),pair=deckAnalyzer.synergy(anchor.card.id,entry.id);
    const coverage=enemyDefenders.reduce((sum,u)=>sum+deckAnalyzer.counterScore(entry.id,u.card.id)/100,0);
    return {entry,score:pair.score/15+coverage*2+(priority<0?0:(style.supportPriority.length-priority)*.35)};
  }).sort((a,b)=>b.score-a.score)[0]?.entry||null;
  if(!support||support.c.buildingsOnly||support.c.spell||support.c.building)return false;
  const reserve=anchor.group.position.z<-5.5?style.reserveAether:0;
  if(support.c.cost>game.aether.enemy-reserve+.001)return false;
  let x=game.ai.pushLane*6.8+(Math.random()-.5)*1.0;
  let z=THREE.MathUtils.clamp(anchor.group.position.z-2.5,-14.7,-3.5);
  if(anchor.group.position.z>5.5 && isPocketUnlocked(TEAM.ENEMY,game.ai.pushLane)){
    const pocket=aiPocketDeployPoint(game.ai.pushLane,support.c,'deep');
    if(pocket){x=pocket.x;z=pocket.z;}
  }
  aiPlay(support,x,z,'support'); game.ai.supportPlays++; return true;
}
function aiOverflowCycle(){
  const entries=aiAffordable().filter(o=>!o.c.spell&&!o.c.building);
  if(!entries.length)return false;
  const style = currentAiStyle();
  let pick = null;
  for(const id of style.overflow){ pick = entries.find(o=>o.id===id); if(pick) break; }
  const cheap=(pick||entries.sort((a,b)=>a.c.cost-b.c.cost)[0]); const lane=aiChooseAttackLane();
  const pocket=game.ai.style==='aggro'?aiPocketDeployPoint(lane,cheap.c,'front'):null;
  if(pocket)aiPlay(cheap,pocket.x,pocket.z,'bank');
  else aiPlay(cheap,lane*6.7+(Math.random()-.5)*.7,-14.5,'bank');
  return true;
}
function aiTryCounterpush(){
  const minAether=game.ai.style==='counter'?4.5:game.ai.style==='control'?5.5:6.5;
  if(game.aether.enemy<minAether)return false;
  const minHp=game.ai.style==='counter'?.26:.38;
  const survivors=units.filter(u=>!u.dead&&u.team===TEAM.ENEMY&&u.hp/u.maxHp>minHp&&u.group.position.z>-11.5&&u.group.position.z<1.2&&!u.buildingsOnly);
  if(!survivors.length)return false;
  survivors.sort((a,b)=>(b.hp/b.maxHp)-(a.hp/a.maxHp));
  game.ai.anchor=survivors[0]; game.ai.pushLane=survivors[0].lane; game.ai.supportPlays=0; game.ai.phase='support';
  aiDecision('COUNTER-PUSH',`${survivors[0].card?.name||'Survivor'} retained ${Math.round(survivors[0].hp/survivors[0].maxHp*100)}% HP`);
  return aiTrySupport();
}
function aiUpdate(dt){
  if(!game.ai.enabled)return;
  game.aiThink-=dt; game.ai.planAge+=dt; if(game.aiThink>0||!game.running||game.tiebreaker)return; game.aiThink=.22+Math.random()*.20;
  const snapshot=aiThreatSnapshot();
  const timeLeft=game.overtime?game.overtimeTime:game.time;
  const behind=game.crowns.enemy<game.crowns.player;
  const ahead=game.crowns.enemy>game.crowns.player;
  const style=currentAiStyle(),m=game.ai.memory;

  // Finishable tower damage has priority for cycle-style brains.
  if(aiTrySpellCycle())return;

  // A large commitment plus a low estimated player Aether pool can be punished opposite lane.
  if((style.punishBias||0)>.65&&aiTryOppositeLanePunish(snapshot))return;

  // Aggressive brains can intentionally trade a small amount of tower damage when their own push is more dangerous.
  const trade=aiCanTradeDamage(snapshot);
  if(!trade&&aiTryDefense(snapshot))return;
  if(trade&&snapshot.threats.length)aiDecision('DAMAGE TRADE',`Enemy threat ${snapshot.total.toFixed(1)} < our pressure ${aiPressureOnPlayer().toFixed(1)}`);

  if(game.ai.phase==='defend'&&!snapshot.threats.length&&aiTryCounterpush())return;

  if(game.ai.phase==='support'&&aiTrySupport()){aiDecision('SUPPORT PUSH',`${game.ai.anchor?.card?.name||'Anchor'} • ${game.ai.supportPlays} support plays`);return;}
  if(game.ai.phase==='support'&&game.ai.anchor&&!game.ai.anchor.dead&&game.aether.enemy<Math.max(5.6, style.desperationThreshold+.4))return;

  // Split-lane brain deliberately avoids stacking everything behind the same anchor.
  if(game.ai.style==='split'&&game.aether.enemy>=7.0&&Math.random()<.42){
    const opposite=-(game.ai.pushLane||aiChooseAttackLane());
    const entries=aiAffordable(1).filter(o=>!o.c.spell&&!o.c.building);
    const pick=entries.find(o=>['rambeast','vampire_bats','twin_blades','sky_manta','ironclad'].includes(o.id));
    if(pick){const spawned=aiPlay(pick,opposite*6.6,-12.6,'split');if(spawned.length){aiDecision('SPLIT-LANE PRESSURE',`${pick.c.name} opposite ${opposite<0?'left':'right'}`);return;}}
  }

  if(ahead&&timeLeft<42&&!game.overtime){
    if(game.aether.enemy>=9.92){aiOverflowCycle();aiDecision('SAFE CYCLE','Leading late • avoiding overflow');}
    else aiDecision('HOLD LEAD',`Reserve ${game.aether.enemy.toFixed(1)} Aether`);
    return;
  }

  if(game.aether.enemy>=style.bankThreshold){
    const before=game.aether.enemy;if(aiStartPush(true)){
      const counterRisk=game.ai.anchor?.card?.id&&aiLikelyCounterReady(game.ai.anchor.card.id);
      aiDecision('START PUSH',`${game.ai.anchor?.card?.name||'Pressure'} • ${game.ai.pushLane<0?'left':'right'}${counterRisk?' • counter likely ready':''}`);return;
    }
    if(before>=9.85&&aiOverflowCycle()){aiDecision('OVERFLOW CYCLE','At Aether cap');return;}
  }

  if(((behind&&timeLeft<38)||game.overtime)&&game.aether.enemy>=style.desperationThreshold){
    if(aiStartPush(true)){aiDecision('DESPERATION PUSH',`${timeLeft.toFixed(0)}s left`);return;}
  }

  game.ai.phase='bank';aiDecision('BANK',`${game.aether.enemy.toFixed(1)} Aether • player est ${(m?.playerAetherEstimate??0).toFixed(1)}`);
}

function updateAether(dt){
  if(game.tiebreaker)return;
  // 0:00-2:00 elapsed = 1x, regulation final minute + first OT minute = 2x,
  // final OT minute = 3x. This preserves the requested 2.8s / 1.4s / 0.933s cadence.
  let mult=1;
  if(game.overtime) mult=game.overtimeTime<=60?3:2;
  else if(game.time<=60) mult=2;
  const rate=mult/MATCH.baseAetherSeconds;
  if(game.recorder){for(const team of [TEAM.PLAYER,TEAM.ENEMY])game.leakPending[team]+=Math.max(0,game.aether[team]+rate*dt-10);}
  game.aether.player=Math.min(10,game.aether.player+rate*dt); game.aether.enemy=Math.min(10,game.aether.enemy+rate*dt);
  if(game.ai.memory){
    game.ai.memory.playerAetherEstimate=Math.min(10,(game.ai.memory.playerAetherEstimate??5)+rate*dt);
    game.ai.memory.recentSpend=Math.max(0,(game.ai.memory.recentSpend||0)-dt*.55);
  }
  ui.aether.textContent=game.aether.player.toFixed(1); ui.aetherFill.style.width=`${game.aether.player*10}%`;
  document.querySelectorAll('.card').forEach((el,i)=>{const c=CARD_LIBRARY[game.playerDeck.hand[i]];el.classList.toggle('disabled',game.aether.player<c.cost);});
  if(mult!==game.aetherMultiplier){
    game.aetherMultiplier=mult;
    if(ui.phaseLabel)ui.phaseLabel.textContent=game.overtime?`OVERTIME • ${mult}× AETHER`:(mult===1?'REGULATION':`${mult}× AETHER`);
    if(mult>1){ui.doubleAether.textContent=`${mult}× AETHER`;ui.doubleAether.classList.remove('hidden');setTimeout(()=>ui.doubleAether.classList.add('hidden'),1750);}
  }
}

function formatTime(sec){sec=Math.max(0,Math.ceil(sec));return `${Math.floor(sec/60)}:${String(sec%60).padStart(2,'0')}`;}
function updateTimer(dt){
  if(!game.running||game.tiebreaker)return;
  if(!game.overtime){
    game.time-=dt;ui.timer.textContent=formatTime(game.time);
    if(game.time<=0){
      game.time=0;
      // Regulation is exactly 3:00. The extra 2:00 is only played if regulation ends 0-0.
      if(game.crowns.player===0&&game.crowns.enemy===0){
        game.overtime=true;game.overtimeTime=MATCH.overtime;
        game.recorder?.event(game.elapsed,'phase',{phase:'overtime'});
        ui.phaseLabel.textContent='OVERTIME • 2× AETHER';ui.timer.textContent='OT 2:00';
        ui.doubleAether.textContent='OVERTIME';ui.doubleAether.classList.remove('hidden');setTimeout(()=>ui.doubleAether.classList.add('hidden'),1750);
        toast('0–0 after regulation — 2:00 sudden-death overtime.');
      }else if(game.crowns.player!==game.crowns.enemy){
        finishMatch(game.crowns.player>game.crowns.enemy?TEAM.PLAYER:TEAM.ENEMY,false);
      }else{
        // If both sides already took the same number of towers, regulation still ends at 3:00.
        finishMatch(null,false);
      }
    }
  } else {
    game.overtimeTime-=dt;ui.timer.textContent=`OT ${formatTime(game.overtimeTime)}`;
    if(game.overtimeTime<=0){
      game.overtimeTime=0;
      if(game.crowns.player===0&&game.crowns.enemy===0)beginTiebreaker();
      else if(game.crowns.player!==game.crowns.enemy)finishMatch(game.crowns.player>game.crowns.enemy?TEAM.PLAYER:TEAM.ENEMY,false);
      else finishMatch(null,false);
    }
  }
}
function clearLiveProjectiles(){
  for(const p of projectiles)if(p.mesh){scene.remove(p.mesh);disposeObject(p.mesh);}projectiles.length=0;
}
function beginTiebreaker(){
  if(game.tiebreaker||!game.running)return;
  game.tiebreaker=true;game.overtime=false;game.tiebreakTime=0;game.selectedIndex=null;placementPreview.visible=false;resetSelectionUi();renderHand();clearLiveProjectiles();
  game.recorder?.event(game.elapsed,'phase',{phase:'tiebreaker'});
  const pT=towers.filter(t=>!t.dead&&t.team===TEAM.PLAYER),eT=towers.filter(t=>!t.dead&&t.team===TEAM.ENEMY);
  game.tiebreakSnapshot={
    playerMin:Math.min(...pT.map(t=>t.hp)),enemyMin:Math.min(...eT.map(t=>t.hp)),
    playerTotal:pT.reduce((a,t)=>a+t.hp,0),enemyTotal:eT.reduce((a,t)=>a+t.hp,0),
  };
  for(const u of units)u.target=null;for(const b of buildings)b.target=null;for(const t of towers)t.target=null;
  ui.phaseLabel.textContent='TIEBREAKER';ui.timer.textContent='TB';ui.doubleAether.textContent='TIEBREAKER';ui.doubleAether.classList.remove('hidden');
  toast('Tiebreaker — all Crown Tower HP is draining. Lowest tower loses.');
}
function tiebreakResolveSimultaneous(playerDown,enemyDown){
  if(playerDown&&!enemyDown)return TEAM.ENEMY;if(enemyDown&&!playerDown)return TEAM.PLAYER;
  const s=game.tiebreakSnapshot||{};
  if((s.playerMin??0)!==(s.enemyMin??0))return (s.playerMin??0)>(s.enemyMin??0)?TEAM.PLAYER:TEAM.ENEMY;
  if((s.playerTotal??0)!==(s.enemyTotal??0))return (s.playerTotal??0)>(s.enemyTotal??0)?TEAM.PLAYER:TEAM.ENEMY;
  return Math.random()<.5?TEAM.PLAYER:TEAM.ENEMY;
}
function updateTiebreaker(dt){
  if(!game.tiebreaker||!game.running)return;
  game.tiebreakTime+=dt;if(game.tiebreakTime<.85)return;
  const drain=MATCH.tiebreakDrainPerSecond*dt;
  for(const t of towers){if(!t.dead){t.hp=Math.max(0,t.hp-drain);updateTowerHud(t);}}
  const pDown=towers.some(t=>t.team===TEAM.PLAYER&&!t.dead&&t.hp<=0),eDown=towers.some(t=>t.team===TEAM.ENEMY&&!t.dead&&t.hp<=0);
  if(!pDown&&!eDown)return;
  const winner=tiebreakResolveSimultaneous(pDown,eDown),loser=winner===TEAM.PLAYER?TEAM.ENEMY:TEAM.PLAYER;
  const fallen=towers.filter(t=>t.team===loser&&!t.dead&&t.hp<=0);
  for(const [index,t] of fallen.entries()){t.dead=true;t.hp=0;game.recorder?.event(game.elapsed,'tower_destroy',{targetId:t._replayId,targetTeam:t.team,crownsAwarded:index===0?1:0,reason:'tiebreaker'});updateTowerHud(t);createTowerCollapse(t.group.position.clone(),t.team,t.kind==='core');t.group.visible=false;}
  game.crowns.player=winner===TEAM.PLAYER?1:0;game.crowns.enemy=winner===TEAM.ENEMY?1:0;updateScore();
  finishMatch(winner,false);
}

function updateScore(){ui.playerCrowns.textContent=game.crowns.player;ui.enemyCrowns.textContent=game.crowns.enemy;game.recorder?.event(game.elapsed,'crowns',{...game.crowns});}
function finishMatch(winner,coreKill=false){
  if(!game.running)return; game.running=false; game.winner=winner; recordMatchResult(winner); if(ui.phaseLabel)ui.phaseLabel.textContent=game.tiebreaker?'TIEBREAKER':'MATCH COMPLETE';
  if(game.recorder)completeBattleRecording({winner,crowns:{...game.crowns},coreKill});game.metaLab?.setBattleRunning(false);
  if(winner===TEAM.PLAYER)ui.endTitle.textContent=coreKill?'CORE BREAK!':'VICTORY';else if(winner===TEAM.ENEMY)ui.endTitle.textContent=coreKill?'CORE LOST':'DEFEAT';else ui.endTitle.textContent='DRAW';
  ui.endScore.textContent=`${game.crowns.player} — ${game.crowns.enemy}`; setTimeout(()=>ui.end.classList.remove('hidden'),550);
}

function worldSizeForPixels(worldPos,pixels){
  const distance=Math.max(1,camera.position.distanceTo(worldPos));
  const visibleHeight=2*distance*Math.tan(THREE.MathUtils.degToRad(camera.fov*.5));
  return visibleHeight*(pixels/Math.max(480,canvas.clientHeight||window.innerHeight));
}
function updateHealthBarFacing(){
  for(const e of units){
    if(e.dead)continue;
    if(e.hpSprite){
      e.hpSprite.quaternion.copy(camera.quaternion);
      const w=worldSizeForPixels(e.group.position,92);
      e.hpSprite.scale.set(w,w*(48/360),1);
    }
  }
  for(const e of towers){
    if(e.dead)continue;
    if(e.hpSprite){
      e.hpSprite.quaternion.copy(camera.quaternion);
      const px=e.kind==='core'?154:142;
      const w=worldSizeForPixels(e.group.position,px);
      e.hpSprite.scale.set(w,w*(160/640),1);
    }
  }
  for(const e of buildings){
    if(e.dead)continue;
    if(e.hpSprite){
      e.hpSprite.quaternion.copy(camera.quaternion);
      const w=worldSizeForPixels(e.group.position,100);
      e.hpSprite.scale.set(w,w*(48/360),1);
    }
  }
}


function devTowerKey(t){
  const side=t.team===TEAM.PLAYER?'P':'E',slot=t.kind==='core'?'CORE':t.lane<0?'L':'R';return `${side}_${slot}`;
}
function initDeveloperLab(){
  if(ui.devSpawnCard){
    ui.devSpawnCard.innerHTML=ALL_CARD_IDS.map(id=>`<option value="${id}">${CARD_LIBRARY[id].name}</option>`).join('');
    ui.devSpawnCard.value=sandbox.spawnCard;
  }
  if(ui.devAiStyle){ui.devAiStyle.innerHTML=Object.entries(AI_STYLES).map(([id,st])=>`<option value="${id}">${st.name}</option>`).join('');ui.devAiStyle.value=game.ai.style;}
  refreshDevTowerSelect();
}
function refreshDevTowerSelect(){
  if(!ui.devTowerSelect)return;
  const previous=ui.devTowerSelect.value;
  ui.devTowerSelect.innerHTML=towers.map(t=>`<option value="${devTowerKey(t)}">${t.team===TEAM.PLAYER?'You':'Enemy'} • ${t.kind==='core'?'Core':t.lane<0?'Left Guard':'Right Guard'}</option>`).join('');
  if([...ui.devTowerSelect.options].some(o=>o.value===previous))ui.devTowerSelect.value=previous;
  syncDevTowerHpInput();
}
function syncDevTowerHpInput(){
  if(!ui.devTowerSelect||!ui.devTowerHp)return;const t=towers.find(x=>devTowerKey(x)===ui.devTowerSelect.value);if(t)ui.devTowerHp.value=Math.ceil(t.hp);
}
function setSandboxSpeed(v){
  sandbox.speed=Math.max(0,Math.min(4,Number(v)||0));
  document.querySelectorAll('[data-dev-speed]').forEach(b=>b.classList.toggle('active',Number(b.dataset.devSpeed)===sandbox.speed));
  toast(sandbox.speed===0?'Simulation paused':`Simulation ${sandbox.speed}×`);
}
function devModifyAether(team,amount){
  captureBattleState(true);const previous=game.aether[team];
  if(amount===10)game.aether[team]=10;else game.aether[team]=THREE.MathUtils.clamp(game.aether[team]+amount,0,10);
  game.recorder?.event(game.elapsed,'aether_grant',{team,amount:game.aether[team]-previous,aether:game.aether[team],sandbox:true});captureBattleState(true);
  if(team===TEAM.PLAYER){ui.aether.textContent=game.aether.player.toFixed(1);ui.aetherFill.style.width=`${game.aether.player*10}%`;}
}
function armDeveloperSpawn(){
  sandbox.spawnCard=ui.devSpawnCard?.value||'ironclad';sandbox.spawnTeam=ui.devSpawnTeam?.value===TEAM.ENEMY?TEAM.ENEMY:TEAM.PLAYER;sandbox.spawnArmed=!sandbox.spawnArmed;
  document.body.classList.toggle('dev-spawn-armed',sandbox.spawnArmed);if(ui.devArmSpawn)ui.devArmSpawn.textContent=sandbox.spawnArmed?'ARMED • CLICK ARENA':'ARM SPAWN • CLICK A TILE';
  if(sandbox.spawnArmed){game.selectedIndex=null;placementPreview.visible=false;renderHand();toast(`Sandbox: place ${CARD_LIBRARY[sandbox.spawnCard].name}`);}
}
function devSpawnAt(point){
  const card=CARD_LIBRARY[sandbox.spawnCard];if(!card)return;
  const snapped=snapPointToTile(point,card,sandbox.spawnTeam),play=recordCardPlay(sandbox.spawnTeam,card,snapped,game.aether[sandbox.spawnTeam],true);spawnCard(sandbox.spawnTeam,card,snapped,play);
  sandbox.spawnArmed=false;document.body.classList.remove('dev-spawn-armed');if(ui.devArmSpawn)ui.devArmSpawn.textContent='ARM SPAWN • CLICK A TILE';
  toast(`Spawned ${sandbox.spawnTeam===TEAM.PLAYER?'friendly':'enemy'} ${card.name}`);
}
function applyDeveloperTowerHp(){
  const t=towers.find(x=>devTowerKey(x)===ui.devTowerSelect?.value);if(!t||t.dead)return toast('That tower is already destroyed');
  const hp=THREE.MathUtils.clamp(Number(ui.devTowerHp?.value)||0,0,t.maxHp);
  if(hp<=0){t.takeDamage(t.hp+1);return;}
  t.hp=hp;game.recorder?.event(game.elapsed,'tower_edit',{targetId:t._replayId,hp,sandbox:true});updateTowerHud(t);toast(`${t.kind==='core'?'Core':'Guard'} set to ${Math.ceil(hp)} HP`);
}
function clearDeveloperField(){
  game.recorder?.event(game.elapsed,'clear_field',{sandbox:true});
  for(const u of units){u.dead=true;if(u.group){scene.remove(u.group);disposeObject(u.group);}}units.length=0;
  for(const b of buildings){b.dead=true;if(b.group){scene.remove(b.group);disposeObject(b.group);}}buildings.length=0;
  clearLiveProjectiles();hazards.length=0;
  for(const e of effects){if(e.mesh){scene.remove(e.mesh);disposeObject(e.mesh);}}effects.length=0;
  game.ai.anchor=null;game.ai.phase='bank';clearDebugDynamic();toast('Sandbox battlefield cleared');
}
function setAiEnabled(enabled){game.ai.enabled=enabled;game.aiThink=0;if(ui.devAiToggle)ui.devAiToggle.textContent=enabled?'AI ON':'AI OFF';aiDecision(enabled?'AI ENABLED':'AI DISABLED',currentAiStyle().name);}
function clearDebugDynamic(){
  while(debugOverlayGroup.children.length){const o=debugOverlayGroup.children[debugOverlayGroup.children.length-1];debugOverlayGroup.remove(o);o.geometry?.dispose?.();if(Array.isArray(o.material))o.material.forEach(m=>m.dispose?.());else o.material?.dispose?.();}
}
function debugLine(points,color=0xffffff,opacity=.9){
  if(points.length<2)return;const g=new THREE.BufferGeometry().setFromPoints(points);const m=new THREE.LineBasicMaterial({color,transparent:true,opacity,depthTest:false});const line=new THREE.Line(g,m);line.renderOrder=60;debugOverlayGroup.add(line);
}
function debugCircle(center,radius,color,opacity=.45,segments=48){
  const pts=[];for(let i=0;i<=segments;i++){const a=i/segments*Math.PI*2;pts.push(new THREE.Vector3(center.x+Math.cos(a)*radius,.18,center.z+Math.sin(a)*radius));}debugLine(pts,color,opacity);
}
function debugSightArc(unit,radius,front,color){
  let dir=new THREE.Vector3(0,0,unit.team===TEAM.PLAYER?-1:1);if(unit.target&&!unit.target.dead){dir.copy(unit.target.group.position).sub(unit.group.position).setY(0);if(dir.lengthSq()>.001)dir.normalize();}
  const base=Math.atan2(dir.z,dir.x),center=front?base:base+Math.PI,pts=[];for(let i=0;i<=28;i++){const a=center-Math.PI/2+i/28*Math.PI;pts.push(new THREE.Vector3(unit.group.position.x+Math.cos(a)*radius,.20,unit.group.position.z+Math.sin(a)*radius));}debugLine(pts,color,.48);
}
function makeDebugLabel(textValue,pos){
  const c=document.createElement('canvas');c.width=128;c.height=48;const x=c.getContext('2d');x.fillStyle='rgba(4,10,18,.76)';x.fillRect(0,4,128,40);x.fillStyle='#bfeeff';x.font='800 18px Arial';x.textAlign='center';x.textBaseline='middle';x.fillText(textValue,64,24);const tex=new THREE.CanvasTexture(c);tex.colorSpace=THREE.SRGBColorSpace;const sp=new THREE.Sprite(new THREE.SpriteMaterial({map:tex,transparent:true,depthTest:false,depthWrite:false}));sp.position.copy(pos);sp.scale.set(1.25,.47,1);sp.renderOrder=70;return sp;
}
function rebuildTileCoordinateLabels(){
  if(debugTileLabelGroup){scene.remove(debugTileLabelGroup);for(const ch of debugTileLabelGroup.children){ch.material?.map?.dispose?.();ch.material?.dispose?.();}debugTileLabelGroup=null;}
  if(!sandbox.debug.tiles)return;
  debugTileLabelGroup=new THREE.Group();
  for(let x=-12;x<=12;x+=4)for(let z=-18;z<=18;z+=4){debugTileLabelGroup.add(makeDebugLabel(`${x},${z}`,new THREE.Vector3(x,.28,z)));}
  scene.add(debugTileLabelGroup);
}
function updateDebugOverlays(dt){
  sandbox.debugClock-=dt;if(sandbox.debugClock>0)return;sandbox.debugClock=.12;clearDebugDynamic();
  if(!(sandbox.debug.paths||sandbox.debug.sight||sandbox.debug.ranges||sandbox.debug.targets))return;
  for(const u of units){
    if(u.dead)continue;const teamColor=u.team===TEAM.PLAYER?0x55dfff:0xff7188;
    if(sandbox.debug.paths&&!u.flying&&u.navPath?.length){const pts=[u.group.position.clone().setY(.24),...u.navPath.slice(u.navIndex).map(p=>p.clone().setY(.24))];debugLine(pts,teamColor,.72);}
    if(sandbox.debug.sight){debugSightArc(u,SIGHT.frontTiles*GRID.tile,true,0x6fffb0);debugSightArc(u,SIGHT.rearTiles*GRID.tile,false,0xffd36f);}
    if(sandbox.debug.ranges)debugCircle(u.group.position,u.range,0x7fcfff,.38);
    if(sandbox.debug.targets&&u.target&&!u.target.dead){debugLine([u.group.position.clone().setY(.55),u.target.group.position.clone().setY(.55)],u.structureLockTarget===u.target?0xffd45a:0xffffff,u.structureLockTarget===u.target?.95:.55);}
  }
  if(sandbox.debug.ranges)for(const b of buildings)if(!b.dead)debugCircle(b.group.position,b.range||0,0xcc8cff,.32);
}
function updateDeveloperReadout(dt){
  sandbox.statusClock-=dt;if(sandbox.statusClock>0)return;sandbox.statusClock=.20;
  const m=game.ai.memory,style=currentAiStyle();
  if(ui.devStatus)ui.devStatus.textContent=`${sandbox.speed===0?'PAUSED':sandbox.speed+'×'} • ${style.name} • AI ${game.ai.enabled?'ON':'OFF'} • Units ${units.filter(u=>!u.dead).length}`;
  if(ui.devAiReadout){const recent=(m?.playerCycle||[]).slice(-5).map(id=>CARD_LIBRARY[id]?.name||id).join(' → ')||'No cards observed';ui.devAiReadout.innerHTML=`<strong>${m?.lastDecision||'—'}</strong><br>${m?.decisionReason||''}<br>AI Aether ${game.aether.enemy.toFixed(1)} • Player est ${(m?.playerAetherEstimate??5).toFixed(1)} • Phase ${game.ai.phase}<br><span>Seen cycle: ${recent}</span>`;}
  if(ui.devPanel&&!ui.devPanel.classList.contains('hidden'))renderDeveloperMatchup();
}
function bindDeveloperLab(){
  ui.devToggle?.addEventListener('click',()=>ui.devPanel?.classList.toggle('hidden'));ui.devClose?.addEventListener('click',()=>ui.devPanel?.classList.add('hidden'));
  document.querySelectorAll('[data-dev-speed]').forEach(b=>b.addEventListener('click',()=>setSandboxSpeed(Number(b.dataset.devSpeed))));
  document.querySelectorAll('[data-dev-aether]').forEach(b=>b.addEventListener('click',()=>{const [team,val]=b.dataset.devAether.split(':');devModifyAether(team,Number(val));}));
  ui.devArmSpawn?.addEventListener('click',armDeveloperSpawn);ui.devSpawnCard?.addEventListener('change',()=>sandbox.spawnCard=ui.devSpawnCard.value);ui.devSpawnTeam?.addEventListener('change',()=>sandbox.spawnTeam=ui.devSpawnTeam.value);
  ui.devAiStyle?.addEventListener('change',()=>{setAiStyle(ui.devAiStyle.value);configureAiDeck();game.aiThink=0;aiDecision('STYLE CHANGED',`${currentAiStyle().name} • deck rebuilt`);});ui.devAiToggle?.addEventListener('click',()=>setAiEnabled(!game.ai.enabled));
  ui.devTowerSelect?.addEventListener('change',syncDevTowerHpInput);ui.devApplyTower?.addEventListener('click',applyDeveloperTowerHp);ui.devClearField?.addEventListener('click',clearDeveloperField);
  const map=[[ui.devShowPaths,'paths'],[ui.devShowSight,'sight'],[ui.devShowRanges,'ranges'],[ui.devShowTargets,'targets'],[ui.devShowTiles,'tiles']];for(const [el,key] of map)el?.addEventListener('change',()=>{sandbox.debug[key]=el.checked;if(key==='tiles')rebuildTileCoordinateLabels();sandbox.debugClock=0;});
}

function cleanupDead(){
  // V12.8: deaths leave no gravestone/corpse marker; dead groups are removed after the short FX window.
  const now=game.elapsed;
  for(let i=units.length-1;i>=0;i--){
    const e=units[i];if(!e.dead||now-(e.deathAt??now)<.42)continue;
    scene.remove(e.group);disposeObject(e.group);units.splice(i,1);
  }
  for(let i=buildings.length-1;i>=0;i--){
    const e=buildings[i];if(!e.dead||now-(e.deathAt??now)<1.0)continue;
    scene.remove(e.group);disposeObject(e.group);buildings.splice(i,1);
  }
  // Towers remain in the array after death because pocket/core activation rules reference their state.
}

function resize(){
  const w=canvas.clientWidth,h=canvas.clientHeight; renderer.setSize(w,h,false); composer.setSize(w,h); camera.aspect=w/h; camera.updateProjectionMatrix();
}
window.addEventListener('resize',resize);resize();

function updateCameraMotion(dt){
  cameraShake=Math.max(0,cameraShake-dt*1.7);
  const amp=cameraShake*cameraShake*1.55;
  camera.position.copy(cameraAnchor);
  if(amp>.0001){camera.position.x+=(Math.random()-.5)*amp;camera.position.y+=(Math.random()-.5)*amp*.45;camera.position.z+=(Math.random()-.5)*amp;}
  camera.lookAt(cameraLook);
}

let lastRecoveredFrameError=0;
function simulateBattleStep(dt){
  game.elapsed+=dt;
  updateCameraMotion(dt);
  if(game.running){
    if(game.tiebreaker)updateTiebreaker(dt);
    else{
      updateTimer(dt);
      if(game.running&&!game.tiebreaker){updateAether(dt);aiUpdate(dt);}
      for(const entities of [towers,buildings,units]){
        for(const entity of entities){if(!game.running||game.tiebreaker)break;entity.update(dt);}
      }
      if(game.running&&!game.tiebreaker)updateProjectiles(dt);
      if(game.running&&!game.tiebreaker)updateHazards(dt);
    }
  }
  if(!game.running){clearLiveProjectiles();hazards.length=0;}
  updateEffects(dt);cleanupDead();
  if(game.running&&game.recorder)captureBattleState();
}
function tick(){
  requestAnimationFrame(tick);
  const rawDt=Math.min(.05,clock.getDelta());
  try{
    updateAmbient(performance.now()*.001);
    if(sandbox.speed>0){
      const scaledDt=rawDt*sandbox.speed;
      const steps=Math.max(1,Math.ceil(scaledDt/(1/60)));
      const dt=scaledDt/steps;
      for(let i=0;i<steps;i++)simulateBattleStep(dt);
    }
    updateDebugOverlays(rawDt);updateDeveloperReadout(rawDt);updateHealthBarFacing();composer.render();
  }catch(err){
    console.error('[Rift Crown] recovered frame error',err);
    const now=performance.now();
    if(now-lastRecoveredFrameError>5000){lastRecoveredFrameError=now;toast('Recovered a simulation error — match continuing.');}
  }
}

function startMatch(training=false){
  if(game.started)return;
  game.started=true; game.running=true;game.elapsed=0;game.training=training;game.battleDeck=activeDeck.slice();
  document.body.classList.remove('home-active');
  document.body.classList.add('in-match');
  setAiStyle(training ? 'control' : randomAiStyle());
  configureAiDeck();
  game.ai.enabled=true;game.ai.memory={playerAetherEstimate:5,lastObservedPlay:0,playerPlays:[],playerCycle:[],recentSpend:0,lastDecision:'OPENING READ',decisionReason:currentAiStyle().name,lastPunish:0,lastSpellCycle:0};game.aiThink=.35;
  game.metaLab?.setBattleRunning(true);game.playSequence=0;game.playsByTeam={player:0,enemy:0};game.leakPending={player:0,enemy:0};game.nextReplaySnapshot=0;game.openingHands={player:game.playerDeck.hand.slice(),enemy:game.enemyDeck.hand.slice()};
  game.recorder?.start({training,version:'V15',playerName:playerProfile.username,decks:{player:activeDeck.slice(),enemy:game.ai.deckProfile.ids.slice()},aiStyle:game.ai.style,rules:MATCH});
  captureBattleState(true);
  if(ui.devAiStyle)ui.devAiStyle.value=game.ai.style;if(ui.devAiToggle)ui.devAiToggle.textContent='AI ON';
  sandbox.speed=1;document.querySelectorAll('[data-dev-speed]').forEach(b=>b.classList.toggle('active',Number(b.dataset.devSpeed)===1));
  game.time=MATCH.regulation;game.overtimeTime=MATCH.overtime;game.aetherMultiplier=1;game.tiebreaker=false;game.overtime=false;ui.timer.textContent='3:00';if(ui.phaseLabel)ui.phaseLabel.textContent='REGULATION';
  refreshPocketOverlays();
  cameraAnchor.set(0,31.8,34.4); cameraLook.set(0,0,4.0); camera.position.copy(cameraAnchor); camera.lookAt(cameraLook);
  clock.getDelta();
  const styleLabel = currentAiStyle().name;
  setTimeout(()=>toast(training ? `Training battle started — ${styleLabel} AI.` : `Enemy AI style: ${styleLabel}`),350);
}

async function openPanel(id){if(id==='replay-history-modal'){await labReady;game.replayViewer?.openHistory();}else document.querySelector(`#${id}`)?.classList.remove('hidden');}

ui.homeHelp?.addEventListener('click',()=>ui.help.classList.remove('hidden'));
ui.profileEdit?.addEventListener('click',openAccountModal);
ui.accountSave?.addEventListener('click',saveAccountFromUi);
ui.accountUsername?.addEventListener('keydown',e=>{if(e.key==='Enter')saveAccountFromUi();});
ui.homeBattle?.addEventListener('click',async()=>{await Promise.all([persistentSaveReady,labReady]);startMatch(false);});
ui.training?.addEventListener('click',async()=>{await Promise.all([persistentSaveReady,labReady]);startMatch(true);});
ui.openDeck?.addEventListener('click',()=>ui.deckModal?.classList.remove('hidden'));
ui.openLoadout?.addEventListener('click',openLoadoutBuilder);
document.querySelectorAll('[data-panel]').forEach(b=>b.addEventListener('click',()=>{if(b.dataset.panel==='loadout-modal')openLoadoutBuilder();else openPanel(b.dataset.panel);}));
document.querySelectorAll('[data-close]').forEach(b=>b.addEventListener('click',()=>document.querySelector(`#${b.dataset.close}`)?.classList.add('hidden')));
for(const modal of document.querySelectorAll('.modal')) modal.addEventListener('pointerdown',e=>{if(e.target===modal)modal.classList.add('hidden');});
ui.restart.addEventListener('click',async()=>{await leaveBattle();sessionStorage.setItem('riftAutostart',game.training?'training':'1');location.reload();});
ui.homeBtn?.addEventListener('click',async()=>{await leaveBattle();sessionStorage.removeItem('riftAutostart');location.reload();});

initDeveloperLab();bindDeveloperLab();renderProfile();renderHomeDeck();renderDeckDetails();renderLoadoutBuilder();renderHand();updateScore();
const persistentSaveReady=hydratePersistentSave();
const labReady=initializeLab();
document.querySelector('#dev-meta-validation')?.addEventListener('click',async()=>{await labReady;openPanel('meta-modal');game.metaLab.showValidation();});
document.querySelector('#end-analysis')?.addEventListener('click',()=>game.replayViewer?.openAnalysis(game.lastReplayId));
document.querySelector('#end-replay')?.addEventListener('click',()=>game.replayViewer?.openReplay(game.lastReplayId));
cameraAnchor.set(-3.6,30.4,33.2); cameraLook.set(0,0,0); camera.position.copy(cameraAnchor); camera.lookAt(cameraLook);
setTimeout(()=>ui.loading.classList.add('done'),1450);
setTimeout(async()=>{
  ui.loading.remove();
  const autostart=sessionStorage.getItem('riftAutostart');
  if(autostart==='1'||autostart==='training'){
    sessionStorage.removeItem('riftAutostart');
    await Promise.all([persistentSaveReady,labReady]);startMatch(autostart==='training');
  }
},2050);
tick();
